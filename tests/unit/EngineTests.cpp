#include "TestAudio.h"
#include "ap/engine/Engine.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <limits>

using ap::engine::Engine;
using ap::test::TestBuffer;

namespace
{
float peakOf (const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (const float s : samples)
        peak = std::max (peak, std::abs (s));
    return peak;
}

float maxStep (const std::vector<float>& samples)
{
    float step = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        step = std::max (step, std::abs (samples[i] - samples[i - 1]));
    return step;
}

void setToneLevel (Engine& engine, float db)
{
    engine.getParameters().set (ap::params::ParamId::toneLevel, db);
}

bool allFinite (const std::vector<float>& samples)
{
    return std::all_of (samples.begin(), samples.end(), [] (float s) { return std::isfinite (s); });
}
} // namespace

TEST_CASE ("Engine outputs silence before prepare", "[engine]")
{
    Engine engine;
    engine.setTestToneEnabled (true);

    TestBuffer buffer (2, 256);
    buffer.fill (1.0f); // stale data the engine must overwrite
    engine.process (buffer.block());

    CHECK (peakOf (buffer.channel (0)) == 0.0f);
    CHECK (peakOf (buffer.channel (1)) == 0.0f);
}

TEST_CASE ("Engine outputs silence when the tone is disabled", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 512);

    TestBuffer buffer (2, 512);
    engine.process (buffer.block());

    CHECK (peakOf (buffer.channel (0)) == 0.0f);
    CHECK (peakOf (buffer.channel (1)) == 0.0f);
}

TEST_CASE ("Engine tone fades in without a click and settles at the requested level", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 4800);
    engine.setTestToneFrequency (1000.0f);
    setToneLevel (engine, -12.0f);
    engine.setTestToneEnabled (true);

    TestBuffer buffer (2, 4800); // 100 ms
    engine.process (buffer.block());

    const auto& left = buffer.channel (0);
    const float expectedPeak = std::pow (10.0f, -12.0f / 20.0f);

    CHECK (std::abs (left.front()) < 1.0e-3f);
    CHECK (peakOf (left) <= expectedPeak + 1.0e-4f);
    CHECK (peakOf (left) > expectedPeak * 0.99f);
    CHECK (allFinite (left));
    CHECK (left == buffer.channel (1));
}

TEST_CASE ("Engine tone is continuous across irregular block sizes", "[engine]")
{
    const int blockSize = GENERATE (1, 17, 64, 128, 333, 512, 2048);
    CAPTURE (blockSize);

    Engine engine;
    engine.prepare (44100.0, blockSize);
    engine.setTestToneFrequency (440.0f);
    setToneLevel (engine, -6.0f);
    engine.setTestToneEnabled (true);

    constexpr int totalSamples = 44100 / 2;
    TestBuffer buffer (2, totalSamples);
    for (int start = 0; start < totalSamples; start += blockSize)
        engine.process (buffer.slice (start, std::min (blockSize, totalSamples - start)));

    // A 440 Hz sine at -6 dB moves at most 2*pi*f/fs*A per sample; anything larger is a discontinuity.
    const float amplitude = std::pow (10.0f, -6.0f / 20.0f);
    const float maxExpectedStep = 2.0f * 3.14159265f * 440.0f / 44100.0f * amplitude * 1.01f;
    CHECK (maxStep (buffer.channel (0)) <= maxExpectedStep);
}

TEST_CASE ("Engine fades out to exact silence after the tone is disabled", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 4800);
    engine.setTestToneEnabled (true);

    TestBuffer warmUp (2, 4800);
    engine.process (warmUp.block());

    engine.setTestToneEnabled (false);
    TestBuffer fadeOut (2, 4800);
    engine.process (fadeOut.block());

    const auto& left = fadeOut.channel (0);
    const auto fadeEnd = static_cast<std::size_t> (48000 * 0.02 + engine.getOutputLatency());
    const std::vector<float> tail (left.begin() + static_cast<std::ptrdiff_t> (fadeEnd), left.end());
    CHECK (peakOf (tail) == 0.0f);

    TestBuffer after (2, 512);
    engine.process (after.block());
    CHECK (peakOf (after.channel (0)) == 0.0f);
}

TEST_CASE ("Engine ignores non-finite parameter values and clamps out-of-range ones", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 4800);
    engine.setTestToneEnabled (true);
    engine.setTestToneFrequency (std::numeric_limits<float>::quiet_NaN());
    setToneLevel (engine, std::numeric_limits<float>::infinity());
    setToneLevel (engine, +40.0f); // clamped to the parameter maximum

    TestBuffer buffer (2, 4800);
    engine.process (buffer.block());

    const float maxPeak
        = std::pow (10.0f, ap::params::descriptor (ap::params::ParamId::toneLevel).max / 20.0f);
    CHECK (allFinite (buffer.channel (0)));
    CHECK (peakOf (buffer.channel (0)) <= maxPeak + 1.0e-4f);
    CHECK (engine.getNonFiniteSampleCount() == 0);
}

TEST_CASE ("Engine peak meter reports and resets", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 4800);
    engine.setTestToneEnabled (true);
    setToneLevel (engine, -6.0f);

    TestBuffer buffer (2, 4800);
    engine.process (buffer.block());

    CHECK (engine.consumeOutputPeak() > 0.4f);
    CHECK (engine.consumeOutputPeak() == 0.0f);
}

TEST_CASE ("Engine handles mono and empty blocks", "[engine]")
{
    Engine engine;
    engine.prepare (48000.0, 256);
    engine.setTestToneEnabled (true);

    TestBuffer mono (1, 256);
    engine.process (mono.block());
    CHECK (allFinite (mono.channel (0)));

    engine.process ({});
    TestBuffer zeroLength (2, 0);
    engine.process (zeroLength.block());
}
