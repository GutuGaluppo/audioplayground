#pragma once

#include "ap/instruments/SampleBuffer.h"
#include "ap/model/Project.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ap::engine
{

// An audio clip ready to play. The buffer is shared with the message thread's cache; the graph
// keeps it alive for as long as the audio thread can see the graph (graphs are only ever freed on
// the message thread, ADR-005).
struct AudioClipRender
{
    core::Ticks start = 0;
    core::Ticks end = 0;
    core::Flicks sourceOffset = 0;
    std::shared_ptr<const instruments::SampleBuffer> buffer; // null: missing or still loading (silent)

    bool operator== (const AudioClipRender&) const = default;
};

struct NoteClipRender
{
    core::Ticks start = 0;
    core::Ticks end = 0;
    core::Ticks contentOffset = 0;
    core::Ticks loopLength = 0;
    std::vector<model::Note> notes; // sorted by start

    bool operator== (const NoteClipRender&) const = default;
};

// Everything the audio thread needs to know about one track, pre-resolved on the message
// thread: no lookups, branching on solo state or dB conversion in the audio callback.
struct TrackRender
{
    model::TrackId id;
    model::TrackKind kind = model::TrackKind::audio;
    model::InstrumentKind instrument = model::InstrumentKind::synth;
    float leftGain = 0.0f; // volume x pan law x mute/solo, linear
    float rightGain = 0.0f;
    bool audible = false;
    std::vector<AudioClipRender> audioClips; // sorted by start
    std::vector<NoteClipRender> noteClips;   // sorted by start

    bool operator== (const TrackRender&) const = default;
};

// Immutable snapshot of the render structure (ADR-005). Built from the Project after every
// structural change and handed to the audio thread through a SnapshotExchange.
struct RenderGraph
{
    std::uint64_t projectVersion = 0;
    std::vector<TrackRender> tracks;
    std::array<int, model::numInstrumentKinds> instrumentTrack {-1, -1, -1}; // index into tracks, or -1
};

// Pan law: constant power, compensated so a centred track plays at unity (the "-3 dB compensated"
// law): the side panned to rises to +3 dB, the other side falls to silence.
struct PanGains
{
    float left;
    float right;
};
[[nodiscard]] PanGains constantPowerPan (float pan) noexcept;

// Decoded audio for an asset at the engine's sample rate, or null if not available.
using AudioLookup = std::function<std::shared_ptr<const instruments::SampleBuffer> (model::AssetId)>;

// Resolves mute and solo: when any track is soloed, only soloed tracks are audible; a muted
// track is never audible (mute wins over solo).
[[nodiscard]] RenderGraph buildRenderGraph (const model::Project& project, std::uint64_t projectVersion,
                                            const AudioLookup& audio = {});

} // namespace ap::engine
