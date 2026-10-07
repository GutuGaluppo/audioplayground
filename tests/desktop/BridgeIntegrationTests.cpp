#include "SampleLoader.h"
#include "TempDirectory.h"
#include "WebUiHost.h"
#include "ap/engine/OfflineRenderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ap;

// UI -> core -> engine, end to end without a window: the real WebUiHost (built without its web
// view, AP_HEADLESS_UI) gets intents as JSON exactly as the page sends them (through the strict
// codec), changes the real session and engine, and emits the events the page would receive.

namespace
{
constexpr double rate = 48000.0;

struct Harness
{
    juce::ScopedJuceInitialiser_GUI juce;
    test::TempDirectory temp;
    engine::Engine engine;
    desktop::AudioDeviceHost host {engine};
    desktop::Session session {engine, io::ProjectFolder {temp.path() / "Unsaved"}};
    desktop::SampleLoader loader {session, engine};
    desktop::AudioRecorder recorder {session, engine};
    juce::PropertiesFile::Options options = [this]
    {
        juce::PropertiesFile::Options o;
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = juce::String (temp.path().string());
        return o;
    }();
    juce::PropertiesFile settings {options};
    desktop::ProjectActions actions {session, settings};
    std::unique_ptr<desktop::WebUiHost> ui;
    std::vector<juce::var> events;

    Harness()
    {
        engine.prepare (rate, 256);
        loader.sync (rate);
        ui = std::make_unique<desktop::WebUiHost> (host, engine, session, actions, loader, recorder);
        ui->eventSink = [this] (const juce::var& event) { events.push_back (event); };
    }

    ~Harness() { ui.reset(); }

    // The page's message: {"type": ..., "payload": {...}}.
    void send (const juce::String& type, const juce::String& payloadJson = "{}")
    {
        auto* message = new juce::DynamicObject();
        message->setProperty ("type", type);
        message->setProperty ("payload", juce::JSON::parse (payloadJson));
        ui->receiveIntent (juce::var (message));
    }

    // The newest payload of an event type; void if none arrived.
    [[nodiscard]] juce::var latest (const juce::String& type) const
    {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if ((*it)["type"].toString() == type)
                return (*it)["payload"];
        return {};
    }

    [[nodiscard]] int count (const juce::String& type) const
    {
        return static_cast<int> (std::count_if (events.begin(), events.end(), [&] (const juce::var& e)
                                                { return e["type"].toString() == type; }));
    }

    [[nodiscard]] double param (const juce::String& id) const
    {
        double value = std::nan ("");
        for (const auto& e : events)
            if (e["type"].toString() == "param.value" && e["payload"]["id"].toString() == id)
                value = static_cast<double> (e["payload"]["value"]);
        return value;
    }

    [[nodiscard]] juce::var drumClip() const
    {
        const auto tracks = latest ("timeline.state")["tracks"];
        for (const auto& track : *tracks.getArray())
            if (static_cast<int> (track["kind"]) == 3 && track["clips"].size() > 0)
                return track["clips"][0];
        return {};
    }

    [[nodiscard]] static bool hasHit (const juce::var& clip, int pad, int step)
    {
        for (const auto& note : *clip["notes"].getArray())
            if (static_cast<int> (note["pitch"]) == 36 + pad
                && static_cast<int> (note["start"]) == step * 240)
                return true;
        return false;
    }

    [[nodiscard]] double loudness (int seconds)
    {
        engine.getTransport().requestSeek (0);
        engine.getTransport().requestPlay();
        const auto audio
            = engine::renderOffline (engine, {rate, 2, static_cast<std::int64_t> (seconds * rate), 512});
        double energy = 0.0;
        for (const auto& channel : audio)
            for (const auto sample : channel)
                energy += static_cast<double> (sample) * static_cast<double> (sample);
        return energy;
    }
};
} // namespace

TEST_CASE ("app.ready tells the page everything it needs to draw", "[bridge][integration]")
{
    Harness h;
    h.send ("app.ready");
    for (const char* type : {"engine.status", "audio.devices", "project.state", "history.state",
                             "transport.state", "timeline.state", "project.assets", "drums.kit"})
        CHECK (h.count (type) >= 1);
    CHECK (h.param ("synth.cutoff") == Catch::Approx (4000.0)); // the default, sent as a param.value
    CHECK (h.latest ("history.state")["canUndo"].equals (false));
}

TEST_CASE ("Malformed or unknown intents change nothing", "[bridge][integration]")
{
    Harness h;
    h.send ("app.ready");

    h.send ("track.add", R"({"kind": 99})");               // out of range
    h.send ("track.add", R"({"kind": 1, "extra": true})"); // unknown field
    h.send ("track.add", R"({"kind": "synth"})");          // wrong type
    h.send ("project.format-disk", "{}");                  // unknown intent
    h.send ("param.set", R"({"id": "nope.nothing", "value": 1, "gesture": 0})");

    CHECK (h.session.project().tracks.empty());
    CHECK_FALSE (h.session.isDirty());
    CHECK (h.count ("timeline.state") == 1); // only the one from app.ready
}

