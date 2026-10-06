#include "TestAudio.h"
#include "TimelineHelpers.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace ap;
using Catch::Matchers::WithinAbs;
using core::Ticks;

namespace
{
constexpr double fs = 48000.0;
constexpr Ticks beat = core::ticksPerQuarterNote; // 24000 samples at 120 BPM, 48 kHz
constexpr Ticks bar = 4 * beat;
constexpr int fade = 96; // 2 ms at 48 kHz
constexpr int closedHat = 2;

std::shared_ptr<const instruments::SampleBuffer> makeBuffer (std::vector<float> samples, double rate = fs)
{
    auto buffer = std::make_shared<instruments::SampleBuffer>();
    buffer->assetId = 1;
    buffer->sampleRate = rate;
    buffer->channels.push_back (std::move (samples));
    return buffer;
}

// Distinct, bounded values so any misplaced frame is detected.
std::vector<float> ramp (std::size_t frames)
{
    std::vector<float> x (frames);
    for (std::size_t i = 0; i < frames; ++i)
        x[i] = 0.25f + 0.5f * static_cast<float> (i % 1000) / 1000.0f;
    return x;
}

struct AudioProject
{
    model::ProjectDocument doc;
    model::TrackId track;
    model::AssetId asset;

    AudioProject()
    {
        doc.perform (model::AddTrack {model::TrackKind::audio});
        track = doc.project().tracks.back().id;
        doc.perform (model::AddAsset {"audio/1-a.wav", "a.wav"});
        asset = doc.project().assets.back().id;
    }

    void addClip (Ticks start, Ticks length, core::Flicks offset = 0)
    {
        model::Clip clip;
        clip.start = start;
        clip.length = length;
        clip.asset = asset;
        clip.sourceOffset = offset;
        REQUIRE (doc.perform (model::AddClip {track, clip}));
    }
};

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

float peak (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float result = 0.0f;
    for (auto i = from; i < std::min (to, x.size()); ++i)
        result = std::max (result, std::abs (x[i]));
    return result;
}

// Drives the engine block by block so tests can act between blocks.
struct LiveRig
{
    engine::Engine engine;
    test::TestBuffer buffer {2, 512};
    std::vector<float> left;

    LiveRig() { engine.prepare (fs, 512); }

    void run (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (buffer.block());
            left.insert (left.end(), buffer.channel (0).begin(), buffer.channel (0).end());
        }
    }
};
} // namespace

TEST_CASE ("Audio clips play the right part of the file on the exact sample", "[timeline][audio]")
{
    const auto source = ramp (4 * 48000);
    AudioProject project;
    project.addClip (beat, 2 * beat, core::flicksPerSecond / 2); // from 0.5 s into the file

    engine::Engine engine;
    test::publish (engine, project.doc.project(), [&] (model::AssetId) { return makeBuffer (source); });
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 2, 2 * 48000, 512});

    const std::size_t start = 24000;
    const std::size_t end = start + 48000;
    CHECK (peak (audio[0], 0, start) == 0.0f);
    CHECK (peak (audio[0], end, audio[0].size()) == 0.0f);

    // Faded in where the clip cuts into the file, then the file itself (unity gain at centre pan).
    CHECK (std::abs (audio[0][start]) < source[24000] / 50.0f);
    for (std::size_t i = start + fade; i < end - fade; i += 97)
    {
        REQUIRE_THAT (audio[0][i], WithinAbs (static_cast<double> (source[24000 + i - start]), 1.0e-6));
        REQUIRE (audio[1][i] == audio[0][i]);
    }
    CHECK (std::abs (audio[0][end - 1]) < source[24000 + end - 1 - start] / 50.0f); // faded out
}

TEST_CASE ("Timeline playback does not depend on the block size", "[timeline][determinism]")
{
    const auto render = [] (int blockSize)
    {
        AudioProject project;
        project.addClip (beat / 3, 3 * beat, 12345);
        project.doc.perform (model::AddTrack {model::InstrumentKind::synth});
        model::Clip melody;
        melody.start = beat / 2;
        melody.length = 2 * bar;
        melody.loopLength = beat * 3 / 2;
        melody.notes = {{0, 200, 60, 1.0f}, {300, 400, 67, 0.6f}};
        project.doc.perform (model::AddClip {project.doc.project().tracks.back().id, melody});

        engine::Engine engine;
        const auto source = ramp (4 * 48000);
        test::publish (engine, project.doc.project(), [&] (model::AssetId) { return makeBuffer (source); });
        engine.getTransport().setLoop (true, 0, 3 * beat);
        engine.getTransport().requestPlay();
        return engine::renderOffline (engine, {fs, 2, 3 * 48000, blockSize});
    };

    const auto reference = render (512);
    const int blockSize = GENERATE (1, 333, 4096);
    CAPTURE (blockSize);
    CHECK (render (blockSize) == reference);
}

