#pragma once

#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/engine/RenderGraph.h"

#include <array>
#include <functional>
#include <optional>

namespace ap::engine
{

// Puts a project into an engine: tempo, meter, parameters, drum pads and the render graph. The
// live session and the export use the same function, so an export sounds like playback.
// Message thread (or the thread that owns the engine).
void configureEngine (Engine& engine, const model::Project& project, std::uint64_t version,
                      const AudioLookup& audio);

// Where the song ends: the end of its last clip, in ticks (0 when there are no clips).
[[nodiscard]] core::Ticks songEnd (const model::Project& project) noexcept;

struct SongRenderSettings
{
    double sampleRate = 48000.0;
    std::int64_t length = 0;      // samples of timeline to render from time 0
    double maxTailSeconds = 10.0; // effect tails are rendered until silent, at most this long
    double silenceSeconds = 0.25; // a tail has ended after this long below silenceLevel
    float silenceLevel = 1.0e-4f; // -80 dBFS
    int blockSize = 512;
};

// The same render, one slice at a time, so that whoever owns the thread can report progress and
// stop between slices (the browser has no second thread to cancel from while a render runs). The
// engine is prepared and started by the constructor, and must stay alive and untouched until
// finish() or abort().
class SongRender
{
public:
    SongRender (Engine& engine, const SongRenderSettings& settings);

    // Renders up to `blocks` more blocks. False once the timeline has played out and the tails have
    // been quiet for long enough (or the limit was reached): call finish().
    bool step (int blocks = 1);
    [[nodiscard]] bool done() const noexcept { return finished; }
    // How much of the timeline (plus latency) has played, 0..1.
    [[nodiscard]] double progress() const noexcept;
    // The finished audio: the engine's latency dropped at the front and the trailing silence at the
    // end, never cutting the song. Call once, after step() returned false.
    [[nodiscard]] RenderedAudio finish();
    // Gives up: the engine is released, nothing is returned.
    void abort();

private:
    Engine& engine;
    SongRenderSettings settings;
    int block = 0;
    std::int64_t latency = 0;
    std::int64_t length = 0;
    std::int64_t limit = 0;
    std::int64_t quiet = 0;
    std::int64_t rendered = 0;
    std::int64_t lastLoud = -1; // in rendered (output) samples
    bool finished = false;
    bool released = false;
    RenderedAudio output;
    std::array<std::vector<float>, 2> buffer;
};

// Renders a song for export (Task 025): the timeline from 0 to `length`, then the tails of
// reverbs and delays until they are silent. The engine's latency is compensated (the result starts
// at time 0). Calls progress (0..1) after each block; returning false cancels (nullopt).
// Not real-time: allocates the whole result. Prepares the engine and starts its transport.
[[nodiscard]] std::optional<RenderedAudio> renderSong (Engine& engine, const SongRenderSettings& settings,
                                                       const std::function<bool (double)>& progress = {});

} // namespace ap::engine
