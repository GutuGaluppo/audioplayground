#include "ap/model/ProjectDocument.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>
#include <random>
#include <vector>

using namespace ap::model;
using ap::params::ParamId;

namespace
{
TrackId addTrack (ProjectDocument& doc, TrackKind kind = TrackKind::audio, std::string name = {})
{
    REQUIRE (doc.perform (AddTrack{kind, std::move (name)}));
    return doc.project().tracks.back().id;
}
} // namespace

TEST_CASE ("A new project has sensible defaults", "[model]")
{
    const Project project;
    CHECK (project.name == "Untitled");
    CHECK (project.tempoBpm == 120.0);
    CHECK (project.timeSignature == ap::core::TimeSignature{4, 4});
    CHECK (project.tracks.empty());
    CHECK (project.parameter (ParamId::toneLevel)
           == ap::params::descriptor (ParamId::toneLevel).defaultValue);
}

TEST_CASE ("Commands apply, undo and redo exactly", "[model][undo]")
{
    ProjectDocument doc;
    const auto initial = doc.project();

    CHECK (doc.perform (SetTempo{96.0}));
    CHECK (doc.project().tempoBpm == 96.0);
    CHECK (doc.undoDescription() == "Change tempo");

    REQUIRE (doc.undo());
    CHECK (doc.project() == initial);
    CHECK (doc.redoDescription() == "Change tempo");

    REQUIRE (doc.redo());
    CHECK (doc.project().tempoBpm == 96.0);
    CHECK_FALSE (doc.redo());
}

TEST_CASE ("Invalid and no-op commands change nothing and are not recorded", "[model][undo]")
{
    ProjectDocument doc;
    const auto version = doc.version();

    CHECK_FALSE (doc.perform (SetTempo{0.0}));
    CHECK_FALSE (doc.perform (SetTempo{std::numeric_limits<double>::quiet_NaN()}));
    CHECK_FALSE (doc.perform (SetTempo{120.0})); // unchanged
    CHECK_FALSE (doc.perform (SetTimeSignature{ap::core::TimeSignature{5, 3}}));
    CHECK_FALSE (doc.perform (RenameProject{"   \n\t "}));
    CHECK_FALSE (doc.perform (RemoveTrack{TrackId{42}}));
    CHECK_FALSE (doc.perform (SetTrackVolume{TrackId{42}, -6.0f}));
    CHECK_FALSE (doc.perform (SetParameter{ParamId::toneLevel, std::numeric_limits<float>::infinity()}));

    CHECK_FALSE (doc.canUndo());
    CHECK (doc.version() == version);
    CHECK (doc.project() == Project{});
}

TEST_CASE ("Track IDs are unique and never reused", "[model]")
{
    ProjectDocument doc;
    const auto a = addTrack (doc);
    const auto b = addTrack (doc);
    CHECK (a != b);

    REQUIRE (doc.perform (RemoveTrack{b}));
    const auto c = addTrack (doc);
    CHECK (c != b);
    CHECK (c.value > b.value);

    // Undoing the add and redoing it recreates the same ID.
    REQUIRE (doc.undo());
    REQUIRE (doc.redo());
    CHECK (doc.project().tracks.back().id == c);
}

TEST_CASE ("Tracks get default names and sanitised custom names", "[model]")
{
    ProjectDocument doc;
    addTrack (doc, TrackKind::audio);
    addTrack (doc, TrackKind::instrument);
    addTrack (doc, TrackKind::audio, "  Lead\tVocal\n ");

    REQUIRE (doc.project().tracks.size() == 3);
    CHECK (doc.project().tracks[0].name == "Audio 1");
    CHECK (doc.project().tracks[1].name == "Instrument 1");
    CHECK (doc.project().tracks[2].name == "Lead Vocal");
}

TEST_CASE ("sanitiseName never splits a UTF-8 code point", "[model]")
{
    // "é" is two bytes; a 4-byte limit on "abcé" must drop the whole character.
    CHECK (sanitiseName ("abc\xC3\xA9", 4) == std::optional<std::string> ("abc"));
    CHECK (sanitiseName ("abc\xC3\xA9", 5) == std::optional<std::string> ("abc\xC3\xA9"));
    CHECK_FALSE (sanitiseName ("", 10).has_value());
    CHECK_FALSE (sanitiseName ("\x01\x02", 10).has_value());
}

TEST_CASE ("Track values are clamped, and removing restores the track in place", "[model]")
{
    ProjectDocument doc;
    const auto first = addTrack (doc);
    const auto second = addTrack (doc);
    addTrack (doc);

    REQUIRE (doc.perform (SetTrackVolume{second, 99.0f}));
    CHECK (doc.project().findTrack (second)->volumeDb == Track::maxVolumeDb);
    REQUIRE (doc.perform (SetTrackPan{second, -3.0f}));
    CHECK (doc.project().findTrack (second)->pan == -1.0f);

    const auto before = doc.project();
    REQUIRE (doc.perform (RemoveTrack{second}));
    CHECK (doc.project().tracks.size() == 2);
    REQUIRE (doc.undo());
    CHECK (doc.project() == before);
    CHECK (doc.project().tracks[1].id == second);
    CHECK (doc.project().tracks[0].id == first);
}

