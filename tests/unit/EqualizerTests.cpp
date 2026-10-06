#include "FxTest.h"
#include "Golden.h"
#include "ap/fx/Equalizer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ap;
using fx::Equalizer;
using fx::EqualizerSettings;

namespace
{
constexpr double fs = 48000.0;

double gainDb (const EqualizerSettings& settings, double frequency)
{
    Equalizer eq;
    eq.prepare (fs);
    eq.set (settings);
    return test::gainAtDb (eq, frequency, fs);
}

EqualizerSettings withBand (std::size_t band, EqualizerSettings::Band value)
{
    EqualizerSettings settings;
    settings.bands[band] = value;
    return settings;
}
} // namespace

TEST_CASE ("A flat equaliser passes audio through unchanged", "[fx][eq]")
{
    Equalizer eq;
    eq.prepare (fs);
    const auto input = test::noise (4800, 0.5f);
    const auto out = test::process (eq, input);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = 0; i < input[ch].size(); ++i)
            REQUIRE (out[ch][i] == Catch::Approx (input[ch][i]).margin (1.0e-6));
}

TEST_CASE ("The mid band is a bell centred on its frequency", "[fx][eq]")
{
    const auto boost = withBand (1, {1000.0f, 1.0f, 6.0f});
    CHECK (gainDb (boost, 1000.0) == Catch::Approx (6.0).margin (0.1));
    CHECK (std::abs (gainDb (boost, 50.0)) < 0.2);
    CHECK (std::abs (gainDb (boost, 15000.0)) < 0.3);

    const auto cut = withBand (1, {3000.0f, 4.0f, -12.0f});
    CHECK (gainDb (cut, 3000.0) == Catch::Approx (-12.0).margin (0.2));
    CHECK (std::abs (gainDb (cut, 1000.0)) < 0.5); // narrow
}

TEST_CASE ("The low and high bands are shelves", "[fx][eq]")
{
    const auto low = withBand (0, {200.0f, 0.7071f, 6.0f});
    CHECK (gainDb (low, 25.0) == Catch::Approx (6.0).margin (0.2));
    CHECK (gainDb (low, 200.0) == Catch::Approx (3.0).margin (0.3)); // half the gain at the corner
    CHECK (std::abs (gainDb (low, 5000.0)) < 0.1);

    const auto high = withBand (2, {4000.0f, 0.7071f, -9.0f});
    CHECK (gainDb (high, 18000.0) == Catch::Approx (-9.0).margin (0.3));
    CHECK (std::abs (gainDb (high, 200.0)) < 0.1);
}

TEST_CASE ("Equaliser settings are clamped and moving a band never jumps", "[fx][eq]")
{
    CHECK (gainDb (withBand (1, {1000.0f, 1.0f, 100.0f}), 1000.0) == Catch::Approx (18.0).margin (0.2));
    CHECK (std::isfinite (gainDb (withBand (1, {std::nanf (""), -3.0f, std::nanf ("")}), 1000.0)));

    const auto sine = test::sine (300.0, fs, 24000);
    Equalizer eq;
    eq.prepare (fs);
    const auto out = test::process (eq, {sine, sine}, 128,
                                    [&eq] (int block)
                                    {
                                        if (block == 40)
                                            eq.set (withBand (1, {300.0f, 2.0f, 18.0f}));
                                        if (block == 100)
                                            eq.set (withBand (0, {400.0f, 0.7071f, -18.0f}));
                                    });
    // 300 Hz at 0.5 moves at most 0.02 per sample; +18 dB (x7.9) raises that to ~0.16.
    CHECK (test::maxStep (out[0]) < 0.17f);
}

TEST_CASE ("Equaliser stays stable under random modulation", "[fx][eq]")
{
    Equalizer eq;
    eq.prepare (fs);
    test::Noise random {99};
    const auto out
        = test::process (eq, test::noise (96000, 1.0f), 64,
                         [&] (int)
                         {
                             EqualizerSettings settings;
                             for (auto& band : settings.bands)
                                 band = {20.0f * std::pow (1000.0f, (random.next() + 1.0f) / 2.0f),
                                         0.3f + (random.next() + 1.0f) * 4.85f, random.next() * 18.0f};
                             eq.set (settings);
                         });
    for (const auto& channel : out)
        for (const float sample : channel)
        {
            REQUIRE (std::isfinite (sample));
            REQUIRE (std::abs (sample) < 100.0f);
        }
}

TEST_CASE ("Equaliser render matches the golden file", "[fx][eq][golden]")
{
    Equalizer eq;
    eq.prepare (fs);
    constexpr int blocks = 48000 / 256;
    const auto out = test::process (eq, test::noise (48000, 0.2f, 4242), 256,
                                    [&eq] (int block)
                                    {
                                        const float t
                                            = static_cast<float> (block) / static_cast<float> (blocks);
                                        EqualizerSettings settings;
                                        settings.bands[0] = {100.0f, 0.7071f, 6.0f};
                                        settings.bands[1] = {300.0f * std::pow (20.0f, t), 3.0f, 12.0f};
                                        settings.bands[2] = {6000.0f, 0.7071f, -12.0f};
                                        eq.set (settings);
                                    });
    const auto result
        = test::compareWithGolden ("eq_sweep_48k", {fs, {out[0], out[1]}}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
