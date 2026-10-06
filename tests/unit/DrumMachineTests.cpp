#include "Golden.h"
#include "TestAudio.h"
#include "TimelineHelpers.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/instruments/DrumMachine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>

using namespace ap;
using instruments::DrumMachine;

namespace
{
constexpr double fs = 48000.0;
constexpr int kick = 0;
constexpr int closedHat = 2;
constexpr int openHat = 3;

std::vector<std::size_t> onsets (const std::vector<float>& x, float threshold = 1.0e-3f)
{
    std::vector<std::size_t> result;
    std::size_t quiet = 10000;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (std::abs (x[i]) > threshold)
        {
            if (quiet > 2000)
                result.push_back (i);
            quiet = 0;
        }
        else
            ++quiet;
    }
    return result;
}

constexpr core::Ticks bar = 4 * core::ticksPerQuarterNote;

double energy (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (auto i = from; i < std::min (to, x.size()); ++i)
        sum += static_cast<double> (x[i]) * static_cast<double> (x[i]);
    return sum;
}
} // namespace

TEST_CASE ("Factory kit sounds are valid, bounded, audible and deterministic", "[drums]")
{
    for (std::size_t pad = 0; pad < instruments::factoryKitSize; ++pad)
    {
        CAPTURE (pad, instruments::factoryKitNames[pad]);
        const auto sound = instruments::makeFactorySound (pad, fs);
        REQUIRE (sound->isValid());
        const auto& x = sound->channels[0];

        float peak = 0.0f;
        for (const float s : x)
        {
            REQUIRE (std::isfinite (s));
            peak = std::max (peak, std::abs (s));
        }
        CHECK (peak > 0.1f);
        CHECK (peak < 1.0f);
        CHECK (std::abs (x.back()) < 1.0e-3f); // faded out: no click at the end
        CHECK (x == instruments::makeFactorySound (pad, fs)->channels[0]);
    }
}

TEST_CASE ("Pattern clips trigger steps sample-accurately at any block size", "[drums]")
{
    const int blockSize = GENERATE (1, 64, 333, 2048);
    CAPTURE (blockSize);

    engine::Engine engine;
    test::publish (engine, test::drumPatternProject ({{closedHat, {0, 4, 8, 12}}}, 4 * bar)); // short sound
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 1, 2 * 96000, blockSize});

    // 120 BPM at 48 kHz: one sixteenth = 6000 samples, four steps = 24000.
    const std::vector<std::size_t> expected {0, 24000, 48000, 72000, 96000, 120000, 144000, 168000};
    CHECK (onsets (audio[0]) == expected);
}

TEST_CASE ("Pattern clips stay silent during the count-in", "[drums]")
{
    // The count-in clicks always sound; subtract a render without the pattern to isolate drums.
    const auto render = [] (bool withPattern)
    {
        engine::Engine engine;
        if (withPattern)
            test::publish (engine, test::drumPatternProject ({{closedHat, {0}}}, 4 * bar));
        engine.getTransport().setCountInBars (1);
        engine.getTransport().requestPlay();
        return engine::renderOffline (engine, {fs, 1, 2 * 96000, 512})[0];
    };

    auto drumsOnly = render (true);
    const auto clicksOnly = render (false);
    for (std::size_t i = 0; i < drumsOnly.size(); ++i)
        drumsOnly[i] -= clicksOnly[i];

    CHECK (onsets (drumsOnly) == std::vector<std::size_t> {96000}); // first hit on bar 1
}

TEST_CASE ("Pattern clips follow the transport loop", "[drums][loop]")
{
    engine::Engine engine;
    test::publish (engine, test::drumPatternProject ({{closedHat, {0}}}, 4 * bar));
    engine.getTransport().setLoop (true, 0, 2 * core::ticksPerQuarterNote); // half a bar
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 1, 96000, 512});
    CHECK (onsets (audio[0]) == std::vector<std::size_t> {0, 48000});
}

