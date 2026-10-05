#include "Session.h"
#include "TempDirectory.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <thread>

using namespace ap;
using ap::test::TempDirectory;

namespace
{
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juce; // message manager for the session's timer
    TempDirectory temp;
    engine::Engine engine;
    desktop::Session session {engine, io::ProjectFolder {temp.path() / "Unsaved"}};
};

void waitForNewerTimestamp()
{
    // File times on some filesystems have 1 s (or coarser) resolution.
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));
}
} // namespace

TEST_CASE ("A new session is clean and untitled", "[session]")
{
    Fixture f;
    CHECK_FALSE (f.session.isDirty());
    CHECK_FALSE (f.session.location().has_value());
    CHECK (f.session.save().has_value()); // no location yet
}

TEST_CASE ("Edits make the session dirty; saving makes it clean", "[session]")
{
    Fixture f;
    int notifications = 0;
    f.session.onChanged = [&] { ++notifications; };

    REQUIRE (f.session.perform (model::SetTempo {100.0}));
    CHECK (f.session.isDirty());
    CHECK (f.engine.getTransport().getTempo() == 100.0);

    const io::ProjectFolder folder {f.temp.path() / "Song.playground"};
    REQUIRE_FALSE (f.session.saveAs (folder).has_value());
    CHECK_FALSE (f.session.isDirty());
    CHECK (f.session.location().has_value());
    CHECK (std::filesystem::exists (folder.projectFile()));

    REQUIRE (f.session.undo());
    CHECK (f.session.isDirty());
    CHECK (notifications >= 3);
}

TEST_CASE ("Opening a project replaces the session and clears history", "[session]")
{
    Fixture f;
    const io::ProjectFolder folder {f.temp.path() / "Song.playground"};
    REQUIRE (f.session.perform (model::SetTempo {77.0}));
    REQUIRE_FALSE (f.session.saveAs (folder).has_value());

    f.session.newProject();
    CHECK (f.session.project().tempoBpm == 120.0);
    CHECK_FALSE (f.session.location().has_value());

    REQUIRE_FALSE (f.session.open (folder).has_value());
    CHECK (f.session.project().tempoBpm == 77.0);
    CHECK_FALSE (f.session.isDirty());
    CHECK_FALSE (f.session.document().canUndo());
    CHECK (f.engine.getTransport().getTempo() == 77.0);
}

TEST_CASE ("Opening an invalid project keeps the current one", "[session]")
{
    Fixture f;
    REQUIRE (f.session.perform (model::SetTempo {90.0}));

    const io::ProjectFolder bad {f.temp.path() / "Bad.playground"};
    std::filesystem::create_directories (bad.root);
    std::ofstream (bad.projectFile()) << "{ not json";

    const auto error = f.session.open (bad);
    REQUIRE (error.has_value());
    CHECK (f.session.project().tempoBpm == 90.0);
    CHECK (f.session.isDirty());
}

TEST_CASE ("Unsaved work is autosaved and recovered after a crash", "[session]")
{
    TempDirectory temp;
    const io::ProjectFolder scratch {temp.path() / "Unsaved"};

    {
        juce::ScopedJuceInitialiser_GUI juce;
        engine::Engine engine;
        desktop::Session crashed (engine, scratch);
        REQUIRE (crashed.perform (model::SetTempo {133.0}));
        crashed.autosaveIfNeeded();
        // ...the app dies here without saving
    }
    REQUIRE (io::listAutosaves (scratch).size() == 1);

    juce::ScopedJuceInitialiser_GUI juce;
    engine::Engine engine;
    desktop::Session next (engine, scratch);
    const auto message = next.recoverFromAutosave (std::nullopt);
    REQUIRE (message.has_value());
    CHECK (next.project().tempoBpm == 133.0);
    CHECK (next.isDirty()); // recovered work is not saved yet

    next.discardScratchAutosaves();
    CHECK (io::listAutosaves (scratch).empty());
}

TEST_CASE ("Recovery ignores autosaves older than the last save", "[session]")
{
    Fixture f;
    const io::ProjectFolder folder {f.temp.path() / "Song.playground"};

    REQUIRE (f.session.perform (model::SetTempo {101.0}));
    REQUIRE_FALSE (f.session.saveAs (folder).has_value());
    REQUIRE (f.session.perform (model::SetTempo {102.0}));
    f.session.autosaveIfNeeded();

    SECTION ("newer autosave is recovered")
    {
        CHECK (f.session.recoverFromAutosave (folder).has_value());
        CHECK (f.session.project().tempoBpm == 102.0);
    }

    SECTION ("a later save supersedes it")
    {
        waitForNewerTimestamp();
        REQUIRE_FALSE (f.session.save().has_value());
        CHECK_FALSE (f.session.recoverFromAutosave (folder).has_value());
    }
}

TEST_CASE ("Autosave does nothing when there are no new changes", "[session]")
{
    Fixture f;
    f.session.autosaveIfNeeded();
    CHECK (io::listAutosaves (io::ProjectFolder {f.temp.path() / "Unsaved"}).empty());

    REQUIRE (f.session.perform (model::SetTempo {99.0}));
    f.session.autosaveIfNeeded();
    f.session.autosaveIfNeeded();
    CHECK (io::listAutosaves (io::ProjectFolder {f.temp.path() / "Unsaved"}).size() == 1);
}
