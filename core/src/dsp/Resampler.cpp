#include "ap/dsp/Resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::dsp
{
namespace
{
constexpr int zeroCrossings = 32;          // kernel half-length, in input samples at unity ratio
constexpr int phasesPerZeroCrossing = 512; // kernel table resolution (linear interpolation between)
constexpr double kaiserBeta = 9.0;         // ~ -90 dB side lobes
constexpr double cutoffFraction = 0.95;    // transition band just below Nyquist of the lower rate

double besselI0 (double x)
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        const double half = x / (2.0 * k);
        term *= half * half;
        sum += term;
        if (term < sum * 1.0e-17)
            break;
    }
    return sum;
}

// Windowed sinc sampled at phasesPerZeroCrossing points per zero crossing for t in
// [0, zeroCrossings]. The kernel is symmetric, so only the positive half is stored.
std::vector<double> makeKernel()
{
    const int size = zeroCrossings * phasesPerZeroCrossing + 2;
    std::vector<double> table (static_cast<std::size_t> (size), 0.0);
    const double norm = besselI0 (kaiserBeta);

    for (int i = 0; i < size; ++i)
    {
        const double t = static_cast<double> (i) / phasesPerZeroCrossing;
        if (t >= zeroCrossings)
            break;
        const double sinc = t == 0.0 ? 1.0 : std::sin (std::numbers::pi * t) / (std::numbers::pi * t);
        const double r = t / zeroCrossings;
        const double window = besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / norm;
        table[static_cast<std::size_t> (i)] = sinc * window;
    }
    return table;
}

const std::vector<double>& kernel()
{
    static const std::vector<double> table = makeKernel();
    return table;
}

double kernelAt (double t)
{
    const double position = std::abs (t) * phasesPerZeroCrossing;
    const auto index = static_cast<std::size_t> (position);
    const auto& table = kernel();
    if (index + 1 >= table.size())
        return 0.0;
    const double frac = position - static_cast<double> (index);
    return table[index] + frac * (table[index + 1] - table[index]);
}
} // namespace

std::vector<float> resample (const std::vector<float>& input, double fromRate, double toRate)
{
    if (input.empty() || !(fromRate > 0.0) || !(toRate > 0.0))
        return {};
    if (fromRate == toRate)
        return input;

    const double ratio = toRate / fromRate;                      // output samples per input sample
    const double scale = std::min (1.0, ratio) * cutoffFraction; // lower cutoff when downsampling
    const double halfWidth = zeroCrossings / scale;              // kernel reach, in input samples
    const auto inputLength = static_cast<std::ptrdiff_t> (input.size());
    const auto outputLength
        = static_cast<std::size_t> (std::llround (static_cast<double> (input.size()) * ratio));

    std::vector<float> output (outputLength, 0.0f);
    for (std::size_t n = 0; n < outputLength; ++n)
    {
        const double centre = static_cast<double> (n) / ratio; // position in the input
        const auto first
            = std::max<std::ptrdiff_t> (0, static_cast<std::ptrdiff_t> (std::ceil (centre - halfWidth)));
        const auto last = std::min<std::ptrdiff_t> (
            inputLength - 1, static_cast<std::ptrdiff_t> (std::floor (centre + halfWidth)));

        double sum = 0.0;
        for (auto k = first; k <= last; ++k)
            sum += static_cast<double> (input[static_cast<std::size_t> (k)])
                 * kernelAt ((centre - static_cast<double> (k)) * scale);
        output[n] = static_cast<float> (sum * scale);
    }
    return output;
}

} // namespace ap::dsp
