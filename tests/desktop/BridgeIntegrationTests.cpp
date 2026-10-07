#include "SampleLoader.h"
#include "TempDirectory.h"
#include "TimelineHelpers.h"
#include "WavFile.h"
#include "WebUiHost.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/model/ClipEditing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

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

// --- Smart Accompaniment (ADR-011) -----------------------------------------------------------

namespace
{
// A drum groove played at `bpm`, optionally delayed by `lateSeconds`: stands in for a recorded take.
std::vector<float> grooveAudio (double bpm, double seconds, double lateSeconds = 0.0)
{
    engine::Engine engine;
    test::publish (engine,
                   test::drumPatternProject ({{0, {0, 6, 8}}, {1, {4, 12}}, {2, {0, 2, 4, 6, 8, 10, 12, 14}}},
                                             16 * 4 * core::ticksPerQuarterNote));
    engine.getTransport().setTempo (bpm);
    engine.getTransport().requestPlay();
    const auto audio
        = engine::renderOffline (engine, {rate, 1, static_cast<std::int64_t> (seconds * rate), 512});
    std::vector<float> out (static_cast<std::size_t> (lateSeconds * rate), 0.0f);
    out.insert (out.end(), audio[0].begin(), audio[0].end());
    return out;
}

// A sustained chord with a slow swell: no rhythm in it.
std::vector<float> padAudio (double seconds)
{
    std::vector<float> x (static_cast<std::size_t> (seconds * rate));
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / rate;
        x[i] = static_cast<float> (0.2 * std::min (1.0, t / 2.0)
                                   * (std::sin (6.2831853 * 220.0 * t) + std::sin (6.2831853 * 277.18 * t)
                                      + std::sin (6.2831853 * 329.63 * t)));
    }
    return x;
}

struct TakeHarness : Harness
{
    std::vector<std::string> productEvents;

    TakeHarness()
    {
        ui->accompanimentForTests().onEvent
            = [this] (std::string_view name) { productEvents.emplace_back (name); };
    }

    // Puts audio on a new audio track at tick 0, as a finished take, and waits for it to decode.
    model::ClipId addTake (const std::vector<float>& audio, double projectBpm)
    {
        if (session.project().tempoBpm != projectBpm)
            REQUIRE (session.perform (model::SetTempo {projectBpm}));
        const auto folder = session.assetRoot().audioDirectory();
        std::filesystem::create_directories (folder);
        test::writeFloatWav (folder / "1-take.wav", {rate, {audio}});

        REQUIRE (session.perform (model::AddTrack {model::TrackKind::audio}));
        const auto track = session.project().tracks.back().id;
        REQUIRE (session.perform (model::AddAsset {"audio/1-take.wav", "take.wav"}));
        model::Clip clip;
        clip.asset = session.project().assets.back().id;
        clip.length = static_cast<core::Ticks> (static_cast<double> (audio.size()) / rate * projectBpm / 60.0
                                                * core::ticksPerQuarterNote);
        REQUIRE (session.perform (model::AddClip {track, clip}));
        const auto id = session.project().tracks.back().clips.back().id;
        loader.sync (rate);
        waitFor (
            [&]
            {
                return loader.getClipAudio().count (session.project().assets.back().id.value) > 0
                    && loader.getClipAudio().at (session.project().assets.back().id.value).buffer != nullptr;
            });
        return id;
    }

