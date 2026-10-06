#pragma once

#include "ap/core/RealtimeSafety.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace ap::dsp
{

enum class EqShape : std::uint8_t
{
    lowShelf,
    bell,
    highShelf
};

// One equaliser band: a TPT state variable filter whose low, band and input signals are mixed
// into a shelf or bell response (Simper, "Solving the continuous SVF equations using trapezoidal
// integration"). Same curves as the RBJ cookbook biquads, but stable and click-free when the
// settings move every sample. At 0 dB the output equals the input exactly. One per channel.
class EqBand
{
public:
    static constexpr double minFrequencyHz = 10.0;
    static constexpr double minQ = 0.1;
    static constexpr double maxQ = 18.0;
    static constexpr double maxGainDb = 24.0;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        reset();
        update();
    }

    void reset() noexcept AP_NONBLOCKING
    {
        ic1 = 0.0;
        ic2 = 0.0;
    }

    void set (EqShape newShape, double frequencyHz, double q, double gainDb) noexcept AP_NONBLOCKING
    {
        const double f = std::isfinite (frequencyHz)
                           ? std::clamp (frequencyHz, minFrequencyHz, sampleRate * 0.49)
                           : 1000.0;
        const double newQ = std::isfinite (q) ? std::clamp (q, minQ, maxQ) : 0.7071;
        const double gain = std::isfinite (gainDb) ? std::clamp (gainDb, -maxGainDb, maxGainDb) : 0.0;
        if (newShape == shape && f == frequency && newQ == resonance && gain == decibels)
            return;
        shape = newShape;
        frequency = f;
        resonance = newQ;
        decibels = gain;
        update();
    }

    [[nodiscard]] float process (float input) noexcept AP_NONBLOCKING
    {
        const double v0 = static_cast<double> (input);
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return static_cast<float> (m0 * v0 + m1 * v1 + m2 * v2);
    }

private:
    void update() noexcept AP_NONBLOCKING
    {
        const double a = std::pow (10.0, decibels / 40.0);
        double g = std::tan (std::numbers::pi * frequency / sampleRate);
        double k = 1.0 / resonance;
        switch (shape)
        {
        case EqShape::bell:
            k = 1.0 / (resonance * a);
            m0 = 1.0;
            m1 = k * (a * a - 1.0);
            m2 = 0.0;
            break;
        case EqShape::lowShelf:
            g /= std::sqrt (a);
            m0 = 1.0;
            m1 = k * (a - 1.0);
            m2 = a * a - 1.0;
            break;
        case EqShape::highShelf:
            g *= std::sqrt (a);
            m0 = a * a;
            m1 = k * (1.0 - a) * a;
            m2 = 1.0 - a * a;
            break;
        }
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 48000.0;
    EqShape shape = EqShape::bell;
    double frequency = 1000.0;
    double resonance = 0.7071;
    double decibels = 0.0;
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
};

} // namespace ap::dsp
