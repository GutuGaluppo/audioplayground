#pragma once

#include "ap/model/Project.h"

#include <cstdint>
#include <vector>

namespace ap::engine
{

// Everything the audio thread needs to know about one track, pre-resolved on the message
// thread: no lookups, branching on solo state or dB conversion in the audio callback.
struct TrackRender
{
    model::TrackId id;
    float leftGain = 0.0f; // volume x pan law x mute/solo, linear
    float rightGain = 0.0f;
    bool audible = false;

    bool operator== (const TrackRender&) const = default;
};

// Immutable snapshot of the render structure (ADR-005). Built from the Project after every
// structural change and handed to the audio thread through a SnapshotExchange.
struct RenderGraph
{
    std::uint64_t projectVersion = 0;
    std::vector<TrackRender> tracks;
};

// Constant-power pan law: -3 dB per side at centre, full level on the side panned to.
struct PanGains
{
    float left;
    float right;
};
[[nodiscard]] PanGains constantPowerPan (float pan) noexcept;

// Resolves mute and solo: when any track is soloed, only soloed tracks are audible; a muted
// track is never audible (mute wins over solo).
[[nodiscard]] RenderGraph buildRenderGraph (const model::Project& project, std::uint64_t projectVersion);

} // namespace ap::engine
