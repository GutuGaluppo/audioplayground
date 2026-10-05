#include "ap/instruments/Synth.h"

#include <algorithm>
#include <cmath>

namespace ap::instruments
{
namespace
{
using params::ParamId;

constexpr double smoothingSeconds = 0.02;

double noteToHz (double note) noexcept AP_NONBLOCKING
{
    return 440.0 * std::exp2 ((note - 69.0) / 12.0);
}

// Perceptual velocity curve: soft notes stay audible, full velocity is unity.
float velocityToGain (float velocity) noexcept AP_NONBLOCKING
{
    const float v = std::clamp (velocity, 0.0f, 1.0f);
    return v * v * 0.75f + v * 0.25f;
}
} // namespace

void Synth::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    for (auto& voice : voices)
    {
        voice.oscA.prepare (sampleRate);
        voice.oscB.prepare (sampleRate);
        voice.filter.prepare (sampleRate);
        voice.filter.setMode (dsp::FilterMode::lowPass);
        voice.envelope.prepare (sampleRate);
    }
    cutoff.reset (sampleRate, smoothingSeconds);
    volume.reset (sampleRate, smoothingSeconds);
    cutoff.setCurrentAndTarget (static_cast<float> (std::log2 (4000.0)));
    volume.setCurrentAndTarget (0.0f);
    reset();
}

void Synth::reset() noexcept AP_NONBLOCKING
{
    for (auto& voice : voices)
    {
        voice.envelope.reset();
        voice.filter.reset();
        voice.held = false;
    }
}

int Synth::activeVoiceCount() const noexcept AP_NONBLOCKING
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.envelope.isActive())
            ++count;
    return count;
}

Synth::Voice& Synth::allocateVoice (std::uint8_t note) noexcept AP_NONBLOCKING
{
    // 1. The same note already sounding: retrigger it rather than stacking a second voice.
    for (auto& voice : voices)
        if (voice.envelope.isActive() && voice.note == note)
            return voice;

    // 2. A silent voice.
    for (auto& voice : voices)
        if (!voice.envelope.isActive())
            return voice;

    // 3. Steal: prefer released voices (quietest first), then the oldest held voice.
    Voice* best = nullptr;
    for (auto& voice : voices)
        if (!voice.held && (best == nullptr || voice.envelope.getLevel() < best->envelope.getLevel()))
            best = &voice;
    if (best != nullptr)
        return *best;

    best = &voices[0];
    for (auto& voice : voices)
        if (voice.startedAt < best->startedAt)
            best = &voice;
    return *best;
}

void Synth::handle (const NoteEvent& event) noexcept AP_NONBLOCKING
{
    switch (event.type)
    {
    case NoteEvent::Type::noteOn:
    {
        if (event.note > 127 || !(event.velocity > 0.0f))
        {
            // Velocity 0 is a note-off by MIDI convention.
            handle ({NoteEvent::Type::noteOff, event.note, 0.0f});
            return;
        }
        auto& voice = allocateVoice (event.note);
        const bool wasSilent = !voice.envelope.isActive();
        voice.note = event.note;
        voice.velocityGain = velocityToGain (event.velocity);
        voice.held = true;
        voice.startedAt = ++noteCounter;
        if (wasSilent)
        {
            // Start a fresh voice from a clean state; a stolen voice keeps its phase and
            // filter state so the takeover is continuous (no click).
            voice.oscA.reset (0.0);
            voice.oscB.reset (0.37); // offset phase: avoids the two oscillators cancelling at onset
            voice.filter.reset();
        }
        voice.envelope.noteOn();
        break;
    }

    case NoteEvent::Type::noteOff:
        for (auto& voice : voices)
            if (voice.held && voice.note == event.note)
            {
                voice.held = false;
                voice.envelope.noteOff();
            }
        break;

    case NoteEvent::Type::allNotesOff:
        for (auto& voice : voices)
        {
            voice.held = false;
            voice.envelope.noteOff();
        }
        break;
    }
}

void Synth::applyParameters (const params::ParameterStore& parameters) noexcept AP_NONBLOCKING
{
    waveform = static_cast<dsp::Waveform> (
        std::clamp (static_cast<int> (parameters.get (ParamId::synthWaveform)), 0, 3));
    pitchSemitones = parameters.get (ParamId::synthPitch);
    detuneCents = parameters.get (ParamId::synthDetune);
    resonance = parameters.get (ParamId::synthResonance);
    cutoff.setTarget (std::log2 (parameters.get (ParamId::synthCutoff)));
    volume.setTarget (std::pow (10.0f, parameters.get (ParamId::synthVolume) / 20.0f));

    const dsp::AdsrEnvelope::Settings envelope {
        parameters.get (ParamId::synthAttack), parameters.get (ParamId::synthDecay),
        parameters.get (ParamId::synthSustain), parameters.get (ParamId::synthRelease)};
    for (auto& voice : voices)
    {
        voice.oscA.setWaveform (waveform);
        voice.oscB.setWaveform (waveform);
        voice.envelope.setSettings (envelope);
    }
}

void Synth::render (core::AudioBlock output, const params::ParameterStore& parameters) noexcept AP_NONBLOCKING
{
    if (output.isEmpty())
        return;

    applyParameters (parameters);

    const double detuneSemitones = static_cast<double> (detuneCents) / 200.0; // +- half the spread each

    for (auto& voice : voices)
    {
        if (!voice.envelope.isActive())
            continue;

        const double note = static_cast<double> (voice.note) + static_cast<double> (pitchSemitones);
        voice.oscA.setFrequency (noteToHz (note - detuneSemitones));
        voice.oscB.setFrequency (noteToHz (note + detuneSemitones));
    }

    for (int i = 0; i < output.numSamples; ++i)
    {
        const double cutoffHz = std::exp2 (static_cast<double> (cutoff.next()));
        const float gain = volume.next();
        float mix = 0.0f;

        for (auto& voice : voices)
        {
            if (!voice.envelope.isActive())
                continue;

            voice.filter.setCutoffAndQ (cutoffHz, static_cast<double> (resonance));
            const float oscillators = 0.5f * (voice.oscA.next() + voice.oscB.next());
            mix += voice.filter.process (oscillators) * voice.envelope.next() * voice.velocityGain;
        }

        const float sample = mix * gain;
        for (int ch = 0; ch < output.numChannels; ++ch)
            output.channels[ch][i] += sample;
    }
}

} // namespace ap::instruments