TEST_CASE ("A continuous gesture is one undo step", "[model][undo]")
{
    ProjectDocument doc;
    const auto track = addTrack (doc);
    const auto before = doc.project();

    for (float db = -1.0f; db >= -12.0f; db -= 1.0f)
        REQUIRE (doc.perform (SetTrackVolume{track, db}, 7));

    CHECK (doc.project().findTrack (track)->volumeDb == -12.0f);

    REQUIRE (doc.undo());
    CHECK (doc.project() == before);
    REQUIRE (doc.undo()); // the add-track step
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("Gestures do not merge across targets, kinds or gesture IDs", "[model][undo]")
{
    ProjectDocument doc;
    const auto a = addTrack (doc);
    const auto b = addTrack (doc);

    REQUIRE (doc.perform (SetTrackVolume{a, -3.0f}, 1));
    REQUIRE (doc.perform (SetTrackVolume{b, -3.0f}, 1)); // other track
    REQUIRE (doc.perform (SetTrackPan{b, 0.5f}, 1));     // other kind
    REQUIRE (doc.perform (SetTrackPan{b, 0.6f}, 2));     // new gesture

    int steps = 0;
    while (doc.undo())
        ++steps;
    CHECK (steps == 6); // 2 adds + 4 edits
}

TEST_CASE ("A new edit clears the redo history", "[model][undo]")
{
    ProjectDocument doc;
    REQUIRE (doc.perform (SetTempo{100.0}));
    REQUIRE (doc.undo());
    REQUIRE (doc.canRedo());
    REQUIRE (doc.perform (SetTempo{90.0}));
    CHECK_FALSE (doc.canRedo());
}

TEST_CASE ("Undo history is bounded", "[model][undo]")
{
    ProjectDocument doc;
    for (std::size_t i = 0; i < ProjectDocument::maxUndoSteps + 50; ++i)
        REQUIRE (doc.perform (SetTempo{i % 2 == 0 ? 100.0 : 101.0}));

    std::size_t steps = 0;
    while (doc.undo())
        ++steps;
    CHECK (steps == ProjectDocument::maxUndoSteps);
}

TEST_CASE ("Random edit sequences undo to the start and redo to the end", "[model][undo][property]")
{
    const auto seed = GENERATE (1u, 2u, 3u, 42u, 1337u);
    CAPTURE (seed);
    std::mt19937 random (seed);

    ProjectDocument doc;
    const auto initial = doc.project();

    const auto pickTrack = [&]() -> TrackId
    {
        const auto& tracks = doc.project().tracks;
        if (tracks.empty() || random() % 8 == 0)
            return TrackId{9999}; // sometimes target a missing track (must be rejected)
        return tracks[random() % tracks.size()].id;
    };

    for (int step = 0; step < 400; ++step)
    {
        const auto gesture = random() % 3 == 0 ? std::uint64_t{random() % 4 + 1} : std::uint64_t{0};
        switch (random() % 11)
        {
        case 0:
            (void)doc.perform (AddTrack{random() % 2 ? TrackKind::audio : TrackKind::instrument});
            break;
        case 1:
            (void)doc.perform (RemoveTrack{pickTrack()});
            break;
        case 2:
            (void)doc.perform (MoveTrack{pickTrack(), random() % 6});
            break;
        case 3:
            (void)doc.perform (SetTrackVolume{pickTrack(), static_cast<float> (random() % 80) - 70.0f},
                               gesture);
            break;
        case 4:
            (void)doc.perform (SetTrackPan{pickTrack(), static_cast<float> (random() % 21) / 10.0f - 1.0f},
                               gesture);
            break;
        case 5:
            (void)doc.perform (SetTrackMute{pickTrack(), random() % 2 == 0});
            break;
        case 6:
            (void)doc.perform (SetTrackSolo{pickTrack(), random() % 2 == 0});
            break;
        case 7:
            (void)doc.perform (RenameTrack{pickTrack(), "T" + std::to_string (random() % 5)}, gesture);
            break;
        case 8:
            (void)doc.perform (SetTempo{60.0 + static_cast<double> (random() % 120)}, gesture);
            break;
        case 9:
            (void)doc.perform (SetParameter{ParamId::metronomeLevel, -static_cast<float> (random() % 60)},
                               gesture);
            break;
        default:
            if (random() % 2 == 0)
                (void)doc.undo();
            else
                (void)doc.redo();
            break;
        }
    }

    // Finish any redo branch so "final" is well-defined, then walk the whole history.
    while (doc.redo())
    {
    }
    const auto final = doc.project();

    while (doc.undo())
    {
    }
    CHECK (doc.project() == initial);

    while (doc.redo())
    {
    }
    CHECK (doc.project() == final);
}
