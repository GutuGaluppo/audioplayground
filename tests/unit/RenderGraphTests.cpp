#include "TestAudio.h"
#include "ap/engine/Engine.h"
#include "ap/engine/RenderGraph.h"
#include "ap/model/ProjectDocument.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace ap::engine;
using namespace ap::model;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Constant-power pan law", "[graph]")
{
    const auto centre = constantPowerPan (0.0f);
    CHECK_THAT (centre.left, WithinAbs (std::sqrt (0.5), 1.0e-6));
    CHECK_THAT (centre.right, WithinAbs (std::sqrt (0.5), 1.0e-6));

    const auto left = constantPowerPan (-1.0f);
    CHECK_THAT (left.left, WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (left.right, WithinAbs (0.0, 1.0e-6));

    // Power is constant across the whole range.
    for (float pan = -1.0f; pan <= 1.0f; pan += 0.1f)
    {
        const auto g = constantPowerPan (pan);
        CHECK_THAT (static_cast<double> (g.left * g.left + g.right * g.right), WithinAbs (1.0, 1.0e-5));
    }

    CHECK (constantPowerPan (NAN).left == constantPowerPan (0.0f).left);
}

TEST_CASE ("Render graph resolves volume, mute and solo", "[graph]")
{
    ProjectDocument doc;
    for (int i = 0; i < 3; ++i)
        REQUIRE (doc.perform (AddTrack {TrackKind::audio}));
    const auto a = doc.project().tracks[0].id;
    const auto b = doc.project().tracks[1].id;
    const auto c = doc.project().tracks[2].id;

    SECTION ("all tracks audible by default, at unity volume")
    {
        const auto graph = buildRenderGraph (doc.project(), doc.version());
        REQUIRE (graph.tracks.size() == 3);
        CHECK (graph.tracks[0].audible);
        CHECK_THAT (static_cast<double> (graph.tracks[0].leftGain), WithinAbs (std::sqrt (0.5), 1.0e-6));
        CHECK (graph.projectVersion == doc.version());
    }

    SECTION ("solo isolates, mute wins over solo")
    {
        REQUIRE (doc.perform (SetTrackSolo {a, true}));
        REQUIRE (doc.perform (SetTrackSolo {b, true}));
        REQUIRE (doc.perform (SetTrackMute {b, true}));
        const auto graph = buildRenderGraph (doc.project(), doc.version());
        CHECK (graph.tracks[0].audible);
        CHECK_FALSE (graph.tracks[1].audible);
        CHECK_FALSE (graph.tracks[2].audible);
        CHECK (graph.tracks[1].leftGain == 0.0f);
    }

    SECTION ("minimum volume is silence")
    {
        REQUIRE (doc.perform (SetTrackVolume {c, Track::minVolumeDb}));
        const auto graph = buildRenderGraph (doc.project(), doc.version());
        CHECK_FALSE (graph.tracks[2].audible);
    }
}

TEST_CASE ("Engine picks up a published render graph at the next block", "[graph][engine]")
{
    Engine engine;
    engine.prepare (48000.0, 256);
    ap::test::TestBuffer buffer (2, 256);

    engine.process (buffer.block());
    CHECK (engine.getRenderedGraphVersion() == 0);

    ProjectDocument doc;
    REQUIRE (doc.perform (AddTrack {TrackKind::audio}));
    engine.publishRenderGraph (
        std::make_unique<RenderGraph> (buildRenderGraph (doc.project(), doc.version())));
    CHECK (engine.getRenderedGraphVersion() == 0); // not until the audio thread runs

    engine.process (buffer.block());
    CHECK (engine.getRenderedGraphVersion() == doc.version());

    REQUIRE (doc.perform (AddTrack {TrackKind::audio}));
    engine.publishRenderGraph (
        std::make_unique<RenderGraph> (buildRenderGraph (doc.project(), doc.version())));
    engine.process (buffer.block());
    CHECK (engine.getRenderedGraphVersion() == doc.version());
    CHECK (engine.collectGarbage() == 1);
}
