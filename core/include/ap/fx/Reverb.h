#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/LinearSmoothedValue.h"

#include <array>
#include <vector>

namespace ap::fx
{

struct ReverbSettings
{
    float size = 0.6f;    // 0..1: room size (delay lengths)
    float decay = 0.6f;   // 0..1: how long the tail lasts
    float damping = 0.4f; // 0..1: how quickly highs die away
    float mix = 0.25f;    // 0 (dry) .. 1 (wet only)

    bool operator== (const ReverbSettings&) const = default;
};

// Reverb V1 (guide §12.5, Task 023): the Schroeder-Moorer structure popularised by Freeverb
// (Jezar at Dreampoint, public domain), implemented here from the published design: per channel,
// eight parallel low-pass-feedback combs into four series all-passes, the right channel's delays
// offset for a wide, decorrelated tail. Size scales the delay lengths and glides over 300 ms (the
// tail morphs instead of clicking); decay sets the comb feedback, damping their low-pass. The V2
// reverb (FDN) replaces it later (plan §3.6).
class Reverb
{
public:
    // Allocates the delay lines (not real-time).
    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;
    void set (const ReverbSettings& settings) noexcept AP_NONBLOCKING;
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

    static constexpr std::size_t numCombs = 8;
    static constexpr std::size_t numAllpasses = 4;

private:
    struct Comb
    {
        std::vector<float> buffer;
        std::size_t write = 0;
        double baseLength = 0.0; // samples at size 1
        float store = 0.0f;      // damping low-pass state
    };

    struct Allpass
    {
        std::vector<float> buffer;
        std::size_t index = 0;
    };

    struct Channel
    {
        std::array<Comb, numCombs> combs;
        std::array<Allpass, numAllpasses> allpasses;
    };

    [[nodiscard]] static float readComb (const Comb& comb, double length) noexcept AP_NONBLOCKING;

    std::array<Channel, 2> channels;
    dsp::LinearSmoothedValue size;     // delay length scale
    dsp::LinearSmoothedValue feedback; // comb feedback
    dsp::LinearSmoothedValue damping;
    dsp::LinearSmoothedValue mix;
};

} // namespace ap::fx
