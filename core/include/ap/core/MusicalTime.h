#pragma once

#include "ap/core/RealtimeSafety.h"

#include <cmath>
#include <cstdint>

namespace ap::core
{

// Time representation (ADR-004): musical time in integer ticks, audio time in integer samples.
// Never use floating point for positions that are stored or compared.
using Ticks = std::int64_t;
using Samples = std::int64_t;

inline constexpr Ticks ticksPerQuarterNote = 960;

inline constexpr double minTempoBpm = 20.0;
inline constexpr double maxTempoBpm = 300.0;
inline constexpr int maxTimeSignatureNumerator = 32;

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        const bool powerOfTwo = denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8
                             || denominator == 16;
        return numerator >= 1 && numerator <= maxTimeSignatureNumerator && powerOfTwo;
    }

    [[nodiscard]] constexpr Ticks ticksPerBeat() const noexcept
    {
        return ticksPerQuarterNote * 4 / denominator;
    }
    [[nodiscard]] constexpr Ticks ticksPerBar() const noexcept { return ticksPerBeat() * numerator; }

    constexpr bool operator== (const TimeSignature&) const = default;
};

[[nodiscard]] constexpr bool isValidTempo (double bpm) noexcept
{
    return bpm >= minTempoBpm && bpm <= maxTempoBpm; // false for NaN
}

// 1-based position for display: bar 1, beat 1 is the project start.
struct BarBeatTick
{
    std::int64_t bar = 1;
    int beat = 1;
    Ticks tick = 0;

    constexpr bool operator== (const BarBeatTick&) const = default;
};

// Tick <-> sample conversion for a project with constant tempo and meter (decision D4).
// This is the only place where the conversion happens. Both directions round to nearest, so
// samplesToTicks (ticksToSamples (t)) == t whenever a tick is at least one sample long.
class TempoMap
{
public:
    constexpr TempoMap() noexcept = default;

    TempoMap (double bpm, TimeSignature signature, double sampleRate) noexcept
        : tempoBpm (isValidTempo (bpm) ? bpm : 120.0),
          meter (signature.isValid() ? signature : TimeSignature{}),
          rate (sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0),
          samplesPerTick (60.0 * rate / (tempoBpm * static_cast<double> (ticksPerQuarterNote)))
    {
    }

    [[nodiscard]] Samples ticksToSamples (Ticks ticks) const noexcept AP_NONBLOCKING
    {
        return static_cast<Samples> (std::llround (static_cast<double> (ticks) * samplesPerTick));
    }

    [[nodiscard]] Ticks samplesToTicks (Samples samples) const noexcept AP_NONBLOCKING
    {
        return static_cast<Ticks> (std::llround (static_cast<double> (samples) / samplesPerTick));
    }

    // The tick that contains the given sample (rounds towards negative infinity).
    [[nodiscard]] Ticks tickAtOrBefore (Samples samples) const noexcept AP_NONBLOCKING
    {
        auto ticks = static_cast<Ticks> (std::floor (static_cast<double> (samples) / samplesPerTick));
        // Guard against floating-point edge cases so the result is always consistent with ticksToSamples.
        while (ticksToSamples (ticks + 1) <= samples)
            ++ticks;
        while (ticks > INT64_MIN && ticksToSamples (ticks) > samples)
            --ticks;
        return ticks;
    }

    [[nodiscard]] BarBeatTick toBarBeatTick (Ticks ticks) const noexcept AP_NONBLOCKING
    {
        const auto perBar = meter.ticksPerBar();
        const auto perBeat = meter.ticksPerBeat();
        auto bar = ticks / perBar;
        auto inBar = ticks % perBar;
        if (inBar < 0)
        {
            inBar += perBar;
            --bar;
        }
        return {bar + 1, static_cast<int> (inBar / perBeat) + 1, inBar % perBeat};
    }

    [[nodiscard]] double getTempo() const noexcept AP_NONBLOCKING { return tempoBpm; }
    [[nodiscard]] TimeSignature getTimeSignature() const noexcept AP_NONBLOCKING { return meter; }
    [[nodiscard]] double getSampleRate() const noexcept AP_NONBLOCKING { return rate; }
    [[nodiscard]] double getSamplesPerTick() const noexcept AP_NONBLOCKING { return samplesPerTick; }

private:
    double tempoBpm = 120.0;
    TimeSignature meter{};
    double rate = 48000.0;
    double samplesPerTick = 60.0 * 48000.0 / (120.0 * static_cast<double> (ticksPerQuarterNote));
};

} // namespace ap::core
