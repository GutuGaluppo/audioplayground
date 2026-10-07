#include "ap/dsp/Peaks.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/host/Base64.h"
#include "ap/host/DocumentEditor.h"
#include "ap/host/IntentApplier.h"
#include "ap/host/Snapshots.h"
#include "ap/model/ClipEditing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ap;
using namespace ap::bridge;

// The intents every host shares (desktop and web): what they do to the project and the engine,
// and when they ask the host to re-send a value the core did not accept. No UI, no devices.

namespace
{
struct Rig
{
    engine::Engine engine;
    host::DocumentEditor editor {engine};
    int timeline = 0, transport = 0, project = 0, drumKit = 0, instrument = 0, status = 0;
    std::vector<params::ParamId> parameters;
    std::vector<std::size_t> pads;
    std::vector<std::pair<int, std::string>> notices;
    host::IntentApplier applier {
        editor,
        engine,
        {[this] { ++transport; }, [this] { ++timeline; }, [this] { ++project; }, [this] { ++drumKit; }, [this]
         { ++instrument; }, [this] { ++status; }, [this] (params::ParamId id) { parameters.push_back (id); },
         [this] (std::size_t pad) { pads.push_back (pad); },
         [this] (int level, std::string_view text) { notices.emplace_back (level, std::string (text)); }}};

    Rig() { engine.prepare (48000.0, 256); }

    template <typename Message> bool send (const Message& message)
    {
        return applier.apply (Intent {message});
    }

    [[nodiscard]] const model::Project& project_() const { return editor.project(); }

    model::TrackId addTrack (int kind)
    {
        TrackAdd add;
        add.kind = kind;
        REQUIRE (send (add));
        return editor.project().tracks.back().id;
    }
};

int id (model::TrackId t)
{
    return static_cast<int> (t.value);
}
} // namespace

TEST_CASE ("Intents for the platform are left to the host", "[host][applier]")
{
    Rig rig;
    CHECK_FALSE (rig.send (AudioSetOutput {}));
    CHECK_FALSE (rig.send (ProjectSave {}));
    CHECK_FALSE (rig.send (TransportRecord {}));
    CHECK_FALSE (rig.send (SamplerLoad {}));
    CHECK_FALSE (rig.send (TrackImportAudio {}));
    CHECK_FALSE (rig.send (AccompanimentAdd {}));
    CHECK_FALSE (rig.send (AppReady {}));
    CHECK (rig.editor.project().tracks.empty());
}

TEST_CASE ("Transport intents drive the engine and ask for a re-send when they must", "[host][applier]")
{
    Rig rig;
    CHECK (rig.send (TransportPlay {}));
    rig.engine.process ({nullptr, 0, 0}); // no channels, no frames: just let it see the request
    CHECK (rig.send (TransportSetCountIn {2}));
    CHECK (rig.engine.getTransport().getCountInBars() == 2);
    CHECK (rig.transport == 1);

    MetronomeSetEnabled metronome;
    metronome.enabled = true;
    CHECK (rig.send (metronome));
    CHECK (rig.engine.getMetronome().isEnabled());

    TransportSetLoop loop;
    loop.enabled = true;
    loop.start = 0;
    loop.end = 3840;
    CHECK (rig.send (loop));
    CHECK (rig.engine.getTransport().getLoop().enabled);
    loop.end = 0; // an empty region is no loop
    CHECK (rig.send (loop));
    CHECK_FALSE (rig.engine.getTransport().getLoop().enabled);

    TransportSetTempo tempo;
    tempo.bpm = 90.0;
    const auto before = rig.transport;
    CHECK (rig.send (tempo));
    CHECK (rig.editor.project().tempoBpm == 90.0);
    CHECK (rig.engine.getTransport().getTempo() == 90.0);
    CHECK (rig.transport == before); // accepted: the project-changed path reports it
    CHECK (rig.send (tempo));        // same value: nothing changes, the field is re-sent
    CHECK (rig.transport == before + 1);

    CHECK (rig.send (TransportStop {}));
    CHECK_FALSE (rig.engine.getTransport().getState().playing);
}

