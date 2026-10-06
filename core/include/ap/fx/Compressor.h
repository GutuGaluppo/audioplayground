#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/LinearSmoothedValue.h"

#include <atomic>

namespace ap::fx
{

struct CompressorSettings
{
    float thresholdDb = -18.0f; // -60..0
    float ratio = 4.0f;         // 1..20 (20 is close to limiting)
    float attackMs = 10.0f;     // 0.1..100
    float releaseMs = 150.0f;   // 10..2000
    float makeupDb = 0.0f;      // 0..24
    float kneeDb = 6.0f;        // 0..24, soft knee width

    bool operator== (const CompressorSettings&) const = default;
};

// Feed-forward compressor (guide §12.3, Task 020), stereo-linked: both channels get the same gain,
// so the stereo image does not shift. The level is the louder channel's peak; the static curve has
// a soft knee; the gain is smoothed in the decibel domain with separate attack and release
// (Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design", 2012), which keeps
// release times musical whatever the amount of reduction. Make-up gain glides over 20 ms.
class Compressor
{
public:
    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;
    void set (const CompressorSettings& settings) noexcept AP_NONBLOCKING;
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

    // Current gain reduction in dB (<= 0), for a meter. Any thread.
    [[nodiscard]] float getGainReductionDb() const noexcept
    {
        return reduction.load (std::memory_order_relaxed);
    }

    // The static curve: output level for an input level, both in dB (pure; for tests and the UI).
    [[nodiscard]] static float curveDb (float inputDb, float thresholdDb, float ratio,
                                        float kneeDb) noexcept AP_NONBLOCKING;

private:
    double rate = 48000.0;
    float threshold = -18.0f;
    float ratio = 4.0f;
    float knee = 6.0f;
    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    float gainDb = 0.0f; // smoothed reduction (<= 0)
    dsp::LinearSmoothedValue makeup;
    std::atomic<float> reduction {0.0f};
};

} // namespace ap::fx
