#include "Spectrum.h"
#include "ap/dsp/Resampler.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <limits>
#include <numbers>

using ap::dsp::resample;

namespace
{
std::vector<float> sine (double frequency, double rate, std::size_t length)
{
    std::vector<float> x (length);
    for (std::size_t n = 0; n < length; ++n)
        x[n] = static_cast<float> (
            std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (n) / rate));
    return x;
}

double middlePeakDb (const std::vector<float>& y)
{
    double peak = 0.0;
    for (std::size_t n = y.size() / 4; n < 3 * y.size() / 4; ++n)
        peak = std::max (peak, static_cast<double> (std::abs (y[n])));
    return 20.0 * std::log10 (peak + 1.0e-12);
}
} // namespace

TEST_CASE ("Resampler keeps length, pitch and level", "[dsp][resampler][quality]")
{
    const auto [from, to] = GENERATE (std::pair {44100.0, 48000.0}, std::pair {48000.0, 44100.0},
                                      std::pair {96000.0, 48000.0}, std::pair {22050.0, 48000.0});
    CAPTURE (from, to);
    const double lower = std::min (from, to);

    const auto out = resample (sine (1000.0, from, static_cast<std::size_t> (from)), from, to);
    CHECK (out.size() == static_cast<std::size_t> (to));
    CHECK (std::abs (middlePeakDb (out)) < 0.01);

    const std::vector<float> middle (out.begin() + static_cast<std::ptrdiff_t> (out.size() / 4),
                                     out.begin() + static_cast<std::ptrdiff_t> (out.size() / 4 + 16384));
    CHECK (ap::test::inharmonicPowerDb (middle, 1000.0, to) < -80.0);

    const double upper = 0.35 * lower;
    CHECK (std::abs (middlePeakDb (resample (sine (upper, from, static_cast<std::size_t> (from)), from, to)))
           < 0.01);

    const double edge = 0.45 * lower;
    CHECK (middlePeakDb (resample (sine (edge, from, static_cast<std::size_t> (from)), from, to)) > -0.5);
}

TEST_CASE ("Resampler removes content above the new Nyquist when downsampling", "[dsp][resampler][quality]")
{
    for (const auto [from, to] : {std::pair {48000.0, 44100.0}, std::pair {96000.0, 48000.0}})
    {
        CAPTURE (from, to);
        const double above = to * 0.5 * 1.1;
        CHECK (middlePeakDb (resample (sine (above, from, static_cast<std::size_t> (from)), from, to))
               < -85.0);
    }
}

TEST_CASE ("Resampler handles trivial input", "[dsp][resampler]")
{
    CHECK (resample ({}, 44100.0, 48000.0).empty());
    CHECK (resample ({1.0f, 2.0f}, 0.0, 48000.0).empty());
    CHECK (resample ({1.0f, 2.0f}, 48000.0, std::numeric_limits<double>::quiet_NaN()).empty());
    CHECK (resample ({0.5f, -0.5f}, 48000.0, 48000.0) == std::vector<float> {0.5f, -0.5f});

    const auto single = resample ({1.0f}, 44100.0, 48000.0);
    CHECK (single.size() == 1);
    CHECK (std::isfinite (single[0]));
}