TEST_CASE ("Parameters are checked, undoable, and echoed when refused", "[host][applier]")
{
    Rig rig;
    ParamSet set;
    set.id = "synth.cutoff";
    set.value = 1200.0;
    CHECK (rig.send (set));
    CHECK (rig.editor.project().parameter (*params::findParamId ("synth.cutoff")) == 1200.0f);

    set.value = 99999999.0; // out of range: ignored
    CHECK (rig.send (set));
    CHECK (rig.editor.project().parameter (*params::findParamId ("synth.cutoff")) == 1200.0f);
    set.id = "nope.nothing"; // unknown: ignored
    CHECK (rig.send (set));

    set.id = "synth.cutoff";
    set.value = 1200.0; // already there: re-sent
    CHECK (rig.send (set));
    REQUIRE (rig.parameters.size() == 1);
    CHECK (rig.parameters[0] == *params::findParamId ("synth.cutoff"));

    // A drag with one gesture is one undo step.
    for (const double value : {2000.0, 2500.0, 3000.0})
    {
        set.value = value;
        set.gesture = 5;
        CHECK (rig.send (set));
    }
    CHECK (rig.send (EditUndo {}));
    CHECK (rig.editor.project().parameter (*params::findParamId ("synth.cutoff")) == 1200.0f);
    CHECK (rig.send (EditRedo {}));
    CHECK (rig.editor.project().parameter (*params::findParamId ("synth.cutoff")) == 3000.0f);
}

TEST_CASE ("A synth preset is all or nothing, and one undo step", "[host][applier]")
{
    Rig rig;
    SynthSetPreset preset;
    preset.values = {2, -12, 3, 700, 2.5f, 0.002f, 0.25f, 0.4f, 0.15f, -10};
    CHECK (rig.send (preset));
    CHECK (rig.editor.project().parameter (*params::findParamId ("synth.cutoff")) == 700.0f);
    CHECK (rig.editor.document().undoDescription() == "Apply synth preset");

    auto bad = preset;
    bad.values[3] = 1.0e9f;
    CHECK (rig.send (bad));
    CHECK (rig.editor.project().parameter (*params::findParamId ("synth.cutoff")) == 700.0f);
    bad.values = {1, 2, 3};
    CHECK (rig.send (bad));
    CHECK (rig.editor.document().undoDescription() == "Apply synth preset");

    CHECK (rig.send (EditUndo {}));
    CHECK (rig.editor.project().parameter (*params::findParamId ("synth.cutoff")) == 4000.0f);
}

TEST_CASE ("Tracks: add, edit, remove", "[host][applier]")
{
    Rig rig;
    const auto audio = rig.addTrack (0);
    const auto synth = rig.addTrack (1);
    const auto drums = rig.addTrack (3);
    CHECK (rig.editor.project().findTrack (audio)->kind == model::TrackKind::audio);
    CHECK (rig.editor.project().findTrack (synth)->instrument == model::InstrumentKind::synth);
    CHECK (rig.editor.project().findTrack (drums)->instrument == model::InstrumentKind::drums);

    TrackRename rename;
    rename.track = id (synth);
    rename.name = "Lead";
    CHECK (rig.send (rename));
    CHECK (rig.editor.project().findTrack (synth)->name == "Lead");
    rename.name = ""; // refused: the timeline is re-sent
    const auto before = rig.timeline;
    CHECK (rig.send (rename));
    CHECK (rig.timeline == before + 1);
    CHECK (rig.editor.project().findTrack (synth)->name == "Lead");

    TrackSetVolume volume;
    volume.track = id (synth);
    volume.volumeDb = -6.0;
    CHECK (rig.send (volume));
    CHECK (rig.editor.project().findTrack (synth)->volumeDb == -6.0f);
    TrackSetPan pan;
    pan.track = id (synth);
    pan.pan = 0.5;
    CHECK (rig.send (pan));
    CHECK (rig.editor.project().findTrack (synth)->pan == 0.5f);
    TrackSetMute mute;
    mute.track = id (synth);
    mute.muted = true;
    CHECK (rig.send (mute));
    TrackSetSolo solo;
    solo.track = id (drums);
    solo.soloed = true;
    CHECK (rig.send (solo));
    CHECK (rig.editor.project().findTrack (synth)->muted);
    CHECK (rig.editor.project().findTrack (drums)->soloed);

    TrackSetVolume missing;
    missing.track = 999;
    const auto timelines = rig.timeline;
    CHECK (rig.send (missing));
    CHECK (rig.timeline == timelines + 1);

    TrackRemove remove;
    remove.track = id (audio);
    CHECK (rig.send (remove));
    CHECK (rig.editor.project().tracks.size() == 2);
    CHECK (rig.send (EditUndo {}));
    CHECK (rig.editor.project().tracks.size() == 3);
}

