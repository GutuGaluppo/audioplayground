#include "ap/instruments/Sampler.h"

#include <algorithm>
#include <cmath>

namespace ap::instruments
{
namespace
{
using params::ParamId;

constexpr double shortFadeSeconds = 0.002;
constexpr double releaseFadeSeconds = 0.03;

// 4-point, 3rd-order Hermite (Catmull-Rom) interpolation.
float hermite (const std::vector<float>& x, double position, std::int64_t lo,
               std::int64_t hi) noexcept AP_NONBLOCKING
{
    const auto i = static_cast<std::int64_t> (std::floor (position));
    const auto frac = static_cast<float> (position - static_cast<double> (i));
    const auto at = [&x, lo, hi] (std::int64_t k) noexcept AP_NONBLOCKING
    { return x[static_cast<std::size_t> (std::clamp (k, lo, hi))]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
}
} // namespace

Sampler::~Sampler()
{
    // Audio is stopped when the owner is destroyed; reclaim the buffers we took from the exchange.
    delete current;
    delete fading;
}

void Sampler::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    shortFadeStep = static_cast<float> (1.0 / (shortFadeSeconds * sampleRate));
    releaseFadeStep = static_cast<float> (1.0 / (releaseFadeSeconds * sampleRate));
    for (auto& voice : voices)
        voice = {};
}

int Sampler::activeVoiceCount() const noexcept AP_NONBLOCKING
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive() && voice.fadeStep >= 0.0f)
            ++count;
    return count;
}

void Sampler::startFadeOut (Voice& voice) noexcept AP_NONBLOCKING
{
    voice.held = false;
    voice.fadeStep = -std::max (shortFadeStep, std::abs (voice.fadeStep));
}

Sampler::Voice& Sampler::allocateVoice() noexcept AP_NONBLOCKING
{
    // Keep at most maxVoices sounding: fade out the oldest one that is not already fading.
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& voice : voices)
        if (voice.isActive() && voice.fadeStep >= 0.0f)
        {
            ++sounding;
            if (oldest == nullptr || voice.startedAt < oldest->startedAt)
                oldest = &voice;
        }
    if (sounding >= maxVoices && oldest != nullptr)
        startFadeOut (*oldest);

    for (auto& voice : voices)
        if (!voice.isActive())
            return voice;

    // No free slot (a burst of notes): reuse the quietest voice that is already fading out.
    // Never take a sounding voice here. At most maxVoices are sounding, so with 2 x maxVoices
    // slots at least maxVoices are fading.
    Voice* quietest = nullptr;
    for (auto& voice : voices)
        if (voice.fadeStep < 0.0f
            && (quietest == nullptr || voice.fade < quietest->fade
                || (voice.fade == quietest->fade && voice.startedAt < quietest->startedAt)))
            quietest = &voice;
    if (quietest == nullptr)
        quietest = oldest != nullptr ? oldest : &voices[0];
    *quietest = {};
    return *quietest;
}

void Sampler::swapInPendingSample() noexcept AP_NONBLOCKING
{
    // Release the previous buffer once no voice reads it any more.
    if (fading != nullptr)
    {
        const bool inUse = std::any_of (voices.begin(), voices.end(),
                                        [this] (const Voice& v) { return v.buffer == fading; });
        if (inUse || !samples.retire (fading))
            return; // try again next block; never swap a third buffer in meanwhile
        fading = nullptr;
    }

    if (!samples.canRetire())
        return;

    SampleBuffer* next = samples.takePending();
    if (next == nullptr)
        return;

    if (current != nullptr)
    {
        for (auto& voice : voices)
            if (voice.buffer == current)
                startFadeOut (voice);
        fading = current;
    }

    current = next->isValid() ? next : nullptr;
    if (current == nullptr && !samples.retire (next))
        fading = fading == nullptr ? next : fading; // extremely unlikely; keep ownership
    currentAssetId.store (current != nullptr ? current->assetId : 0, std::memory_order_release);
}

