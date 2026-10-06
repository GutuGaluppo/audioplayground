#include "ap/fx/Compressor.h"

#include <algorithm>
#include <cmath>

namespace ap::fx
{
namespace
{
constexpr float silenceDb = -120.0f;

float clampFinite (float value, float low, float high, float fallback) noexcept AP_NONBLOCKING
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}

float coefficient (float milliseconds, double sampleRate) noexcept AP_NONBLOCKING
{
    // One time constant: the gain covers 63 % of a step in this time.
    return static_cast<float> (std::exp (-1000.0 / (static_cast<double> (milliseconds) * sampleRate)));
}
} // namespace

float Compressor::curveDb (float inputDb, float thresholdDb, float curveRatio,
                           float kneeDb) noexcept AP_NONBLOCKING
{
    const float over = inputDb - thresholdDb;
    const float slopeAbove = 1.0f / curveRatio - 1.0f;
    if (2.0f * over < -kneeDb)
        return inputDb;
    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float into = over + kneeDb / 2.0f;
        return inputDb + slopeAbove * into * into / (2.0f * kneeDb);
    }
    return thresholdDb + over / curveRatio;
}

void Compressor::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    makeup.reset (rate, 0.02);
    makeup.setCurrentAndTarget (1.0f);
    set ({});
    reset();
}

void Compressor::reset() noexcept AP_NONBLOCKING
{
    gainDb = 0.0f;
    reduction.store (0.0f, std::memory_order_relaxed);
}

void Compressor::set (const CompressorSettings& settings) noexcept AP_NONBLOCKING
{
    const CompressorSettings defaults;
    threshold = clampFinite (settings.thresholdDb, -60.0f, 0.0f, defaults.thresholdDb);
    ratio = clampFinite (settings.ratio, 1.0f, 20.0f, defaults.ratio);
    knee = clampFinite (settings.kneeDb, 0.0f, 24.0f, defaults.kneeDb);
    attackCoefficient = coefficient (clampFinite (settings.attackMs, 0.1f, 100.0f, defaults.attackMs), rate);
    releaseCoefficient
        = coefficient (clampFinite (settings.releaseMs, 10.0f, 2000.0f, defaults.releaseMs), rate);
    makeup.setTarget (std::pow (10.0f, clampFinite (settings.makeupDb, 0.0f, 24.0f, 0.0f) / 20.0f));
}

void Compressor::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty())
        return;
    const int channels = std::min (block.numChannels, 2);

    for (int i = 0; i < block.numSamples; ++i)
    {
        float level = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float magnitude = std::abs (block.channels[ch][i]);
            if (magnitude > level) // also false for NaN: a broken sample never sticks in the detector
                level = magnitude;
        }

        const float inputDb = level > 1.0e-6f ? 20.0f * std::log10 (level) : silenceDb;
        const float target = curveDb (inputDb, threshold, ratio, knee) - inputDb; // <= 0
        const float a = target < gainDb ? attackCoefficient : releaseCoefficient;
        gainDb = a * gainDb + (1.0f - a) * target;

        const float gain = std::pow (10.0f, gainDb / 20.0f) * makeup.next();
        for (int ch = 0; ch < channels; ++ch)
            block.channels[ch][i] *= gain;
    }
    reduction.store (gainDb, std::memory_order_relaxed);
}

} // namespace ap::fx