TEST_CASE ("Stopping fades the audio out over 2 ms instead of cutting it", "[timeline][audio]")
{
    AudioProject project;
    project.addClip (0, 4 * bar);
    LiveRig rig;
    test::publish (rig.engine, project.doc.project(),
                   [] (model::AssetId) { return makeBuffer (std::vector<float> (10 * 48000, 0.5f)); });
    rig.engine.getTransport().requestPlay();
    rig.run (10);
    REQUIRE_THAT (rig.left.back(), WithinAbs (0.5, 1.0e-6));

    rig.engine.getTransport().requestStop();
    rig.run (2);
    const auto stopped = 10 * 512 + static_cast<std::size_t> (rig.engine.getOutputLatency());
    for (std::size_t i = stopped; i < stopped + fade; ++i)
        REQUIRE (std::abs (rig.left[i] - rig.left[i - 1]) <= 0.5f / fade + 1.0e-6f);
    CHECK (peak (rig.left, stopped + fade, rig.left.size()) == 0.0f);
}

TEST_CASE ("A loop jump crossfades instead of clicking", "[timeline][audio][loop]")
{
    AudioProject project;
    project.addClip (0, 4 * bar);
    engine::Engine engine;
    test::publish (engine, project.doc.project(),
                   [] (model::AssetId) { return makeBuffer (std::vector<float> (10 * 48000, 0.5f)); });
    engine.getTransport().setLoop (true, 0, beat);
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 1, 3 * 24000, 512});

    // Constant source: fade out + fade in of the same level sums to the same level.
    for (std::size_t i = fade; i < audio[0].size(); ++i)
        REQUIRE_THAT (audio[0][i], WithinAbs (0.5, 1.0e-5));
}

TEST_CASE ("Missing or not-yet-resampled audio is silent", "[timeline][audio]")
{
    AudioProject project;
    project.addClip (0, bar);
    const auto buffer = GENERATE (std::shared_ptr<const instruments::SampleBuffer> {},
                                  makeBuffer (std::vector<float> (48000, 0.5f), 44100.0),
                                  std::make_shared<const instruments::SampleBuffer>());
    engine::Engine engine;
    test::publish (engine, project.doc.project(), [&] (model::AssetId) { return buffer; });
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 2, 48000, 512});
    CHECK (peak (audio[0], 0, audio[0].size()) == 0.0f);
}

TEST_CASE ("Note clips start notes on their exact sample, honouring trim and loop", "[timeline][notes]")
{
    model::ProjectDocument doc;
    doc.perform (model::AddTrack {model::InstrumentKind::drums});
    model::Clip clip;
    clip.start = bar;
    clip.length = 4 * beat;
    clip.contentOffset = beat; // trimmed: starts on the second note
    clip.loopLength = 2 * beat;
    const auto hat = static_cast<std::uint8_t> (model::DrumKit::firstPadNote + closedHat);
    clip.notes = {{0, 10, hat, 1.0f}, {beat, 10, hat, 1.0f}};
    REQUIRE (doc.perform (model::AddClip {doc.project().tracks[0].id, clip}));

    const int blockSize = GENERATE (64, 500, 2048);
    CAPTURE (blockSize);
    engine::Engine engine;
    test::publish (engine, doc.project());
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 1, 4 * 48000, blockSize});

    // A hit on every beat of the clip (bar 2 = 96000), nothing after it ends.
    CHECK (onsets (audio[0]) == std::vector<std::size_t> {96000, 120000, 144000, 168000});
}

