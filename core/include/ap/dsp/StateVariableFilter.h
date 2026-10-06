#pragma once

#include "ap/core/RealtimeSafety.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace ap::dsp
{

enum class FilterMode : std::uint8_t
{
    lowPass,
    highPass,
    bandPass
};

// Topology-preserving-transform state variable filter (Zavalishin; Simper's formulation),
// 12 dB/octave. Unlike direct-form biquads it stays stable and click-free when the cutoff is
// modulated every sample, which is what synth envelopes and sweeps do. One instance per channel.
class StateVariableFilter
{
public:
    static constexpr double minCutoffHz = 10.0;
    static constexpr double minQ = 0.5;
    static constexpr double maxQ = 20.0;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        reset();
        updateCoefficients();
    }

    void reset() noexcept AP_NONBLOCKING
    {
        ic1 = 0.0;
        ic2 = 0.0;
    }

    void setMode (FilterMode newMode) noexcept AP_NONBLOCKING { mode = newMode; }

    // Q = 0.7071 is Butterworth (maximally flat); higher values resonate.
    void setCutoffAndQ (double cutoffHz, double q) noexcept AP_NONBLOCKING
    {
        const double maxCutoff = sampleRate * 0.49;
        const double newCutoff
            = std::isfinite (cutoffHz) ? std::clamp (cutoffHz, minCutoffHz, maxCutoff) : 1000.0;
        const double newQ = std::isfinite (q) ? std::clamp (q, minQ, maxQ) : 0.7071;
        if (newCutoff == cutoff && newQ == resonance)
            return;
        cutoff = newCutoff;
        resonance = newQ;
        updateCoefficients();
    }

    [[nodiscard]] float process (float input) noexcept AP_NONBLOCKING
    {
        const auto out = processAll (input);
        switch (mode)
        {
        case FilterMode::lowPass:
            return static_cast<float> (out.low);
        case FilterMode::highPass:
            return static_cast<float> (out.high);
        case FilterMode::bandPass:
            return static_cast<float> (out.band);
        }
        return 0.0f;
    }

    // All three responses come from the same state, so blending them switches modes without a click.
    struct Outputs
    {
        double low;
        double band;
        double high;
    };
    [[nodiscard]] Outputs processAll (float input) noexcept AP_NONBLOCKING
    {
        const double x = static_cast<double> (input);
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return {v2, v1, x - k * v1 - v2};
    }

private:
    void updateCoefficients() noexcept AP_NONBLOCKING
    {
        const double g = std::tan (std::numbers::pi * cutoff / sampleRate);
        k = 1.0 / resonance;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 48000.0;
    double cutoff = 1000.0;
    double resonance = 0.7071;
    double k = 1.0 / 0.7071;
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0; // integrator states in double: low-frequency settings stay precise
    FilterMode mode = FilterMode::lowPass;
};

} // namespace ap::dsp
