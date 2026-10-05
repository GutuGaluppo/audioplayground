#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/AdsrEnvelope.h"
#include "ap/dsp/LinearSmoothedValue.h"
#include "ap/dsp/PolyBlepOscillator.h"
#include "ap/dsp/StateVariableFilter.h"
#include "ap/instruments/NoteEvent.h"
#include "ap/params/Parameters.h"

#include <array>
#include <cstdint>

namespace ap::instruments
{

// Subtractive synth V1 (guide §13.1): two detuned band-limited oscillators -> TPT low-pass ->
// ADSR amplifier, 16 voices. Real-time safe: all voices are preallocated.
class Synth
{
public:
    static constexpr int maxVoices = 16;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept AP_NONBLOCKING;

    void handle (const NoteEvent& event) noexcept AP_NONBLOCKING;

    // Reads synth.* parameters, then adds the rendered voices to every channel of output.
    void render (core::AudioBlock output, const params::ParameterStore& parameters) noexcept AP_NONBLOCKING;

    [[nodiscard]] int activeVoiceCount() const noexcept AP_NONBLOCKING;

private:
    struct Voice
    {
        dsp::PolyBlepOscillator oscA;
        dsp::PolyBlepOscillator oscB;
        dsp::StateVariableFilter filter;
        dsp::AdsrEnvelope envelope;
        std::uint8_t note = 0;
        float velocityGain = 0.0f;
        bool held = false;           // key still down
        std::uint64_t startedAt = 0; // for oldest-voice stealing
    };

    Voice& allocateVoice (std::uint8_t note) noexcept AP_NONBLOCKING;
    void applyParameters (const params::ParameterStore& parameters) noexcept AP_NONBLOCKING;

    std::array<Voice, maxVoices> voices {};
    double sampleRate = 48000.0;
    std::uint64_t noteCounter = 0;

    dsp::Waveform waveform = dsp::Waveform::saw;
    float pitchSemitones = 0.0f;
    float detuneCents = 0.0f;
    float resonance = 0.8f;
    dsp::LinearSmoothedValue cutoff; // in octaves (log2 Hz) so sweeps sound even
    dsp::LinearSmoothedValue volume; // linear gain
};

} // namespace ap::instruments
