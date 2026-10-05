#pragma once

#include "ap/core/RealtimeSafety.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::dsp
{

// Short, decaying sine "tick" used by the metronome. Pure sine, so it is alias-free at any pitch.
// A new trigger restarts the envelope from the current phase, which avoids a discontinuity.
class ClickVoice
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        // -60 dB after decaySeconds.
        decayPerSample = static_cast<float> (std::pow (0.001, 1.0 / (decaySeconds * sampleRate)));
        attackSamples = std::max (1, static_cast<int> (attackSeconds * sampleRate));
        reset();
    }

    void reset() noexcept AP_NONBLOCKING
    {
        envelope = 0.0f;
        attackRemaining = 0;
        phase = 0.0;
    }

    void trigger (double frequencyHz, float level) noexcept AP_NONBLOCKING
    {
        // A silent voice restarts at zero phase, so every click starts exactly at 0 (no step).
        if (!isActive())
            phase = 0.0;
        increment = frequencyHz / sampleRate;
        targetLevel = level;
        attackRemaining = attackSamples;
        attackStep = (targetLevel - envelope) / static_cast<float> (attackSamples);
    }

    [[nodiscard]] bool isActive() const noexcept AP_NONBLOCKING
    {
        return attackRemaining > 0 || envelope > 1.0e-5f;
    }

    [[nodiscard]] float next() noexcept AP_NONBLOCKING
    {
        if (attackRemaining > 0)
        {
            envelope += attackStep;
            --attackRemaining;
        }
        else
        {
            envelope *= decayPerSample;
            if (envelope <= 1.0e-5f)
                envelope = 0.0f;
        }

        const auto value = static_cast<float> (std::sin (2.0 * std::numbers::pi * phase)) * envelope;
        phase += increment;
        phase -= std::floor (phase);
        return value;
    }

private:
    static constexpr double decaySeconds = 0.06;
    static constexpr double attackSeconds = 0.0005; // 0.5 ms: crisp but click-free onset

    double sampleRate = 48000.0;
    double phase = 0.0;
    double increment = 0.0;
    float envelope = 0.0f;
    float targetLevel = 0.0f;
    float attackStep = 0.0f;
    float decayPerSample = 0.999f;
    int attackSamples = 24;
    int attackRemaining = 0;
};

} // namespace ap::dsp
