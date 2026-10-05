#pragma once

#include "ap/core/AudioBlock.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ap::test
{

// Owns planar float buffers and hands out AudioBlock views of them.
class TestBuffer
{
public:
    TestBuffer (int numChannels, int numSamples)
        : data (static_cast<std::size_t> (numChannels),
                std::vector<float> (static_cast<std::size_t> (numSamples), 0.0f))
    {
        for (auto& channel : data)
            pointers.push_back (channel.data());
    }

    [[nodiscard]] core::AudioBlock block() noexcept
    {
        return {pointers.data(), static_cast<int> (pointers.size()), numSamples()};
    }

    // View of [start, start + length) in every channel.
    [[nodiscard]] core::AudioBlock slice (int start, int length)
    {
        offsetPointers.clear();
        for (auto& channel : data)
            offsetPointers.push_back (channel.data() + start);
        return {offsetPointers.data(), static_cast<int> (offsetPointers.size()), length};
    }

    void fill (float value)
    {
        for (auto& channel : data)
            std::fill (channel.begin(), channel.end(), value);
    }

    [[nodiscard]] const std::vector<float>& channel (int index) const
    {
        return data.at (static_cast<std::size_t> (index));
    }
    [[nodiscard]] int numSamples() const noexcept
    {
        return data.empty() ? 0 : static_cast<int> (data.front().size());
    }

private:
    std::vector<std::vector<float>> data;
    std::vector<float*> pointers;
    std::vector<float*> offsetPointers;
};

} // namespace ap::test
