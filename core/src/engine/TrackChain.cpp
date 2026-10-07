#include "ap/engine/TrackChain.h"

#include <algorithm>
#include <cmath>

namespace ap::engine
{
namespace
{
using params::EffectKind;

template <typename Param> float value (const model::EffectState& state, Param param) noexcept AP_NONBLOCKING
{
    return state.values[static_cast<std::size_t> (param)];
}

constexpr std::size_t index (EffectKind kind) noexcept
{
    return static_cast<std::size_t> (kind);
}
} // namespace

fx::EqualizerSettings eqSettings (const model::EffectState& s) noexcept
{
    using P = params::EqParam;
    fx::EqualizerSettings settings;
    settings.bands[0] = {value (s, P::lowFreq), 0.7071f, value (s, P::lowGain)};
    settings.bands[1] = {value (s, P::midFreq), value (s, P::midQ), value (s, P::midGain)};
    settings.bands[2] = {value (s, P::highFreq), 0.7071f, value (s, P::highGain)};
    return settings;
}

fx::CompressorSettings compressorSettings (const model::EffectState& s) noexcept
{
    using P = params::CompressorParam;
    return {value (s, P::threshold), value (s, P::ratio),  value (s, P::attack),
            value (s, P::release),   value (s, P::makeup), 6.0f};
}

fx::FilterSettings filterSettings (const model::EffectState& s) noexcept
{
    using P = params::FilterParam;
    const auto mode = static_cast<int> (std::lround (value (s, P::mode)));
    return {mode == 1   ? dsp::FilterMode::highPass
            : mode == 2 ? dsp::FilterMode::bandPass
                        : dsp::FilterMode::lowPass,
            value (s, P::cutoff), value (s, P::resonance)};
}

fx::DistortionSettings distortionSettings (const model::EffectState& s) noexcept
{
    using P = params::DistortionParam;
    return {value (s, P::drive), value (s, P::tone) / 100.0f, value (s, P::output),
            value (s, P::mix) / 100.0f};
}

fx::DelaySettings delaySettings (const model::EffectState& s, double tempoBpm) noexcept
{
    using P = params::DelayParam;
    float timeMs = value (s, P::time);
    const auto sync = static_cast<std::size_t> (std::lround (value (s, P::sync)));
    if (sync > 0 && sync < delaySyncBeats.size() && tempoBpm > 0.0)
    {
        const auto synced
            = static_cast<float> (static_cast<double> (delaySyncBeats[sync]) * 60000.0 / tempoBpm);
        timeMs = std::clamp (synced, fx::DelaySettings::minTimeMs, fx::DelaySettings::maxTimeMs);
    }
    return {timeMs, value (s, P::feedback) / 100.0f, value (s, P::mix) / 100.0f};
}

fx::ReverbSettings reverbSettings (const model::EffectState& s) noexcept
{
    using P = params::ReverbParam;
    return {value (s, P::size) / 100.0f, value (s, P::decay) / 100.0f, value (s, P::damping) / 100.0f,
            value (s, P::mix) / 100.0f};
}

void TrackChain::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = std::max (1, maxBlockSize);
    eq.prepare (rate);
    compressor.prepare (rate);
    filter.prepare (rate);
    distortion.prepare (rate);
    delay.prepare (rate);
    reverb.prepare (rate);
    for (auto& channel : scratch)
        channel.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    for (auto& weight : weights)
    {
        weight.reset (rate, 0.02);
        weight.setCurrentAndTarget (0.0f);
    }
    enabled.fill (false);
    needsReset.fill (false);

    // Off until set(): only the distortion's dry delay runs.
    auto off = fx::DistortionSettings {};
    off.mix = 0.0f;
    off.outputDb = 0.0f;
    distortion.setImmediately (off);
    distortion.reset();
}

void TrackChain::set (const model::TrackEffects& effects, double tempoBpm) noexcept AP_NONBLOCKING
{
    for (std::size_t e = 0; e < effects.size(); ++e)
    {
        if (effects[e].enabled && !enabled[e] && needsReset[e])
        {
            // Coming back after being fully off: start clean.
            switch (static_cast<EffectKind> (e))
            {
            case EffectKind::eq:
                eq.reset();
                break;
            case EffectKind::compressor:
                compressor.reset();
                break;
            case EffectKind::filter:
                filter.reset();
                break;
            case EffectKind::distortion:
                break; // it never stops
            case EffectKind::delay:
                delay.reset();
                break;
            case EffectKind::reverb:
                reverb.reset();
                break;
            }
            needsReset[e] = false;
        }
        enabled[e] = effects[e].enabled;
        weights[e].setTarget (enabled[e] ? 1.0f : 0.0f);
    }

    eq.set (eqSettings (effects[index (EffectKind::eq)]));
    compressor.set (compressorSettings (effects[index (EffectKind::compressor)]));
    filter.set (filterSettings (effects[index (EffectKind::filter)]));
    delay.set (delaySettings (effects[index (EffectKind::delay)], tempoBpm));
    reverb.set (reverbSettings (effects[index (EffectKind::reverb)]));

    // The distortion crossfades through its own (latency-aligned) mix and output.
    auto drive = distortionSettings (effects[index (EffectKind::distortion)]);
    if (!enabled[index (EffectKind::distortion)])
    {
        drive.mix = 0.0f;
        drive.outputDb = 0.0f;
    }
    distortion.set (drive);
}

template <typename Effect>
void TrackChain::stage (std::size_t e, Effect& effect, core::AudioBlock block) noexcept AP_NONBLOCKING
{
    auto& weight = weights[e];
    if (!weight.isSmoothing() && weight.getCurrent() == 0.0f)
    {
        needsReset[e] = true;
        return;
    }

    const int channels = std::min (block.numChannels, 2);
    std::array<float*, 2> wet {scratch[0].data(), scratch[1].data()};
    for (int ch = 0; ch < channels; ++ch)
        std::copy_n (block.channels[ch], block.numSamples, wet[static_cast<std::size_t> (ch)]);
    effect.process ({wet.data(), channels, block.numSamples});

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float w = weight.next();
        for (int ch = 0; ch < channels; ++ch)
        {
            float& sample = block.channels[ch][i];
            sample += w * (wet[static_cast<std::size_t> (ch)][i] - sample);
        }
    }
}

void TrackChain::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty() || block.numSamples > maxBlock)
        return;

    stage (index (EffectKind::eq), eq, block);
    stage (index (EffectKind::compressor), compressor, block);
    stage (index (EffectKind::filter), filter, block);
    distortion.process (block);
    stage (index (EffectKind::delay), delay, block);
    stage (index (EffectKind::reverb), reverb, block);
}

} // namespace ap::engine
