#include "ap/dsp/Oversampler.h"

#include <cmath>
#include <numbers>

namespace ap::dsp
{
namespace
{
// Modified Bessel function of the first kind, order 0 (series; converges quickly for beta < 20).
double besselI0 (double x)
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1.0e-12 * sum)
            break;
    }
    return sum;
}
} // namespace

HalfbandFilter::HalfbandFilter (int numTaps, double kaiserBeta)
    : length (numTaps)
    , history (static_cast<std::size_t> (numTaps), 0.0f)
{
    const int centre = (length - 1) / 2;
    std::vector<double> taps (static_cast<std::size_t> (length), 0.0);
    double sum = 0.0;
    for (int n = 0; n < length; ++n)
    {
        const int m = n - centre;
        const double sinc = m == 0 ? 0.5 : std::sin (std::numbers::pi * m / 2.0) / (std::numbers::pi * m);
        const double r = 2.0 * n / (length - 1) - 1.0;
        const double window = besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / besselI0 (kaiserBeta);
        taps[static_cast<std::size_t> (n)] = sinc * window;
        sum += taps[static_cast<std::size_t> (n)];
    }
    for (int n = 0; n < length; ++n)
    {
        const double tap = taps[static_cast<std::size_t> (n)] / sum; // unity gain at DC
        if (std::abs (tap) > 1.0e-12)
        {
            offsets.push_back (n);
            weights.push_back (static_cast<float> (tap));
        }
    }
}

void HalfbandFilter::reset() noexcept AP_NONBLOCKING
{
    for (auto& sample : history)
        sample = 0.0f;
    position = 0;
}

void HalfbandFilter::push (float sample) noexcept AP_NONBLOCKING
{
    position = position == 0 ? history.size() - 1 : position - 1;
    history[position] = sample;
}

float HalfbandFilter::convolve() const noexcept AP_NONBLOCKING
{
    // history[position] is the newest sample; tap n multiplies the sample n steps back.
    const auto size = history.size();
    float sum = 0.0f;
    for (std::size_t t = 0; t < offsets.size(); ++t)
    {
        auto index = position + static_cast<std::size_t> (offsets[t]);
        if (index >= size)
            index -= size;
        sum += weights[t] * history[index];
    }
    return sum;
}

void HalfbandFilter::upsample (float input, float* output) noexcept AP_NONBLOCKING
{
    // Zero-stuffing halves the level; the factor 2 restores it.
    push (input);
    output[0] = 2.0f * convolve();
    push (0.0f);
    output[1] = 2.0f * convolve();
}

float HalfbandFilter::downsample (const float* input) noexcept AP_NONBLOCKING
{
    push (input[0]);
    push (input[1]);
    return convolve();
}

Oversampler4x::Oversampler4x() = default;

void Oversampler4x::reset() noexcept AP_NONBLOCKING
{
    up1.reset();
    up2.reset();
    down2.reset();
    down1.reset();
    pad = 0.0f;
}

void Oversampler4x::upsample (float input, std::array<float, factor>& output) noexcept AP_NONBLOCKING
{
    std::array<float, 2> twice {};
    up1.upsample (input, twice.data());
    up2.upsample (twice[0], output.data());
    up2.upsample (twice[1], output.data() + 2);

    // Delay by one 4x sample.
    const std::array<float, factor> current = output;
    output = {pad, current[0], current[1], current[2]};
    pad = current[3];
}

float Oversampler4x::downsample (const std::array<float, factor>& input) noexcept AP_NONBLOCKING
{
    const std::array<float, 2> twice {down2.downsample (input.data()), down2.downsample (input.data() + 2)};
    return down1.downsample (twice.data());
}

} // namespace ap::dsp
