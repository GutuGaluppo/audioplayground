#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/LinearSmoothedValue.h"
#include "ap/dsp/Oversampler.h"
#include "ap/dsp/StateVariableFilter.h"

#include <array>

namespace ap::fx
{

struct DistortionSettings
{
    float driveDb = 12.0f; // 0..36
    float tone = 0.7f;     // 0..1: dark (800 Hz low-pass) .. open (20 kHz)
    float outputDb = 0.0f; // -24..+12
    float mix = 1.0f;      // 0 (dry) .. 1 (wet)

    bool operator== (const DistortionSettings&) const = default;
};

// Waveshaping distortion (guide §12.6, Task 021): drive into a slightly asymmetric tanh (even
// harmonics, warmer than a symmetric clip) at 4x oversampling, so the harmonics it creates do not
// fold back as aliasing (below -70 dB up to 24 dB of drive; only extreme drive on high notes
// approaches -45 dB). A DC blocker removes the offset the asymmetry adds; the drive is
// compensated so a -12 dBFS signal keeps its level as the drive rises. Tone is a low-pass after
// the shaper. The dry signal is delayed by the oversampler's latency, so mixing never combs.
// Every setting glides over 20 ms.
//
// Latency: latency() samples for the whole effect (wet and dry alike); the effect chain must
// compensate it (Task 024).
class Distortion
{
public:
    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;
    void set (const DistortionSettings& settings) noexcept AP_NONBLOCKING;
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

    [[nodiscard]] static constexpr int latency() noexcept { return dsp::Oversampler4x::latency(); }

private:
    static constexpr std::size_t dryLength = static_cast<std::size_t> (dsp::Oversampler4x::latency()) + 1;

    struct Channel
    {
        dsp::Oversampler4x oversampler;
        dsp::StateVariableFilter tone;
        double dcIn = 0.0;
        double dcOut = 0.0;
        std::array<float, dryLength> dry {};
        std::size_t dryPosition = 0;
    };

    double rate = 48000.0;
    double dcCoefficient = 0.999;
    std::array<Channel, 2> channels;
    dsp::LinearSmoothedValue drive;   // linear gain
    dsp::LinearSmoothedValue logTone; // log2 (Hz)
    dsp::LinearSmoothedValue output;  // linear gain
    dsp::LinearSmoothedValue mix;
};

} // namespace ap::fx
