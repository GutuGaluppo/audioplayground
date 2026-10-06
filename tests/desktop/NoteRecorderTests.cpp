#include "NoteRecorder.h"
#include "TempDirectory.h"
#include "TestAudio.h"

#include <catch2/catch_test_macros.hpp>

using namespace ap;
using instruments::NoteEvent;

namespace
{
constexpr core::Ticks bar = 4 * core::ticksPerQuarterNote;

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juce;
    test::TempDirectory temp;
    engine::Engine engine;
    desktop::Session session {engine, io::ProjectFolder {temp.path() / "Unsaved"}};
    desktop::NoteRecorder recorder {session};
    test::TestBuffer buffer {2, 480}; // 10 ms at 48 kHz

    Fixture() { engine.prepare (48000.0, 480); }

    void run (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
            engine.process (buffer.block());
        while (const auto note = engine.popPlayedNote())
            recorder.handle (*note);
    }

    void note (NoteEvent::Type type, int pitch, float velocity = 0.8f)
    {
        engine.sendNoteFromUi ({type, static_cast<std::uint8_t> (pitch), velocity});
    }
};
} // namespace

TEST_CASE ("Recorded notes become a whole-bar clip on a new instrument track, as one undo step",
           "[recording]")
{
    Fixture f;
    f.recorder.start();
    f.engine.getTransport().requestPlay();

    // 120 BPM: one beat = 0.5 s = 50 blocks of 10 ms. Play a note on beat 3 of bar 2 for one beat.
    f.run (300);
    f.note (NoteEvent::Type::noteOn, 62);
    f.run (50);
    f.note (NoteEvent::Type::noteOff, 62);
    f.run (10);
    const auto stopAt = f.engine.getTransport().getState().positionTicks;
    f.engine.getTransport().requestStop();
    REQUIRE (f.recorder.stop (stopAt));

    const auto& project = f.session.project();
    REQUIRE (project.tracks.size() == 1);
    CHECK (project.tracks[0].instrument == model::InstrumentKind::synth);
    REQUIRE (project.tracks[0].clips.size() == 1);
    const auto& clip = project.tracks[0].clips[0];
    CHECK (clip.start == bar);
    CHECK (clip.length == bar);
    REQUIRE (clip.notes.size() == 1);
    CHECK (clip.notes[0].pitch == 62);
    CHECK (clip.notes[0].velocity == 0.8f);
    // Block-quantised (10 ms = 19.2 ticks): within one block of beat 3 and one beat long.
    CHECK (std::abs (clip.notes[0].start - 2 * core::ticksPerQuarterNote) <= 20);
    CHECK (std::abs (clip.notes[0].length - core::ticksPerQuarterNote) <= 20);
    CHECK (f.session.document().undoDescription() == "Record notes");

    REQUIRE (f.session.undo());
    CHECK (f.session.project().tracks.empty());
}

TEST_CASE ("Notes still held at stop end at the stop position; an empty take adds nothing", "[recording]")
{
    Fixture f;
    SECTION ("held")
    {
        f.recorder.start();
        f.engine.getTransport().requestPlay();
        f.run (10);
        f.note (NoteEvent::Type::noteOn, 60);
        f.run (40);
        REQUIRE (f.recorder.stop (f.engine.getTransport().getState().positionTicks));
        const auto& note = f.session.project().tracks[0].clips[0].notes.at (0);
        CHECK (note.length > 0);
        CHECK (note.start + note.length <= bar);
    }

    SECTION ("empty")
    {
        f.recorder.start();
        f.engine.getTransport().requestPlay();
        f.run (10);
        CHECK_FALSE (f.recorder.stop (f.engine.getTransport().getState().positionTicks));
        CHECK (f.session.project().tracks.empty());
        CHECK_FALSE (f.session.document().canUndo());
    }
}

TEST_CASE ("Recording uses the existing track of the instrument played", "[recording]")
{
    Fixture f;
    REQUIRE (f.session.perform (model::AddTrack {model::InstrumentKind::drums}));
    f.engine.setLiveInstrument (engine::Engine::LiveInstrument::drums);
    f.recorder.start();
    f.engine.getTransport().requestPlay();
    f.run (5);
    f.note (NoteEvent::Type::noteOn, 36, 1.0f);
    f.run (5);
    REQUIRE (f.recorder.stop (f.engine.getTransport().getState().positionTicks));
    REQUIRE (f.session.project().tracks.size() == 1);
    CHECK (f.session.project().tracks[0].clips.size() == 1);
    CHECK (f.session.project().tracks[0].clips[0].notes.at (0).pitch == 36);
}
