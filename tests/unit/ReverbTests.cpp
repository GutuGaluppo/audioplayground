#include "FxTest.h"
#include "Golden.h"
#include "ap/core/ScopedNoDenormals.h"
#include "ap/fx/Reverb.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace ap;
using fx::Reverb;
using fx::ReverbSettings;

namespace
{
constexpr double fs = 48000.0;

Reverb prepared (const ReverbSettings& settings)
{
    Reverb reverb;
    reverb.prepare (fs);
    reverb.set (settings);
    (void)test::process (reverb, test::Stereo {std::vector<float> (24000), std::vector<float> (24000)});
    reverb.reset();
    return reverb;
}

test::Stereo impulse (std::size_t length)
{
    test::Stereo audio {std::vector<float> (length, 0.0f), std::vector<float> (length, 0.0f)};
    audio[0][0] = 1.0f;
    audio[1][0] = 1.0f;
    return audio;
}

// Energy of [from, to) seconds, in dB.
double energyDb (const std::vector<float>& signal, double from, double to)
{
    const auto rms
        = test::rms (signal, static_cast<std::size_t> (from * fs), static_cast<std::size_t> (to * fs));
    return test::toDb (rms + 1.0e-20);
}

// Brightness of a signal: mean absolute first difference over mean absolute value.
double brightness (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double change = 0.0;
    double level = 0.0;
    for (auto i = from + 1; i < to; ++i)
    {
        change += std::abs (static_cast<double> (signal[i] - signal[i - 1]));
        level += std::abs (static_cast<double> (signal[i]));
    }
    return change / level;
}
} // namespace

TEST_CASE ("Reverb tail decays, and lasts longer with more decay", "[fx][reverb]")
{
    const core::ScopedNoDenormals noDenormals;
    auto shortRoom = prepared ({0.6f, 0.2f, 0.4f, 1.0f});
    auto longRoom = prepared ({0.6f, 0.9f, 0.4f, 1.0f});
    const auto shortTail = test::process (shortRoom, impulse (3 * 48000));
    const auto longTail = test::process (longRoom, impulse (3 * 48000));

    CHECK (energyDb (shortTail[0], 0.0, 0.25) - energyDb (shortTail[0], 1.5, 2.0) > 40.0);
    CHECK (energyDb (longTail[0], 1.5, 2.0) - energyDb (shortTail[0], 1.5, 2.0) > 20.0);
    CHECK (energyDb (longTail[0], 2.5, 3.0) < energyDb (longTail[0], 0.0, 0.5)); // still decaying
}

TEST_CASE ("Reverb tail is stereo, dark with damping, and dry at mix 0", "[fx][reverb]")
{
    const core::ScopedNoDenormals noDenormals;
    auto reverb = prepared ({0.6f, 0.6f, 0.0f, 1.0f});
    const auto bright = test::process (reverb, impulse (48000));

    // Left and right tails are decorrelated.
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = 2400; i < 48000; ++i)
    {
        lr += static_cast<double> (bright[0][i]) * static_cast<double> (bright[1][i]);
        ll += static_cast<double> (bright[0][i]) * static_cast<double> (bright[0][i]);
        rr += static_cast<double> (bright[1][i]) * static_cast<double> (bright[1][i]);
    }
    CHECK (std::abs (lr / std::sqrt (ll * rr)) < 0.3);

    auto damped = prepared ({0.6f, 0.6f, 1.0f, 1.0f});
    const auto dark = test::process (damped, impulse (48000));
    CHECK (brightness (dark[0], 12000, 48000) < 0.7 * brightness (bright[0], 12000, 48000));

    auto dry = prepared ({0.6f, 0.9f, 0.4f, 0.0f});
    const auto input = test::noise (12000, 0.5f);
    const auto out = test::process (dry, input);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = 0; i < input[ch].size(); ++i)
            REQUIRE (out[ch][i] == input[ch][i]);
}

TEST_CASE ("Reverb stays bounded at maximum decay, survives broken input, and morphs size smoothly",
           "[fx][reverb]")
{
    const core::ScopedNoDenormals noDenormals;
    auto reverb = prepared ({1.0f, 1.0f, 0.0f, 1.0f});
    auto input = test::noise (96000, 1.0f);
    input[1][700] = std::numeric_limits<float>::infinity();
    const auto out = test::process (reverb, input);
    for (const auto& channel : out)
    {
        float firstHalf = 0.0f;
        float secondHalf = 0.0f;
        for (std::size_t i = 0; i < channel.size(); ++i)
        {
            REQUIRE (std::isfinite (channel[i]));
            auto& peak = i < channel.size() / 2 ? firstHalf : secondHalf;
            peak = std::max (peak, std::abs (channel[i]));
        }
        // Full-scale noise into the longest tail builds up a loud wash, but it settles: the level
        // stops growing instead of running away.
        CHECK (secondHalf < 8.0f);
        CHECK (secondHalf < 1.5f * firstHalf);
    }

    // A size change on a sustained sine morphs the tail rather than stepping.
    auto morphing = prepared ({0.2f, 0.6f, 0.4f, 0.5f});
    const auto sine = test::sine (220.0, fs, 48000, 0.3);
    const auto morphed = test::process (morphing, {sine, sine}, 128,
                                        [&morphing] (int block)
                                        {
                                            if (block == 150)
                                                morphing.set ({1.0f, 0.6f, 0.4f, 0.5f});
                                        });
    CHECK (test::maxStep (morphed[0]) < 0.1f);
}

TEST_CASE ("Reverb render matches the golden file", "[fx][reverb][golden]")
{
    const core::ScopedNoDenormals noDenormals;
    Reverb reverb;
    reverb.prepare (fs);
    auto audio = test::noise (72000, 0.0f);
    const auto click = test::noise (2400, 0.8f, 99); // a 50 ms noise hit
    for (std::size_t i = 0; i < 2400; ++i)
    {
        const auto fade = static_cast<float> (2400 - i) / 2400.0f;
        audio[0][i] = click[0][i] * fade;
        audio[1][i] = click[1][i] * fade;
    }
    reverb.set ({0.7f, 0.7f, 0.5f, 0.4f});
    const auto out = test::process (reverb, audio);
    const auto result
        = test::compareWithGolden ("reverb_hit_48k", {fs, {out[0], out[1]}}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
