#pragma once

#include "ap/model/Project.h"

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
    explicit SetTempo (double newBpm) noexcept : bpm (newBpm) {}

    double bpm = 120.0;
    double previous = 0.0;
};

struct SetTimeSignature
{
    explicit SetTimeSignature (core::TimeSignature newSignature) noexcept : signature (newSignature) {}

    core::TimeSignature signature{};
    core::TimeSignature previous{};
};

struct RenameProject
{
    explicit RenameProject (std::string newName) : name (std::move (newName)) {}

    std::string name;
    std::string previous;
};

struct AddTrack
{
    explicit AddTrack (TrackKind trackKind, std::string trackName = {}, std::optional<std::size_t> at = {})
        : kind (trackKind), name (std::move (trackName)), index (at)
    {
    }

    TrackKind kind = TrackKind::audio;
    std::string name;                 // empty: a default such as "Audio 2"
    std::optional<std::size_t> index; // empty: append
    TrackId created;                  // captured
    std::uint64_t previousNextTrackId = 0;
};

struct RemoveTrack
{
    explicit RemoveTrack (TrackId track) noexcept : id (track) {}

    TrackId id;
    Track removed; // captured
    std::size_t index = 0;
};

struct MoveTrack
{
    MoveTrack (TrackId track, std::size_t to) noexcept : id (track), toIndex (to) {}

    TrackId id;
    std::size_t toIndex = 0;
    std::size_t fromIndex = 0; // captured
};

struct RenameTrack
{
    RenameTrack (TrackId track, std::string newName) : id (track), name (std::move (newName)) {}

    TrackId id;
    std::string name;
    std::string previous;
};

struct SetTrackVolume
{
    SetTrackVolume (TrackId track, float db) noexcept : id (track), volumeDb (db) {}

    TrackId id;
    float volumeDb = 0.0f;
    float previous = 0.0f;
};

struct SetTrackPan
{
    SetTrackPan (TrackId track, float newPan) noexcept : id (track), pan (newPan) {}

    TrackId id;
    float pan = 0.0f;
    float previous = 0.0f;
};

struct SetTrackMute
{
    SetTrackMute (TrackId track, bool mute) noexcept : id (track), muted (mute) {}

    TrackId id;
    bool muted = false;
    bool previous = false;
};

struct SetTrackSolo
{
    SetTrackSolo (TrackId track, bool solo) noexcept : id (track), soloed (solo) {}

    TrackId id;
    bool soloed = false;
    bool previous = false;
};

struct SetParameter
{
    SetParameter (params::ParamId parameter, float newValue) noexcept : id (parameter), value (newValue) {}

    params::ParamId id{};
    float value = 0.0f;
    float previous = 0.0f;
};

using Command
    = std::variant<SetTempo, SetTimeSignature, RenameProject, AddTrack, RemoveTrack, MoveTrack, RenameTrack,
                   SetTrackVolume, SetTrackPan, SetTrackMute, SetTrackSolo, SetParameter>;

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
