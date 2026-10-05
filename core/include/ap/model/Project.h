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

struct Track
{
    static constexpr float minVolumeDb = -60.0f; // treated as silence
    static constexpr float maxVolumeDb = 6.0f;
    static constexpr std::size_t maxNameLength = 64;

    TrackId id;
    TrackKind kind = TrackKind::audio;
    std::string name;
    float volumeDb = 0.0f;
    float pan = 0.0f; // -1 (left) .. +1 (right)
    bool muted = false;
    bool soloed = false;

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

// 4x4 drum machine: pad settings and a one-bar, 16-step pattern (bit n of steps[pad] = step n).
struct DrumKit
{
    static constexpr std::size_t numPads = 16;
    static constexpr std::size_t numSteps = 16;

    std::array<DrumPad, numPads> pads {};
    std::array<std::uint16_t, numPads> steps {};

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

    bool operator== (const Project&) const = default;
};

// Names shown to people: trimmed, control characters removed, length-limited (in UTF-8 bytes,
// never splitting a code point). Returns nullopt if nothing printable remains.
[[nodiscard]] std::optional<std::string> sanitiseName (std::string_view input, std::size_t maxBytes);

} // namespace ap::model
