#include "ap/engine/OfflineRenderer.h"

#include "ap/core/AudioBlock.h"

#include <algorithm>
#include <stdexcept>

namespace ap::engine
{

RenderedAudio renderOffline (Engine& engine, const RenderSettings& settings)
{
    if (settings.sampleRate <= 0.0 || settings.numChannels <= 0 || settings.numSamples < 0
        || settings.blockSize <= 0)
        throw std::invalid_argument ("renderOffline: invalid render settings");

    engine.prepare (settings.sampleRate, settings.blockSize);

    // The engine delays everything by its latency (ADR-009): render that much more and drop it
    // from the front, so sample 0 of the result is time 0 of the timeline.
    const std::int64_t latency = engine.getOutputLatency();
    const std::int64_t total = settings.numSamples + latency;
    RenderedAudio output (static_cast<std::size_t> (settings.numChannels),
                          std::vector<float> (static_cast<std::size_t> (total), 0.0f));

    std::vector<float*> channelPointers (output.size());

    for (std::int64_t start = 0; start < total; start += settings.blockSize)
    {
        const auto length = static_cast<int> (std::min<std::int64_t> (settings.blockSize, total - start));

        for (std::size_t ch = 0; ch < output.size(); ++ch)
            channelPointers[ch] = output[ch].data() + start;

        engine.process (core::AudioBlock {channelPointers.data(), settings.numChannels, length});
    }

    engine.releaseResources();
    for (auto& channel : output)
        channel.erase (channel.begin(), channel.begin() + static_cast<std::ptrdiff_t> (latency));
    return output;
}

} // namespace ap::engine
