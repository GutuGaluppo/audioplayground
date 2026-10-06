#include "FxTest.h"
#include "Golden.h"
#include "ap/fx/Filter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ap;
using dsp::FilterMode;
using fx::Filter;
using fx::FilterSettings;

namespace
{
constexpr double fs = 48000.0;

double gainDb (const FilterSettings& settings, double frequency)
{
    Filter filter;
    filter.prepare (fs);
    filter.set (settings);
    return test::gainAtDb (filter, frequency, fs);
}
} // namespace

TEST_CASE ("Filter low-pass and high-pass responses are 12 dB per octave Butterworth", "[fx][filter]")
{
    const FilterSettings low {FilterMode::lowPass, 1000.0f, 0.7071f};
    CHECK (gainDb (low, 100.0) == Catch::Approx (0.0).margin (0.1));
    CHECK (gainDb (low, 1000.0) == Catch::Approx (-3.0).margin (0.2));
    CHECK (gainDb (low, 2000.0) == Catch::Approx (-12.3).margin (0.5));
    CHECK (gainDb (low, 8000.0) < -35.0);

    const FilterSettings high {FilterMode::highPass, 1000.0f, 0.7071f};
    CHECK (gainDb (high, 10000.0) == Catch::Approx (0.0).margin (0.1));
    CHECK (gainDb (high, 1000.0) == Catch::Approx (-3.0).margin (0.2));
    CHECK (gainDb (high, 125.0) < -35.0);

    const FilterSettings band {FilterMode::bandPass, 1000.0f, 0.7071f};
    CHECK (gainDb (band, 100.0) < -10.0);
    CHECK (gainDb (band, 10000.0) < -10.0);
}

TEST_CASE ("Filter resonance peaks at the cutoff", "[fx][filter]")
{
    const FilterSettings resonant {FilterMode::lowPass, 1000.0f, 8.0f};
    CHECK (gainDb (resonant, 1000.0) == Catch::Approx (20.0 * std::log10 (8.0)).margin (0.5));
}

TEST_CASE ("Filter settings are clamped to safe ranges", "[fx][filter]")
{
    CHECK (gainDb ({FilterMode::lowPass, 1.0e9f, 0.7071f}, 1000.0) == Catch::Approx (0.0).margin (0.1));
    CHECK (gainDb ({FilterMode::lowPass, std::nanf (""), 1000.0f}, 100.0)
           == Catch::Approx (0.0).margin (0.3));
    CHECK (std::isfinite (gainDb ({FilterMode::highPass, -5.0f, -1.0f}, 1000.0)));
}

TEST_CASE ("Changing the filter mode or cutoff never jumps", "[fx][filter]")
{
    // A 200 Hz sine at 0.5 changes by at most 0.013 per sample; an instant LP -> HP switch would
    // jump by ~0.5.
    const auto sine = test::sine (200.0, fs, 24000);
    Filter filter;
    filter.prepare (fs);
    filter.set ({FilterMode::lowPass, 1000.0f, 0.7071f});
    const auto switched = test::process (filter, {sine, sine}, 128,
                                         [&filter] (int block)
                                         {
                                             if (block == 60)
                                                 filter.set ({FilterMode::highPass, 1000.0f, 0.7071f});
                                             if (block == 120)
                                                 filter.set ({FilterMode::highPass, 20.0f, 4.0f});
                                         });
    CHECK (test::maxStep (switched[0]) < 0.03f);
}

TEST_CASE ("Filter stays stable under random modulation", "[fx][filter]")
{
    Filter filter;
    filter.prepare (fs);
    test::Noise settings {777};
    const auto out = test::process (filter, test::noise (96000, 1.0f), 64,
                                    [&] (int)
                                    {
                                        const auto mode = static_cast<FilterMode> (
                                            static_cast<int> ((settings.next() + 1.0f) * 1.5f) % 3);
                                        const float cutoff
                                            = 20.0f * std::pow (1000.0f, (settings.next() + 1.0f) / 2.0f);
                                        const float q = 0.5f + (settings.next() + 1.0f) * 4.75f;
                                        filter.set ({mode, cutoff, q});
                                    });
    float peak = 0.0f;
    for (const auto& channel : out)
        for (const float sample : channel)
        {
            REQUIRE (std::isfinite (sample));
            peak = std::max (peak, std::abs (sample));
        }
    CHECK (peak < 40.0f); // resonance 10 can gain 20 dB; nothing runs away
}

TEST_CASE ("Filter sweep matches the golden file", "[fx][filter][golden]")
{
    Filter filter;
    filter.prepare (fs);
    constexpr int blocks = 48000 / 256;
    const auto out = test::process (filter, test::noise (48000, 0.25f), 256,
                                    [&filter] (int block)
                                    {
                                        const float t
                                            = static_cast<float> (block) / static_cast<float> (blocks);
                                        filter.set ({t < 0.75f ? FilterMode::lowPass : FilterMode::highPass,
                                                     200.0f * std::pow (40.0f, t), 4.0f});
                                    });

    const auto result
        = test::compareWithGolden ("filter_sweep_48k", {fs, {out[0], out[1]}}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
