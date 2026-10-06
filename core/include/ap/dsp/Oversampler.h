#pragma once

#include "ap/core/RealtimeSafety.h"

#include <array>
#include <cstddef>
#include <vector>

namespace ap::dsp
{

// Linear-phase half-band FIR for 2x resampling (Kaiser-windowed sinc). Every other tap of a
// half-band filter is zero, so only the non-zero ones are stored. Taps are computed in the
// constructor (not real-time); upsample and downsample are.
class HalfbandFilter
{
public:
    // numTaps must be 4k + 3 (the end taps are non-zero).
    explicit HalfbandFilter (int numTaps, double kaiserBeta = 8.0);

    void reset() noexcept AP_NONBLOCKING;

    // One input sample at the low rate -> two at the high rate.
    void upsample (float input, float* output) noexcept AP_NONBLOCKING;
    // Two input samples at the high rate -> one at the low rate.
    [[nodiscard]] float downsample (const float* input) noexcept AP_NONBLOCKING;

    // Group delay in high-rate samples.
    [[nodiscard]] int delay() const noexcept { return (length - 1) / 2; }

private:
    [[nodiscard]] float convolve() const noexcept AP_NONBLOCKING;
    void push (float sample) noexcept AP_NONBLOCKING;

    int length = 0;
    std::vector<int> offsets;   // positions of the non-zero taps
    std::vector<float> weights; // their values
    std::vector<float> history; // circular, high rate
    std::size_t position = 0;
};

// 4x oversampling of one channel through two half-band stages (63 taps at 2x, 31 at 4x).
// Alias-free below ~20 kHz at 44.1/48 kHz with about 80 dB of stop-band rejection.
// latency() base-rate samples of delay from up to down (an integer, so a dry signal can be
// aligned exactly).
class Oversampler4x
{
public:
    static constexpr int factor = 4;

    Oversampler4x();

    void reset() noexcept AP_NONBLOCKING;
    void upsample (float input, std::array<float, factor>& output) noexcept AP_NONBLOCKING;
    [[nodiscard]] float downsample (const std::array<float, factor>& input) noexcept AP_NONBLOCKING;

    [[nodiscard]] static constexpr int latency() noexcept { return 38; }

private:
    HalfbandFilter up1 {63}, up2 {31}, down2 {31}, down1 {63};
    // One 4x sample of extra delay makes the total a whole number of base samples: the filters
    // delay by 4 * 31 + 2 * 15 = 154 4x samples, keeping the last sample of each pair when
    // decimating takes 3 back, and 154 - 3 + 1 = 152 = 4 * 38.
    float pad = 0.0f;
};

} // namespace ap::dsp
