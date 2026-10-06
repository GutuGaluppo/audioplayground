#pragma once

#include "ap/model/Project.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ap::model
{

// Every change to a Project is one of these commands (guide §15, ADR-003). Each command holds
// the requested change (its constructor arguments) plus the state it replaced, captured by
// apply(), so revert() is exact. Commands are plain values: easy to test, log and serialise.

struct SetTempo
{
    explicit SetTempo (double newBpm) noexcept
        : bpm (newBpm)
    {
    }

    double bpm = 120.0;
    double previous = 0.0;
};

struct SetTimeSignature
{
    explicit SetTimeSignature (core::TimeSignature newSignature) noexcept
        : signature (newSignature)
    {
    }

    core::TimeSignature signature {};
    core::TimeSignature previous {};
};

struct RenameProject
{
    explicit RenameProject (std::string newName)
        : name (std::move (newName))
    {
    }

    std::string name;
    std::string previous;
};

struct AddTrack
{
    explicit AddTrack (TrackKind trackKind, std::string trackName = {}, std::optional<std::size_t> at = {})
        : kind (trackKind)
        , name (std::move (trackName))
        , index (at)
    {
    }

    // An instrument track. Rejected if the project already has a track for that instrument.
    explicit AddTrack (InstrumentKind instrumentKind, std::string trackName = {},
                       std::optional<std::size_t> at = {})
        : kind (TrackKind::instrument)
        , instrument (instrumentKind)
        , name (std::move (trackName))
        , index (at)
    {
    }

    TrackKind kind = TrackKind::audio;
    InstrumentKind instrument = InstrumentKind::synth;
    std::string name;                 // empty: a default such as "Audio 2" or "Drums"
    std::optional<std::size_t> index; // empty: append
    TrackId created;                  // captured
    std::uint64_t previousNextTrackId = 0;
};

struct RemoveTrack
{
    explicit RemoveTrack (TrackId track) noexcept
        : id (track)
    {
    }

    TrackId id;
    Track removed; // captured
    std::size_t index = 0;
};

struct MoveTrack
{
    MoveTrack (TrackId track, std::size_t to) noexcept
        : id (track)
        , toIndex (to)
    {
    }

    TrackId id;
    std::size_t toIndex = 0;
    std::size_t fromIndex = 0; // captured
};

struct RenameTrack
{
    RenameTrack (TrackId track, std::string newName)
        : id (track)
        , name (std::move (newName))
    {
    }

    TrackId id;
    std::string name;
    std::string previous;
};

struct SetTrackVolume
{
    SetTrackVolume (TrackId track, float db) noexcept
        : id (track)
        , volumeDb (db)
    {
    }

    TrackId id;
    float volumeDb = 0.0f;
    float previous = 0.0f;
};

// Changes one effect of a track's chain (on/off and values). Gestures merge per track and effect.
struct SetTrackEffect
{
    SetTrackEffect (TrackId track, params::EffectKind which, EffectState state) noexcept
        : id (track)
        , effect (which)
        , value (state)
    {
    }

    TrackId id;
    params::EffectKind effect;
    EffectState value;
    EffectState previous;
};

struct SetTrackPan
{
    SetTrackPan (TrackId track, float newPan) noexcept
        : id (track)
        , pan (newPan)
    {
    }

    TrackId id;
    float pan = 0.0f;
    float previous = 0.0f;
};

struct SetTrackMute
{
    SetTrackMute (TrackId track, bool mute) noexcept
        : id (track)
        , muted (mute)
    {
    }

    TrackId id;
    bool muted = false;
    bool previous = false;
};

struct SetTrackSolo
{
    SetTrackSolo (TrackId track, bool solo) noexcept
        : id (track)
        , soloed (solo)
    {
    }

    TrackId id;
    bool soloed = false;
    bool previous = false;
};

struct SetParameter
{
    SetParameter (params::ParamId parameter, float newValue) noexcept
        : id (parameter)
        , value (newValue)
    {
    }

    params::ParamId id {};
    float value = 0.0f;
    float previous = 0.0f;
};

// Registers a file already copied into the project's audio folder.
struct AddAsset
{
    AddAsset (std::string path, std::string displayName)
        : relativePath (std::move (path))
        , name (std::move (displayName))
    {
    }

    std::string relativePath;
    std::string name;
    AssetId created; // captured
    std::uint64_t previousNextAssetId = 0;
};

// Points an existing asset at another file in the project (e.g. after "Locate..." found a missing
// one). Everything using the asset follows.
struct RelinkAsset
{
    RelinkAsset (AssetId asset, std::string path, std::string displayName)
        : id (asset)
        , relativePath (std::move (path))
        , name (std::move (displayName))
    {
    }

    AssetId id;
    std::string relativePath;
    std::string name;
    Asset previous; // captured
};

struct SetSamplerAsset
{
    explicit SetSamplerAsset (AssetId newAsset) noexcept
        : asset (newAsset)
    {
    }

    AssetId asset; // invalid = no sample
    AssetId previous;
};

// Replaces one pad's settings (volume, pitch, mute, sample).
struct SetDrumPad
{
    SetDrumPad (std::size_t padIndex, DrumPad settings) noexcept
        : pad (padIndex)
        , value (settings)
    {
    }

    std::size_t pad = 0;
    DrumPad value;
    DrumPad previous;
};

// Adds a clip to a track (ADR-007). The clip's id is assigned here; the one passed is ignored.
struct AddClip
{
    AddClip (TrackId targetTrack, Clip newClip)
        : track (targetTrack)
        , clip (std::move (newClip))
    {
    }

    TrackId track;
    Clip clip;
    ClipId created; // captured
    std::uint64_t previousNextClipId = 0;
};

struct RemoveClip
{
    explicit RemoveClip (ClipId clip) noexcept
        : id (clip)
    {
    }

    ClipId id;
    TrackId track; // captured
    Clip removed;  // captured
};

// What a SetClip changes. Only edits of the same kind on the same clip merge into one undo step
// (a drag), and the kind names the step in the Undo menu.
enum class ClipEdit : std::uint8_t
{
    move,   // position and/or track
    resize, // either edge
    notes,  // note content (piano roll, drum steps)
    loop    // loop length
};

// Replaces a clip's region and content, and optionally moves it to another track of the same
// kind. Everything except the id is taken from value.
struct SetClip
{
    SetClip (ClipId clip, TrackId targetTrack, Clip newValue, ClipEdit kind)
        : id (clip)
        , track (targetTrack)
        , value (std::move (newValue))
        , edit (kind)
    {
    }

    ClipId id;
    TrackId track;
    Clip value;
    ClipEdit edit = ClipEdit::move;
    TrackId previousTrack; // captured
    Clip previous;         // captured
};

// Splits a clip in two at a timeline position (see splitClip). The left part keeps the id.
struct SplitClip
{
    SplitClip (ClipId clip, core::Ticks position) noexcept
        : id (clip)
        , at (position)
    {
    }

    ClipId id;
    core::Ticks at = 0;
    TrackId track;  // captured
    Clip original;  // captured
    ClipId created; // captured: the right part
    std::uint64_t previousNextClipId = 0;
};

using Command = std::variant<SetTempo, SetTimeSignature, RenameProject, AddTrack, RemoveTrack, MoveTrack,
                             RenameTrack, SetTrackVolume, SetTrackPan, SetTrackEffect, SetTrackMute,
                             SetTrackSolo, SetParameter, AddAsset, RelinkAsset, SetSamplerAsset, SetDrumPad,
                             AddClip, RemoveClip, SetClip, SplitClip>;

enum class ApplyResult
{
    applied,   // project changed; record for undo
    unchanged, // valid but a no-op (e.g. same value); nothing to record
    rejected   // invalid (unknown track, out-of-range, ...); project untouched
};

// Validates the command against the project, captures the previous state and applies it.
// Never leaves the project partially modified.
[[nodiscard]] ApplyResult apply (Command& command, Project& project);

// Exactly undoes a command previously applied to the same project state.
void revert (const Command& command, Project& project);

// Short, user-facing description for undo/redo menus ("Change tempo").
[[nodiscard]] std::string_view describe (const Command& command) noexcept;

// True if next continues the same edit as previous (same kind, same target), so a continuous
// gesture such as a fader drag becomes one undo step.
[[nodiscard]] bool canMerge (const Command& previous, const Command& next) noexcept;

// Folds next into previous: keeps previous' captured "before" state, takes next's new value.
void merge (Command& previous, const Command& next);

} // namespace ap::model
