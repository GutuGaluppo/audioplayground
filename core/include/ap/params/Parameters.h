#pragma once

#include "ap/core/RealtimeSafety.h"
#include "ap/params/generated/ParameterIds.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <string_view>

namespace ap::params
{

[[nodiscard]] constexpr const ParameterDescriptor& descriptor (ParamId id) noexcept
{
    return descriptors[static_cast<std::size_t> (id)];
}

// Lookup by persisted/bridge ID. Returns nullopt for unknown IDs (never guesses).
[[nodiscard]] constexpr std::optional<ParamId> findParamId (std::string_view id) noexcept
{
    for (std::size_t i = 0; i < numParameters; ++i)
        if (descriptors[i].id == id)
            return static_cast<ParamId> (i);
    return std::nullopt;
}

// Plain value -> [0, 1] for UI controls. Logarithmic curves give equal travel per octave/decade.
[[nodiscard]] inline float toNormalized (const ParameterDescriptor& d, float value) noexcept AP_NONBLOCKING
{
    const float clamped = std::clamp (value, d.min, d.max);
    if (d.curve == Curve::logarithmic)
        return std::log (clamped / d.min) / std::log (d.max / d.min);
    return (clamped - d.min) / (d.max - d.min);
}

[[nodiscard]] inline float fromNormalized (const ParameterDescriptor& d,
                                           float normalized) noexcept AP_NONBLOCKING
{
    const float n = std::clamp (normalized, 0.0f, 1.0f);
    if (d.curve == Curve::logarithmic)
        return std::clamp (d.min * std::pow (d.max / d.min, n), d.min, d.max);
    return std::clamp (d.min + n * (d.max - d.min), d.min, d.max);
}

[[nodiscard]] constexpr bool isInRange (const ParameterDescriptor& d, float value) noexcept
{
    return value >= d.min && value <= d.max; // false for NaN
}

// Current plain values of every parameter. Lock-free; safe to read on the audio thread.
// Writers validate: non-finite values are ignored and others are clamped to the range.
class ParameterStore
{
public:
    ParameterStore() noexcept
    {
        for (std::size_t i = 0; i < numParameters; ++i)
            values[i].store (descriptors[i].defaultValue, std::memory_order_relaxed);
    }

    // Returns false if the value was rejected (non-finite).
    bool set (ParamId id, float value) noexcept
    {
        if (!std::isfinite (value))
            return false;
        const auto& d = descriptor (id);
        values[index (id)].store (std::clamp (value, d.min, d.max), std::memory_order_relaxed);
        return true;
    }

    void resetToDefault (ParamId id) noexcept { set (id, descriptor (id).defaultValue); }

    [[nodiscard]] float get (ParamId id) const noexcept AP_NONBLOCKING
    {
        return values[index (id)].load (std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t index (ParamId id) noexcept AP_NONBLOCKING
    {
        return static_cast<std::size_t> (id);
    }

    std::array<std::atomic<float>, numParameters> values;
};

} // namespace ap::params
