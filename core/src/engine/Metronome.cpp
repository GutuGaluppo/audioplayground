#include "ap/engine/Metronome.h"

#include <algorithm>
#include <cmath>

namespace ap::engine
{

void Metronome::setLevelDb (float db) noexcept
{
    if (std::isfinite (db))
        levelDb.store (std::clamp (db, minLevelDb, maxLevelDb), std::memory_order_relaxed);
}

void Metronome::renderVoice (core::AudioBlock output, int from, int to) noexcept AP_NONBLOCKING
{
    if (!click.isActive())
        return;

    for (int i = from; i < to; ++i)
    {
        const float sample = click.next();
        for (int ch = 0; ch < output.numChannels; ++ch)
            output.channels[ch][i] += sample;
    }
}

void Metronome::render (core::AudioBlock output, int offset, int length, core::Samples startSample,
                        const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING
{
    const bool enabled = enabledFlag.load (std::memory_order_relaxed);
    const bool countingIn = startSample < 0;
    const float gain = std::pow (10.0f, levelDb.load (std::memory_order_relaxed) / 20.0f);

    const auto signature = tempoMap.getTimeSignature();
    const auto ticksPerBeat = signature.ticksPerBeat();
    const auto endSample = startSample + length;

    // First beat whose sample position is >= startSample.
    const auto firstTick = tempoMap.tickAtOrBefore (startSample);
    auto beat = firstTick / ticksPerBeat;
    if (firstTick < 0 && firstTick % ticksPerBeat != 0)
        --beat; // floor division: count-in ticks are negative
    while (tempoMap.ticksToSamples (beat * ticksPerBeat) < startSample)
        ++beat;

    int cursor = offset;
    for (;; ++beat)
    {
        const auto beatSample = tempoMap.ticksToSamples (beat * ticksPerBeat);
        if (beatSample >= endSample)
            break;

        const auto beatOffset = offset + static_cast<int> (beatSample - startSample);
        renderVoice (output, cursor, beatOffset);
        cursor = beatOffset;

        if (enabled || (countingIn && beatSample < 0))
        {
            auto beatInBar = beat % signature.numerator;
            if (beatInBar < 0)
                beatInBar += signature.numerator;
            const bool accent = beatInBar == 0;
            click.trigger (accent ? accentFrequencyHz : beatFrequencyHz, accent ? gain : gain * 0.6f);
        }
    }

    renderVoice (output, cursor, offset + length);
}

void Metronome::renderTail (core::AudioBlock output) noexcept AP_NONBLOCKING
{
    renderVoice (output, 0, output.numSamples);
}

} // namespace ap::engine
