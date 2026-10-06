#include "FxTest.h"
#include "Golden.h"
#include "ap/fx/Compressor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace ap;
using fx::Compressor;
using fx::CompressorSettings;

namespace
{
constexpr double fs = 48000.0;

float peakDb (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    float peak = 0.0f;
    for (auto i = from; i < to; ++i)
        peak = std::max (peak, std::abs (signal[i]));
    return 20.0f * std::log10 (peak);
}

// A 1 kHz sine at the given peak level (dBFS).
std::vector<float> tone (float levelDb, std::size_t length)
{
    return test::sine (1000.0, fs, length, std::pow (10.0, static_cast<double> (levelDb) / 20.0));
}
} // namespace

TEST_CASE ("Compressor static curve: unity below the threshold, 1/ratio above, smooth knee",
           "[fx][compressor]")
{
    CHECK (Compressor::curveDb (-30.0f, -18.0f, 4.0f, 6.0f) == -30.0f);
    CHECK (Compressor::curveDb (-6.0f, -18.0f, 4.0f, 0.0f) == Catch::Approx (-15.0f));
    CHECK (Compressor::curveDb (-6.0f, -18.0f, 1.0f, 6.0f) == Catch::Approx (-6.0f));
    // Inside the knee the curve lies between the two lines and joins both continuously.
    CHECK (Compressor::curveDb (-18.0f, -18.0f, 4.0f, 6.0f) == Catch::Approx (-18.0f - 0.75f * 9.0f / 12.0f));
    CHECK (Compressor::curveDb (-21.0f, -18.0f, 4.0f, 6.0f) == Catch::Approx (-21.0f));
    CHECK (Compressor::curveDb (-15.0f, -18.0f, 4.0f, 6.0f) == Catch::Approx (-17.25f));
}

TEST_CASE ("Compressor reduces loud signals to the curve and leaves quiet ones alone", "[fx][compressor]")
{
    Compressor compressor;
    compressor.prepare (fs);
    compressor.set ({-18.0f, 4.0f, 5.0f, 200.0f, 0.0f, 0.0f});

    const auto quiet = tone (-30.0f, 48000);
    const auto quietOut = test::process (compressor, {quiet, quiet});
    CHECK (peakDb (quietOut[0], 24000, 48000) == Catch::Approx (-30.0f).margin (0.05));

    const auto loud = tone (-6.0f, 48000);
    const auto loudOut = test::process (compressor, {loud, loud});
    CHECK (peakDb (loudOut[0], 24000, 48000) == Catch::Approx (-15.0f).margin (0.5));
    CHECK (compressor.getGainReductionDb() < -8.0f);
}

TEST_CASE ("Compressor attack and release follow their times", "[fx][compressor]")
{
    Compressor compressor;
    compressor.prepare (fs);
    compressor.set ({-30.0f, 20.0f, 10.0f, 100.0f, 0.0f, 0.0f});

    auto signal = tone (-6.0f, 48000);
    std::fill (signal.begin() + 24000, signal.end(), 0.0f); // loud for 0.5 s, then silence
    auto copy = signal;
    std::array<float*, 2> channels {signal.data(), copy.data()};

    // 1 ms into the attack: far from full reduction; 50 ms in (5 time constants): nearly there.
    const auto runFor = [&] (int start, int length)
    {
        compressor.process (
            {std::array<float*, 2> {channels[0] + start, channels[1] + start}.data(), 2, length});
        return compressor.getGainReductionDb();
    };
    const float early = runFor (0, 48);
    const float settled = runFor (48, 2400 - 48);
    const float full = runFor (2400, 24000 - 2400);
    CHECK (early > 0.5f * full);
    CHECK (settled < 0.95f * full);

    // 100 ms after the signal stops: about 1 / e of the reduction is left.
    const float released = runFor (24000, 4800);
    CHECK (released / full == Catch::Approx (std::exp (-1.0f)).margin (0.05));
}

TEST_CASE ("Compressor make-up gain, stereo link and robustness", "[fx][compressor]")
{
    Compressor compressor;
    compressor.prepare (fs);
    compressor.set ({-18.0f, 1.0f, 10.0f, 100.0f, 6.0f, 6.0f}); // ratio 1: only the make-up gain
    const auto quiet = tone (-30.0f, 24000);
    CHECK (peakDb (test::process (compressor, {quiet, quiet})[0], 12000, 24000)
           == Catch::Approx (-24.0f).margin (0.05));

    // Stereo link: a loud left channel reduces the silent-ish right channel by the same gain.
    compressor.set ({-20.0f, 10.0f, 1.0f, 100.0f, 0.0f, 0.0f});
    const auto loudLeft = tone (0.0f, 24000);
    const auto quietRight = tone (-40.0f, 24000);
    const auto linked = test::process (compressor, {loudLeft, quietRight});
    CHECK (peakDb (linked[1], 12000, 24000) < -55.0f);

    // A NaN sample never sticks in the detector.
    compressor.reset();
    auto broken = tone (-30.0f, 4800);
    broken[100] = std::numeric_limits<float>::quiet_NaN();
    const auto out = test::process (compressor, {broken, broken});
    CHECK (std::isfinite (compressor.getGainReductionDb()));
    CHECK (std::isfinite (out[0][4000]));
}

TEST_CASE ("Compressor render matches the golden file", "[fx][compressor][golden]")
{
    // Noise bursts with a fast decay (a drum-like signal), compressed hard with make-up gain.
    auto audio = test::noise (48000, 1.0f, 2024);
    for (std::size_t i = 0; i < audio[0].size(); ++i)
    {
        const auto inBurst = static_cast<float> (i % 6000) / 6000.0f;
        const float envelope = std::exp (-8.0f * inBurst) * (i % 12000 < 6000 ? 0.9f : 0.3f);
        audio[0][i] *= envelope;
        audio[1][i] *= envelope;
    }
    Compressor compressor;
    compressor.prepare (fs);
    compressor.set ({-24.0f, 6.0f, 3.0f, 120.0f, 9.0f, 6.0f});
    const auto out = test::process (compressor, audio);
    const auto result = test::compareWithGolden ("compressor_bursts_48k", {fs, {out[0], out[1]}},
                                                 test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