TEST_CASE ("Track effects and sends are validated against the effect's shape", "[host][applier]")
{
    Rig rig;
    const auto track = rig.addTrack (1);
    const auto& delay = params::effectDescriptors[static_cast<std::size_t> (params::EffectKind::delay)];

    TrackSetEffect effect;
    effect.track = id (track);
    effect.effect = static_cast<int> (params::EffectKind::delay);
    effect.enabled = true;
    effect.values.assign (delay.numParameters, 0.0f);
    for (std::size_t i = 0; i < delay.numParameters; ++i)
        effect.values[i] = delay.parameters[i].defaultValue;
    CHECK (rig.send (effect));
    CHECK (rig.editor.project()
               .findTrack (track)
               ->effects[static_cast<std::size_t> (params::EffectKind::delay)]
               .enabled);

    const auto before = rig.timeline;
    effect.values.pop_back(); // the wrong number of values
    CHECK (rig.send (effect));
    CHECK (rig.timeline == before + 1);

    BusAdd add;
    CHECK (rig.send (add));
    REQUIRE (rig.editor.project().buses.size() == 1);
    TrackSetSend send;
    send.track = id (track);
    send.bus = static_cast<int> (rig.editor.project().buses[0].id.value);
    send.levelDb = -6.0;
    CHECK (rig.send (send));
    REQUIRE (rig.editor.project().findTrack (track)->sends.size() == 1);
    CHECK (rig.editor.project().findTrack (track)->sends[0].levelDb == -6.0f);
}

TEST_CASE ("Buses: add, edit, remove", "[host][applier]")
{
    Rig rig;
    CHECK (rig.send (BusAdd {}));
    const auto bus = static_cast<int> (rig.editor.project().buses[0].id.value);
    BusRename rename;
    rename.bus = bus;
    rename.name = "Verb";
    CHECK (rig.send (rename));
    BusSetVolume volume;
    volume.bus = bus;
    volume.volumeDb = -3.0;
    CHECK (rig.send (volume));
    BusSetPan pan;
    pan.bus = bus;
    pan.pan = -0.25;
    CHECK (rig.send (pan));
    BusSetMute mute;
    mute.bus = bus;
    mute.muted = true;
    CHECK (rig.send (mute));
    const auto& stored = rig.editor.project().buses[0];
    CHECK (stored.name == "Verb");
    CHECK (stored.volumeDb == -3.0f);
    CHECK (stored.pan == -0.25f);
    CHECK (stored.muted);

    BusSetEffect effect;
    effect.bus = bus;
    effect.effect = static_cast<int> (params::EffectKind::reverb);
    effect.enabled = true;
    const auto& reverb = params::effectDescriptors[static_cast<std::size_t> (params::EffectKind::reverb)];
    for (std::size_t i = 0; i < reverb.numParameters; ++i)
        effect.values.push_back (reverb.parameters[i].defaultValue);
    CHECK (rig.send (effect));
    CHECK (
        rig.editor.project().buses[0].effects[static_cast<std::size_t> (params::EffectKind::reverb)].enabled);

    BusRemove remove;
    remove.bus = 999; // unknown: the timeline is re-sent
    const auto before = rig.timeline;
    CHECK (rig.send (remove));
    CHECK (rig.timeline == before + 1);
    remove.bus = bus;
    CHECK (rig.send (remove));
    CHECK (rig.editor.project().buses.empty());
}

