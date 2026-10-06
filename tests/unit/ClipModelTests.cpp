#include "ap/model/ClipEditing.h"
#include "ap/model/ProjectDocument.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <random>

using namespace ap::model;
using ap::core::Ticks;

namespace
{
constexpr Ticks beat = ap::core::ticksPerQuarterNote;
constexpr Ticks bar = 4 * beat;

struct Fixture
{
    ProjectDocument doc;
    TrackId audio;
    TrackId synth;
    AssetId asset;

    Fixture()
    {
        REQUIRE (doc.perform (AddTrack {TrackKind::audio}));
        audio = doc.project().tracks.back().id;
        REQUIRE (doc.perform (AddTrack {InstrumentKind::synth}));
        synth = doc.project().tracks.back().id;
        REQUIRE (doc.perform (AddAsset {"audio/1-take.wav", "take.wav"}));
        asset = doc.project().assets.back().id;
    }

    ClipId add (TrackId track, Clip clip)
    {
        REQUIRE (doc.perform (AddClip {track, std::move (clip)}));
        return ClipId {doc.project().nextClipId - 1};
    }

    [[nodiscard]] const Clip& clip (ClipId id) const
    {
        const auto* found = doc.project().findClip (id);
        REQUIRE (found != nullptr);
        return *found;
    }
};

Clip audioClip (AssetId asset, Ticks start, Ticks length, ap::core::Flicks offset = 0)
{
    Clip clip;
    clip.start = start;
    clip.length = length;
    clip.asset = asset;
    clip.sourceOffset = offset;
    return clip;
}

Clip noteClip (Ticks start, Ticks length, std::vector<Note> notes = {}, Ticks loop = 0)
{
    Clip clip;
    clip.start = start;
    clip.length = length;
    clip.notes = std::move (notes);
    clip.loopLength = loop;
    return clip;
}
} // namespace

TEST_CASE ("Instrument tracks: one per instrument, named after it", "[model][clips]")
{
    ProjectDocument doc;
    REQUIRE (doc.perform (AddTrack {InstrumentKind::drums}));
    CHECK (doc.project().tracks[0].name == "Drums");
    CHECK (doc.project().tracks[0].instrument == InstrumentKind::drums);
    CHECK_FALSE (doc.perform (AddTrack {InstrumentKind::drums}));
    REQUIRE (doc.perform (AddTrack {InstrumentKind::sampler}));
    CHECK (doc.project().findInstrumentTrack (InstrumentKind::sampler) == &doc.project().tracks[1]);
    CHECK (doc.project().findInstrumentTrack (InstrumentKind::synth) == nullptr);
    CHECK_FALSE (doc.perform (AddTrack {static_cast<InstrumentKind> (7)}));
}

TEST_CASE ("Clips are validated against their track", "[model][clips]")
{
    Fixture f;
    CHECK_FALSE (f.doc.perform (AddClip {f.audio, noteClip (0, bar)}));           // audio needs an asset
    CHECK_FALSE (f.doc.perform (AddClip {f.synth, audioClip (f.asset, 0, bar)})); // notes only
    CHECK_FALSE (f.doc.perform (AddClip {f.audio, audioClip (AssetId {99}, 0, bar)}));
    CHECK_FALSE (f.doc.perform (AddClip {f.audio, audioClip (f.asset, -1, bar)}));
    CHECK_FALSE (f.doc.perform (AddClip {f.audio, audioClip (f.asset, 0, 0)}));
    CHECK_FALSE (f.doc.perform (AddClip {f.audio, audioClip (f.asset, maxTimelineTicks - beat, bar)}));
    CHECK_FALSE (f.doc.perform (AddClip {f.audio, audioClip (f.asset, 0, bar, -1)}));
    CHECK_FALSE (f.doc.perform (AddClip {TrackId {42}, noteClip (0, bar)}));
    CHECK_FALSE (f.doc.perform (AddClip {f.synth, noteClip (0, bar, {{0, 0, 60, 1.0f}})}));   // zero length
    CHECK_FALSE (f.doc.perform (AddClip {f.synth, noteClip (0, bar, {{0, 10, 200, 1.0f}})})); // pitch
    CHECK_FALSE (f.doc.perform (AddClip {f.synth, noteClip (0, bar, {}, 1)}));                // tiny loop
    CHECK (f.doc.project().nextClipId == 1);
}

