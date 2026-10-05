#include "ap/engine/Engine.h"

#include "ap/core/ScopedNoDenormals.h"

#include <algorithm>
#include <cmath>

namespace ap::engine
{
namespace
{
constexpr double gainRampSeconds = 0.02;
constexpr double frequencyRampSeconds = 0.05;

// Safety ceiling until the master limiter exists: never send more than full scale to the device.
constexpr float outputCeiling = 1.0f;

float decibelsToGain (float db) noexcept AP_NONBLOCKING
{
    return std::pow (10.0f, db / 20.0f);
}
} // namespace

void Engine::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    toneOscillator.prepare (sampleRate);
    toneGain.reset (sampleRate, gainRampSeconds);
    toneFrequency.reset (sampleRate, frequencyRampSeconds);

    // Start silent: a device restart must never begin with a jump to full level.
    toneGain.setCurrentAndTarget (0.0f);
    toneFrequency.setCurrentAndTarget (toneFrequencyHz.load (std::memory_order_relaxed));

    prepared = true;
}

void Engine::releaseResources() noexcept
{
    prepared = false;
}

void Engine::process (core::AudioBlock output) noexcept AP_NONBLOCKING
{
    if (output.isEmpty())
        return;

    const core::ScopedNoDenormals noDenormals;

    const auto clearOutput = [&output]() noexcept AP_NONBLOCKING
    {
        for (int ch = 0; ch < output.numChannels; ++ch)
            std::fill_n (output.channels[ch], output.numSamples, 0.0f);
    };

    if (!prepared)
    {
        clearOutput();
        return;
    }

    const bool enabled = toneEnabled.load (std::memory_order_relaxed);
    const float levelGain = decibelsToGain (toneLevelDb.load (std::memory_order_relaxed));
    toneGain.setTarget (enabled ? levelGain : 0.0f);
    toneFrequency.setTarget (toneFrequencyHz.load (std::memory_order_relaxed));

    if (!toneGain.isSmoothing() && toneGain.getCurrent() == 0.0f)
    {
        toneOscillator.reset();
        clearOutput();
        return;
    }

    float* const first = output.channels[0];
    for (int i = 0; i < output.numSamples; ++i)
    {
        toneOscillator.setFrequency (static_cast<double> (toneFrequency.next()));
        first[i] = toneOscillator.next() * toneGain.next();
    }

    float blockPeak = 0.0f;
    int nonFinite = 0;
    for (int i = 0; i < output.numSamples; ++i)
    {
        float sample = first[i];
        if (!std::isfinite (sample))
        {
            sample = 0.0f;
            ++nonFinite;
        }
        sample = std::clamp (sample, -outputCeiling, outputCeiling);
        first[i] = sample;
        blockPeak = std::max (blockPeak, std::abs (sample));
    }

    for (int ch = 1; ch < output.numChannels; ++ch)
        std::copy_n (first, output.numSamples, output.channels[ch]);

    if (nonFinite > 0)
        nonFiniteSamples.fetch_add (nonFinite, std::memory_order_relaxed);

    updatePeak (blockPeak);
}

void Engine::updatePeak (float blockPeak) noexcept AP_NONBLOCKING
{
    float previous = outputPeak.load (std::memory_order_relaxed);
    while (blockPeak > previous
           && !outputPeak.compare_exchange_weak (previous, blockPeak, std::memory_order_relaxed))
    {
    }
}

void Engine::setTestToneEnabled (bool enabled) noexcept
{
    toneEnabled.store (enabled, std::memory_order_relaxed);
}

void Engine::setTestToneFrequency (float hz) noexcept
{
    if (!std::isfinite (hz))
        return;
    toneFrequencyHz.store (std::clamp (hz, minToneFrequencyHz, maxToneFrequencyHz),
                           std::memory_order_relaxed);
}

void Engine::setTestToneLevelDb (float db) noexcept
{
    if (!std::isfinite (db))
        return;
    toneLevelDb.store (std::clamp (db, minToneLevelDb, maxToneLevelDb), std::memory_order_relaxed);
}

bool Engine::isTestToneEnabled() const noexcept
{
    return toneEnabled.load (std::memory_order_relaxed);
}

float Engine::consumeOutputPeak() noexcept
{
    return outputPeak.exchange (0.0f, std::memory_order_relaxed);
}

int Engine::getNonFiniteSampleCount() const noexcept
{
    return nonFiniteSamples.load (std::memory_order_relaxed);
}

} // namespace ap::engine
