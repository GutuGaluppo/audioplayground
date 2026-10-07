#include "Golden.h"
#include "TimelineHelpers.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/model/Commands.h"
#include "ap/model/ProjectDocument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>

using namespace ap;
using namespace ap::model;

namespace
{
constexpr double fs = 48000.0;
constexpr int closedHat = 2;
constexpr core::Ticks bar = 4 * core::ticksPerQuarterNote;

// A quiet hi-hat on the drum track (so the sum of two copies stays clear of the limiter).
ProjectDocument hatDocument()
{
    ProjectDocument doc;
    doc.perform (AddTrack {InstrumentKind::drums});
    auto clip = makePatternClip (0, 4 * bar);
    clip = withDrumStep (clip, closedHat, 0, true);
    doc.perform (AddClip {doc.project().tracks.front().id, clip});
    doc.perform (SetTrackVolume {doc.project().tracks.front().id, -12.0f});
    return doc;
}

std::vector<float> render (const Project& project, int blockSize = 512, std::int64_t length = 96000)
{
    engine::Engine engine;
    test::publish (engine, project);
    engine.getTransport().requestPlay();
    return engine::renderOffline (engine, {fs, 1, length, blockSize})[0];
}

double energy (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (auto i = from; i < std::min (to, x.size()); ++i)
        sum += static_cast<double> (x[i]) * static_cast<double> (x[i]);
    return sum;
}
} // namespace

TEST_CASE ("A bus is limited, named, and removed with the sends that fed it", "[bus][model]")
{
    ProjectDocument doc;
    doc.perform (AddTrack {TrackKind::audio});
    const auto track = doc.project().tracks[0].id;

    for (std::size_t i = 0; i < Bus::maxBuses; ++i)
        REQUIRE (doc.perform (AddBus {}));
    CHECK_FALSE (doc.perform (AddBus {}));
    CHECK (doc.project().buses[0].name == "Bus 1");

    const auto bus = doc.project().buses[2].id;
    CHECK_FALSE (doc.perform (SetTrackSend {track, BusId {99}, -6.0f}));
    CHECK_FALSE (doc.perform (SetTrackSend {TrackId {99}, bus, -6.0f}));
    CHECK_FALSE (doc.perform (SetTrackSend {track, bus, NAN}));
    CHECK_FALSE (doc.perform (SetTrackSend {track, bus, Send::minLevelDb})); // no send to remove
    REQUIRE (doc.perform (SetTrackSend {track, bus, -6.0f}));
    REQUIRE (doc.perform (SetTrackSend {track, doc.project().buses[5].id, 40.0f}));
    CHECK (doc.project().tracks[0].sends[1].levelDb == Send::maxLevelDb);

    const auto before = doc.project();
    REQUIRE (doc.perform (RemoveBus {bus}));
    CHECK (doc.project().buses.size() == Bus::maxBuses - 1);
    CHECK (doc.project().tracks[0].sends.size() == 1);

    REQUIRE (doc.undo());
    CHECK (doc.project() == before); // the bus and its send are back, in place
}

TEST_CASE ("Send edits undo exactly, including a send removed by dragging to the minimum", "[bus][model]")
{
    ProjectDocument doc;
    doc.perform (AddTrack {TrackKind::audio});
    doc.perform (AddBus {});
    const auto track = doc.project().tracks[0].id;
    const auto bus = doc.project().buses[0].id;

    REQUIRE (doc.perform (SetTrackSend {track, bus, -6.0f}));
    const auto sent = doc.project();
    REQUIRE (doc.perform (SetTrackSend {track, bus, Send::minLevelDb}));
    CHECK (doc.project().tracks[0].sends.empty());
    REQUIRE (doc.undo());
    CHECK (doc.project() == sent);
    REQUIRE (doc.undo());
    CHECK (doc.project().tracks[0].sends.empty());
}

TEST_CASE ("Bus settings are validated, clamped and undoable", "[bus][model]")
{
    ProjectDocument doc;
    doc.perform (AddBus {"  Verb  "});
    const auto bus = doc.project().buses[0].id;
    CHECK (doc.project().buses[0].name == "Verb");

    CHECK_FALSE (doc.perform (SetBusVolume {BusId {7}, 0.0f}));
    CHECK_FALSE (doc.perform (SetBusVolume {bus, NAN}));
    REQUIRE (doc.perform (SetBusVolume {bus, 40.0f}));
    CHECK (doc.project().buses[0].volumeDb == Bus::maxVolumeDb);
    REQUIRE (doc.perform (SetBusPan {bus, -3.0f}));
    CHECK (doc.project().buses[0].pan == -1.0f);
    REQUIRE (doc.perform (SetBusMute {bus, true}));
    REQUIRE (doc.perform (RenameBus {bus, "Space"}));

    auto reverb = defaultEffectState (params::EffectKind::reverb);
    reverb.enabled = true;
    REQUIRE (doc.perform (SetBusEffect {bus, params::EffectKind::reverb, reverb}));

    for (int i = 0; i < 5; ++i)
        REQUIRE (doc.undo());
    CHECK (doc.project().buses[0].volumeDb == 0.0f);
    CHECK (doc.project().buses[0].pan == 0.0f);
    CHECK_FALSE (doc.project().buses[0].muted);
    CHECK (doc.project().buses[0].name == "Verb");
    CHECK (doc.project().buses[0].effects == defaultTrackEffects());
}

