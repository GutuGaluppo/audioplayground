#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/LinearSmoothedValue.h"
#include "ap/fx/Compressor.h"
#include "ap/fx/Delay.h"
#include "ap/fx/Distortion.h"
#include "ap/fx/Equalizer.h"
#include "ap/fx/Filter.h"
#include "ap/fx/Reverb.h"
#include "ap/model/Project.h"

#include <array>
#include <vector>

namespace ap::engine
{

// A track's fixed effect chain (ADR-009): EQ -> Compressor -> Filter -> Distortion -> Delay ->
// Reverb, in the order of params::EffectKind.
//
// Switching an effect on or off crossfades over 20 ms. An effect that is off costs nothing and is
// reset when it comes back, so no stale echo reappears. The distortion always runs its dry
// delay, so the chain's latency is constant (latency) whatever is switched on: every other
// signal path in the engine is delayed by the same amount, and nothing ever moves in time.
//
// Threading: prepare() allocates (not real-time; while the audio callback is stopped). set() and
// process() run on the audio thread.
class TrackChain
{
public:
    [[nodiscard]] static constexpr int latency() noexcept { return fx::Distortion::latency(); }

    void prepare (double sampleRate, int maxBlockSize);
    [[nodiscard]] double getSampleRate() const noexcept { return rate; }

    void set (const model::TrackEffects& effects) noexcept AP_NONBLOCKING;

    // In place, stereo (or mono). numSamples must not exceed the prepared block size.
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

private:
    template <typename Effect>
    void stage (std::size_t index, Effect& effect, core::AudioBlock block) noexcept AP_NONBLOCKING;

    double rate = 0.0;
    int maxBlock = 0;
    fx::Equalizer eq;
    fx::Compressor compressor;
    fx::Filter filter;
    fx::Distortion distortion;
    fx::Delay delay;
    fx::Reverb reverb;

    std::array<bool, model::TrackEffects {}.size()> enabled {};
    std::array<bool, model::TrackEffects {}.size()> needsReset {};
    std::array<dsp::LinearSmoothedValue, model::TrackEffects {}.size()> weights;
    std::array<std::vector<float>, 2> scratch;
};

// The settings each processor takes, from a track's stored values (percentages become 0..1).
[[nodiscard]] fx::EqualizerSettings eqSettings (const model::EffectState& state) noexcept;
[[nodiscard]] fx::CompressorSettings compressorSettings (const model::EffectState& state) noexcept;
[[nodiscard]] fx::FilterSettings filterSettings (const model::EffectState& state) noexcept;
[[nodiscard]] fx::DistortionSettings distortionSettings (const model::EffectState& state) noexcept;
[[nodiscard]] fx::DelaySettings delaySettings (const model::EffectState& state) noexcept;
[[nodiscard]] fx::ReverbSettings reverbSettings (const model::EffectState& state) noexcept;

} // namespace ap::engine
