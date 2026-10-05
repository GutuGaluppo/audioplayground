#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/MusicalTime.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/ClickVoice.h"

#include <atomic>

namespace ap::engine
{

// Sample-accurate metronome. Clicks on every beat; the first beat of each bar is accented.
class Metronome
{
public:
    static constexpr double accentFrequencyHz = 1760.0;
    static constexpr double beatFrequencyHz = 1320.0;

    void prepare (double sampleRate) noexcept { click.prepare (sampleRate); }
    void reset() noexcept AP_NONBLOCKING { click.reset(); }

    void setEnabled (bool enabled) noexcept { enabledFlag.store (enabled, std::memory_order_relaxed); }
    [[nodiscard]] bool isEnabled() const noexcept { return enabledFlag.load (std::memory_order_relaxed); }

    // Adds clicks for the musical range [startSample, startSample + length) into output at
    // [offset, offset + length). Always clicks during the count-in (negative positions).
    // accentGain is the linear level of the downbeat click; other beats are quieter.
    void render (core::AudioBlock output, int offset, int length, core::Samples startSample,
                 const core::TempoMap& tempoMap, float accentGain) noexcept AP_NONBLOCKING;

    // Lets a ringing click decay naturally after the transport stops.
    void renderTail (core::AudioBlock output) noexcept AP_NONBLOCKING;

private:
    void renderVoice (core::AudioBlock output, int from, int to) noexcept AP_NONBLOCKING;

    dsp::ClickVoice click;
    std::atomic<bool> enabledFlag {false};
};

} // namespace ap::engine
