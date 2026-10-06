#include "NoteCapture.h"
#include "TempDirectory.h"
#include "TestAudio.h"

#include <catch2/catch_test_macros.hpp>

using namespace ap;
using instruments::NoteEvent;

namespace
{
constexpr core::Ticks beat = core::ticksPerQuarterNote;
constexpr core::Ticks bar = 4 * beat;
constexpr double rate = 48000.0;
constexpr int blocksPerBeat = 50; // 120 BPM: one beat = 0.5 s = 50 blocks of 10 ms

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juce;
    test::TempDirectory temp;
    engine::Engine engine;
    desktop::Session session {engine, io::ProjectFolder {temp.path() / "Unsaved"}};
    desktop::NoteCapture capture {session};
    test::TestBuffer buffer {2, 480};

    Fixture() { engine.prepare (rate, 480); }

    void run (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
            engine.process (buffer.block());
        while (const auto note = engine.popPlayedNote())
            capture.handle (*note);
    }

    // A note `beats` long, followed by `rest` beats of silence.
    void play (int pitch, int beats = 1, int rest = 0)
    {
        engine.sendNoteFromUi ({NoteEvent::Type::noteOn, static_cast<std::uint8_t> (pitch), 0.8f});
        run (beats * blocksPerBeat);
        engine.sendNoteFromUi ({NoteEvent::Type::noteOff, static_cast<std::uint8_t> (pitch), 0.0f});
        run (rest * blocksPerBeat + 1);
    }

    bool doCapture()
    {
        return capture.capture (engine.getHeardClock(), engine.getTransport().getState().positionTicks, rate);
    }

    [[nodiscard]] const model::Clip& onlyClip() const
    {
        const auto& tracks = session.project().tracks;
        REQUIRE (tracks.size() == 1);
        REQUIRE (tracks[0].clips.size() == 1);
        return tracks[0].clips[0];
    }
};

bool near (core::Ticks actual, core::Ticks expected)
{
    return std::abs (actual - expected) <= 20; // notes are stamped per 10 ms block (19.2 ticks)
}
} // namespace

TEST_CASE ("Notes played while stopped are captured at the playhead's bar, keeping their rhythm", "[capture]")
{
    Fixture f;
    f.run (30);
    CHECK_FALSE (f.capture.hasNotes());
    f.play (60, 1, 1); // a beat, a beat of rest
    f.play (64, 1);
    CHECK (f.capture.hasNotes());

    f.engine.getTransport().requestSeek (2 * bar + beat); // bar 3, beat 2
    f.run (1);
    REQUIRE (f.doCapture());
    CHECK_FALSE (f.capture.hasNotes());
    CHECK (f.session.document().undoDescription() == "Capture");

    const auto& clip = f.onlyClip();
    CHECK (f.session.project().tracks[0].instrument == model::InstrumentKind::synth);
    CHECK (clip.start == 2 * bar);
    CHECK (clip.length == bar);
    REQUIRE (clip.notes.size() == 2);
    CHECK (clip.notes[0].pitch == 60);
    CHECK (clip.notes[0].start == 0); // the phrase starts on the bar
    CHECK (near (clip.notes[0].length, beat));
    CHECK (clip.notes[1].pitch == 64);
    CHECK (near (clip.notes[1].start, 2 * beat));
    CHECK (near (clip.notes[1].length, beat));

    // Capturing again finds nothing new.
    CHECK_FALSE (f.doCapture());
}

TEST_CASE ("Only the last phrase is captured", "[capture]")
{
    Fixture f;
    f.play (48, 1, 10); // then 5 s of silence
    f.play (72, 1);
    REQUIRE (f.doCapture());
    const auto& clip = f.onlyClip();
    REQUIRE (clip.notes.size() == 1);
    CHECK (clip.notes[0].pitch == 72);
}

TEST_CASE ("A phrase played along with the transport keeps its place on the timeline", "[capture]")
{
    Fixture f;
    f.engine.getTransport().requestPlay();
    f.run (4 * blocksPerBeat + 2 * blocksPerBeat); // to bar 2, beat 3
    f.play (67, 1);
    f.engine.getTransport().requestStop();
    f.run (1);
    f.engine.getTransport().requestSeek (0);
    f.run (1);

    REQUIRE (f.doCapture());
    const auto& clip = f.onlyClip();
    CHECK (clip.start == bar);
    REQUIRE (clip.notes.size() == 1);
    CHECK (near (clip.notes[0].start, 2 * beat));
    CHECK (near (clip.notes[0].length, beat));
}

TEST_CASE ("A phrase across a loop wrap keeps its rhythm instead of its positions", "[capture]")
{
    Fixture f;
    f.engine.getTransport().setLoop (true, 0, bar);
    f.engine.getTransport().requestPlay();
    f.run (3 * blocksPerBeat);
    f.play (60, 1); // beat 4
    f.play (62, 1); // beat 1 again, after the wrap
    f.engine.getTransport().requestStop();
    f.run (1);

    REQUIRE (f.doCapture());
    const auto& clip = f.onlyClip();
    REQUIRE (clip.notes.size() == 2);
    CHECK (clip.notes[0].pitch == 60);
    CHECK (clip.notes[0].start == 0);
    CHECK (near (clip.notes[1].start, beat));
}

TEST_CASE ("Notes still held are captured up to now; instruments get their own clips", "[capture]")
{
    Fixture f;
    f.engine.sendNoteFromUi ({NoteEvent::Type::noteOn, 60, 0.8f});
    f.run (blocksPerBeat);
    f.engine.setLiveInstrument (engine::Engine::LiveInstrument::drums); // releases the synth note
    f.run (1);
    f.engine.sendNoteFromUi ({NoteEvent::Type::noteOn, 36, 1.0f});
    f.run (blocksPerBeat);

    REQUIRE (f.doCapture());
    const auto& tracks = f.session.project().tracks;
    REQUIRE (tracks.size() == 2);
    for (const auto& track : tracks)
    {
        REQUIRE (track.clips.size() == 1);
        REQUIRE (track.clips[0].notes.size() == 1);
        CHECK (near (track.clips[0].notes[0].length, beat));
    }

    // One undo step removes both.
    REQUIRE (f.session.undo());
    CHECK (f.session.project().tracks.empty());
}
