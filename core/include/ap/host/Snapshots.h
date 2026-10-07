#pragma once

#include "ap/bridge/generated/Messages.h"
#include "ap/engine/Engine.h"
#include "ap/model/ProjectDocument.h"

#include <string>

namespace ap::host
{

// The events that describe the project and the engine to a UI (ADR-003): pure functions of their
// inputs, shared by every host (desktop and web), so a UI sees the same thing wherever the core runs.

[[nodiscard]] bridge::TimelineState timelineState (const model::Project& project);
[[nodiscard]] bridge::HistoryState historyState (const model::ProjectDocument& document);
[[nodiscard]] bridge::ParamValue paramValue (const model::Project& project, params::ParamId id);
[[nodiscard]] bridge::DrumsKit drumsKit (const model::Project& project);
[[nodiscard]] bridge::InstrumentState instrumentState (const engine::Engine& engine);

// What the host knows about a pad's own sample (a pad on the factory sound has none).
struct PadSample
{
    std::string name;
    bool missing = false;
};
[[nodiscard]] bridge::DrumsPad drumsPad (const model::Project& project, std::size_t pad,
                                         const PadSample& sample);

// What only the host knows about the transport.
struct TransportExtras
{
    bool recording = false;
    model::TrackId armedTrack;
    bool captureAvailable = false;
};
[[nodiscard]] bridge::TransportState transportState (const model::Project& project,
                                                     const engine::Engine& engine,
                                                     const TransportExtras& extras = {});
[[nodiscard]] bridge::TransportPosition transportPosition (const engine::Engine& engine);

// Output and input peaks and limiter reduction since the last call (it resets them).
[[nodiscard]] bridge::EngineMeters engineMeters (engine::Engine& engine);

} // namespace ap::host
