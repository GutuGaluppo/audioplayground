#include "FxTest.h"
#include "Golden.h"
#include "ap/fx/Delay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace ap;
using fx::Delay;
using fx::DelaySettings;

namespace
{
constexpr double fs = 48000.0;

// A short low-frequency burst (Hann-windowed 200 Hz), so the feedback damping barely touches it.
test::Stereo burst (std::size_t length)
{
    test::Stereo audio {std::vector<float> (length, 0.0f), std::vector<float> (length, 0.0f)};
    for (std::size_t i = 0; i < 480; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / 480.0);
        const auto value = static_cast<float> (
            0.5 * window * std::sin (2.0 * std::numbers::pi * 200.0 * static_cast<double> (i) / fs));
        audio[0][i] = value;
        audio[1][i] = value;
    }
    return audio;
}

float peakIn (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    float peak = 0.0f;
    for (auto i = from; i < std::min (to, signal.size()); ++i)
        peak = std::max (peak, std::abs (signal[i]));
    return peak;
}

Delay prepared (const DelaySettings& settings)
{
    Delay delay;
    delay.prepare (fs);
    delay.set (settings);
    (void)test::process (
        delay, test::Stereo {std::vector<float> (9600), std::vector<float> (9600)}); // settle the glides
    delay.reset();
    return delay;
}
} // namespace

TEST_CASE ("Delay repeats arrive on time and decay by the feedback", "[fx][delay]")
{
    auto delay = prepared ({100.0f, 0.5f, 1.0f}); // wet only: 4800 samples per repeat
    const auto out = test::process (delay, burst (24000));

    CHECK (peakIn (out[0], 0, 4700) < 1.0e-6f); // nothing before the first repeat
    const float first = peakIn (out[0], 4800, 5400);
    const float second = peakIn (out[0], 9600, 10200);
    const float third = peakIn (out[0], 14400, 15000);
    CHECK (first == Catch::Approx (peakIn (burst (480)[0], 0, 480)).epsilon (0.01));
    CHECK (second / first == Catch::Approx (0.5).epsilon (0.03));
    CHECK (third / second == Catch::Approx (0.5).epsilon (0.03));
    CHECK (peakIn (out[0], 5400, 9500) < 1.0e-3f); // silence between repeats
}

TEST_CASE ("Delay mix at zero passes the input unchanged", "[fx][delay]")
{
    auto delay = prepared ({250.0f, 0.9f, 0.0f});
    const auto input = test::noise (12000, 0.5f);
    const auto out = test::process (delay, input);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = 0; i < input[ch].size(); ++i)
            REQUIRE (out[ch][i] == input[ch][i]);
}

TEST_CASE ("Changing the delay time glides instead of clicking", "[fx][delay]")
{
    Delay delay;
    delay.prepare (fs);
    delay.set ({100.0f, 0.0f, 1.0f});
    const auto sine = test::sine (220.0, fs, 48000);
    const auto out = test::process (delay, {sine, sine}, 128,
                                    [&delay] (int block)
                                    {
                                        if (block == 100)
                                            delay.set ({400.0f, 0.0f, 1.0f});
                                        if (block == 250)
                                            delay.set ({5.0f, 0.0f, 1.0f});
                                    });
    // Gliding bends the pitch (shortening by 395 ms in 150 ms plays 3.6x faster, so the 220 Hz
    // sine at 0.5 then moves up to 0.052 per sample), but never jumps: a click would be ~0.5.
    CHECK (test::maxStep (out[0]) < 0.06f);
}

TEST_CASE ("Delay stays bounded at maximum feedback and survives broken input", "[fx][delay]")
{
    auto delay = prepared ({30.0f, 1.0f, 1.0f}); // clamped to 0.95
    auto input = test::noise (96000, 1.0f);
    input[0][500] = std::numeric_limits<float>::quiet_NaN();
    const auto out = test::process (delay, input);
    for (const auto& channel : out)
        for (const float sample : channel)
        {
            REQUIRE (std::isfinite (sample));
            REQUIRE (std::abs (sample) < 2.5f);
        }
}

TEST_CASE ("Delay render matches the golden file", "[fx][delay][golden]")
{
    Delay delay;
    delay.prepare (fs);
    auto audio = burst (96000);
    for (std::size_t i = 24000; i < 24480; ++i) // a second burst, then the time changes
    {
        audio[0][i] = -audio[0][i - 24000];
        audio[1][i] = audio[0][i];
    }
    const auto out = test::process (delay, audio, 256,
                                    [&delay] (int block)
                                    {
                                        delay.set (block < 150 ? DelaySettings {187.5f, 0.6f, 0.5f}
                                                               : DelaySettings {300.0f, 0.7f, 0.5f});
                                    });
    const auto result
        = test::compareWithGolden ("delay_repeats_48k", {fs, {out[0], out[1]}}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
