#pragma once

#include "ap/core/MusicalTime.h"
#include "ap/params/Parameters.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ap::model
{

// Stable identity for a track. Never reused within a project, even after deletion, so undo,
// persistence and UI references cannot alias a different track.
struct TrackId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    constexpr auto operator<=> (const TrackId&) const = default;
};

// Stable identity for an imported audio file in the project folder. Never reused.
struct AssetId
{
    std::uint64_t value = 0; // 0 = none

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    constexpr auto operator<=> (const AssetId&) const = default;
};

// An audio file owned by the project, stored under its folder (ADR-006). The project only ever
// references copies it owns; the user's original files are never modified or referenced.
struct Asset
{
    static constexpr std::size_t maxNameLength = 128;

    AssetId id;
    std::string relativePath; // canonical, e.g. "audio/3-kick.wav" (see isSafeAssetPath)
    std::string name;         // shown to the user, e.g. "kick.wav"

    bool operator== (const Asset&) const = default;
};

// Pure syntax check for asset references: "audio/" + one plain file name, ASCII letters, digits,
// '-', '_', '.', no leading dot. The filesystem check (symlinks) lives in io::resolveAssetPath.
[[nodiscard]] bool isSafeAssetPath (std::string_view path) noexcept;

enum class TrackKind : std::uint8_t
{
    audio,
    instrument
};

// The instrument an instrument track plays (ADR-007: at most one track per instrument).
enum class InstrumentKind : std::uint8_t
{
    synth = 0,
    sampler = 1,
    drums = 2
};
inline constexpr std::size_t numInstrumentKinds = 3;

// Stable identity for a clip, unique across the whole project. Never reused.
struct ClipId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    constexpr auto operator<=> (const ClipId&) const = default;
};

// Largest timeline position anything may reach: 10 000 bars of 4/4 (~5.5 h at 120 BPM).
inline constexpr core::Ticks maxTimelineTicks = 10'000 * 4 * core::ticksPerQuarterNote;

struct Note
{
    static constexpr float minVelocity = 1.0f / 127.0f;

    core::Ticks start = 0;  // from the start of the clip's content
    core::Ticks length = 0; // > 0
    std::uint8_t pitch = 60;
    float velocity = 1.0f; // minVelocity..1

    bool operator== (const Note&) const = default;
};

// A region on a track (ADR-007). Audio clips use asset and sourceOffset; note clips use notes,
// contentOffset and loopLength. Fields of the other kind stay at their defaults.
struct Clip
{
    static constexpr core::Ticks minLength = core::ticksPerQuarterNote / 32; // a 128th note
    static constexpr std::size_t maxNotes = 4096;

    ClipId id;
    core::Ticks start = 0;  // timeline position, >= 0
    core::Ticks length = 0; // >= minLength; start + length <= maxTimelineTicks

    // Audio clips: which file, and where in it playback starts.
    AssetId asset;
    core::Flicks sourceOffset = 0;

    // Note clips: content time at the clip start (left trim) and, if non-zero, the length after
    // which the content repeats. Notes are sorted by start, then pitch, and never duplicated.
    std::vector<Note> notes;
    core::Ticks contentOffset = 0;
    core::Ticks loopLength = 0;

    [[nodiscard]] core::Ticks end() const noexcept { return start + length; }

    bool operator== (const Clip&) const = default;
};

struct Track
{
    static constexpr float minVolumeDb = -60.0f; // treated as silence
    static constexpr float maxVolumeDb = 6.0f;
    static constexpr std::size_t maxNameLength = 64;

    static constexpr std::size_t maxClips = 512;

    TrackId id;
    TrackKind kind = TrackKind::audio;
    InstrumentKind instrument = InstrumentKind::synth; // instrument tracks only
    std::string name;
    float volumeDb = 0.0f;
    float pan = 0.0f; // -1 (left) .. +1 (right)
    bool muted = false;
    bool soloed = false;
    std::vector<Clip> clips; // sorted by start, then id

