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

    RenderedAudio output (static_cast<std::size_t> (settings.numChannels),
                          std::vector<float> (static_cast<std::size_t> (settings.numSamples), 0.0f));

    std::vector<float*> channelPointers (output.size());

    engine.prepare (settings.sampleRate, settings.blockSize);

    for (std::int64_t start = 0; start < settings.numSamples; start += settings.blockSize)
    {
        const auto length
            = static_cast<int> (std::min<std::int64_t> (settings.blockSize, settings.numSamples - start));

        for (std::size_t ch = 0; ch < output.size(); ++ch)
            channelPointers[ch] = output[ch].data() + start;

        engine.process (core::AudioBlock{channelPointers.data(), settings.numChannels, length});
    }

    engine.releaseResources();
    return output;
}

} // namespace ap::engine