TEST_CASE ("Clips: create, move, resize, split, duplicate, loop, notes, remove", "[host][applier]")
{
    Rig rig;
    const auto synth = rig.addTrack (1);

    ClipCreate create;
    create.track = id (synth);
    create.start = 0;
    create.length = 3840;
    CHECK (rig.send (create));
    const auto& track = [&]() -> const model::Track& { return *rig.editor.project().findTrack (synth); };
    REQUIRE (track().clips.size() == 1);
    const auto clip = static_cast<int> (track().clips[0].id.value);

    ClipAddNote add;
    add.clip = clip;
    add.start = 0;
    add.length = 480;
    add.pitch = 60;
    add.velocity = 0.8;
    CHECK (rig.send (add));
    add.start = 960;
    add.pitch = 64;
    CHECK (rig.send (add));
    CHECK (track().clips[0].notes.size() == 2);

    ClipEditNote edit;
    edit.clip = clip;
    edit.fromStart = 960;
    edit.fromPitch = 64;
    edit.start = 1920;
    edit.length = 480;
    edit.pitch = 67;
    CHECK (rig.send (edit));
    CHECK (model::findNote (track().clips[0], 1920, 67) != nullptr);
    CHECK (model::findNote (track().clips[0], 960, 64) == nullptr);

    ClipRemoveNote removeNote;
    removeNote.clip = clip;
    removeNote.start = 0;
    removeNote.pitch = 60;
    CHECK (rig.send (removeNote));
    CHECK (track().clips[0].notes.size() == 1);

    ClipMove move;
    move.clip = clip;
    move.track = id (synth);
    move.start = 3840;
    CHECK (rig.send (move));
    CHECK (track().clips[0].start == 3840);

    ClipResize resize;
    resize.clip = clip;
    resize.edge = 1;
    resize.ticks = 3840 + 1920;
    CHECK (rig.send (resize));
    CHECK (track().clips[0].length == 1920);

    ClipSplit split;
    split.clip = clip;
    split.ticks = 3840 + 960;
    CHECK (rig.send (split));
    CHECK (track().clips.size() == 2);

    ClipDuplicate duplicate;
    duplicate.clip = clip;
    CHECK (rig.send (duplicate));
    CHECK (track().clips.size() == 3);

    ClipSetLoop loop;
    loop.clip = clip;
    loop.enabled = true;
    CHECK (rig.send (loop));
    CHECK (track().clips[0].loopLength > 0);
    loop.enabled = false;
    CHECK (rig.send (loop));
    CHECK (track().clips[0].loopLength == 0);

    ClipRemove remove;
    remove.clip = clip;
    CHECK (rig.send (remove));
    CHECK (track().clips.size() == 2);

    // Edits of a clip that is not there re-send the timeline instead of failing silently.
    const auto before = rig.timeline;
    move.clip = 12345;
    CHECK (rig.send (move));
    split.clip = 12345;
    CHECK (rig.send (split));
    add.clip = 12345;
    CHECK (rig.send (add));
    CHECK (rig.timeline == before + 3);
}

