#include "ap/fx/Reverb.h"

#include <algorithm>
#include <cmath>

namespace ap::fx
{
namespace
{
// Freeverb's tunings, in samples at 44.1 kHz.
constexpr std::array<int, Reverb::numCombs> combTuning {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, Reverb::numAllpasses> allpassTuning {556, 441, 341, 225};
constexpr int stereoSpread = 23;
constexpr double tuningRate = 44100.0;

constexpr float inputGain = 0.015f; // the combs sum eight resonators: keep them in range
constexpr float wetGain = 3.0f;     // brings the wet level back near the dry one
constexpr float allpassFeedback = 0.5f;
constexpr float minScale = 0.4f; // size 0 -> 40 % of the tuned lengths
constexpr double smoothingSeconds = 0.02;
constexpr double sizeGlideSeconds = 0.3;

float clampFinite (float value, float low, float high, float fallback) noexcept AP_NONBLOCKING
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}

float scaleFor (float size) noexcept AP_NONBLOCKING
{
    return minScale + (1.0f - minScale) * size;
}
} // namespace

void Reverb::prepare (double sampleRate)
{
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double ratio = rate / tuningRate;

    for (std::size_t ch = 0; ch < channels.size(); ++ch)
    {
        const int spread = ch == 0 ? 0 : stereoSpread;
        for (std::size_t c = 0; c < numCombs; ++c)
        {
            auto& comb = channels[ch].combs[c];
            comb.baseLength = static_cast<double> (combTuning[c] + spread) * ratio;
            comb.buffer.assign (static_cast<std::size_t> (std::ceil (comb.baseLength)) + 4, 0.0f);
        }
        for (std::size_t a = 0; a < numAllpasses; ++a)
            channels[ch].allpasses[a].buffer.assign (
                static_cast<std::size_t> (
                    std::lround (static_cast<double> (allpassTuning[a] + spread) * ratio)),
                0.0f);
    }

    const ReverbSettings defaults;
    size.reset (rate, sizeGlideSeconds);
    feedback.reset (rate, smoothingSeconds);
    damping.reset (rate, smoothingSeconds);
    mix.reset (rate, smoothingSeconds);
    size.setCurrentAndTarget (scaleFor (defaults.size));
    feedback.setCurrentAndTarget (0.7f + 0.28f * defaults.decay);
    damping.setCurrentAndTarget (0.4f * defaults.damping);
    mix.setCurrentAndTarget (defaults.mix);
    reset();
}

void Reverb::reset() noexcept AP_NONBLOCKING
{
    for (auto& channel : channels)
    {
        for (auto& comb : channel.combs)
        {
            std::fill (comb.buffer.begin(), comb.buffer.end(), 0.0f);
            comb.write = 0;
            comb.store = 0.0f;
        }
        for (auto& allpass : channel.allpasses)
        {
            std::fill (allpass.buffer.begin(), allpass.buffer.end(), 0.0f);
            allpass.index = 0;
        }
    }
}

void Reverb::set (const ReverbSettings& settings) noexcept AP_NONBLOCKING
{
    const ReverbSettings defaults;
    size.setTarget (scaleFor (clampFinite (settings.size, 0.0f, 1.0f, defaults.size)));
    feedback.setTarget (0.7f + 0.28f * clampFinite (settings.decay, 0.0f, 1.0f, defaults.decay));
    damping.setTarget (0.4f * clampFinite (settings.damping, 0.0f, 1.0f, defaults.damping));
    mix.setTarget (clampFinite (settings.mix, 0.0f, 1.0f, defaults.mix));
}

float Reverb::readComb (const Comb& comb, double length) noexcept AP_NONBLOCKING
{
    // Linear interpolation: lengths glide with the size.
    const auto bufferSize = comb.buffer.size();
    const double whole = std::floor (length);
    const auto frac = static_cast<float> (length - whole);
    const auto back = static_cast<std::size_t> (whole);
    const float a = comb.buffer[(comb.write + bufferSize - back) % bufferSize];
    const float b = comb.buffer[(comb.write + bufferSize - back - 1) % bufferSize];
    return a + frac * (b - a);
}

void Reverb::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty() || channels[0].combs[0].buffer.empty())
        return;
    const bool stereo = block.numChannels >= 2;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float scale = size.next();
        const float fb = feedback.next();
        const float damp = damping.next();
        const float wet = mix.next();

        const float left = std::isfinite (block.channels[0][i]) ? block.channels[0][i] : 0.0f;
        const float right
            = stereo ? (std::isfinite (block.channels[1][i]) ? block.channels[1][i] : 0.0f) : left;
        const float input = (left + right) * inputGain;

        std::array<float, 2> out {};
        for (std::size_t ch = 0; ch < (stereo ? 2u : 1u); ++ch)
        {
            auto& channel = channels[ch];
            float sum = 0.0f;
            for (auto& comb : channel.combs)
            {
                const float delayed
                    = readComb (comb, std::max (1.0, comb.baseLength * static_cast<double> (scale)));
                comb.store = delayed * (1.0f - damp) + comb.store * damp;
                comb.buffer[comb.write] = input + comb.store * fb;
                comb.write = (comb.write + 1) % comb.buffer.size();
                sum += delayed;
            }
            for (auto& allpass : channel.allpasses)
            {
                const float delayed = allpass.buffer[allpass.index];
                allpass.buffer[allpass.index] = sum + delayed * allpassFeedback;
                allpass.index = (allpass.index + 1) % allpass.buffer.size();
                sum = delayed - sum;
            }
            out[ch] = sum * wetGain;
        }

        block.channels[0][i] = left + wet * (out[0] - left);
        if (stereo)
            block.channels[1][i] = right + wet * (out[1] - right);
    }
}

} // namespace ap::fx
