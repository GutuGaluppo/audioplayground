#include "ap/fx/Distortion.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::fx
{
namespace
{
constexpr float bias = 0.15f;           // asymmetry: tanh (x + bias) - tanh (bias)
constexpr float referenceLevel = 0.25f; // -12 dBFS keeps its level whatever the drive
constexpr double smoothingSeconds = 0.02;

float clampFinite (float value, float low, float high, float fallback) noexcept AP_NONBLOCKING
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}

float decibelsToGain (float db) noexcept AP_NONBLOCKING
{
    return std::pow (10.0f, db / 20.0f);
}

float shape (float x) noexcept AP_NONBLOCKING
{
    return std::tanh (x + bias) - std::tanh (bias);
}

float toneHz (float tone) noexcept AP_NONBLOCKING
{
    return 800.0f * std::pow (25.0f, tone); // 800 Hz .. 20 kHz
}
} // namespace

void Distortion::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    dcCoefficient = std::exp (-2.0 * std::numbers::pi * 10.0 / rate); // 10 Hz high-pass
    for (auto& channel : channels)
        channel.tone.prepare (rate);

    const DistortionSettings defaults;
    drive.reset (rate, smoothingSeconds);
    logTone.reset (rate, smoothingSeconds);
    output.reset (rate, smoothingSeconds);
    mix.reset (rate, smoothingSeconds);
    drive.setCurrentAndTarget (decibelsToGain (defaults.driveDb));
    logTone.setCurrentAndTarget (std::log2 (toneHz (defaults.tone)));
    output.setCurrentAndTarget (decibelsToGain (defaults.outputDb));
    mix.setCurrentAndTarget (defaults.mix);
    reset();
}

void Distortion::reset() noexcept AP_NONBLOCKING
{
    for (auto& channel : channels)
    {
        channel.oversampler.reset();
        channel.tone.reset();
        channel.dcIn = 0.0;
        channel.dcOut = 0.0;
        channel.dry.fill (0.0f);
        channel.dryPosition = 0;
    }
}

void Distortion::set (const DistortionSettings& settings) noexcept AP_NONBLOCKING
{
    const DistortionSettings defaults;
    drive.setTarget (decibelsToGain (clampFinite (settings.driveDb, 0.0f, 36.0f, defaults.driveDb)));
    logTone.setTarget (std::log2 (toneHz (clampFinite (settings.tone, 0.0f, 1.0f, defaults.tone))));
    output.setTarget (decibelsToGain (clampFinite (settings.outputDb, -24.0f, 12.0f, defaults.outputDb)));
    mix.setTarget (clampFinite (settings.mix, 0.0f, 1.0f, defaults.mix));
}

void Distortion::setImmediately (const DistortionSettings& settings) noexcept AP_NONBLOCKING
{
    set (settings);
    drive.setCurrentAndTarget (drive.getTarget());
    logTone.setCurrentAndTarget (logTone.getTarget());
    output.setCurrentAndTarget (output.getTarget());
    mix.setCurrentAndTarget (mix.getTarget());
}

void Distortion::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty())
        return;
    const int numChannels = std::min (block.numChannels, 2);
    std::array<float, dsp::Oversampler4x::factor> up {};

    // Fully dry (e.g. switched off in a track's chain): only the latency-matching delay runs.
    if (!mix.isSmoothing() && mix.getCurrent() == 0.0f)
    {
        for (int i = 0; i < block.numSamples; ++i)
        {
            (void)drive.next();
            (void)logTone.next();
            const float out = output.next();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& c = channels[static_cast<std::size_t> (ch)];
                float& sample = block.channels[ch][i];
                c.dry[c.dryPosition] = std::isfinite (sample) ? sample : 0.0f;
                c.dryPosition = (c.dryPosition + 1) % dryLength;
                sample = out * c.dry[c.dryPosition];
            }
        }
        return;
    }

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float g = drive.next();
        const float compensation = referenceLevel / shape (g * referenceLevel);
        const double cutoff = std::exp2 (static_cast<double> (logTone.next()));
        const float out = output.next();
        const float wet = mix.next();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& c = channels[static_cast<std::size_t> (ch)];
            float& sample = block.channels[ch][i];
            const float input = std::isfinite (sample) ? sample : 0.0f;

            // Shape at 4x.
            c.oversampler.upsample (input, up);
            for (auto& x : up)
                x = shape (g * x);
            const float shaped = c.oversampler.downsample (up);

            // Remove the asymmetry's DC offset, then colour.
            const double y = static_cast<double> (shaped) - c.dcIn + dcCoefficient * c.dcOut;
            c.dcIn = static_cast<double> (shaped);
            c.dcOut = y;
            c.tone.setCutoffAndQ (cutoff, 0.7071);
            const float coloured = c.tone.process (static_cast<float> (y)) * compensation;

            // The dry path is delayed to line up with the wet one.
            c.dry[c.dryPosition] = input;
            c.dryPosition = (c.dryPosition + 1) % dryLength;
            const float dry = c.dry[c.dryPosition];

            sample = out * (dry + wet * (coloured - dry));
        }
    }
}

} // namespace ap::fx
