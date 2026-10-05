#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/LinearSmoothedValue.h"
#include "ap/dsp/SineOscillator.h"
#include "ap/engine/Metronome.h"
#include "ap/engine/Transport.h"

#include <atomic>

namespace ap::engine
{

// Headless audio engine. Owns everything that runs inside the audio callback.
//
// Threading:
// - prepare() / releaseResources(): called while the audio callback is stopped.
// - process(): audio thread only. Real-time safe (no allocation, locks or I/O).
// - set*() and getters: any thread, lock-free.
class Engine
{
public:
    static constexpr float minToneFrequencyHz = 20.0f;
    static constexpr float maxToneFrequencyHz = 20000.0f;
    static constexpr float minToneLevelDb = -60.0f;
    static constexpr float maxToneLevelDb = -6.0f;

    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources() noexcept;

    void process (core::AudioBlock output) noexcept AP_NONBLOCKING;

    void setTestToneEnabled (bool enabled) noexcept;
    void setTestToneFrequency (float hz) noexcept;
    void setTestToneLevelDb (float db) noexcept;

    // Thread-safe controls; see Transport and Metronome for details.
    [[nodiscard]] Transport& getTransport() noexcept { return transport; }
    [[nodiscard]] const Transport& getTransport() const noexcept { return transport; }
    [[nodiscard]] Metronome& getMetronome() noexcept { return metronome; }
    [[nodiscard]] const Metronome& getMetronome() const noexcept { return metronome; }

    [[nodiscard]] bool isTestToneEnabled() const noexcept;
    [[nodiscard]] float getTestToneLevelDb() const noexcept;

    // Highest absolute output sample since the last call. Resets the meter.
    [[nodiscard]] float consumeOutputPeak() noexcept;

    // Number of non-finite samples replaced with silence since start (should always be 0).
    [[nodiscard]] int getNonFiniteSampleCount() const noexcept;

private:
    void renderTestTone (core::AudioBlock output) noexcept AP_NONBLOCKING;
    void finaliseOutput (core::AudioBlock output) noexcept AP_NONBLOCKING;
    void updatePeak (float blockPeak) noexcept AP_NONBLOCKING;

    double sampleRate = 48000.0;
    bool prepared = false;

    Transport transport;
    Metronome metronome;

    dsp::SineOscillator toneOscillator;
    dsp::LinearSmoothedValue toneGain;
    dsp::LinearSmoothedValue toneFrequency;

    std::atomic<bool> toneEnabled{false};
    std::atomic<float> toneFrequencyHz{440.0f};
    std::atomic<float> toneLevelDb{-18.0f};

    std::atomic<float> outputPeak{0.0f};
    std::atomic<int> nonFiniteSamples{0};
};

} // namespace ap::engine
