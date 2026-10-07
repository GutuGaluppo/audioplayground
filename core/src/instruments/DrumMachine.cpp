#include "ap/instruments/DrumMachine.h"

#include <algorithm>
#include <cmath>

namespace ap::instruments
{
namespace
{
constexpr int closedHat = 2;
constexpr int openHat = 3;

bool isValidPad (int pad) noexcept AP_NONBLOCKING
{
    return pad >= 0 && pad < DrumMachine::numPads;
}
} // namespace

void DrumMachine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    fadeStep = static_cast<float> (1.0 / (0.003 * sampleRate));
    // The rate may have changed: rebuild kit 0 and the kit in use; other kits are built when picked.
    for (auto& kit : kits)
        for (auto& sound : kit)
            sound.reset();
    for (const auto kit : {std::size_t {0}, static_cast<std::size_t> (requestedKit)})
        for (std::size_t pad = 0; pad < numPads; ++pad)
            if (!kits[kit][pad])
                kits[kit][pad] = makeFactorySound (pad, sampleRate, kit);
    activeKit.store (requestedKit, std::memory_order_release);
    for (auto& voice : voices)
        voice = {};
}

void DrumMachine::setKit (int kit)
{
    if (kit < 0 || kit >= static_cast<int> (factoryKitCount))
        return;
    const auto index = static_cast<std::size_t> (kit);
    for (std::size_t pad = 0; pad < numPads; ++pad)
        if (!kits[index][pad])
            kits[index][pad] = makeFactorySound (pad, sampleRate, index);
    requestedKit = kit;
    activeKit.store (kit, std::memory_order_release); // sounds are complete before the audio thread can see it
}

void DrumMachine::setPad (int pad, float volumeDb, float pitchSemitones, bool muted) noexcept
{
    if (!isValidPad (pad))
        return;
    auto& p = pads[static_cast<std::size_t> (pad)];
    if (std::isfinite (volumeDb))
        p.volumeDb.store (std::clamp (volumeDb, minVolumeDb, maxVolumeDb), std::memory_order_relaxed);
    if (std::isfinite (pitchSemitones))
        p.pitch.store (std::clamp (pitchSemitones, -24.0f, 24.0f), std::memory_order_relaxed);
    p.muted.store (muted, std::memory_order_relaxed);
}

void DrumMachine::loadPadSample (int pad, std::unique_ptr<SampleBuffer> buffer) noexcept
{
    if (isValidPad (pad))
        user[static_cast<std::size_t> (pad)].publish (buffer != nullptr ? std::move (buffer)
                                                                        : std::make_unique<SampleBuffer>());
}

std::size_t DrumMachine::collectGarbage() noexcept
{
    std::size_t freed = 0;
    for (auto& slot : user)
        freed += slot.collectGarbage();
    return freed;
}

const SampleBuffer* DrumMachine::soundFor (int pad) const noexcept AP_NONBLOCKING
{
    const auto index = static_cast<std::size_t> (pad);
    if (const auto* custom = user[index].get())
        return custom;
    return kits[static_cast<std::size_t> (activeKit.load (std::memory_order_acquire))][index].get();
}

int DrumMachine::activeVoiceCount() const noexcept AP_NONBLOCKING
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive() && voice.fadeStep >= 0.0f)
            ++count;
    return count;
}

void DrumMachine::fadeOutVoices (int pad) noexcept AP_NONBLOCKING
{
    // Only voices already sounding; a hit scheduled later in the block is not choked early.
    for (auto& voice : voices)
        if (voice.isActive() && voice.pad == pad && voice.delay == 0 && voice.position > 0.0)
            voice.fadeStep = -fadeStep;
}

void DrumMachine::trigger (int pad, float velocity, int delaySamples) noexcept AP_NONBLOCKING
{
    if (!isValidPad (pad) || !(velocity > 0.0f))
        return;
    const auto& p = pads[static_cast<std::size_t> (pad)];
    if (p.muted.load (std::memory_order_relaxed))
        return;
    const auto* buffer = soundFor (pad);
    if (buffer == nullptr)
        return;

    // Free slot, else the oldest voice is cut short with a fade and replaced.
    Voice* slot = nullptr;
    for (auto& voice : voices)
        if (!voice.isActive())
        {
            slot = &voice;
            break;
        }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& voice : voices)
            if (voice.startedAt < slot->startedAt)
                slot = &voice;
    }

    const float volume = std::pow (10.0f, p.volumeDb.load (std::memory_order_relaxed) / 20.0f);
    *slot = {};
    slot->buffer = buffer;
    slot->pad = pad;
    slot->rate = std::exp2 (static_cast<double> (p.pitch.load (std::memory_order_relaxed)) / 12.0)
               * buffer->sampleRate / sampleRate;
    slot->gain = volume * std::clamp (velocity, 0.0f, 1.0f);
    slot->delay = std::max (0, delaySamples);
    slot->startedAt = ++triggerCounter;
}

