#include "TimelineHelpers.h"
#include "ap/accompaniment/Grooves.h"
#include "ap/accompaniment/MusicalContext.h"
#include "ap/analysis/RhythmAnalysis.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/model/ClipEditing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace ap;
using namespace ap::accompaniment;

namespace
{
analysis::RhythmAnalysis steady (double bpm, double confidence = 0.9)
{
    analysis::RhythmAnalysis a;
    a.durationSeconds = 20.0;
    a.bpm = bpm;
    a.bpmConfidence = confidence;
    a.onsetsPerSecond = 3.0;
    a.rmsDb = -24.0;
    a.feel = analysis::RhythmAnalysis::Feel::straight;
    return a;
}

MusicalContext context (double bpm, Feel feel, Density density, Energy energy)
{
    MusicalContext c;
    c.bpm = bpm;
    c.feel = feel;
    c.density = density;
    c.energy = energy;
    return c;
}

std::vector<std::string> ids (const std::vector<Suggestion>& list)
{
    std::vector<std::string> out;
    for (const auto& s : list)
        out.push_back (s.grooveId);
    return out;
}
} // namespace

TEST_CASE ("The shipped groove library is complete and valid", "[accompaniment][library]")
{
    std::vector<std::string> problems;
    const auto library = parseGrooves ("", &problems); // an empty file is reported, not trusted
    CHECK_FALSE (problems.empty());
    CHECK (library.grooves.empty());

    const auto& grooves = factoryGrooves();
    CHECK (grooves.grooves.size() >= 10);
    std::set<std::string> seen;
    int swing = 0;
    for (const auto& g : grooves.grooves)
    {
        CAPTURE (g.id);
        CHECK (seen.insert (g.id).second);
        CHECK (g.id.starts_with ("groove."));
        CHECK (g.timeSignature == core::TimeSignature {4, 4});
        CHECK (g.minBpm < g.maxBpm);
        CHECK (g.pattern[0] != 0); // every groove has a kick
        swing += g.feel == Feel::swing ? 1 : 0;
    }
    CHECK (swing >= 1);
    // The library covers the tempo range a take is likely to be in, for every feel it has.
    for (const double bpm : {60.0, 75.0, 90.0, 105.0, 120.0, 140.0, 160.0})
        CHECK_FALSE (findAccompaniment (
                         Kind::drums, context (bpm, Feel::straight, Density::medium, Energy::medium), grooves)
                         .empty());
}

TEST_CASE ("A bad groove is reported and left out, the rest is kept", "[accompaniment][library]")
{
    std::vector<std::string> problems;
    const auto library = parseGrooves (R"({"grooves": [
        {"id": "ok", "name": "Ok", "kind": "drums", "timeSignature": [4,4], "feel": "straight", "density": "medium",
         "energy": "soft", "minBpm": 60, "maxBpm": 120, "pattern": {"kick": "x...x...x...x..."}},
        {"id": "short", "name": "S", "kind": "drums", "timeSignature": [4,4], "feel": "straight", "density": "medium",
         "energy": "soft", "minBpm": 60, "maxBpm": 120, "pattern": {"kick": "x..."}},
        {"id": "pad", "name": "P", "kind": "drums", "timeSignature": [4,4], "feel": "straight", "density": "medium",
         "energy": "soft", "minBpm": 60, "maxBpm": 120, "pattern": {"gong": "x...x...x...x..."}},
        {"id": "range", "name": "R", "kind": "drums", "timeSignature": [4,4], "feel": "straight", "density": "medium",
         "energy": "soft", "minBpm": 120, "maxBpm": 60, "pattern": {"kick": "x...x...x...x..."}},
        {"id": "empty", "name": "E", "kind": "drums", "timeSignature": [4,4], "feel": "straight", "density": "medium",
         "energy": "soft", "minBpm": 60, "maxBpm": 120, "pattern": {"kick": "................"}},
        {"id": "mood", "name": "M", "kind": "drums", "timeSignature": [4,4], "feel": "angry", "density": "medium",
         "energy": "soft", "minBpm": 60, "maxBpm": 120, "pattern": {"kick": "x...x...x...x..."}},
        {"id": "ok", "name": "Dup", "kind": "drums", "timeSignature": [4,4], "feel": "straight", "density": "medium",
         "energy": "soft", "minBpm": 60, "maxBpm": 120, "pattern": {"kick": "x...x...x...x..."}},
        {"name": "no id"}
    ]})",
                                       &problems);
    REQUIRE (library.grooves.size() == 1);
    CHECK (library.grooves[0].id == "ok");
    CHECK (library.grooves[0].pattern[0] == 0b0001000100010001);
    CHECK (problems.size() == 7);
    CHECK (parseGrooves ("not json").grooves.empty());
}

