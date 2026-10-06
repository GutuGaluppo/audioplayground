#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"

#include <array>
#include <atomic>
#include <vector>

namespace ap::fx
{

// Master limiter (plan §3.6, Task 024): keeps the output's true peak (the peaks between samples
// that a DAC reconstructs, estimated at 4x) under the ceiling, protecting ears and speakers.
//
// Lookahead: the audio is delayed so the gain is already down when a peak arrives. The required
// gain is held for the lookahead window and then averaged over it, so the gain reaches its target
// exactly at the peak without stepping; it then recovers over the release time. Stereo-linked.
// Below the ceiling it changes nothing but the delay.
class Limiter
{
public:
    static constexpr float ceilingDb = -1.0f; // dBTP
    static constexpr double lookaheadSeconds = 0.001;
    static constexpr double releaseSeconds = 0.1;
    static constexpr int detectorDelay = 4; // samples, from the interpolator's centre

    // Allocates (not real-time).
    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

    // Total delay in samples at the prepared rate.
    [[nodiscard]] int latency() const noexcept { return detectorDelay + window; }

    // Lowest gain applied since the last call, in dB (<= 0), for a meter. Resets it. Any thread.
    [[nodiscard]] float consumeGainReductionDb() noexcept;

    // True peak (4x interpolated) around the centre of 8 samples: the larger of history[3] and the
    // three interpolated points after it. Needs prepare().
    [[nodiscard]] float truePeak (const std::array<float, 8>& history) const noexcept AP_NONBLOCKING;

private:
    int window = 48; // lookahead in samples
    float releaseCoefficient = 0.0f;
    float ceiling = 1.0f;
    std::array<std::array<float, 8>, 3> taps {}; // windowed sinc at 1/4, 2/4, 3/4

    std::array<std::array<float, 8>, 2> history {}; // newest last
    std::vector<float> required;                    // ring of the last window + 1 required gains
    std::vector<float> smoothed;                    // ring of the last window + 1 released gains
    std::array<std::vector<float>, 2> delayLines;   // audio, latency samples
    std::size_t gainPosition = 0;
    std::size_t delayPosition = 0;
    double sum = 0.0; // of `smoothed`
    float released = 1.0f;
    std::atomic<float> lowestGain {1.0f};
};

} // namespace ap::fx
