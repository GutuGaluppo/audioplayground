#pragma once

#include <cmath>
#include <numbers>
#include <vector>

namespace ap::test
{

// Ideal band-limited waveforms by additive synthesis: every harmonic below Nyquist, nothing
// above. The reference for measuring an oscillator's aliasing.
enum class ReferenceShape
{
    saw,
    square,
    triangle
};

inline std::vector<double> bandLimitedReference (ReferenceShape shape, double frequency, double sampleRate,
                                                 int numSamples)
{
    std::vector<double> out (static_cast<std::size_t> (numSamples), 0.0);
    const int harmonics = static_cast<int> (std::floor (sampleRate * 0.5 / frequency));
    const double pi = std::numbers::pi;

    for (int k = 1; k <= harmonics; ++k)
    {
        if (shape != ReferenceShape::saw && k % 2 == 0)
            continue;
        const double kd = static_cast<double> (k);
        for (int n = 0; n < numSamples; ++n)
        {
            const double t = frequency * static_cast<double> (n) / sampleRate;
            const double angle = 2.0 * pi * kd * t;
            double& sample = out[static_cast<std::size_t> (n)];
            switch (shape)
            {
            case ReferenceShape::saw:
                sample += -(2.0 / pi) * std::sin (angle) / kd;
                break;
            case ReferenceShape::square:
                sample += (4.0 / pi) * std::sin (angle) / kd;
                break;
            case ReferenceShape::triangle:
                sample += -(8.0 / (pi * pi)) * std::cos (angle) / (kd * kd);
                break;
            }
        }
    }
    return out;
}

template <typename A, typename B> double rmsDifference (const A& a, const B& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const double d = static_cast<double> (a[i]) - static_cast<double> (b[i]);
        sum += d * d;
    }
    return std::sqrt (sum / static_cast<double> (a.size()));
}

} // namespace ap::test