TEST_CASE ("Painting a drum step builds a track and a clip, and the engine plays it", "[bridge][integration]")
{
    Harness h;
    h.send ("app.ready");
    REQUIRE (h.loudness (1) == 0.0); // nothing yet

    h.send ("drums.setStep", R"({"clip": 0, "pad": 0, "step": 0, "on": true, "gesture": 7})");
    h.send ("drums.setStep", R"({"clip": 0, "pad": 1, "step": 4, "on": true, "gesture": 7})");

    REQUIRE (h.session.project().tracks.size() == 1);
    const auto clip = h.drumClip();
    REQUIRE_FALSE (clip.isVoid());
    CHECK (Harness::hasHit (clip, 0, 0));
    CHECK (Harness::hasHit (clip, 1, 4));
    CHECK (h.latest ("history.state")["undoLabel"].toString() == "Add pattern"); // one step

    CHECK (h.loudness (2) > 0.0); // the kick and snare reached the speakers' signal path
}

TEST_CASE ("A drum pattern preset is one undo step and replaces what was there", "[bridge][integration]")
{
    Harness h;
    h.send ("app.ready");
    h.send ("drums.setStep", R"({"clip": 0, "pad": 2, "step": 3, "on": true, "gesture": 0})");

    // Kick on the beat (bits 0, 4, 8, 12) and nothing else.
    h.send ("drums.setPattern", R"({"clip": 0, "pads": [4369]})");
    auto clip = h.drumClip();
    for (int step = 0; step < 16; ++step)
        CHECK (Harness::hasHit (clip, 0, step) == (step % 4 == 0));
    CHECK_FALSE (Harness::hasHit (clip, 2, 3)); // the earlier hat is gone

    REQUIRE (h.session.undo()); // the whole preset in one step
    clip = h.drumClip();
    CHECK (Harness::hasHit (clip, 2, 3));
    CHECK_FALSE (Harness::hasHit (clip, 0, 0));

    // The page cannot send more than 16 pads or a mask outside 16 bits.
    h.send ("drums.setPattern", R"({"clip": 0, "pads": [70000]})");
    CHECK_FALSE (Harness::hasHit (h.drumClip(), 0, 0));
}

TEST_CASE ("A synth preset changes every parameter and undoes as one step", "[bridge][integration]")
{
    Harness h;
    h.send ("app.ready");
    const auto before = h.param ("synth.cutoff");

    // waveform, pitch, detune, cutoff, resonance, attack, decay, sustain, release, volume
    h.send ("synth.setPreset", R"({"values": [2, -12, 3, 700, 2.5, 0.002, 0.25, 0.4, 0.15, -10]})");
    CHECK (h.param ("synth.cutoff") == Catch::Approx (700.0));
    CHECK (h.param ("synth.pitch") == Catch::Approx (-12.0));
    CHECK (h.param ("synth.volume") == Catch::Approx (-10.0));
    CHECK (h.latest ("history.state")["undoLabel"].toString() == "Apply synth preset");

    // Out of range for one parameter: the whole preset is refused.
    h.send ("synth.setPreset", R"({"values": [2, -12, 3, 99999, 2.5, 0.002, 0.25, 0.4, 0.15, -10]})");
    CHECK (h.param ("synth.cutoff") == Catch::Approx (700.0));
    // A short list is refused too.
    h.send ("synth.setPreset", R"({"values": [1, 2, 3]})");
    CHECK (h.param ("synth.cutoff") == Catch::Approx (700.0));

    h.send ("edit.undo");
    CHECK (h.param ("synth.cutoff") == Catch::Approx (before));
    CHECK (h.param ("synth.pitch") == Catch::Approx (0.0));
}

TEST_CASE ("Unused audio is listed, removed in one step and brought back by undo", "[bridge][integration]")
{
    Harness h;
    REQUIRE (h.session.perform (model::AddAsset {"audio/1-old.wav", "old.wav"}));
    REQUIRE (h.session.perform (model::AddAsset {"audio/2-older.wav", "older.wav"}));
    h.send ("app.ready");

    auto assets = h.latest ("project.assets")["assets"];
    REQUIRE (assets.size() == 2);
    CHECK (static_cast<int> (assets[0]["clips"]) == 0);
    CHECK (static_cast<bool> (assets[0]["missing"])); // no such file in the temp project

    h.send ("asset.removeUnused");
    CHECK (h.latest ("project.assets")["assets"].size() == 0);
    CHECK (h.session.project().assets.empty());

    h.send ("edit.undo");
    CHECK (h.latest ("project.assets")["assets"].size() == 2);
    CHECK (h.session.project().assets.size() == 2);
}

TEST_CASE ("Tempo set in the UI reaches the engine and the song", "[bridge][integration]")
{
    Harness h;
    h.send ("app.ready");
    h.send ("transport.setTempo", R"({"bpm": 90})");
    CHECK (h.engine.getTransport().getTempo() == 90.0);
    CHECK (h.session.project().tempoBpm == 90.0);
    CHECK (h.latest ("transport.state")["bpm"].operator double() == 90.0);

    h.send ("transport.setTempo", R"({"bpm": 5000})"); // outside 20..300: the codec refuses it
    CHECK (h.engine.getTransport().getTempo() == 90.0);
}
