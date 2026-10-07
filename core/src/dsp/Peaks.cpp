#include "ap/dsp/Peaks.h"

#include <algorithm>
#include <cmath>

namespace ap::dsp
{

std::vector<float> computeOverview (const std::vector<std::vector<float>>& channels, int points)
{
    std::vector<float> overview (static_cast<std::size_t> (std::max (points, 0)), 0.0f);
    if (channels.empty() || channels.front().empty())
        return overview;

    const auto frames = channels.front().size();
    for (std::size_t p = 0; p < overview.size(); ++p)
    {
        const auto from = frames * p / overview.size();
        const auto to = std::max (from + 1, frames * (p + 1) / overview.size());
        float peak = 0.0f;
        for (const auto& channel : channels)
            for (auto i = from; i < std::min (to, frames); ++i)
                peak = std::max (peak, std::abs (channel[i]));
        overview[p] = std::min (peak, 1.0f);
    }
    return overview;
}

std::vector<std::uint8_t> computePeaks (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    if (channels.empty() || channels.front().empty() || !(sampleRate > 0.0))
        return {};

    const auto frames = channels.front().size();
    const double framesPerPeak = sampleRate / peaksPerSecond;
    const auto count = std::min (
        maxPeaks, static_cast<std::size_t> (std::ceil (static_cast<double> (frames) / framesPerPeak)));

    std::vector<std::uint8_t> peaks (count, 0);
    for (std::size_t p = 0; p < count; ++p)
    {
        const auto from = static_cast<std::size_t> (std::llround (static_cast<double> (p) * framesPerPeak));
        const auto to = std::min (
            frames, static_cast<std::size_t> (std::llround (static_cast<double> (p + 1) * framesPerPeak)));
        float peak = 0.0f;
        for (const auto& channel : channels)
            for (auto i = from; i < std::min (to, channel.size()); ++i)
            {
                const float magnitude = std::abs (channel[i]);
                if (magnitude > peak) // also false for NaN
                    peak = magnitude;
            }
        peaks[p] = peak > 0.0f
                     ? static_cast<std::uint8_t> (std::clamp (std::ceil (peak * 255.0f), 1.0f, 255.0f))
                     : 0;
    }
    return peaks;
}

} // namespace ap::dsp
