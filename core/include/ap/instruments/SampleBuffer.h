#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ap::instruments
{

// Immutable decoded audio ready for playback: already converted to the engine's sample rate
// (off the audio thread), 1 or 2 channels of equal length. Shared with the audio thread through
// a SnapshotExchange; never modified after publication.
struct SampleBuffer
{
    static constexpr std::size_t maxChannels = 2;

    std::uint64_t assetId = 0; // which project asset this came from (0 = none)
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;

    [[nodiscard]] std::int64_t frames() const noexcept
    {
        return channels.empty() ? 0 : static_cast<std::int64_t> (channels.front().size());
    }
    [[nodiscard]] bool isValid() const noexcept
    {
        if (channels.empty() || channels.size() > maxChannels || channels.front().empty())
            return false;
        for (const auto& channel : channels)
            if (channel.size() != channels.front().size())
                return false;
        return sampleRate > 0.0;
    }
};

} // namespace ap::instruments
