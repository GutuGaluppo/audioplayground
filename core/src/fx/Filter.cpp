#include "ap/fx/Filter.h"

#include <algorithm>
#include <cmath>

namespace ap::fx
{
namespace
{
float clampFinite (float value, float low, float high, float fallback) noexcept AP_NONBLOCKING
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}
} // namespace

void Filter::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& filter : filters)
        filter.prepare (rate);
    logCutoff.reset (rate, smoothingSeconds);
    resonance.reset (rate, smoothingSeconds);
    for (auto& weight : modeWeights)
        weight.reset (rate, smoothingSeconds / 2);

    const FilterSettings defaults;
    logCutoff.setCurrentAndTarget (std::log2 (defaults.cutoffHz));
    resonance.setCurrentAndTarget (defaults.resonance);
    for (std::size_t m = 0; m < modeWeights.size(); ++m)
        modeWeights[m].setCurrentAndTarget (m == 0 ? 1.0f : 0.0f);
    reset();
}

void Filter::reset() noexcept AP_NONBLOCKING
{
    for (auto& filter : filters)
        filter.reset();
}

void Filter::set (const FilterSettings& settings) noexcept AP_NONBLOCKING
{
    const FilterSettings defaults;
    logCutoff.setTarget (std::log2 (clampFinite (settings.cutoffHz, FilterSettings::minCutoffHz,
                                                 FilterSettings::maxCutoffHz, defaults.cutoffHz)));
    resonance.setTarget (clampFinite (settings.resonance, FilterSettings::minResonance,
                                      FilterSettings::maxResonance, defaults.resonance));

    modeWeights[0].setTarget (settings.mode == dsp::FilterMode::lowPass ? 1.0f : 0.0f);
    modeWeights[1].setTarget (settings.mode == dsp::FilterMode::bandPass ? 1.0f : 0.0f);
    modeWeights[2].setTarget (settings.mode == dsp::FilterMode::highPass ? 1.0f : 0.0f);
}

void Filter::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty())
        return;
    const int channels = std::min (block.numChannels, 2);

    for (int i = 0; i < block.numSamples; ++i)
    {
        const bool gliding = logCutoff.isSmoothing() || resonance.isSmoothing();
        const double cutoff = std::exp2 (static_cast<double> (logCutoff.next()));
        const double q = static_cast<double> (resonance.next());
        if (gliding || i == 0)
            for (auto& filter : filters)
                filter.setCutoffAndQ (cutoff, q); // no-op when unchanged

        const double low = static_cast<double> (modeWeights[0].next());
        const double band = static_cast<double> (modeWeights[1].next());
        const double high = static_cast<double> (modeWeights[2].next());

        for (int ch = 0; ch < channels; ++ch)
        {
            float& sample = block.channels[ch][i];
            const auto out = filters[static_cast<std::size_t> (ch)].processAll (sample);
            sample = static_cast<float> (low * out.low + band * out.band + high * out.high);
        }
    }
}

} // namespace ap::fx
