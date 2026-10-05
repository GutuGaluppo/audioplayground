#pragma once

#include "ap/core/RealtimeSafety.h"

#include <cmath>

namespace ap::dsp
{

// Linear ramp towards a target over a fixed number of samples. Removes zipper noise and clicks
// when a parameter jumps. The ramp restarts from the current value whenever the target changes.
class LinearSmoothedValue
{
public:
    // Not real-time safe to call concurrently with next(); call from prepare().
    void reset (double sampleRate, double rampSeconds) noexcept
    {
        const auto length = std::lround (sampleRate * rampSeconds);
        rampLength = length > 1 ? static_cast<int> (length) : 1;
        setCurrentAndTarget (target);
    }

    void setCurrentAndTarget (float value) noexcept AP_NONBLOCKING
    {
        current = value;
        target = value;
        remaining = 0;
    }

    void setTarget (float value) noexcept AP_NONBLOCKING
    {
        if (value == target)
            return;

        target = value;

        if (rampLength <= 1)
        {
            setCurrentAndTarget (value);
            return;
        }

        remaining = rampLength;
        step = (target - current) / static_cast<float> (rampLength);
    }

    [[nodiscard]] float next() noexcept AP_NONBLOCKING
    {
        if (remaining <= 0)
            return target;

        --remaining;
        current = remaining == 0 ? target : current + step;
        return current;
    }

    [[nodiscard]] bool isSmoothing() const noexcept AP_NONBLOCKING { return remaining > 0; }
    [[nodiscard]] float getCurrent() const noexcept AP_NONBLOCKING { return current; }
    [[nodiscard]] float getTarget() const noexcept AP_NONBLOCKING { return target; }

private:
    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
    int remaining = 0;
    int rampLength = 1;
};

} // namespace ap::dsp