TEST_CASE ("A send adds a time-aligned copy through the bus", "[bus][engine]")
{
    auto doc = hatDocument();
    const auto dry = render (doc.project());

    REQUIRE (doc.perform (AddBus {}));
    REQUIRE (doc.perform (SetTrackSend {doc.project().tracks[0].id, doc.project().buses[0].id, 0.0f}));
    const auto sent = render (doc.project());

    REQUIRE (sent.size() == dry.size());
    // Same sound, same instant: with every effect off the bus is transparent, so the two paths
    // sum to exactly twice the dry signal. Any latency mismatch would show up as a comb filter.
    for (std::size_t i = 0; i < dry.size(); ++i)
        REQUIRE (sent[i] == Catch::Approx (2.0f * dry[i]).margin (1.0e-4));
}

TEST_CASE ("Bus rendering does not depend on block size", "[bus][engine][determinism]")
{
    auto doc = hatDocument();
    doc.perform (AddBus {});
    doc.perform (SetTrackSend {doc.project().tracks[0].id, doc.project().buses[0].id, -6.0f});
    doc.perform (SetBusVolume {doc.project().buses[0].id, -3.0f});
    auto delay = defaultEffectState (params::EffectKind::delay);
    delay.enabled = true;
    doc.perform (SetBusEffect {doc.project().buses[0].id, params::EffectKind::delay, delay});

    const auto reference = render (doc.project(), 512);
    const int blockSize = GENERATE (1, 333, 4096);
    CAPTURE (blockSize);
    const auto other = render (doc.project(), blockSize);
    REQUIRE (other.size() == reference.size());
    for (std::size_t i = 0; i < reference.size(); ++i)
        REQUIRE (other[i] == Catch::Approx (reference[i]).margin (1.0e-5));
}

TEST_CASE ("A muted bus is silent", "[bus][engine]")
{
    auto doc = hatDocument();
    const auto dry = render (doc.project());
    doc.perform (AddBus {});
    const auto bus = doc.project().buses[0].id;
    doc.perform (SetTrackSend {doc.project().tracks[0].id, bus, 0.0f});
    doc.perform (SetBusMute {bus, true});
    CHECK (render (doc.project()) == dry);
}

TEST_CASE ("A bus effect rings on after the sound that fed it has ended", "[bus][engine]")
{
    auto doc = hatDocument();
    doc.perform (AddBus {});
    const auto bus = doc.project().buses[0].id;
    doc.perform (SetTrackSend {doc.project().tracks[0].id, bus, 0.0f});
    auto delay = defaultEffectState (params::EffectKind::delay);
    delay.enabled = true;
    doc.perform (SetBusEffect {bus, params::EffectKind::delay, delay}); // 375 ms echo

    const auto withDelay = render (doc.project());
    doc.perform (
        SetBusEffect {bus, params::EffectKind::delay, defaultEffectState (params::EffectKind::delay)});
    const auto without = render (doc.project());

    // The hat is over after ~0.15 s; only the echo is left around 0.4 s.
    CHECK (energy (without, 20000, 28000) < 1.0e-9);
    CHECK (energy (withDelay, 20000, 28000) > 1.0e-4);
}

namespace
{
// A one-bar groove through a shared bus (echo and reverb) at -6 dB, in stereo.
std::vector<std::vector<float>> renderBusMix (int blockSize)
{
    ProjectDocument doc {
        test::drumPatternProject ({{0, {0, 8}}, {1, {4, 12}}, {2, {0, 2, 4, 6, 8, 10, 12, 14}}}, bar)};
    doc.perform (AddBus {});
    const auto bus = doc.project().buses[0].id;
    doc.perform (RenameBus {bus, "Space"});
    for (const auto effect : {params::EffectKind::delay, params::EffectKind::reverb})
    {
        auto state = defaultEffectState (effect);
        state.enabled = true;
        doc.perform (SetBusEffect {bus, effect, state});
    }
    doc.perform (SetTrackSend {doc.project().tracks.front().id, bus, -6.0f});

    engine::Engine engine;
    test::publish (engine, doc.project());
    engine.getTransport().requestPlay();
    return engine::renderOffline (engine, {fs, 2, static_cast<std::int64_t> (fs * 2.5), blockSize});
}
} // namespace

TEST_CASE ("A bus mix does not depend on block size", "[bus][engine][determinism]")
{
    const auto reference = renderBusMix (512);
    for (const int blockSize : {1, 333, 4096})
    {
        CAPTURE (blockSize);
        CHECK (renderBusMix (blockSize) == reference);
    }
}

TEST_CASE ("A groove through a shared echo and reverb bus matches the golden file", "[bus][golden]")
{
    const auto audio = renderBusMix (512);
    const auto result = test::compareWithGolden ("bus_mix_48k", {fs, audio}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
