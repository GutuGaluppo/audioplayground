#include "Golden.h"
#include "ap/fx/Filter.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace ap;
using dsp::FilterMode;
using fx::Filter;
using fx::FilterSettings;

namespace
{
constexpr double fs = 48000.0;

// Deterministic white noise in [-1, 1].
struct Noise
{
    std::uint32_t state = 12345;
    float next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state >> 8) / static_cast<float> (1u << 23) - 1.0f;
    }
};

// Processes a stereo signal in blocks; before each block, configure (blockIndex) may change settings.
template <typename Configure>
std::array<std::vector<float>, 2> run (Filter& filter, std::array<std::vector<float>, 2> audio, int blockSize,
                                       Configure configure)
{
    const auto length = static_cast<int> (audio[0].size());
    for (int start = 0, index = 0; start < length; start += blockSize, ++index)
    {
        configure (index);
        const int n = std::min (blockSize, length - start);
        std::array<float*, 2> channels {audio[0].data() + start, audio[1].data() + start};
        filter.process ({channels.data(), 2, n});
    }
    return audio;
}

// Steady-state gain of the filter at a frequency, in dB.
double gainDb (const FilterSettings& settings, double frequency)
{
    Filter filter;
    filter.prepare (fs);
    filter.set (settings);
    const int length = 48000;
    std::vector<float> sine (static_cast<std::size_t> (length));
    for (int i = 0; i < length; ++i)
        sine[static_cast<std::size_t> (i)]
            = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * frequency * i / fs));
    const auto out = run (filter, {sine, sine}, 256, [] (int) {});

    // Skip the first half (glide and transient settle).
    double in = 0.0;
    double result = 0.0;
    for (auto i = static_cast<std::size_t> (length / 2); i < static_cast<std::size_t> (length); ++i)
    {
        const auto x = static_cast<double> (sine[i]);
        const auto y = static_cast<double> (out[0][i]);
        in += x * x;
        result += y * y;
    }
    return 10.0 * std::log10 (result / in);
}

float maxStep (const std::vector<float>& signal)
{
    float step = 0.0f;
    for (std::size_t i = 1; i < signal.size(); ++i)
        step = std::max (step, std::abs (signal[i] - signal[i - 1]));
    return step;
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
    std::vector<float> sine (24000);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = static_cast<float> (
            0.5 * std::sin (2.0 * std::numbers::pi * 200.0 * static_cast<double> (i) / fs));

    Filter filter;
    filter.prepare (fs);
    filter.set ({FilterMode::lowPass, 1000.0f, 0.7071f});
    const auto switched = run (filter, {sine, sine}, 128,
                               [&filter] (int block)
                               {
                                   if (block == 60)
                                       filter.set ({FilterMode::highPass, 1000.0f, 0.7071f});
                                   if (block == 120)
                                       filter.set ({FilterMode::highPass, 20.0f, 4.0f});
                               });
    CHECK (maxStep (switched[0]) < 0.03f);
}

TEST_CASE ("Filter stays stable under random modulation", "[fx][filter]")
{
    Noise noise;
    std::array<std::vector<float>, 2> input {std::vector<float> (96000), std::vector<float> (96000)};
    for (auto& channel : input)
        for (auto& sample : channel)
            sample = noise.next();

    Filter filter;
    filter.prepare (fs);
    Noise settings {777};
    const auto out
        = run (filter, input, 64,
               [&] (int)
               {
                   const auto mode
                       = static_cast<FilterMode> (static_cast<int> ((settings.next() + 1.0f) * 1.5f) % 3);
                   const float cutoff = 20.0f * std::pow (1000.0f, (settings.next() + 1.0f) / 2.0f);
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
    Noise noise;
    std::array<std::vector<float>, 2> input {std::vector<float> (48000), std::vector<float> (48000)};
    for (std::size_t i = 0; i < input[0].size(); ++i)
    {
        input[0][i] = 0.25f * noise.next();
        input[1][i] = 0.25f * noise.next();
    }

    Filter filter;
    filter.prepare (fs);
    const auto blocks = static_cast<int> (input[0].size() / 256);
    const auto out = run (filter, input, 256,
                          [&filter, blocks] (int block)
                          {
                              const float t = static_cast<float> (block) / static_cast<float> (blocks);
                              filter.set ({t < 0.75f ? FilterMode::lowPass : FilterMode::highPass,
                                           200.0f * std::pow (40.0f, t), 4.0f});
                          });

    const auto result
        = test::compareWithGolden ("filter_sweep_48k", {fs, {out[0], out[1]}}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