TEST_CASE ("Notes are stored sorted, without duplicates, with velocity clamped", "[model][clips]")
{
    Fixture f;
    const auto id = f.add (f.synth, noteClip (0, bar,
                                              {{480, 10, 64, 1.0f},
                                               {0, 10, 67, 2.0f},
                                               {0, 10, 60, 0.5f},
                                               {0, 20, 60, 0.7f}})); // replaces the earlier 60 at 0
    const auto& notes = f.clip (id).notes;
    REQUIRE (notes.size() == 3);
    CHECK (notes[0] == Note {0, 20, 60, 0.7f});
    CHECK (notes[1] == Note {0, 10, 67, 1.0f});
    CHECK (notes[2] == Note {480, 10, 64, 1.0f});
}

TEST_CASE ("Clip edits undo exactly and ids are never reused", "[model][clips][undo]")
{
    Fixture f;
    const auto initial = f.doc.project();
    const auto a = f.add (f.audio, audioClip (f.asset, bar, 2 * bar));
    CHECK (f.doc.undoDescription() == "Add clip");
    const auto afterAdd = f.doc.project();

    REQUIRE (f.doc.perform (SetClip {a, f.audio, moveClip (f.clip (a), 3 * bar), ClipEdit::move}));
    CHECK (f.doc.undoDescription() == "Move clip");
    REQUIRE (f.doc.perform (SplitClip {a, 4 * bar}));
    CHECK (f.doc.undoDescription() == "Split clip");
    const auto& clips = f.doc.project().tracks[0].clips;
    REQUIRE (clips.size() == 2);
    CHECK (clips[0].id == a);
    CHECK (clips[1].id == ClipId {2});
    CHECK (clips[0].end() == clips[1].start);

    REQUIRE (f.doc.perform (RemoveClip {a}));
    CHECK (f.doc.undoDescription() == "Delete clip");
    CHECK (f.doc.project().tracks[0].clips.size() == 1);

    for (int i = 0; i < 4; ++i) // remove, split, move, add
        REQUIRE (f.doc.undo());
    CHECK (f.doc.project() == initial);
    REQUIRE (f.doc.redo());
    CHECK (f.doc.project() == afterAdd); // same id on redo
    while (f.doc.redo())
    {
    }
    REQUIRE (f.doc.perform (AddClip {f.audio, audioClip (f.asset, 0, bar)}));
    CHECK (f.doc.project().tracks[0].clips.front().id == ClipId {3}); // never reuses 1 or 2
}

TEST_CASE ("Dragging a clip is one undo step and may change tracks of the same kind", "[model][clips][undo]")
{
    Fixture f;
    REQUIRE (f.doc.perform (AddTrack {TrackKind::audio}));
    const auto otherAudio = f.doc.project().tracks.back().id;
    const auto id = f.add (f.audio, audioClip (f.asset, 0, bar));
    const auto before = f.doc.project();

    for (Ticks to = beat; to <= 4 * beat; to += beat)
        REQUIRE (f.doc.perform (
            SetClip {id, to == 4 * beat ? otherAudio : f.audio, moveClip (f.clip (id), to), ClipEdit::move},
            7));
    CHECK (f.doc.project().tracks[0].clips.empty());
    CHECK (f.doc.project().tracks[2].clips.front().start == 4 * beat);
    CHECK_FALSE (f.doc.perform (SetClip {id, f.synth, f.clip (id), ClipEdit::move})); // wrong kind

    REQUIRE (f.doc.undo());
    CHECK (f.doc.project() == before);
}

