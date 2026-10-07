#pragma once

#include "ap/bridge/generated/Messages.h"
#include "ap/engine/Engine.h"
#include "ap/host/ProjectEditor.h"

#include <functional>
#include <optional>
#include <string_view>

namespace ap::host
{

// What a host must re-send to the UI when an intent was refused or changed nothing, so a control
// never keeps a value the core did not accept. Any of them may be empty.
struct Resync
{
    std::function<void()> transport;
    std::function<void()> timeline;
    std::function<void()> project;
    std::function<void()> drumKit;
    std::function<void()> instrument;
    std::function<void()> status;
    std::function<void (params::ParamId)> parameter;
    std::function<void (std::size_t)> drumPad;
    // A message for the person (level 0 info, 1 warning, 2 error).
    std::function<void (int, std::string_view)> notice;
};

// The intents that only edit the project or drive the engine (ADR-003): the same code runs in the
// desktop app and in the browser. Intents that need the host's platform (audio devices, files,
// recording, the sampler, exports, accompaniment) are not handled here; apply() returns false for
// them and the host deals with them.
//
// Threading: message thread only, like the project editor it works on.
class IntentApplier
{
public:
    IntentApplier (ProjectEditor& editor, engine::Engine& engine, Resync resync = {});

    // How long a loaded audio asset is, in seconds; nullopt when it is not loaded (or unknown). An
    // audio clip cannot be stretched past its file.
    std::function<std::optional<double> (model::AssetId)> loadedAudioSeconds;

    // True if the intent is one this class handles (whether or not it changed anything).
    bool apply (const bridge::Intent& intent);

    // Transport and clip editing that hosts also call from their own code.
    void stopTransport();

private:
    template <typename T> bool handle (const T&) { return false; }

    bool handle (const bridge::ToneSetEnabled&);
    bool handle (const bridge::TransportPlay&);
    bool handle (const bridge::TransportStop&);
    bool handle (const bridge::TransportReturnToStart&);
    bool handle (const bridge::TransportSetTempo&);
    bool handle (const bridge::TransportSetCountIn&);
    bool handle (const bridge::TransportSeek&);
    bool handle (const bridge::TransportSetLoop&);
    bool handle (const bridge::MetronomeSetEnabled&);
    bool handle (const bridge::ParamSet&);
    bool handle (const bridge::SynthSetPreset&);
    bool handle (const bridge::EditUndo&);
    bool handle (const bridge::EditRedo&);
    bool handle (const bridge::ProjectRename&);
    bool handle (const bridge::NoteOn&);
    bool handle (const bridge::NoteOff&);
    bool handle (const bridge::NoteAllOff&);
    bool handle (const bridge::InstrumentSelect&);
    bool handle (const bridge::DrumsTrigger&);
    bool handle (const bridge::DrumsSetPad&);
    bool handle (const bridge::DrumsResetPad&);
    bool handle (const bridge::DrumsSetKit&);
    bool handle (const bridge::DrumsSetStep&);
    bool handle (const bridge::DrumsClear&);
    bool handle (const bridge::DrumsSetPattern&);
    bool handle (const bridge::AssetRemove&);
    bool handle (const bridge::AssetRemoveUnused&);
    bool handle (const bridge::TrackAdd&);
    bool handle (const bridge::TrackRemove&);
    bool handle (const bridge::TrackRename&);
    bool handle (const bridge::TrackSetVolume&);
    bool handle (const bridge::TrackSetPan&);
    bool handle (const bridge::TrackSetMute&);
    bool handle (const bridge::TrackSetSolo&);
    bool handle (const bridge::TrackSetEffect&);
    bool handle (const bridge::TrackSetSend&);
    bool handle (const bridge::BusAdd&);
    bool handle (const bridge::BusRemove&);
    bool handle (const bridge::BusRename&);
    bool handle (const bridge::BusSetVolume&);
    bool handle (const bridge::BusSetPan&);
    bool handle (const bridge::BusSetMute&);
    bool handle (const bridge::BusSetEffect&);
    bool handle (const bridge::ClipCreate&);
    bool handle (const bridge::ClipMove&);
    bool handle (const bridge::ClipResize&);
    bool handle (const bridge::ClipSplit&);
    bool handle (const bridge::ClipRemove&);
    bool handle (const bridge::ClipDuplicate&);
    bool handle (const bridge::ClipSetLoop&);
    bool handle (const bridge::ClipAddNote&);
    bool handle (const bridge::ClipRemoveNote&);
    bool handle (const bridge::ClipEditNote&);

    bool editClip (std::uint64_t clip, model::ClipEdit edit, std::uint64_t gesture,
                   const std::function<model::Clip (const model::Clip&)>& change);
    // The pattern clip `clipId`, or (0) the one at the playhead, created when missing and wanted.
    void editDrumPattern (std::uint64_t clipId, std::uint64_t gesture, bool createIfMissing,
                          const std::function<model::Clip (const model::Clip&)>& change);

    void resyncTransport() const;
    void resyncTimeline() const;

    ProjectEditor& editor;
    engine::Engine& engine;
    Resync resync;
};

} // namespace ap::host