TEST_CASE ("Timeline notes end at the clip end and when playback stops", "[timeline][notes]")
{
    model::ProjectDocument doc;
    doc.perform (model::AddTrack {model::InstrumentKind::synth});
    model::Clip clip;
    clip.length = beat;
    clip.notes = {{0, 16 * bar, 60, 1.0f}}; // far longer than the clip
    REQUIRE (doc.perform (model::AddClip {doc.project().tracks[0].id, clip}));

    SECTION ("clip end")
    {
        engine::Engine engine;
        test::publish (engine, doc.project());
        engine.getTransport().requestPlay();
        const auto audio = engine::renderOffline (engine, {fs, 1, 3 * 48000, 512});
        CHECK (peak (audio[0], 0, 24000) > 0.05f);
        CHECK (peak (audio[0], 2 * 48000, 3 * 48000) < 1.0e-4f); // released, then silent
    }

    SECTION ("stop")
    {
        auto longer = doc.project().tracks[0].clips[0];
        longer.length = 16 * bar;
        REQUIRE (doc.perform (
            model::SetClip {longer.id, doc.project().tracks[0].id, longer, model::ClipEdit::resize}));
        LiveRig rig;
        test::publish (rig.engine, doc.project());
        rig.engine.getTransport().requestPlay();
        rig.run (40);
        rig.engine.getTransport().requestStop();
        rig.run (300);
        CHECK (peak (rig.left, 0, 40 * 512) > 0.05f);
        CHECK (peak (rig.left, rig.left.size() - 48000, rig.left.size()) < 1.0e-4f);
    }
}

TEST_CASE ("Track mute and volume apply to the instrument's timeline and live notes", "[timeline][mix]")
{
    const auto render = [] (float volumeDb, bool muted)
    {
        auto project = test::drumPatternProject ({{0, {0, 4, 8, 12}}}, 4 * bar);
        project.tracks[0].volumeDb = volumeDb;
        project.tracks[0].muted = muted;
        engine::Engine engine;
        test::publish (engine, project);
        engine.getTransport().requestPlay();
        return engine::renderOffline (engine, {fs, 1, 48000, 512})[0];
    };

    const auto full = render (0.0f, false);
    const auto quieter = render (-6.0f, false);
    REQUIRE (peak (full, 0, full.size()) > 0.1f);
    CHECK_THAT (peak (quieter, 0, quieter.size()) / peak (full, 0, full.size()), WithinAbs (0.501, 0.002));
    CHECK (peak (render (0.0f, true), 0, 48000) == 0.0f);
}

TEST_CASE ("Live notes are logged where the player heard them", "[timeline][recording]")
{
    LiveRig rig;
    rig.engine.setRecordingLatency (2048);

    SECTION ("while stopped: on the sample clock only")
    {
        rig.run (4);
        rig.engine.sendNoteFromUi ({instruments::NoteEvent::Type::noteOn, 60, 1.0f});
        rig.run (1);
        const auto note = rig.engine.popPlayedNote();
        REQUIRE (note);
        const auto latency = 2048 + rig.engine.getOutputLatency();
        CHECK_FALSE (note->playing);
        CHECK (note->clock == 4 * 512 - latency);
        CHECK (rig.engine.getHeardClock() == 5 * 512 - latency);
    }

    SECTION ("while playing, minus the output latency")
    {
        rig.engine.getTransport().requestPlay();
        rig.run (20); // 10240 samples
        rig.engine.sendNoteFromUi ({instruments::NoteEvent::Type::noteOn, 64, 0.5f});
        rig.run (1);
        rig.engine.sendNoteFromUi ({instruments::NoteEvent::Type::noteOff, 64, 0.0f});
        rig.run (1);

        const auto on = rig.engine.popPlayedNote();
        const auto off = rig.engine.popPlayedNote();
        REQUIRE (on);
        REQUIRE (off);
        const core::TempoMap map (120.0, {}, fs);
        const auto latency = 2048 + rig.engine.getOutputLatency(); // the device's and the engine's
        CHECK (on->playing);
        CHECK (on->ticks == map.samplesToTicks (20 * 512 - latency));
        CHECK (on->clock == 20 * 512 - latency);
        CHECK (on->event.note == 64);
        CHECK (on->event.velocity == 0.5f);
        CHECK (on->instrument == model::InstrumentKind::synth);
        CHECK (off->ticks == map.samplesToTicks (21 * 512 - latency));
        CHECK (off->event.type == instruments::NoteEvent::Type::noteOff);
        CHECK_FALSE (rig.engine.popPlayedNote());
    }
}