TEST_CASE ("Trimming and splitting never change what plays", "[model][clips][property]")
{
    const auto seed = GENERATE (1u, 7u, 99u);
    CAPTURE (seed);
    std::mt19937 random (seed);
    const auto pick = [&random] (Ticks lo, Ticks hi)
    { return lo + static_cast<Ticks> (random() % static_cast<unsigned> (hi - lo)); };

    for (int i = 0; i < 300; ++i)
    {
        Clip clip = noteClip (pick (0, 8 * bar), pick (Clip::minLength * 3, 8 * bar));
        clip.loopLength = random() % 2 ? pick (Clip::minLength, 2 * bar) : 0;
        clip.contentOffset = clip.loopLength != 0 ? pick (0, clip.loopLength) : pick (0, bar);

        const auto at = pick (clip.start + Clip::minLength, clip.end() - Clip::minLength + 1);
        const auto parts = splitClip (clip, at, 120.0);
        REQUIRE (parts);
        const auto& [left, right] = *parts;
        CHECK (left.start == clip.start);
        CHECK (left.end() == at);
        CHECK (right.start == at);
        CHECK (right.end() == clip.end());

        for (Ticks t = clip.start; t < clip.end(); t += 7)
        {
            const auto& part = t < at ? left : right;
            REQUIRE (contentTimeAt (part, t) == contentTimeAt (clip, t));
        }

        // Trimming the left edge in and back out restores the clip exactly.
        const auto trimmed = trimClipStart (clip, at, 120.0);
        CHECK (trimClipStart (trimmed, clip.start, 120.0) == clip);
    }
}

TEST_CASE ("Audio clips keep their place in the file when trimmed or split", "[model][clips]")
{
    const auto clip = audioClip (AssetId {1}, bar, 4 * bar, 1000);
    const auto parts = splitClip (clip, 2 * bar, 120.0);
    REQUIRE (parts);
    // One bar at 120 BPM is 2 s.
    CHECK (parts->second.sourceOffset == 1000 + 2 * ap::core::flicksPerSecond);
    CHECK (trimClipStart (parts->second, bar, 120.0) == clip);

    // The left edge cannot reach before the start of the file.
    const auto earliest
        = trimClipStart (audioClip (AssetId {1}, 4 * bar, bar, ap::core::flicksPerSecond), 0, 120.0);
    CHECK (earliest.start == 4 * bar - beat * 2); // 1 s = 2 beats at 120 BPM
    CHECK (earliest.sourceOffset == 0);
}

TEST_CASE ("Drum steps toggle notes in the pattern", "[model][clips][drums]")
{
    auto pattern = makePatternClip (0, 4 * bar);
    CHECK (pattern.loopLength == DrumKit::patternLength);
    pattern = withDrumStep (pattern, 2, 4, true);
    CHECK (hasDrumStep (pattern, 2, 4));
    CHECK (pattern.notes.front() == Note {4 * DrumKit::ticksPerStep, DrumKit::ticksPerStep, 38, 1.0f});
    CHECK_FALSE (hasDrumStep (pattern, 2, 5));
    CHECK_FALSE (hasDrumStep (withDrumStep (pattern, 2, 4, false), 2, 4));
    CHECK (withDrumStep (pattern, 16, 0, true) == pattern); // out of range: unchanged
}

TEST_CASE ("Groups are one undo step and are all-or-nothing", "[model][undo]")
{
    ProjectDocument doc;
    const auto initial = doc.project();

    REQUIRE (doc.performGroup ("Add pattern",
                               [] (ProjectDocument::Group& group)
                               {
                                   const auto* added = group.perform (AddTrack {InstrumentKind::drums});
                                   REQUIRE (added != nullptr);
                                   const auto track = std::get<AddTrack> (*added).created;
                                   REQUIRE (group.perform (AddClip {track, makePatternClip (0, bar)})
                                            != nullptr);
                               }));
    CHECK (doc.undoDescription() == "Add pattern");
    CHECK (doc.project().tracks.front().clips.size() == 1);
    const auto after = doc.project();

    REQUIRE (doc.undo());
    CHECK (doc.project() == initial);
    REQUIRE (doc.redo());
    CHECK (doc.project() == after);

    // A rejected command rolls the whole group back.
    CHECK_FALSE (doc.performGroup ("Broken",
                                   [] (ProjectDocument::Group& group)
                                   {
                                       (void)group.perform (AddTrack {TrackKind::audio});
                                       (void)group.perform (
                                           AddClip {TrackId {999}, makePatternClip (0, bar)});
                                   }));
    CHECK (doc.project() == after);
    CHECK (doc.undoDescription() == "Add pattern");
}

