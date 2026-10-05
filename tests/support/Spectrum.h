#pragma once

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace ap::test
{

// In-place radix-2 FFT (size must be a power of two). Test-only: clarity over speed.
inline void fft (std::vector<std::complex<double>>& data)
{
    const auto n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double> (length);
        const std::complex<double> step (std::cos (angle), std::sin (angle));
        for (std::size_t i = 0; i < n; i += length)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto u = data[i + k];
                const auto v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}

// Power of the windowed signal that does NOT sit on a harmonic of `fundamental`, relative to
// the total, in dB. For an ideal band-limited periodic waveform this is the noise floor; for an
// aliasing oscillator the folded components raise it.
template <typename Samples>
double inharmonicPowerDb (const Samples& signal, double fundamental, double sampleRate)
{
    const auto n = signal.size();
    std::vector<std::complex<double>> bins (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        // 4-term Blackman-Harris: very low leakage, so harmonics do not mask aliasing.
        const double x = 2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n - 1);
        const double window
            = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
        bins[i] = static_cast<double> (signal[i]) * window;
    }
    fft (bins);

    const double binHz = sampleRate / static_cast<double> (n);
    double harmonic = 0.0, inharmonic = 0.0;
    for (std::size_t k = 1; k < n / 2; ++k)
    {
        const double frequency = static_cast<double> (k) * binHz;
        const double nearest = std::round (frequency / fundamental) * fundamental;
        const double power = std::norm (bins[k]);
        if (std::abs (frequency - nearest) <= 6.0 * binHz && nearest > 0.0)
            harmonic += power;
        else
            inharmonic += power;
    }
    return 10.0 * std::log10 (inharmonic / (harmonic + inharmonic) + 1.0e-30);
}

} // namespace ap::test
