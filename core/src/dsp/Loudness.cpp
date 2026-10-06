#include "ap/dsp/Loudness.h"

#include "ap/dsp/Oversampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace ap::dsp
{
namespace
{
struct Biquad
{
    std::array<double, 3> b {};
    std::array<double, 3> a {}; // a[0] = 1
    double z1 = 0.0;
    double z2 = 0.0;

    double process (double x)
    {
        const double y = b[0] * x + z1;
        z1 = b[1] * x - a[1] * y + z2;
        z2 = b[2] * x - a[2] * y;
        return y;
    }
};

// BS.1770 K-weighting, re-derived for the sample rate (the coefficients printed in the standard
// are for 48 kHz only).
std::array<Biquad, 2> kWeighting (double rate)
{
    std::array<Biquad, 2> filters;

    // Stage 1: high-shelf (head effects).
    {
        const double f0 = 1681.974450955533;
        const double gainDb = 3.999843853973347;
        const double q = 0.7071752369554196;
        const double k = std::tan (std::numbers::pi * f0 / rate);
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        filters[0].b
            = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0};
        filters[0].a = {1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    // Stage 2: high-pass (RLB weighting).
    {
        const double f0 = 38.13547087602444;
        const double q = 0.5003270373238773;
        const double k = std::tan (std::numbers::pi * f0 / rate);
        const double a0 = 1.0 + k / q + k * k;
        filters[1].b = {1.0, -2.0, 1.0};
        filters[1].a = {1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    return filters;
}

double toLufs (double meanSquare)
{
    return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare)
                            : -std::numeric_limits<double>::infinity();
}
} // namespace

LoudnessReport measureLoudness (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    LoudnessReport report;
    if (channels.empty() || channels.front().empty() || !(sampleRate > 0.0))
        return report;
    const auto frames = channels.front().size();

    // Peaks.
    float samplePeak = 0.0f;
    float truePeak = 0.0f;
    for (const auto& channel : channels)
    {
        Oversampler4x oversampler;
        std::array<float, Oversampler4x::factor> up {};
        for (std::size_t i = 0; i < frames + static_cast<std::size_t> (Oversampler4x::latency()); ++i)
        {
            const float x = i < frames ? channel[i] : 0.0f;
            samplePeak = std::max (samplePeak, std::abs (x));
            oversampler.upsample (x, up);
            for (const float y : up)
                truePeak = std::max (truePeak, std::abs (y));
        }
    }
    truePeak = std::max (truePeak, samplePeak);
    if (samplePeak > 0.0f)
    {
        report.samplePeakDb = 20.0 * std::log10 (static_cast<double> (samplePeak));
        report.truePeakDb = 20.0 * std::log10 (static_cast<double> (truePeak));
    }

    // Mean square of the K-weighted signal per 100 ms step (blocks are four steps).
    const auto step = static_cast<std::size_t> (std::lround (sampleRate * 0.1));
    const auto steps = frames / step;
    std::vector<double> power (steps, 0.0);
    for (const auto& channel : channels)
    {
        auto filters = kWeighting (sampleRate);
        for (std::size_t s = 0; s < steps; ++s)
            for (std::size_t i = s * step; i < (s + 1) * step; ++i)
            {
                const double y = filters[1].process (filters[0].process (static_cast<double> (channel[i])));
                power[s] += y * y;
            }
    }

    std::vector<double> blocks; // mean square of each 400 ms block
    for (std::size_t s = 0; s + 4 <= steps; ++s)
        blocks.push_back ((power[s] + power[s + 1] + power[s + 2] + power[s + 3])
                          / static_cast<double> (4 * step));

    const auto gatedMean = [&blocks] (double threshold)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (const double block : blocks)
            if (toLufs (block) > threshold)
            {
                sum += block;
                ++count;
            }
        return count > 0 ? sum / static_cast<double> (count) : 0.0;
    };

    const double absolute = gatedMean (-70.0);
    if (absolute > 0.0)
    {
        const double relative = gatedMean (std::max (-70.0, toLufs (absolute) - 10.0)); // both gates
        report.integratedLufs = std::max (-70.0, toLufs (relative));
    }
    return report;
}

} // namespace ap::dsp