void DrumMachine::handle (const NoteEvent& event) noexcept AP_NONBLOCKING
{
    if (event.type == NoteEvent::Type::noteOn)
        trigger (static_cast<int> (event.note) - firstMidiNote, event.velocity);
    // Drum hits are one-shots: note off is ignored; all-notes-off lets them ring out.
}

void DrumMachine::render (core::AudioBlock output) noexcept AP_NONBLOCKING
{
    // Swap in newly loaded pad samples; voices on a replaced sample fade out.
    for (std::size_t pad = 0; pad < user.size(); ++pad)
    {
        const auto* replaced = user[pad].update (
            [this] (const SampleBuffer* buffer) noexcept AP_NONBLOCKING
            {
                return std::any_of (voices.begin(), voices.end(),
                                    [buffer] (const Voice& v) { return v.buffer == buffer; });
            });
        if (replaced != nullptr)
            for (auto& voice : voices)
                if (voice.buffer == replaced)
                    voice.fadeStep = -fadeStep;
    }

    if (output.isEmpty())
        return;

    // Render in chunks split wherever a closed hat starts, so its choke of the open hat lands on
    // the exact sample instead of at the next block.
    for (int done = 0; done < output.numSamples;)
    {
        int chunk = output.numSamples - done;
        for (const auto& voice : voices)
            if (voice.isActive() && voice.pad == closedHat && voice.delay > 0 && voice.delay < chunk)
                chunk = voice.delay;

        for (const auto& voice : voices)
            if (voice.isActive() && voice.pad == closedHat && voice.delay == 0 && voice.position == 0.0)
            {
                fadeOutVoices (openHat);
                break;
            }

        renderVoices (output, done, chunk);
        done += chunk;
    }
}

void DrumMachine::renderVoices (core::AudioBlock output, int offset, int length) noexcept AP_NONBLOCKING
{
    // Sum voices in trigger order, not slot order: which slot a voice lands in depends on the
    // block size, and floating-point addition order must not (bit-identical renders).
    std::array<Voice*, maxVoices> order {};
    std::size_t count = 0;
    for (auto& voice : voices)
    {
        if (!voice.isActive())
            continue;
        auto position = count++;
        while (position > 0 && order[position - 1]->startedAt > voice.startedAt)
        {
            order[position] = order[position - 1];
            --position;
        }
        order[position] = &voice;
    }

    for (std::size_t v = 0; v < count; ++v)
    {
        auto& voice = *order[v];

        int i = offset;
        if (voice.delay > 0)
        {
            const int wait = std::min (voice.delay, length);
            voice.delay -= wait;
            i += wait;
        }

        const auto& source = voice.buffer->channels;
        const auto frames = static_cast<double> (voice.buffer->frames());

        for (; i < offset + length; ++i)
        {
            if (voice.position >= frames - 1.0)
            {
                voice = {};
                break;
            }

            voice.fade = std::clamp (voice.fade + voice.fadeStep, 0.0f, 1.0f);
            if (voice.fadeStep < 0.0f && voice.fade <= 0.0f)
            {
                voice = {};
                break;
            }

            // Linear interpolation is enough for one-shot drums (pitch changes are small).
            const auto index = static_cast<std::size_t> (voice.position);
            const auto frac = static_cast<float> (voice.position - static_cast<double> (index));
            const float amplitude = voice.gain * voice.fade;
            for (int ch = 0; ch < output.numChannels; ++ch)
            {
                const auto& channel = source[std::min (static_cast<std::size_t> (ch), source.size() - 1)];
                const float sample = channel[index] + frac * (channel[index + 1] - channel[index]);
                output.channels[ch][i] += sample * amplitude;
            }
            voice.position += voice.rate;
        }
    }
}

} // namespace ap::instruments