TEST_CASE ("Random clip edits undo to the start and redo to the end", "[model][clips][undo][property]")
{
    const auto seed = GENERATE (3u, 21u, 2026u);
    CAPTURE (seed);
    std::mt19937 random (seed);
    Fixture f;

    const auto anyClip = [&]() -> std::optional<std::pair<TrackId, Clip>>
    {
        std::vector<std::pair<TrackId, Clip>> all;
        for (const auto& track : f.doc.project().tracks)
            for (const auto& clip : track.clips)
                all.emplace_back (track.id, clip);
        if (all.empty())
            return std::nullopt;
        return all[random() % all.size()];
    };

    for (int step = 0; step < 500; ++step)
    {
        const auto at = static_cast<Ticks> (random() % 64) * beat;
        const auto gesture = random() % 3 == 0 ? std::uint64_t {random() % 3 + 1} : std::uint64_t {0};
        const auto target = anyClip();
        switch (random() % 8)
        {
        case 0:
            (void)f.doc.perform (AddClip {f.audio, audioClip (f.asset, at, bar)});
            break;
        case 1:
            (void)f.doc.perform (
                AddClip {f.synth, noteClip (at, bar, {{0, beat, 60, 1.0f}}, random() % 2 ? beat : 0)});
            break;
        case 2:
            if (target)
                (void)f.doc.perform (
                    SetClip {target->second.id, target->first, moveClip (target->second, at), ClipEdit::move},
                    gesture);
            break;
        case 3:
            if (target)
                (void)f.doc.perform (SetClip {target->second.id, target->first,
                                              trimClipStart (target->second, at, f.doc.project().tempoBpm),
                                              ClipEdit::resize},
                                     gesture);
            break;
        case 4:
            if (target)
                (void)f.doc.perform (SplitClip {target->second.id, target->second.start + beat});
            break;
        case 5:
            if (target)
                (void)f.doc.perform (RemoveClip {target->second.id});
            break;
        case 6:
            (void)f.doc.perform (SetTempo {80.0 + static_cast<double> (random() % 80)});
            break;
        default:
            if (random() % 2 == 0)
                (void)f.doc.undo();
            else
                (void)f.doc.redo();
            break;
        }
    }

    while (f.doc.redo())
    {
    }
    const auto final = f.doc.project();
    while (f.doc.undo())
    {
    }
    CHECK (f.doc.project() == Project {}); // the fixture's own setup is undone too
    while (f.doc.redo())
    {
    }
    CHECK (f.doc.project() == final);
}

TEST_CASE ("Editing a clip in the gesture that created it stays one undo step", "[model][clips][undo]")
{
    ProjectDocument doc;
    const auto before = doc.project();
    REQUIRE (doc.performGroup (
        "Add pattern",
        [] (ProjectDocument::Group& group)
        {
            const auto* added = group.perform (AddTrack {InstrumentKind::drums});
            (void)group.perform (AddClip {std::get<AddTrack> (*added).created,
                                          withDrumStep (makePatternClip (0, bar), 0, 0, true)});
        },
        5));
    const auto track = doc.project().tracks[0].id;
    const auto id = doc.project().tracks[0].clips[0].id;
    for (std::size_t step = 1; step < 4; ++step)
        REQUIRE (
            doc.perform (SetClip {id, track, withDrumStep (doc.project().tracks[0].clips[0], 0, step, true),
                                  ClipEdit::notes},
                         5));
    CHECK (doc.project().tracks[0].clips[0].notes.size() == 4);
    const auto after = doc.project();

    REQUIRE (doc.undo());
    CHECK (doc.project() == before);
    CHECK_FALSE (doc.canUndo());
    REQUIRE (doc.redo());
    CHECK (doc.project() == after);
}

TEST_CASE ("The first-run starter song has a four-bar beat and a synth track to play on", "[model]")
{
    const auto project = starterProject();
    REQUIRE (project.tracks.size() == 2);
    CHECK (project.tracks[0].instrument == InstrumentKind::drums);
    CHECK (project.tracks[1].instrument == InstrumentKind::synth);
    REQUIRE (project.tracks[0].clips.size() == 1);
    const auto& groove = project.tracks[0].clips[0];
    CHECK (groove.length == 4 * DrumKit::patternLength);
    CHECK (groove.loopLength == DrumKit::patternLength);
    CHECK (groove.notes.size() == 3 + 2 + 8);
    CHECK (project.tracks[1].clips.empty());
}