    bool operator== (const Track&) const = default;
};

struct DrumPad
{
    static constexpr float minVolumeDb = -60.0f;
    static constexpr float maxVolumeDb = 6.0f;
    static constexpr float maxPitch = 24.0f;

    AssetId sample; // invalid = built-in factory sound
    float volumeDb = 0.0f;
    float pitch = 0.0f; // semitones
    bool muted = false;

    bool operator== (const DrumPad&) const = default;
};

// 4x4 drum machine settings. The patterns themselves are note clips on the drum track (ADR-007).
struct DrumKit
{
    static constexpr std::size_t numPads = 16;
    static constexpr std::size_t numSteps = 16;                                // per pattern
    static constexpr core::Ticks ticksPerStep = core::ticksPerQuarterNote / 4; // sixteenths
    static constexpr core::Ticks patternLength = numSteps * ticksPerStep;      // one 4/4 bar
    static constexpr std::uint8_t firstPadNote = 36;                           // GM kick

    std::array<DrumPad, numPads> pads {};

    bool operator== (const DrumKit&) const = default;
};

// Project state (guide §20 "Project State"). Plain value type: copyable, comparable, no
// pointers. Changed only through commands (ADR-003).
struct Project
{
    static constexpr std::size_t maxNameLength = 128;
    static constexpr std::size_t maxTracks = 64;
    static constexpr std::size_t maxAssets = 1024;

    std::string name = "Untitled";
    double tempoBpm = 120.0;              // fixed per project in the MVP (decision D4)
    core::TimeSignature timeSignature {}; // fixed per project in the MVP (decision D4)
    double exportSampleRate = 48000.0;    // default export rate only (ADR-006)
    std::vector<Track> tracks;
    std::array<float, params::numParameters> parameters = defaultParameterValues();
    std::uint64_t nextTrackId = 1;
    std::uint64_t nextClipId = 1;

    std::vector<Asset> assets;
    std::uint64_t nextAssetId = 1;
    AssetId samplerAsset; // sample loaded in the sampler (none if invalid)
    DrumKit drums;

    // Parameters this build does not know (written by a newer version with the same schema).
    // Kept verbatim and written back so opening and saving never loses them (guide §11).
    std::vector<std::pair<std::string, double>> preservedParameters;

    [[nodiscard]] static std::array<float, params::numParameters> defaultParameterValues() noexcept
    {
        std::array<float, params::numParameters> values {};
        for (std::size_t i = 0; i < params::numParameters; ++i)
            values[i] = params::descriptors[i].defaultValue;
        return values;
    }

    [[nodiscard]] float parameter (params::ParamId id) const noexcept
    {
        return parameters[static_cast<std::size_t> (id)];
    }

    [[nodiscard]] const Track* findTrack (TrackId id) const noexcept;
    [[nodiscard]] Track* findTrack (TrackId id) noexcept;
    [[nodiscard]] std::optional<std::size_t> indexOf (TrackId id) const noexcept;
    [[nodiscard]] const Asset* findAsset (AssetId id) const noexcept;
    [[nodiscard]] const Track* findInstrumentTrack (InstrumentKind instrument) const noexcept;

    struct ClipLocation
    {
        std::size_t track = 0; // index into tracks
        std::size_t clip = 0;  // index into tracks[track].clips
    };
    [[nodiscard]] std::optional<ClipLocation> locate (ClipId id) const noexcept;
    [[nodiscard]] const Clip* findClip (ClipId id) const noexcept;

    bool operator== (const Project&) const = default;
};

// Names shown to people: trimmed, control characters removed, length-limited (in UTF-8 bytes,
// never splitting a code point). Returns nullopt if nothing printable remains.
[[nodiscard]] std::optional<std::string> sanitiseName (std::string_view input, std::size_t maxBytes);

} // namespace ap::model
