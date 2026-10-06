#include "ap/fx/Limiter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::fx
{

float Limiter::truePeak (const std::array<float, 8>& h) const noexcept AP_NONBLOCKING
{
    float peak = std::abs (h[3]);
    for (const auto& phase : taps)
    {
        float value = 0.0f;
        for (std::size_t j = 0; j < 8; ++j)
            value += phase[j] * h[j];
        peak = std::max (peak, std::abs (value));
    }
    return peak;
}

void Limiter::prepare (double sampleRate)
{
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    // Interpolate at 1/4, 2/4 and 3/4 between history[3] and history[4] (the detector looks at a
    // point 4 samples back, so it sees 4 samples on each side).
    for (std::size_t p = 0; p < taps.size(); ++p)
    {
        const double t = 3.0 + static_cast<double> (p + 1) / 4.0;
        std::array<double, 8> values {};
        double total = 0.0;
        for (std::size_t j = 0; j < 8; ++j)
        {
            const double x = static_cast<double> (j) - t;
            const double sinc = std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
            const double hann = 0.5 + 0.5 * std::cos (std::numbers::pi * x / 4.5);
            values[j] = sinc * hann;
            total += values[j];
        }
        for (std::size_t j = 0; j < 8; ++j)
            taps[p][j] = static_cast<float> (values[j] / total); // unity at DC
    }
    window = std::max (1, static_cast<int> (std::lround (lookaheadSeconds * rate)));
    releaseCoefficient = static_cast<float> (std::exp (-1.0 / (releaseSeconds * rate)));
    ceiling = std::pow (10.0f, ceilingDb / 20.0f);
    required.assign (static_cast<std::size_t> (window) + 1, 1.0f);
    smoothed.assign (static_cast<std::size_t> (window) + 1, 1.0f);
    for (auto& line : delayLines)
        line.assign (static_cast<std::size_t> (latency()), 0.0f);
    reset();
}

void Limiter::reset() noexcept AP_NONBLOCKING
{
    for (auto& h : history)
        h.fill (0.0f);
    std::fill (required.begin(), required.end(), 1.0f);
    std::fill (smoothed.begin(), smoothed.end(), 1.0f);
    for (auto& line : delayLines)
        std::fill (line.begin(), line.end(), 0.0f);
    gainPosition = 0;
    delayPosition = 0;
    sum = static_cast<double> (smoothed.size());
    released = 1.0f;
}

float Limiter::consumeGainReductionDb() noexcept
{
    const float gain = lowestGain.exchange (1.0f, std::memory_order_relaxed);
    return 20.0f * std::log10 (std::max (gain, 1.0e-6f));
}

void Limiter::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty() || required.empty())
        return;
    const int channels = std::min (block.numChannels, 2);
    const auto ringSize = required.size();
    float lowest = 1.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        // Detect the true peak of the point detectorDelay samples back.
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& h = history[static_cast<std::size_t> (ch)];
            std::copy (h.begin() + 1, h.end(), h.begin());
            const float x = block.channels[ch][i];
            h[7] = std::isfinite (x) ? x : 0.0f;
            peak = std::max (peak, truePeak (h));
        }
        required[gainPosition] = peak > ceiling ? ceiling / peak : 1.0f;

        // Hold the lowest requirement of the window, release upwards slowly...
        const float hold = *std::min_element (required.begin(), required.end());
        released = hold < released ? hold : hold + releaseCoefficient * (released - hold);

        // ...and average over the window, so the gain is fully down when the peak comes out.
        sum += static_cast<double> (released) - static_cast<double> (smoothed[gainPosition]);
        smoothed[gainPosition] = released;
        gainPosition = (gainPosition + 1) % ringSize;
        const float gain = std::min (1.0f, static_cast<float> (sum / static_cast<double> (ringSize)));
        lowest = std::min (lowest, gain);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = delayLines[static_cast<std::size_t> (ch)];
            const float delayed = line[delayPosition];
            line[delayPosition] = history[static_cast<std::size_t> (ch)][7];
            block.channels[ch][i] = delayed * gain;
        }
        delayPosition = (delayPosition + 1) % delayLines[0].size();
    }

    float previous = lowestGain.load (std::memory_order_relaxed);
    while (lowest < previous
           && !lowestGain.compare_exchange_weak (previous, lowest, std::memory_order_relaxed))
    {
    }
}

} // namespace ap::fx
