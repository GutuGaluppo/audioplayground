#include "ap/fx/Delay.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::fx
{
namespace
{
constexpr double dampingHz = 6000.0;
constexpr std::size_t guard = 4; // room for the interpolator's neighbours

float clampFinite (float value, float low, float high, float fallback) noexcept AP_NONBLOCKING
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}
} // namespace

void Delay::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto capacity = static_cast<std::size_t> (
                              std::ceil (rate * static_cast<double> (DelaySettings::maxTimeMs) / 1000.0))
                        + 2 * guard;
    for (auto& line : lines)
        line.buffer.assign (capacity, 0.0f);
    dampingCoefficient = static_cast<float> (std::exp (-2.0 * std::numbers::pi * dampingHz / rate));

    const DelaySettings defaults;
    delaySamples.reset (rate, timeGlideSeconds);
    feedback.reset (rate, 0.02);
    mix.reset (rate, 0.02);
    delaySamples.setCurrentAndTarget (
        static_cast<float> (static_cast<double> (defaults.timeMs) * rate / 1000.0));
    feedback.setCurrentAndTarget (defaults.feedback);
    mix.setCurrentAndTarget (defaults.mix);
    reset();
}

void Delay::reset() noexcept AP_NONBLOCKING
{
    for (auto& line : lines)
    {
        std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        line.write = 0;
        line.damping = 0.0f;
    }
}

void Delay::set (const DelaySettings& settings) noexcept AP_NONBLOCKING
{
    const DelaySettings defaults;
    const float time
        = clampFinite (settings.timeMs, DelaySettings::minTimeMs, DelaySettings::maxTimeMs, defaults.timeMs);
    delaySamples.setTarget (static_cast<float> (static_cast<double> (time) * rate / 1000.0));
    feedback.setTarget (clampFinite (settings.feedback, 0.0f, DelaySettings::maxFeedback, defaults.feedback));
    mix.setTarget (clampFinite (settings.mix, 0.0f, 1.0f, defaults.mix));
}

float Delay::read (const Line& line, double delay) const noexcept AP_NONBLOCKING
{
    // Cubic Hermite between the samples around `delay` samples ago.
    const auto size = line.buffer.size();
    const double whole = std::floor (delay);
    const auto frac = static_cast<float> (delay - whole);
    const auto back = static_cast<std::size_t> (whole);
    const auto at = [&] (std::size_t samplesAgo) noexcept AP_NONBLOCKING
    { return line.buffer[(line.write + size - samplesAgo) % size]; };

    const float xm1 = at (back - 1);
    const float x0 = at (back);
    const float x1 = at (back + 1);
    const float x2 = at (back + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * frac + c2) * frac + c1) * frac + x0;
}

void Delay::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty() || lines[0].buffer.empty())
        return;
    const int channels = std::min (block.numChannels, 2);
    const double longest = static_cast<double> (lines[0].buffer.size() - 2 * guard);

    for (int i = 0; i < block.numSamples; ++i)
    {
        const double delay = std::clamp (static_cast<double> (delaySamples.next()), 2.0, longest);
        const float fb = feedback.next();
        const float wet = mix.next();

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = lines[static_cast<std::size_t> (ch)];
            float& sample = block.channels[ch][i];
            const float input = std::isfinite (sample) ? sample : 0.0f;

            const float echo = read (line, delay);
            line.damping = echo + dampingCoefficient * (line.damping - echo);
            // Only the fed-back part saturates: the first echo is clean, and the repeats stay
            // bounded even at the highest feedback.
            const float repeat = input + std::tanh (fb * line.damping);

            line.buffer[line.write] = repeat;
            line.write = (line.write + 1) % line.buffer.size();
            sample = input + wet * (echo - input);
        }
    }
}

} // namespace ap::fx
