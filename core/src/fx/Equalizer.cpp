#include "ap/fx/Equalizer.h"

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

void Equalizer::prepare (double sampleRate)
{
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const EqualizerSettings defaults;
    for (std::size_t b = 0; b < EqualizerSettings::numBands; ++b)
    {
        auto& s = smoothed[b];
        s.logFrequency.reset (rate, smoothingSeconds);
        s.q.reset (rate, smoothingSeconds);
        s.gainDb.reset (rate, smoothingSeconds);
        s.logFrequency.setCurrentAndTarget (std::log2 (defaults.bands[b].frequencyHz));
        s.q.setCurrentAndTarget (defaults.bands[b].q);
        s.gainDb.setCurrentAndTarget (defaults.bands[b].gainDb);
        for (auto& channel : filters)
            channel[b].prepare (rate);
        apply (b);
    }
}

void Equalizer::reset() noexcept AP_NONBLOCKING
{
    for (auto& channel : filters)
        for (auto& band : channel)
            band.reset();
}

void Equalizer::set (const EqualizerSettings& settings) noexcept AP_NONBLOCKING
{
    const EqualizerSettings defaults;
    for (std::size_t b = 0; b < EqualizerSettings::numBands; ++b)
    {
        const auto& band = settings.bands[b];
        auto& s = smoothed[b];
        s.logFrequency.setTarget (
            std::log2 (clampFinite (band.frequencyHz, EqualizerSettings::minFrequencyHz,
                                    EqualizerSettings::maxFrequencyHz, defaults.bands[b].frequencyHz)));
        s.q.setTarget (
            clampFinite (band.q, EqualizerSettings::minQ, EqualizerSettings::maxQ, defaults.bands[b].q));
        s.gainDb.setTarget (
            clampFinite (band.gainDb, -EqualizerSettings::maxGainDb, EqualizerSettings::maxGainDb, 0.0f));
        if (!s.logFrequency.isSmoothing() && !s.q.isSmoothing() && !s.gainDb.isSmoothing())
            apply (b); // a change too small to ramp still reaches the filters
    }
}

void Equalizer::apply (std::size_t band) noexcept AP_NONBLOCKING
{
    auto& s = smoothed[band];
    const double frequency = std::exp2 (static_cast<double> (s.logFrequency.next()));
    const double q = static_cast<double> (s.q.next());
    const double gain = static_cast<double> (s.gainDb.next());
    for (auto& channel : filters)
        channel[band].set (shapes[band], frequency, q, gain); // no-op when unchanged
}

void Equalizer::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty())
        return;
    const int channels = std::min (block.numChannels, 2);

    for (int i = 0; i < block.numSamples; ++i)
    {
        for (std::size_t b = 0; b < EqualizerSettings::numBands; ++b)
        {
            const auto& s = smoothed[b];
            if (s.logFrequency.isSmoothing() || s.q.isSmoothing() || s.gainDb.isSmoothing())
                apply (b);
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            float& sample = block.channels[ch][i];
            for (auto& band : filters[static_cast<std::size_t> (ch)])
                sample = band.process (sample);
        }
    }
}

} // namespace ap::fx
