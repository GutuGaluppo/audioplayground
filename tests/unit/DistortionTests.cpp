#include "FxTest.h"
#include "Golden.h"
#include "Spectrum.h"
#include "ap/dsp/Oversampler.h"
#include "ap/fx/Distortion.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <limits>

using namespace ap;
using fx::Distortion;
using fx::DistortionSettings;

namespace
{
constexpr double fs = 48000.0;
constexpr auto latency = static_cast<std::size_t> (Distortion::latency());

double peakDb (const std::vector<float>& signal, std::size_t from)
{
    float peak = 0.0f;
    for (auto i = from; i < signal.size(); ++i)
        peak = std::max (peak, std::abs (signal[i]));
    return test::toDb (static_cast<double> (peak));
}
} // namespace

TEST_CASE ("4x oversampling round trip is a pure delay in the audible band", "[dsp][oversampling]")
{
    const double frequency = GENERATE (100.0, 1000.0, 10000.0, 18000.0);
    CAPTURE (frequency);

    dsp::Oversampler4x oversampler;
    const auto input = test::sine (frequency, fs, 8000);
    float error = 0.0f;
    std::array<float, 4> up {};
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        oversampler.upsample (input[i], up);
        const float out = oversampler.downsample (up);
        if (i >= 400)
            error = std::max (
                error, std::abs (out - input[i - static_cast<std::size_t> (dsp::Oversampler4x::latency())]));
    }
    CHECK (error < (frequency < 15000.0 ? 1.0e-3f : 2.0e-2f)); // 18 kHz sits near the band edge
}

TEST_CASE ("Distortion with the mix at zero is the input, delayed by its latency", "[fx][distortion]")
{
    Distortion dryOnly;
    dryOnly.prepare (fs);
    dryOnly.set ({24.0f, 0.5f, 0.0f, 0.0f});
    (void)test::process (dryOnly, test::noise (4800, 0.0f)); // let the mix glide to 0
    const auto input = test::noise (4800, 0.5f);
    const auto out = test::process (dryOnly, input);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = latency; i < input[ch].size(); ++i)
            REQUIRE (out[ch][i] == input[ch][i - latency]);
}

TEST_CASE ("Oversampled distortion keeps aliasing far below the harmonics", "[fx][distortion]")
{
    Distortion distortion;
    distortion.prepare (fs);
    distortion.set ({24.0f, 1.0f, 0.0f, 1.0f});
    // 4567 Hz does not divide 48 kHz, so folded harmonics land between the real ones. Without
    // oversampling this measures about -16 dB; at 4x, -72 dB (measured). Only extreme drive on
    // high notes (30 dB at 4.5 kHz) comes back up to about -44 dB.
    const auto input = test::sine (4567.0, fs, 32768 + 8192);
    const auto out = test::process (distortion, {input, input});
    const std::vector<float> settled (out[0].begin() + 8192, out[0].end());
    const auto aliasing = test::inharmonicPowerDb (settled, 4567.0, fs);
    INFO ("inharmonic power " << aliasing << " dB");
    CHECK (aliasing < -68.0);
}

TEST_CASE ("Distortion level, DC and smooth setting changes", "[fx][distortion]")
{
    // -12 dBFS keeps roughly its peak level whatever the drive (heavy drive squares the wave, and
    // band-limiting a square overshoots a little).
    for (const float drive : {0.0f, 12.0f, 24.0f, 36.0f})
    {
        Distortion distortion;
        distortion.prepare (fs);
        distortion.set ({drive, 1.0f, 0.0f, 1.0f});
        const auto input = test::sine (220.0, fs, 48000, 0.25);
        const auto out = test::process (distortion, {input, input});
        CAPTURE (drive);
        CHECK (peakDb (out[0], 24000) == Catch::Approx (-12.0).margin (2.5));

        // The asymmetric shaper adds DC; the blocker removes it.
        double mean = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i)
            mean += static_cast<double> (out[0][i]);
        CHECK (std::abs (mean / 24000.0) < 1.0e-3);
    }

    Distortion distortion;
    distortion.prepare (fs);
    const auto sine = test::sine (200.0, fs, 24000);
    const auto out = test::process (distortion, {sine, sine}, 128,
                                    [&distortion] (int block)
                                    {
                                        if (block == 50)
                                            distortion.set ({36.0f, 0.0f, 6.0f, 0.3f});
                                        if (block == 120)
                                            distortion.set ({0.0f, 1.0f, -6.0f, 1.0f});
                                    });
    CHECK (test::maxStep (out[0]) < 0.15f);

    // A broken input sample is treated as silence.
    auto broken = test::sine (200.0, fs, 4800);
    broken[1000] = std::numeric_limits<float>::infinity();
    const auto robust = test::process (distortion, {broken, broken});
    for (const float sample : robust[0])
        REQUIRE (std::isfinite (sample));
}

TEST_CASE ("Distortion render matches the golden file", "[fx][distortion][golden]")
{
    // A sine sweeping from 80 Hz to 2 kHz, driven hard with a darker tone and half mix.
    std::vector<float> sweep (48000);
    double phase = 0.0;
    for (std::size_t i = 0; i < sweep.size(); ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (sweep.size());
        phase += 2.0 * std::numbers::pi * 80.0 * std::pow (25.0, t) / fs;
        sweep[i] = static_cast<float> (0.5 * std::sin (phase));
    }
    Distortion distortion;
    distortion.prepare (fs);
    distortion.set ({24.0f, 0.4f, -3.0f, 0.8f});
    const auto out = test::process (distortion, {sweep, sweep});
    const auto result = test::compareWithGolden ("distortion_sweep_48k", {fs, {out[0], out[1]}},
                                                 test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