TEST_CASE ("Clips can only be made on the right kind of track, and audio clips stay inside their file",
           "[host][applier]")
{
    Rig rig;
    const auto audio = rig.addTrack (0);
    ClipCreate create;
    create.track = id (audio); // an audio track gets audio by import, not by creating a note clip
    create.start = 0;
    create.length = 960;
    CHECK (rig.send (create));
    CHECK (rig.editor.project().findTrack (audio)->clips.empty());

    // An audio clip (made through the model: importing is the host's job) cannot outlast its file.
    REQUIRE (rig.editor.perform (model::AddAsset {"audio/1-a.wav", "a.wav"}));
    model::Clip clip;
    clip.asset = rig.editor.project().assets[0].id;
    clip.length = 960;
    REQUIRE (rig.editor.perform (model::AddClip {audio, clip}));
    const auto clipId = static_cast<int> (rig.editor.project().findTrack (audio)->clips[0].id.value);

    ClipResize resize;
    resize.clip = clipId;
    resize.edge = 1;
    resize.ticks = 96000; // far past the file
    rig.applier.loadedAudioSeconds = [] (model::AssetId) -> std::optional<double> { return 2.0; };
    CHECK (rig.send (resize));
    // 2 s at 120 BPM is 4 beats = 3840 ticks.
    CHECK (rig.editor.project().findTrack (audio)->clips[0].length == 3840);

    rig.applier.loadedAudioSeconds = nullptr; // file not loaded: no limit known
    resize.ticks = 7680;
    CHECK (rig.send (resize));
    CHECK (rig.editor.project().findTrack (audio)->clips[0].length == 7680);
}

TEST_CASE ("Drum intents: steps create the pattern, patterns replace it, pads and kits are kept",
           "[host][applier]")
{
    Rig rig;
    DrumsSetStep step;
    step.clip = 0;
    step.pad = 0;
    step.step = 0;
    step.on = true;
    step.gesture = 4;
    CHECK (rig.send (step));
    REQUIRE (rig.editor.project().tracks.size() == 1);
    REQUIRE (rig.editor.project().tracks[0].clips.size() == 1);
    step.pad = 1;
    step.step = 4;
    CHECK (rig.send (step));
    CHECK (rig.editor.document().undoDescription() == "Add pattern"); // one stroke, one step
    const auto clip = static_cast<int> (rig.editor.project().tracks[0].clips[0].id.value);
    CHECK (model::hasDrumStep (rig.editor.project().tracks[0].clips[0], 1, 4));

    DrumsSetPattern pattern;
    pattern.clip = clip;
    pattern.pads = {4369}; // kick on the beat
    CHECK (rig.send (pattern));
    CHECK_FALSE (model::hasDrumStep (rig.editor.project().tracks[0].clips[0], 1, 4));
    CHECK (model::hasDrumStep (rig.editor.project().tracks[0].clips[0], 0, 8));

    DrumsClear clear;
    clear.clip = clip;
    CHECK (rig.send (clear));
    CHECK (rig.editor.project().tracks[0].clips[0].notes.empty());

    DrumsSetKit kit;
    kit.kit = 2;
    CHECK (rig.send (kit));
    CHECK (rig.editor.project().drums.kit == 2);

    DrumsSetPad pad;
    pad.pad = 3;
    pad.volumeDb = -9.0;
    pad.pitch = 5.0;
    pad.muted = true;
    CHECK (rig.send (pad));
    CHECK (rig.editor.project().drums.pads[3].volumeDb == -9.0f);
    CHECK (rig.editor.project().drums.pads[3].muted);
    CHECK (rig.send (pad)); // unchanged: the pad is re-sent
    REQUIRE (rig.pads.size() == 1);
    CHECK (rig.pads[0] == 3);

    CHECK (rig.send (DrumsResetPad {3}));
    CHECK_FALSE (rig.editor.project().drums.pads[3].sample.isValid());
}

