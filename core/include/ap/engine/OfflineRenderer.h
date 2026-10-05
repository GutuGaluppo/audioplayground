#pragma once

#include "ap/engine/Engine.h"

#include <cstdint>
#include <vector>

namespace ap::engine
{

struct RenderSettings
{
    double sampleRate = 48000.0;
    int numChannels = 2;
    std::int64_t numSamples = 0;
    int blockSize = 512;
};

// Planar audio owned by the caller.
using RenderedAudio = std::vector<std::vector<float>>;

// Renders the engine faster than real time by calling process() in blocks, exactly as an audio
// device would. Used for export and as the harness for golden and determinism tests.
// Not real-time safe: allocates the output. Calls engine.prepare() first.
[[nodiscard]] RenderedAudio renderOffline (Engine& engine, const RenderSettings& settings);

} // namespace ap::engine
