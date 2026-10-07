#include "ap/engine/Export.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace ap::engine
{

void configureEngine (Engine& engine, const model::Project& project, std::uint64_t version,
                      const AudioLookup& audio)
{
    // Lock-free setters; the audio thread picks the values up on its next block.
    engine.getTransport().setTempo (project.tempoBpm);
    engine.getTransport().setTimeSignature (project.timeSignature);

    for (std::size_t i = 0; i < params::numParameters; ++i)
        engine.getParameters().set (static_cast<params::ParamId> (i), project.parameters[i]);

    auto& drums = engine.getDrums();
    drums.setKit (project.drums.kit);
    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
    {
        const auto& settings = project.drums.pads[pad];
        drums.setPad (static_cast<int> (pad), settings.volumeDb, settings.pitch, settings.muted);
    }

    engine.publishRenderGraph (std::make_unique<RenderGraph> (buildRenderGraph (project, version, audio)));
}

core::Ticks songEnd (const model::Project& project) noexcept
{
    core::Ticks end = 0;
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
            end = std::max (end, clip.end());
    return end;
}

SongRender::SongRender (Engine& engineToUse, const SongRenderSettings& settingsToUse)
    : engine (engineToUse)
    , settings (settingsToUse)
    , block (std::max (16, settingsToUse.blockSize))
    , output (2)
    , buffer {std::vector<float> (static_cast<std::size_t> (std::max (16, settingsToUse.blockSize))),
              std::vector<float> (static_cast<std::size_t> (std::max (16, settingsToUse.blockSize)))}
{
    engine.prepare (settings.sampleRate, block);
    engine.getTransport().requestSeek (0);
    engine.getTransport().requestPlay();

    latency = static_cast<std::int64_t> (engine.getOutputLatency());
    length = std::max<std::int64_t> (0, settings.length);
    const auto maxTail = static_cast<std::int64_t> (
        std::ceil (std::max (0.0, settings.maxTailSeconds) * settings.sampleRate));
    quiet = static_cast<std::int64_t> (std::ceil (settings.silenceSeconds * settings.sampleRate));
    limit = latency + length + maxTail;
    finished = limit <= 0;
}

double SongRender::progress() const noexcept
{
    return std::min (1.0, static_cast<double> (rendered)
                              / static_cast<double> (std::max<std::int64_t> (1, latency + length)));
}

bool SongRender::step (int blocks)
{
    std::array<float*, 2> pointers {buffer[0].data(), buffer[1].data()};
    for (int done = 0; done < blocks && !finished; ++done)
    {
        const auto n = static_cast<int> (std::min<std::int64_t> (block, limit - rendered));
        engine.process ({pointers.data(), 2, n});
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            output[ch].insert (output[ch].end(), buffer[ch].begin(), buffer[ch].begin() + n);
            for (int i = 0; i < n; ++i)
                if (std::abs (buffer[ch][static_cast<std::size_t> (i)]) > settings.silenceLevel)
                    lastLoud = std::max (lastLoud, rendered + i);
        }
        rendered += n;

        // The song is done once its timeline has played out and the tail has been quiet a while.
        if (rendered >= limit
            || (rendered >= latency + length && rendered - std::max (lastLoud, latency + length) >= quiet))
            finished = true;
    }
    return !finished;
}

RenderedAudio SongRender::finish()
{
    if (!released)
        engine.releaseResources();
    released = true;

    // Drop the latency at the front and the silence after the tail (never cutting the song).
    const auto end = std::max (latency + length, std::min (rendered, lastLoud + 1));
    for (auto& channel : output)
    {
        channel.resize (static_cast<std::size_t> (end));
        channel.erase (channel.begin(),
                       channel.begin() + static_cast<std::ptrdiff_t> (std::min (latency, end)));
    }
    return std::move (output);
}

void SongRender::abort()
{
    if (!released)
        engine.releaseResources();
    released = true;
    finished = true;
    output.clear();
}

std::optional<RenderedAudio> renderSong (Engine& engine, const SongRenderSettings& settings,
                                         const std::function<bool (double)>& progress)
{
    SongRender render (engine, settings);
    while (!render.done())
    {
        render.step (1);
        // Not asked about the block that finished the song: it is done, there is nothing to stop.
        if (!render.done() && progress && !progress (render.progress()))
        {
            render.abort();
            return std::nullopt;
        }
    }
    auto audio = render.finish();
    if (progress)
        (void)progress (1.0);
    return audio;
}

} // namespace ap::engine