TEST_CASE ("A take becomes a context only when there is a rhythm to follow", "[accompaniment][context]")
{
    const double project = 120.0;
    const core::TimeSignature meter {4, 4};

    SECTION ("a clear pulse at the project's tempo")
    {
        const auto r = makeContext (steady (120.5), project, meter);
        REQUIRE (r.verdict == Verdict::ready);
        CHECK (r.context.bpm == project); // grooves play at the project's tempo
        CHECK (r.context.feel == Feel::straight);
        CHECK (r.context.density == Density::medium); // 3 onsets/s at 120 BPM = 1.5 per beat
        CHECK (r.context.energy == Energy::medium);
    }
    SECTION ("twice or half the tempo is the same pulse")
    {
        CHECK (makeContext (steady (240.0), project, meter).verdict == Verdict::ready);
        CHECK (makeContext (steady (60.0), project, meter).verdict == Verdict::ready);
        CHECK (sameTempoUpToOctave (87.0, 174.0, 0.04));
        CHECK_FALSE (sameTempoUpToOctave (87.0, 120.0, 0.04));
        CHECK_FALSE (sameTempoUpToOctave (0.0, 120.0, 0.04));
    }
    SECTION ("a different tempo is reported with the take's own estimate")
    {
        const auto r = makeContext (steady (87.0), project, meter);
        CHECK (r.verdict == Verdict::tempoMismatch);
        CHECK (r.detectedBpm == 87.0);
    }
    SECTION ("the beats must fall on the project's grid, or the groove would clash")
    {
        // 120 BPM: a beat every 0.5 s. A first beat at 4.02 s is 20 ms off (4 %): fine.
        CHECK (makeContext (steady (120.0), project, meter, 4.02).verdict == Verdict::ready);
        CHECK (makeContext (steady (120.0), project, meter, 3.98).verdict == Verdict::ready);
        // A quarter of a beat late (125 ms) is not.
        CHECK (makeContext (steady (120.0), project, meter, 4.125).verdict == Verdict::offGrid);
        CHECK (makeContext (steady (120.0), project, meter, 3.9).verdict == Verdict::offGrid);
        // A take counted in doubles has beats on the project's eighths, which are on the grid.
        CHECK (makeContext (steady (240.0), project, meter, 4.25).verdict == Verdict::ready);
        CHECK (makeContext (steady (240.0), project, meter, 4.125).verdict == Verdict::offGrid);
        // Not asked: no check.
        CHECK (makeContext (steady (120.0), project, meter).verdict == Verdict::ready);

        CHECK (beatGridError (4.02, 120.0) == Catch::Approx (0.02).margin (1e-9));
        CHECK (beatGridError (4.48, 120.0) == Catch::Approx (-0.02).margin (1e-9));
        CHECK (std::abs (beatGridError (4.25, 120.0)) == Catch::Approx (0.25).margin (1e-9));
    }
    SECTION ("an unsure tempo asks for help, an absent one says there is no rhythm")
    {
        CHECK (makeContext (steady (120.0, 0.6), project, meter).verdict == Verdict::tempoUncertain);
        CHECK (makeContext (steady (120.0, 0.69), project, meter).verdict == Verdict::tempoUncertain);
        CHECK (makeContext (steady (120.0, 0.4), project, meter).verdict == Verdict::noRhythm);
        auto sparse = steady (120.0);
        sparse.onsetsPerSecond = 0.3;
        CHECK (makeContext (sparse, project, meter).verdict == Verdict::noRhythm);
        auto none = steady (120.0);
        none.bpm = 0.0;
        CHECK (makeContext (none, project, meter).verdict == Verdict::noRhythm);
    }
    SECTION ("density, energy and feel follow the take")
    {
        auto a = steady (120.0);
        a.onsetsPerSecond = 0.9; // under one per beat
        a.rmsDb = -40.0;
        a.feel = analysis::RhythmAnalysis::Feel::swing;
        const auto soft = makeContext (a, project, meter);
        CHECK (soft.context.density == Density::sparse);
        CHECK (soft.context.energy == Energy::soft);
        CHECK (soft.context.feel == Feel::swing);

        a.onsetsPerSecond = 6.0; // three per beat
        a.rmsDb = -10.0;
        a.feel = analysis::RhythmAnalysis::Feel::unknown;
        const auto loud = makeContext (a, project, meter);
        CHECK (loud.context.density == Density::dense);
        CHECK (loud.context.energy == Energy::strong);
        CHECK (loud.context.feel == Feel::unknown);
    }
}