    void waitFor (const std::function<bool()>& done)
    {
        for (int i = 0; i < 1500 && !done(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        REQUIRE (done());
    }

    [[nodiscard]] int accompanimentState() const
    {
        return static_cast<int> (latest ("accompaniment.state")["state"]);
    }

    void waitForSuggestionOutcome()
    {
        waitFor ([&] { return accompanimentState() != 1 && accompanimentState() != 0; });
    }
};

constexpr int stateReady = 2, statePreviewing = 3, stateAccepted = 4, stateDismissed = 5,
              stateUnavailable = 6;
} // namespace

TEST_CASE ("A take with a clear beat gets a suggestion that is previewed, then added in one undo step",
           "[bridge][integration][accompaniment]")
{
    TakeHarness h;
    h.send ("app.ready");
    const auto take = h.addTake (grooveAudio (100.0, 16.0), 100.0);

    h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
    CHECK (h.accompanimentState() == 1); // listening; playback is not held up
    h.waitForSuggestionOutcome();
    REQUIRE (h.accompanimentState() == stateReady);
    const auto offer = h.latest ("accompaniment.state");
    CHECK (offer["message"].toString() == "Try a beat");
    CHECK (offer["grooveName"].toString().isNotEmpty());
    CHECK (offer["detail"].toString().isNotEmpty());
    CHECK (static_cast<int> (offer["clip"]) == static_cast<int> (take.value));
    CHECK (h.session.project().tracks.size() == 1); // nothing added yet

    // Preview: the groove plays with the take, the project is untouched.
    const auto before = h.session.project().tracks.size();
    const auto undoDepthBefore = h.latest ("history.state")["undoLabel"].toString();
    h.send ("accompaniment.preview");
    CHECK (h.accompanimentState() == statePreviewing);
    CHECK (h.session.isPreviewing());
    CHECK (h.session.project().tracks.size() == before);
    CHECK (h.latest ("history.state")["undoLabel"].toString() == undoDepthBefore);
    const auto withPreview = h.loudness (6);

    h.send ("accompaniment.stopPreview");
    CHECK (h.accompanimentState() == stateReady);
    CHECK_FALSE (h.session.isPreviewing());
    const auto takeOnly = h.loudness (6);
    CHECK (withPreview > takeOnly * 1.05); // the groove really was in the signal path

    // Add: one normal undo step; undo brings the suggestion back.
    h.send ("accompaniment.preview");
    h.send ("accompaniment.add");
    CHECK (h.accompanimentState() == stateAccepted);
    CHECK_FALSE (h.session.isPreviewing());
    REQUIRE (h.session.project().tracks.size() == before + 1);
    const auto& drums = h.session.project().tracks.back();
    REQUIRE (drums.clips.size() == 1);
    CHECK (drums.clips[0].start == 0); // on the bar the take starts in
    CHECK (drums.clips[0].loopLength == model::DrumKit::patternLength);
    CHECK (h.latest ("history.state")["undoLabel"].toString() == "Add drums");

    h.send ("edit.undo");
    CHECK (h.session.project().tracks.size() == before);
    CHECK (h.accompanimentState() == stateReady);

    CHECK (h.productEvents
           == std::vector<std::string> {"accompaniment_suggested", "accompaniment_previewed",
                                        "accompaniment_previewed", "accompaniment_accepted",
                                        "accompaniment_removed"});
}

TEST_CASE ("Try another moves through the ranking without repeating, and dismiss clears the offer",
           "[bridge][integration][accompaniment]")
{
    TakeHarness h;
    h.send ("app.ready");
    const auto take = h.addTake (grooveAudio (100.0, 16.0), 100.0);
    h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
    h.waitForSuggestionOutcome();
    REQUIRE (h.accompanimentState() == stateReady);

    std::set<std::string> names {h.latest ("accompaniment.state")["grooveName"].toString().toStdString()};
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE (static_cast<bool> (h.latest ("accompaniment.state")["canTryAnother"]));
        h.send ("accompaniment.next");
        CHECK (names.insert (h.latest ("accompaniment.state")["grooveName"].toString().toStdString()).second);
    }

    h.send ("accompaniment.preview");
    h.send ("accompaniment.next"); // switches the groove being previewed
    CHECK (h.accompanimentState() == statePreviewing);
    CHECK (h.session.isPreviewing());

    h.send ("accompaniment.dismiss");
    CHECK (h.accompanimentState() == stateDismissed);
    CHECK_FALSE (h.session.isPreviewing());
    CHECK (h.session.project().tracks.size() == 1);
    h.send ("accompaniment.add"); // nothing on offer any more
    CHECK (h.session.project().tracks.size() == 1);
    CHECK (std::count (h.productEvents.begin(), h.productEvents.end(), "accompaniment_changed") == 4);
    CHECK (std::count (h.productEvents.begin(), h.productEvents.end(), "accompaniment_skipped") == 1);
}

