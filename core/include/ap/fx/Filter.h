#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/LinearSmoothedValue.h"
#include "ap/dsp/StateVariableFilter.h"

#include <array>

namespace ap::fx
{

struct FilterSettings
{
    static constexpr float minCutoffHz = 20.0f;
    static constexpr float maxCutoffHz = 20000.0f;
    static constexpr float minResonance = 0.5f; // Q
    static constexpr float maxResonance = 10.0f;

    dsp::FilterMode mode = dsp::FilterMode::lowPass;
    float cutoffHz = 1000.0f;
    float resonance = 0.7071f; // Butterworth: flat, no peak

    bool operator== (const FilterSettings&) const = default;
};

// Track filter effect (guide §12.1, Task 018): a stereo 12 dB/octave TPT state variable filter.
//
// Nothing clicks when settings change: the cutoff glides in the logarithmic domain (so a sweep
// sounds even across octaves) and the resonance linearly, both over 20 ms, and changing the mode
// crossfades between the filter's simultaneous low/band/high outputs. Out-of-range or non-finite
// settings are clamped.
class Filter
{
public:
    static constexpr double smoothingSeconds = 0.02;

    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;

    // Audio thread (or before processing). Takes effect gradually from the next sample.
    void set (const FilterSettings& settings) noexcept AP_NONBLOCKING;

    // In place. Mono blocks use the left filter only; channels beyond two pass through.
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

private:
    double rate = 48000.0;
    std::array<dsp::StateVariableFilter, 2> filters;
    dsp::LinearSmoothedValue logCutoff; // log2 (Hz)
    dsp::LinearSmoothedValue resonance;
    std::array<dsp::LinearSmoothedValue, 3> modeWeights; // low, band, high
};

} // namespace ap::fx
