#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/LinearSmoothedValue.h"

#include <array>
#include <vector>

namespace ap::fx
{

struct DelaySettings
{
    static constexpr float minTimeMs = 1.0f;
    static constexpr float maxTimeMs = 2000.0f;
    static constexpr float maxFeedback = 0.95f;

    float timeMs = 375.0f;  // a dotted eighth at 120 BPM
    float feedback = 0.35f; // 0..0.95
    float mix = 0.3f;       // 0 (dry) .. 1 (wet only)

    bool operator== (const DelaySettings&) const = default;
};

// Feedback delay (guide §12.4, Task 022), stereo. Changing the time glides over 150 ms and reads
// between samples (cubic Hermite), so it bends pitch like tape instead of clicking. The feedback
// path is gently damped (repeats get darker, like an analogue delay) and soft-saturated, so even
// at the highest feedback the repeats stay bounded. Mix and feedback glide over 20 ms.
// The time is in milliseconds; tempo sync is resolved before it gets here (engine/TrackChain).
class Delay
{
public:
    static constexpr double timeGlideSeconds = 0.15;

    // Allocates the delay lines (not real-time).
    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;
    void set (const DelaySettings& settings) noexcept AP_NONBLOCKING;
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

private:
    struct Line
    {
        std::vector<float> buffer;
        std::size_t write = 0;
        float damping = 0.0f; // one-pole low-pass state in the feedback path
    };

    [[nodiscard]] float read (const Line& line, double delaySamples) const noexcept AP_NONBLOCKING;

    double rate = 48000.0;
    float dampingCoefficient = 0.0f;
    std::array<Line, 2> lines;
    dsp::LinearSmoothedValue delaySamples;
    dsp::LinearSmoothedValue feedback;
    dsp::LinearSmoothedValue mix;
};

} // namespace ap::fx
