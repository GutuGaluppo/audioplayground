#include "TestAudio.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <vector>

using namespace ap::core;
using ap::engine::Engine;
using ap::engine::renderOffline;

namespace
{
// Sample indices where a click starts: silence followed by sound. A click's first sample is
// exactly 0 (zero-phase sine under a 0.5 ms attack), so the first audible sample is beat + 1.
constexpr std::size_t firstAudibleSample = 1;

std::vector<std::size_t> onsets (const std::vector<float>& samples)
{
    std::vector<std::size_t> result;
    bool silent = true;
    std::size_t quietRun = 0;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        if (std::abs (samples[i]) > 1.0e-4f)
        {
            if (silent)
                result.push_back (i - firstAudibleSample);
            silent = false;
            quietRun = 0;
        }
        else if (++quietRun > 64)
        {
            silent = true;
        }
    }
    return result;
}

Engine& playingEngine (Engine& engine)
{
    engine.getMetronome().setEnabled (true);
    engine.getTransport().requestPlay();
    return engine;
}
} // namespace

TEST_CASE ("Metronome clicks exactly on every beat", "[metronome]")
{
    const int blockSize = GENERATE (1, 64, 333, 4096);
    CAPTURE (blockSize);

    Engine engine;
    const auto audio = renderOffline (playingEngine (engine), {48000.0, 2, 8 * 24000, blockSize});

    std::vector<std::size_t> expected;
    for (std::size_t beat = 0; beat < 8; ++beat)
        expected.push_back (beat * 24000);

    CHECK (onsets (audio[0]) == expected);
    CHECK (audio[0] == audio[1]);
}

TEST_CASE ("Metronome output is identical for any block size", "[metronome][determinism]")
{
    Engine reference;
    const auto expected = renderOffline (playingEngine (reference), {44100.0, 2, 3 * 44100, 512});

    const int blockSize = GENERATE (1, 17, 128, 1000, 8192);
    CAPTURE (blockSize);
    Engine engine;
    CHECK (renderOffline (playingEngine (engine), {44100.0, 2, 3 * 44100, blockSize}) == expected);
}

TEST_CASE ("Metronome accents the first beat of each bar", "[metronome]")
{
    Engine engine;
    engine.getTransport().setTimeSignature ({3, 4});
    const auto audio = renderOffline (playingEngine (engine), {48000.0, 1, 6 * 24000, 256});

    const auto peakAround = [&audio] (std::size_t start)
    {
        float peak = 0.0f;
        for (std::size_t i = start; i < start + 2000; ++i)
            peak = std::max (peak, std::abs (audio[0][i]));
        return peak;
    };

    CHECK (peakAround (0) > peakAround (24000) * 1.4f);
    CHECK (peakAround (72000) > peakAround (48000) * 1.4f);
    CHECK (onsets (audio[0]).size() == 6);
}

TEST_CASE ("Metronome is silent when disabled, except during the count-in", "[metronome]")
{
    Engine engine;
    engine.getTransport().setCountInBars (1);
    engine.getTransport().requestPlay();

    const auto audio = renderOffline (engine, {48000.0, 1, 2 * 96000, 512});
    const auto clicks = onsets (audio[0]);

    // Four count-in clicks, then nothing once playback reaches bar 1.
    CHECK (clicks == std::vector<std::size_t>{0, 24000, 48000, 72000});
}

TEST_CASE ("Metronome follows the loop", "[metronome][loop]")
{
    Engine engine;
    // Loop the second half of bar 1: beats 3 and 4 (48000..96000).
    engine.getTransport().setLoop (true, 2 * ticksPerQuarterNote, 4 * ticksPerQuarterNote);
    engine.getTransport().requestSeek (2 * ticksPerQuarterNote);

    const auto audio = renderOffline (playingEngine (engine), {48000.0, 1, 3 * 48000, 512});
    CHECK (onsets (audio[0]) == std::vector<std::size_t>{0, 24000, 48000, 72000, 96000, 120000});
}

TEST_CASE ("Metronome click is free of discontinuities and bounded", "[metronome]")
{
    Engine engine;
    engine.getParameters().set (ap::params::ParamId::metronomeLevel, 0.0f);
    const auto audio = renderOffline (playingEngine (engine), {48000.0, 1, 48000, 512});

    float maxStep = 0.0f;
    float peak = 0.0f;
    for (std::size_t i = 1; i < audio[0].size(); ++i)
    {
        maxStep = std::max (maxStep, std::abs (audio[0][i] - audio[0][i - 1]));
        peak = std::max (peak, std::abs (audio[0][i]));
    }

    CHECK (peak <= 1.0f);
    // 0.5 ms attack: at most ~1/24 per sample from the envelope plus the sine slope at 1760 Hz.
    CHECK (maxStep < 0.3f);
}