TEST_CASE ("Playing and choosing the instrument reach the engine", "[host][applier]")
{
    Rig rig;
    InstrumentSelect select;
    select.instrument = 0; // the synth
    CHECK (rig.send (select));
    CHECK (rig.engine.getLiveInstrument() == engine::Engine::LiveInstrument::synth);
    CHECK (rig.instrument == 1);

    NoteOn on;
    on.note = 60;
    on.velocity = 0.8;
    CHECK (rig.send (on));
    rig.engine.getTransport().requestPlay();
    std::vector<float> left (256), right (256);
    std::array<float*, 2> channels {left.data(), right.data()};
    float peak = 0.0f;
    for (int block = 0; block < 20; ++block)
    {
        rig.engine.process ({channels.data(), 2, 256});
        for (const auto sample : left)
            peak = std::max (peak, std::abs (sample));
    }
    CHECK (peak > 0.001f); // the note sounds

    CHECK (rig.send (NoteAllOff {}));
    ToneSetEnabled tone;
    tone.enabled = true;
    CHECK (rig.send (tone));
    CHECK (rig.engine.isTestToneEnabled());
    CHECK (rig.status == 1);

    ProjectRename rename;
    rename.name = "My song";
    CHECK (rig.send (rename));
    CHECK (rig.editor.project().name == "My song");
    rename.name = "";
    CHECK (rig.send (rename));
    CHECK (rig.project == 1);
}

TEST_CASE ("The snapshots describe the project the way the UI expects", "[host][snapshots]")
{
    Rig rig;
    rig.addTrack (3);
    DrumsSetStep step;
    step.clip = 0;
    step.pad = 0;
    step.step = 0;
    step.on = true;
    REQUIRE (rig.send (step));

    const auto timeline = host::timelineState (rig.editor.project());
    REQUIRE (timeline.tracks.size() == 1);
    CHECK (timeline.tracks[0].kind == 3); // 1 + the drums instrument (0 is an audio track)
    REQUIRE (timeline.tracks[0].clips.size() == 1);
    CHECK (timeline.tracks[0].clips[0].notes.size() == 1);
    CHECK (timeline.tracks[0].effects.size() == params::numEffects);

    const auto history = host::historyState (rig.editor.document());
    CHECK (history.canUndo);
    CHECK_FALSE (history.canRedo);

    const auto transport
        = host::transportState (rig.editor.project(), rig.engine, {true, model::TrackId {7}, true});
    CHECK (transport.recording);
    CHECK (transport.armedTrack == 7);
    CHECK (transport.captureAvailable);
    CHECK (transport.bpm == 120.0);

    const auto pad = host::drumsPad (rig.editor.project(), 1, {"ignored", false});
    CHECK (pad.name == "Snare"); // a pad on the factory sound uses the kit's name
    CHECK (host::paramValue (rig.editor.project(), *params::findParamId ("synth.cutoff")).value == 4000.0);
    CHECK (host::drumsKit (rig.editor.project()).kit == 0);
    CHECK (host::transportPosition (rig.engine).bar >= 1);
}

TEST_CASE ("Unused audio goes in one step; what is in use is kept, with a reason", "[host][applier][assets]")
{
    Rig rig;
    const auto audio = rig.addTrack (0);
    REQUIRE (rig.editor.perform (model::AddAsset {"audio/1-used.wav", "used.wav"}));
    REQUIRE (rig.editor.perform (model::AddAsset {"audio/2-spare.wav", "spare.wav"}));
    REQUIRE (rig.editor.perform (model::AddAsset {"audio/3-spare.wav", "spare 2.wav"}));
    model::Clip clip;
    clip.asset = rig.editor.project().assets[0].id;
    clip.length = 960;
    REQUIRE (rig.editor.perform (model::AddClip {audio, clip}));

    AssetRemove remove;
    remove.asset = static_cast<int> (rig.editor.project().assets[0].id.value);
    CHECK (rig.send (remove));
    CHECK (rig.editor.project().assets.size() == 3);
    REQUIRE (rig.notices.size() == 1);
    CHECK (rig.notices[0].second == "That audio is still in use.");

    CHECK (rig.send (AssetRemoveUnused {}));
    REQUIRE (rig.editor.project().assets.size() == 1);
    CHECK (rig.editor.project().assets[0].name == "used.wav");
    CHECK (rig.editor.document().undoDescription() == "Remove unused audio");
    CHECK (rig.send (AssetRemoveUnused {})); // nothing left to remove: nothing happens
    CHECK (rig.editor.project().assets.size() == 1);
    CHECK (rig.send (EditUndo {}));
    CHECK (rig.editor.project().assets.size() == 3);
}

