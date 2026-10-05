#pragma once

#include <cstddef>

namespace ap::core
{

// Non-owning view of planar float audio. Channel count and length are always explicit.
struct AudioBlock
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return channels == nullptr || numChannels <= 0 || numSamples <= 0;
    }
};

} // namespace ap::core