void Sampler::handle (const NoteEvent& event) noexcept AP_NONBLOCKING
{
    switch (event.type)
    {
    case NoteEvent::Type::noteOn:
    {
        if (event.note > 127 || !(event.velocity > 0.0f))
        {
            handle ({NoteEvent::Type::noteOff, event.note, 0.0f});
            return;
        }
        if (current == nullptr)
            return;

        auto& voice = allocateVoice();
        voice.buffer = current;
        voice.note = event.note;
        voice.held = true;
        voice.startedAt = ++noteCounter;
        voice.gain = std::clamp (event.velocity, 0.0f, 1.0f);
        voice.position = static_cast<double> (startFrame);
        // A sample cut in the middle needs a fade-in; one played from its start does not
        // (the recording's own onset is preserved).
        voice.fade = startFrame > 0 ? 0.0f : 1.0f;
        voice.fadeStep = startFrame > 0 ? shortFadeStep : 0.0f;
        break;
    }

    case NoteEvent::Type::noteOff:
        for (auto& voice : voices)
            if (voice.isActive() && voice.held && voice.note == event.note)
            {
                voice.held = false;
                if (mode == Mode::gate)
                    voice.fadeStep = -releaseFadeStep;
            }
        break;

    case NoteEvent::Type::allNotesOff:
        for (auto& voice : voices)
            if (voice.isActive())
                startFadeOut (voice);
        break;
    }
}

void Sampler::render (core::AudioBlock output,
                      const params::ParameterStore& parameters) noexcept AP_NONBLOCKING
{
    swapInPendingSample();
    if (output.isEmpty())
        return;

    mode = parameters.get (ParamId::samplerMode) >= 0.5f ? Mode::gate : Mode::oneShot;
    pitchSemitones = static_cast<double> (parameters.get (ParamId::samplerPitch));
    gain = std::pow (10.0f, parameters.get (ParamId::samplerGain) / 20.0f);

    if (current != nullptr)
    {
        const auto frames = current->frames();
        startFrame = static_cast<std::int64_t> (static_cast<double> (frames)
                                                * static_cast<double> (parameters.get (ParamId::samplerStart))
                                                / 100.0);
        endFrame = static_cast<std::int64_t> (static_cast<double> (frames)
                                              * static_cast<double> (parameters.get (ParamId::samplerEnd))
                                              / 100.0);
        startFrame = std::clamp<std::int64_t> (startFrame, 0, frames);
        endFrame = std::clamp<std::int64_t> (endFrame, startFrame, frames);
    }

    for (auto& voice : voices)
    {
        if (!voice.isActive())
            continue;

        const auto& buffer = *voice.buffer;
        const auto frames = buffer.frames();
        // Voices on a swapped-out buffer play to its natural end with their own bounds.
        const std::int64_t end = voice.buffer == current ? endFrame : frames;
        const bool trimmedEnd = end < frames;
        const double fadeOutFrames = shortFadeSeconds * sampleRate;

        voice.rate = std::exp2 ((static_cast<double> (voice.note - rootNote) + pitchSemitones) / 12.0)
                   * buffer.sampleRate / sampleRate;

        const bool interpolate = voice.rate != 1.0;
        const auto lastFrame = frames - 1;

        for (int i = 0; i < output.numSamples; ++i)
        {
            if (voice.position >= static_cast<double> (end))
            {
                voice = {};
                break;
            }

            // Fade towards a trimmed end so the cut is not a click.
            float edge = 1.0f;
            if (trimmedEnd)
            {
                const double remaining = static_cast<double> (end) - voice.position;
                if (remaining < fadeOutFrames)
                    edge = static_cast<float> (remaining / fadeOutFrames);
            }

            voice.fade = std::clamp (voice.fade + voice.fadeStep, 0.0f, 1.0f);
            if (voice.fadeStep < 0.0f && voice.fade <= 0.0f)
            {
                voice = {};
                break;
            }

            const float amplitude = voice.gain * gain * voice.fade * edge;
            for (int ch = 0; ch < output.numChannels; ++ch)
            {
                const auto& source = buffer.channels[std::min<std::size_t> (static_cast<std::size_t> (ch),
                                                                            buffer.channels.size() - 1)];
                const float sample = interpolate ? hermite (source, voice.position, 0, lastFrame)
                                                 : source[static_cast<std::size_t> (voice.position)];
                output.channels[ch][i] += sample * amplitude;
            }

            voice.position += voice.rate;
        }
    }
}

} // namespace ap::instruments
