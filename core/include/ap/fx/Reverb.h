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
    float decay = 0.6f;   // 0..1: reverberation time, 0.3 s .. 12 s (see decaySeconds)
    float damping = 0.4f; // 0..1: how much faster the highs die away than the lows
    float mix = 0.25f;    // 0 (dry) .. 1 (wet only)

    bool operator== (const ReverbSettings&) const = default;
};

// Reverb V2 (plan §3.6): the app's own feedback delay network.
//
// - Each channel passes four short all-pass diffusers (a dense onset), then feeds half of eight
//   delay lines (left the even lines, right the odd ones).
// - The lines are mixed through an 8x8 Hadamard matrix: orthogonal, so the network neither gains
//   nor loses energy by itself, and every line feeds every other one (a dense, even tail).
// - Each line's gain is set from its own length so the whole tail falls by 60 dB in exactly the
//   decay time (Jot), and a one-pole low-pass in each line makes the highs die faster (damping).
// - The line lengths move slowly (a few samples, at different rates) so no mode rings metallic.
// - Left and right read the lines with different sign patterns: a wide, decorrelated tail.
//
// Size scales the line lengths and glides over 300 ms (the tail morphs instead of clicking);
// decay, damping and mix glide over 20 ms. Replaces the Freeverb-style V1 with the same settings,
// so projects and presets keep working.
class Reverb
{
public:
    static constexpr std::size_t numLines = 8;
    static constexpr std::size_t numDiffusers = 4;

    // Reverberation time for a decay setting.
    [[nodiscard]] static double decaySeconds (float decay) noexcept;

    // Allocates the delay lines (not real-time).
    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;
    void set (const ReverbSettings& settings) noexcept AP_NONBLOCKING;
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

private:
    struct Line
    {
        std::vector<float> buffer;
        std::size_t write = 0;
        double baseLength = 0.0; // samples at size 1
        float lowpass = 0.0f;    // damping filter state
        float gain = 0.0f;       // per round trip, from the decay time (refreshed every 32 samples)
        // Modulation: a slowly rotating phasor (cos, sin), cheaper than sin() per sample.
        double cosine = 1.0;
        double sine = 0.0;
        double stepCos = 1.0;
        double stepSin = 0.0;
    };

    struct Diffuser
    {
        std::vector<float> buffer;
        std::size_t index = 0;
    };

    [[nodiscard]] static float read (const Line& line, double length) noexcept AP_NONBLOCKING;

    double rate = 48000.0;
    double modulationDepth = 0.0; // samples
    std::array<Line, numLines> lines;
    std::array<std::array<Diffuser, numDiffusers>, 2> diffusers;
    dsp::LinearSmoothedValue size;    // length scale
    dsp::LinearSmoothedValue decay;   // seconds
    dsp::LinearSmoothedValue damping; // low-pass coefficient
    dsp::LinearSmoothedValue mix;
    int untilGainUpdate = 0;
    float level = 0.0f; // wet output gain, eased down for long decays
};

} // namespace ap::fx