TEST_CASE ("Grooves are ranked by how well they fit, deterministically", "[accompaniment][matcher]")
{
    const auto& library = factoryGrooves();

    SECTION ("a soft, sparse take at a slow tempo gets the quiet grooves")
    {
        const auto top = findAccompaniment (
            Kind::drums, context (72.0, Feel::straight, Density::sparse, Energy::soft), library);
        REQUIRE_FALSE (top.empty());
        CHECK ((top[0].grooveId == "groove.ballad-sparse"
                || top[0].grooveId == "groove.pocket-soft")); // equal fits: by id
        CHECK ((top[1].grooveId == "groove.ballad-sparse" || top[1].grooveId == "groove.pocket-soft"));
        CHECK_FALSE (top.front().reasons.empty());
        for (std::size_t i = 1; i < top.size(); ++i)
            CHECK (top[i - 1].score >= top[i].score);
    }
    SECTION ("a loud, dense take at a fast tempo gets the driving ones")
    {
        const auto top = findAccompaniment (
            Kind::drums, context (140.0, Feel::straight, Density::dense, Energy::strong), library);
        REQUIRE_FALSE (top.empty());
        CHECK (top.front().grooveId == "groove.rock-driving");
    }
    SECTION ("a swung take gets the shuffles first")
    {
        const auto top = findAccompaniment (
            Kind::drums, context (90.0, Feel::swing, Density::medium, Energy::medium), library);
        REQUIRE (top.size() >= 2);
        CHECK (top[0].grooveId.find ("shuffle") != std::string::npos);
        CHECK (top[1].grooveId.find ("shuffle") != std::string::npos);
    }
    SECTION ("the same context always gives the same list, and equal scores are ordered by id")
    {
        const auto c = context (100.0, Feel::straight, Density::medium, Energy::medium);
        CHECK (ids (findAccompaniment (Kind::drums, c, library))
               == ids (findAccompaniment (Kind::drums, c, library)));
    }
    SECTION ("grooves already shown are skipped, so 'try another' moves on")
    {
        const auto c = context (100.0, Feel::straight, Density::medium, Energy::medium);
        auto shown = std::vector<std::string> {};
        std::set<std::string> distinct;
        for (int i = 0; i < 20; ++i)
        {
            const auto next = findAccompaniment (Kind::drums, c, library, shown);
            if (next.empty())
                break;
            CHECK (distinct.insert (next.front().grooveId).second);
            shown.push_back (next.front().grooveId);
        }
        CHECK (distinct.size() >= 4);
    }
    SECTION ("nothing is offered when nothing fits")
    {
        CHECK (findAccompaniment (Kind::drums,
                                  context (280.0, Feel::straight, Density::medium, Energy::medium), library)
                   .empty());
        auto waltz = context (100.0, Feel::straight, Density::medium, Energy::medium);
        waltz.timeSignature = {3, 4};
        CHECK (findAccompaniment (Kind::drums, waltz, library).empty());
    }
    SECTION ("the weights are settings, not constants")
    {
        const auto c = context (72.0, Feel::straight, Density::dense, Energy::strong);
        MatchWeights tempoOnly {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        const auto byTempo = findAccompaniment (Kind::drums, c, library, {}, tempoOnly);
        REQUIRE_FALSE (byTempo.empty());
        CHECK (byTempo.front().score == 1.0); // everything in range scores the same: ordered by id
        CHECK (byTempo.front().grooveId < byTempo.back().grooveId);
        CHECK (findAccompaniment (Kind::drums, c, library, {}, MatchWeights {0, 0, 0, 0, 0, 0, 0}).empty());
        MatchWeights strict;
        strict.minimumScore = 0.99;
        CHECK (findAccompaniment (Kind::drums, c, library, {}, strict).size() < byTempo.size());
    }
}

TEST_CASE ("A groove becomes a looping drum pattern clip", "[accompaniment][clip]")
{
    const auto* groove = factoryGrooves().find ("groove.four-on-the-floor");
    REQUIRE (groove != nullptr);
    const auto clip
        = grooveClip (*groove, 4 * core::ticksPerQuarterNote * 2, 4 * model::DrumKit::patternLength);
    CHECK (clip.start == 8 * core::ticksPerQuarterNote);
    CHECK (clip.length == 4 * model::DrumKit::patternLength);
    CHECK (clip.loopLength == model::DrumKit::patternLength);
    for (const std::size_t step : {0u, 4u, 8u, 12u})
        CHECK (model::hasDrumStep (clip, 0, step)); // the four kicks
    CHECK_FALSE (model::hasDrumStep (clip, 0, 1));
    CHECK (model::hasDrumStep (clip, 4, 4)); // clap on 2
}

TEST_CASE ("From a played groove to a suggestion, end to end", "[accompaniment][integration]")
{
    constexpr double fs = 48000.0;
    engine::Engine engine;
    test::publish (engine,
                   test::drumPatternProject ({{0, {0, 6, 8}}, {1, {4, 12}}, {2, {0, 2, 4, 6, 8, 10, 12, 14}}},
                                             8 * 4 * core::ticksPerQuarterNote));
    engine.getTransport().setTempo (96.0);
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 1, static_cast<std::int64_t> (fs * 20.0), 512});
    instruments::SampleBuffer take;
    take.sampleRate = fs;
    take.channels = {audio[0]};

    const auto analysed = analysis::analyseRhythm (take);
    const auto result = makeContext (analysed, 96.0, {4, 4});
    REQUIRE (result.verdict == Verdict::ready);
    const auto suggestions = findAccompaniment (Kind::drums, result.context, factoryGrooves());
    REQUIRE_FALSE (suggestions.empty());
    CHECK (suggestions.front().score > 0.7);

    // The same take against a project at another tempo is not offered a beat.
    CHECK (makeContext (analysed, 130.0, {4, 4}).verdict == Verdict::tempoMismatch);
}