TEST_CASE ("Asset snapshots count uses and report missing files", "[host][snapshots][assets]")
{
    Rig rig;
    const auto audio = rig.addTrack (0);
    REQUIRE (rig.editor.perform (model::AddAsset {"audio/1-a.wav", "a.wav"}));
    REQUIRE (rig.editor.perform (model::AddAsset {"audio/2-b.wav", "b.wav"}));
    model::Clip clip;
    clip.asset = rig.editor.project().assets[0].id;
    clip.length = 960;
    REQUIRE (rig.editor.perform (model::AddClip {audio, clip}));

    const auto event = host::projectAssets (rig.editor.project(), [&] (model::AssetId id)
                                            { return id == rig.editor.project().assets[1].id; });
    REQUIRE (event.assets.size() == 2);
    CHECK (event.assets[0].clips == 1);
    CHECK_FALSE (event.assets[0].missing);
    CHECK (event.assets[1].clips == 0);
    CHECK (event.assets[1].missing);
    CHECK (host::projectAssets (rig.editor.project(), {}).assets[1].missing == false);

    host::AssetAudio loaded;
    loaded.loaded = true;
    loaded.durationSeconds = 1.5;
    loaded.overview = {0.5f, 1.0f};
    const auto asset = host::timelineAsset (rig.editor.project(), rig.editor.project().assets[0].id, loaded);
    CHECK (asset.name == "a.wav"); // the project's name wins
    CHECK (asset.loaded);
    CHECK (asset.durationSeconds == Catch::Approx (1.5));
    CHECK (asset.overview.size() == 2);

    CHECK_FALSE (host::timelinePeaks (rig.editor.project().assets[0].id, {}).has_value());
    const auto peaks = host::timelinePeaks (rig.editor.project().assets[0].id, {1, 2, 3, 255});
    REQUIRE (peaks.has_value());
    CHECK (peaks->data == "AQID/w==");
}

TEST_CASE ("base64 matches RFC 4648", "[host][base64]")
{
    const auto encode = [] (std::string_view text)
    { return host::base64Encode (reinterpret_cast<const std::uint8_t*> (text.data()), text.size()); };
    CHECK (encode ("") == "");
    CHECK (encode ("f") == "Zg==");
    CHECK (encode ("fo") == "Zm8=");
    CHECK (encode ("foo") == "Zm9v");
    CHECK (encode ("foob") == "Zm9vYg==");
    CHECK (encode ("fooba") == "Zm9vYmE=");
    CHECK (encode ("foobar") == "Zm9vYmFy");
}

TEST_CASE ("The overview is the loudest value of each slice, 0 to 1", "[host][overview]")
{
    std::vector<std::vector<float>> channels (2, std::vector<float> (1000, 0.0f));
    channels[0][10] = 0.5f;
    channels[1][990] = -2.0f; // clipped to 1
    const auto overview = dsp::computeOverview (channels, 4);
    REQUIRE (overview.size() == 4);
    CHECK (overview[0] == 0.5f);
    CHECK (overview[1] == 0.0f);
    CHECK (overview[3] == 1.0f);
    CHECK (dsp::computeOverview ({}, 8) == std::vector<float> (8, 0.0f));
    CHECK (dsp::computeOverview (channels).size() == static_cast<std::size_t> (dsp::overviewPoints));
}
