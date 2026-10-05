#pragma once

#include "ap/core/RealtimeSafety.h"

#include <cmath>
#include <numbers>

namespace ap::dsp
{

// Pure sine oscillator. Phase is kept in double precision in [0, 1) so long notes do not drift.
class SineOscillator
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        reset();
    }

    void reset() noexcept AP_NONBLOCKING { phase = 0.0; }

    void setFrequency (double hz) noexcept AP_NONBLOCKING { increment = hz / sampleRate; }

    [[nodiscard]] float next() noexcept AP_NONBLOCKING
    {
        const auto value = std::sin (2.0 * std::numbers::pi * phase);
        phase += increment;
        phase -= std::floor (phase);
        return static_cast<float> (value);
    }

private:
    double sampleRate = 48000.0;
    double phase = 0.0;
    double increment = 0.0;
};

} // namespace ap::dsp