TEST_CASE ("Muted pads are silent and volume scales the hit", "[drums]")
{
    test::TestBuffer buffer (1, 4800);
    DrumMachine drums;
    drums.prepare (fs);

    drums.setPad (kick, 0.0f, 0.0f, true);
    drums.trigger (kick, 1.0f);
    drums.render (buffer.block());
    CHECK (energy (buffer.channel (0), 0, 4800) == 0.0);

    drums.setPad (kick, 0.0f, 0.0f, false);
    drums.trigger (kick, 1.0f);
    drums.render (buffer.block());
    const auto loud = energy (buffer.channel (0), 0, 4800);

    DrumMachine quieter;
    quieter.prepare (fs);
    quieter.setPad (kick, -20.0f, 0.0f, false);
    test::TestBuffer quiet (1, 4800);
    quieter.trigger (kick, 1.0f);
    quieter.render (quiet.block());
    CHECK (energy (quiet.channel (0), 0, 4800) < loud * 0.011); // -20 dB = 1/100 power
}

TEST_CASE ("Closed hat chokes the open hat", "[drums]")
{
    const auto render = [] (bool choke)
    {
        DrumMachine drums;
        drums.prepare (fs);
        test::TestBuffer buffer (1, 14400);
        drums.trigger (openHat, 1.0f);
        if (choke)
            drums.trigger (closedHat, 1.0f, 2400); // lands 50 ms in, inside this block
        drums.render (buffer.block());
        return buffer.channel (0);
    };

    const auto choked = render (true);
    const auto ringing = render (false);

    // Before the closed hat, the open hat is untouched (no early choke).
    CHECK (energy (choked, 0, 2400) == energy (ringing, 0, 2400));
    // After the closed hat has ended (120 ms long), the open hat is gone.
    CHECK (energy (choked, 8400, 14400) < energy (ringing, 8400, 14400) * 0.001);
}

TEST_CASE ("MIDI notes 36-51 play the pads", "[drums]")
{
    DrumMachine drums;
    drums.prepare (fs);
    test::TestBuffer buffer (1, 4800);
    drums.handle ({instruments::NoteEvent::Type::noteOn, 36, 1.0f});
    drums.handle ({instruments::NoteEvent::Type::noteOn, 20, 1.0f}); // outside the kit: ignored
    drums.render (buffer.block());
    CHECK (drums.activeVoiceCount() == 1);
    CHECK (energy (buffer.channel (0), 0, 4800) > 0.0);
}

TEST_CASE ("A user sample replaces a pad and an empty one restores the factory sound", "[drums]")
{
    DrumMachine drums;
    drums.prepare (fs);
    test::TestBuffer buffer (1, 256);

    auto custom = std::make_unique<instruments::SampleBuffer>();
    custom->sampleRate = fs;
    custom->channels.assign (1, std::vector<float> (1000, 0.25f));
    drums.loadPadSample (kick, std::move (custom));
    drums.render (buffer.block()); // swap in

    drums.trigger (kick, 1.0f);
    buffer.fill (0.0f);
    drums.render (buffer.block());
    CHECK (buffer.channel (0)[10] == 0.25f);

    drums.loadPadSample (kick, nullptr);
    for (int i = 0; i < 10; ++i)
        drums.render (buffer.block()); // fade out the custom voice, then swap
    CHECK (drums.collectGarbage() >= 1);

    drums.trigger (kick, 1.0f);
    buffer.fill (0.0f);
    drums.render (buffer.block());
    CHECK (buffer.channel (0)[10] != 0.25f); // factory kick again
}

namespace
{
engine::RenderedAudio renderGroove (int blockSize)
{
    engine::Engine engine;
    test::publish (engine, test::drumPatternProject ({{kick, {0, 6, 8}},
                                                      {1, {4, 12}}, // snare
                                                      {closedHat, {0, 2, 4, 6, 8, 10, 14}},
                                                      {openHat, {12}}},
                                                     4 * bar));
    engine.getTransport().setTempo (100.0);
    engine.getTransport().requestPlay();
    return engine::renderOffline (engine, {fs, 2, static_cast<std::int64_t> (fs * 4.8), blockSize});
}
} // namespace

TEST_CASE ("Drum rendering, including chokes, does not depend on block size", "[drums][determinism]")
{
    const auto reference = renderGroove (512);
    const int blockSize = GENERATE (1, 333, 4096);
    CAPTURE (blockSize);
    CHECK (renderGroove (blockSize) == reference);
}

TEST_CASE ("A two-bar groove matches the golden file", "[drums][golden]")
{
    const auto audio = renderGroove (512);
    const auto result
        = test::compareWithGolden ("drums_groove_48k", {fs, audio}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