TEST_CASE ("Editing the project ends a preview and keeps the offer", "[bridge][integration][accompaniment]")
{
    TakeHarness h;
    h.send ("app.ready");
    const auto take = h.addTake (grooveAudio (100.0, 16.0), 100.0);
    h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
    h.waitForSuggestionOutcome();
    h.send ("accompaniment.preview");
    REQUIRE (h.session.isPreviewing());

    h.send ("transport.setTempo", R"({"bpm": 101})");
    CHECK_FALSE (h.session.isPreviewing());
    CHECK (h.accompanimentState() == stateReady);

    h.send ("accompaniment.preview");
    h.send ("transport.stop"); // the music stops: so does the preview
    CHECK_FALSE (h.session.isPreviewing());

    // Deleting the take takes the offer with it.
    h.send ("clip.remove", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
    CHECK (h.accompanimentState() == 0);
}

TEST_CASE ("Material that cannot be accompanied says why, and does not suggest",
           "[bridge][integration][accompaniment]")
{
    SECTION ("a sustained pad")
    {
        TakeHarness h;
        h.send ("app.ready");
        const auto take = h.addTake (padAudio (14.0), 100.0);
        h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
        h.waitForSuggestionOutcome();
        REQUIRE (h.accompanimentState() == stateUnavailable);
        CHECK (static_cast<int> (h.latest ("accompaniment.state")["reason"]) == 1);
        CHECK (h.latest ("accompaniment.state")["message"].toString().contains (
            "Couldn't find a rhythm pattern"));
        h.send ("accompaniment.preview"); // nothing to preview
        CHECK_FALSE (h.session.isPreviewing());
        CHECK (h.productEvents.empty());
    }
    SECTION ("a take at another tempo than the project")
    {
        TakeHarness h;
        h.send ("app.ready");
        const auto take = h.addTake (grooveAudio (90.0, 16.0), 120.0);
        h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
        h.waitForSuggestionOutcome();
        REQUIRE (h.accompanimentState() == stateUnavailable);
        CHECK (static_cast<int> (h.latest ("accompaniment.state")["reason"]) == 3);
        const auto message = h.latest ("accompaniment.state")["message"].toString();
        CHECK (message.contains ("90 BPM"));
        CHECK (message.contains ("120"));
    }
    SECTION ("a take that is in time with itself but not with the project's beat")
    {
        TakeHarness h;
        h.send ("app.ready");
        const auto take
            = h.addTake (grooveAudio (100.0, 16.0, 0.17), 100.0); // 0.17 s late: over a quarter of a beat
        h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
        h.waitForSuggestionOutcome();
        REQUIRE (h.accompanimentState() == stateUnavailable);
        CHECK (static_cast<int> (h.latest ("accompaniment.state")["reason"]) == 6);
    }
    SECTION ("drums are already there")
    {
        TakeHarness h;
        h.send ("app.ready");
        const auto take = h.addTake (grooveAudio (100.0, 16.0), 100.0);
        h.send ("drums.setStep", R"({"clip": 0, "pad": 0, "step": 0, "on": true, "gesture": 0})");
        h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (take.value)) + "}");
        h.waitForSuggestionOutcome();
        REQUIRE (h.accompanimentState() == stateUnavailable);
        CHECK (static_cast<int> (h.latest ("accompaniment.state")["reason"]) == 5);
    }
    SECTION ("not an audio clip")
    {
        TakeHarness h;
        h.send ("app.ready");
        h.send ("drums.setStep", R"({"clip": 0, "pad": 0, "step": 0, "on": true, "gesture": 0})");
        const auto clip = h.drumClip();
        const auto before = h.count ("accompaniment.state");
        h.send ("accompaniment.suggest", "{\"clip\": " + juce::String (static_cast<int> (clip["id"])) + "}");
        CHECK (h.count ("accompaniment.state") == before);
    }
}
