#include "ap/fx/Reverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::fx
{
namespace
{
// Line lengths at size 1, in milliseconds: spread over an octave and mutually prime when rounded,
// so their echoes never line up.
constexpr std::array<double, Reverb::numLines> lineMs {31.3, 37.9, 41.1, 47.3, 53.9, 59.1, 67.3, 73.7};
// Diffuser lengths in milliseconds (left; the right channel's are 7 % longer) and their gain.
constexpr std::array<double, Reverb::numDiffusers> diffuserMs {4.71, 3.59, 12.73, 9.31};
constexpr float diffusion = 0.62f;

constexpr float minScale = 0.4f;      // size 0 -> 40 % of the line lengths
constexpr double modulationMs = 0.25; // depth of the slow length wobble
constexpr double minDecaySeconds = 0.3;
constexpr double maxDecaySeconds = 12.0;
constexpr float wetGain = 0.35f; // at the default decay the tail sits at the dry signal's level
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

// In-place fast Walsh-Hadamard transform, normalised to stay orthogonal.
void hadamard (std::array<float, Reverb::numLines>& x) noexcept AP_NONBLOCKING
{
    for (std::size_t half = 1; half < x.size(); half *= 2)
        for (std::size_t i = 0; i < x.size(); i += 2 * half)
            for (std::size_t j = i; j < i + half; ++j)
            {
                const float a = x[j];
                const float b = x[j + half];
                x[j] = a + b;
                x[j + half] = a - b;
            }
    const float norm = 1.0f / std::sqrt (static_cast<float> (x.size()));
    for (auto& value : x)
        value *= norm;
}
} // namespace

double Reverb::decaySeconds (float value) noexcept
{
    const double d = std::isfinite (value) ? std::clamp (static_cast<double> (value), 0.0, 1.0) : 0.6;
    return minDecaySeconds * std::pow (maxDecaySeconds / minDecaySeconds, d);
}

void Reverb::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    modulationDepth = modulationMs * rate / 1000.0;

    for (std::size_t i = 0; i < numLines; ++i)
    {
        auto& line = lines[i];
        line.baseLength = lineMs[i] * rate / 1000.0;
        // The wobble reaches twice its depth above the base length.
        line.buffer.assign (
            static_cast<std::size_t> (std::ceil (line.baseLength + 2.0 * modulationDepth)) + 4, 0.0f);
        const double step = 2.0 * std::numbers::pi * (0.11 + 0.07 * static_cast<double> (i)) / rate;
        line.stepCos = std::cos (step);
        line.stepSin = std::sin (step);
    }
    for (std::size_t ch = 0; ch < diffusers.size(); ++ch)
        for (std::size_t d = 0; d < numDiffusers; ++d)
            diffusers[ch][d].buffer.assign (static_cast<std::size_t> (std::lround (
                                                diffuserMs[d] * (ch == 0 ? 1.0 : 1.07) * rate / 1000.0)),
                                            0.0f);

    const ReverbSettings defaults;
    size.reset (rate, sizeGlideSeconds);
    decay.reset (rate, smoothingSeconds);
    damping.reset (rate, smoothingSeconds);
    mix.reset (rate, smoothingSeconds);
    size.setCurrentAndTarget (scaleFor (defaults.size));
    decay.setCurrentAndTarget (static_cast<float> (decaySeconds (defaults.decay)));
    damping.setCurrentAndTarget (0.7f * defaults.damping);
    mix.setCurrentAndTarget (defaults.mix);
    reset();
}

void Reverb::reset() noexcept AP_NONBLOCKING
{
    for (std::size_t i = 0; i < numLines; ++i)
    {
        auto& line = lines[i];
        std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        line.write = 0;
        line.lowpass = 0.0f;
        line.cosine = std::cos (static_cast<double> (i) * 0.9); // lines start out of phase
        line.sine = std::sin (static_cast<double> (i) * 0.9);
    }
    untilGainUpdate = 0;
    for (auto& channel : diffusers)
        for (auto& diffuser : channel)
        {
            std::fill (diffuser.buffer.begin(), diffuser.buffer.end(), 0.0f);
            diffuser.index = 0;
        }
}

void Reverb::set (const ReverbSettings& settings) noexcept AP_NONBLOCKING
{
    const ReverbSettings defaults;
    size.setTarget (scaleFor (clampFinite (settings.size, 0.0f, 1.0f, defaults.size)));
    decay.setTarget (static_cast<float> (decaySeconds (settings.decay)));
    damping.setTarget (0.7f * clampFinite (settings.damping, 0.0f, 1.0f, defaults.damping));
    mix.setTarget (clampFinite (settings.mix, 0.0f, 1.0f, defaults.mix));
}

float Reverb::read (const Line& line, double length) noexcept AP_NONBLOCKING
{
    const auto bufferSize = line.buffer.size();
    const double whole = std::floor (length);
    const auto frac = static_cast<float> (length - whole);
    const auto back = static_cast<std::size_t> (whole);
    const float a = line.buffer[(line.write + bufferSize - back) % bufferSize];
    const float b = line.buffer[(line.write + bufferSize - back - 1) % bufferSize];
    return a + frac * (b - a);
}

void Reverb::process (core::AudioBlock block) noexcept AP_NONBLOCKING
{
    if (block.isEmpty() || lines[0].buffer.empty())
        return;
    const bool stereo = block.numChannels >= 2;

    // Left output taps the lines with alternating signs, right with signs in pairs.
    constexpr std::array<float, numLines> leftSigns {1, -1, 1, -1, 1, -1, 1, -1};
    constexpr std::array<float, numLines> rightSigns {1, 1, -1, -1, 1, 1, -1, -1};

    for (int i = 0; i < block.numSamples; ++i)
    {
        const double scale = static_cast<double> (size.next());
        const double rt60 = static_cast<double> (decay.next());
        if (--untilGainUpdate <= 0)
        {
            // 60 dB of loss over rt60 seconds, shared out by each line's length (Jot).
            for (auto& line : lines)
                line.gain
                    = static_cast<float> (std::pow (10.0, -3.0 * line.baseLength * scale / (rt60 * rate)));
            // A longer tail holds more energy; ease its level down a little so long decays do
            // not swamp the mix (a sustained sound stays within a few dB across the range).
            level = wetGain * static_cast<float> (std::min (1.0, std::pow (2.0 / rt60, 0.25)));
            untilGainUpdate = 32;
        }
        const float damp = damping.next();
        const float wet = mix.next();

        const float left = std::isfinite (block.channels[0][i]) ? block.channels[0][i] : 0.0f;
        const float right
            = stereo ? (std::isfinite (block.channels[1][i]) ? block.channels[1][i] : 0.0f) : left;

        // Diffuse each input channel.
        std::array<float, 2> input {left, right};
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (auto& diffuser : diffusers[ch])
            {
                const float delayed = diffuser.buffer[diffuser.index];
                const float in = input[ch] + diffusion * delayed;
                diffuser.buffer[diffuser.index] = in;
                diffuser.index = (diffuser.index + 1) % diffuser.buffer.size();
                input[ch] = delayed - diffusion * in;
            }

        // Read the lines (gain and damping per line), mix them, write them back with the input.
        std::array<float, numLines> state {};
        float outLeft = 0.0f;
        float outRight = 0.0f;
        for (std::size_t l = 0; l < numLines; ++l)
        {
            auto& line = lines[l];
            const double c = line.cosine * line.stepCos - line.sine * line.stepSin;
            line.sine = line.sine * line.stepCos + line.cosine * line.stepSin;
            line.cosine = c;
            const double length
                = std::max (2.0, line.baseLength * scale + modulationDepth * (1.0 + line.sine));
            const float delayed = read (line, length) * line.gain;
            line.lowpass = delayed + damp * (line.lowpass - delayed);
            state[l] = line.lowpass;
            outLeft += leftSigns[l] * state[l];
            outRight += rightSigns[l] * state[l];
        }
        hadamard (state);
        if (untilGainUpdate == 32)
            for (auto& line : lines)
            {
                // Keep the phasor on the unit circle (rounding drifts it slowly).
                const double norm = 1.0 / std::sqrt (line.cosine * line.cosine + line.sine * line.sine);
                line.cosine *= norm;
                line.sine *= norm;
            }
        for (std::size_t l = 0; l < numLines; ++l)
        {
            auto& line = lines[l];
            line.buffer[line.write] = state[l] + (l % 2 == 0 ? input[0] : input[1]);
            line.write = (line.write + 1) % line.buffer.size();
        }

        const float tailLeft = outLeft * level;
        const float tailRight = outRight * level;
        block.channels[0][i] = left + wet * (tailLeft - left);
        if (stereo)
            block.channels[1][i] = right + wet * (tailRight - right);
    }
}

} // namespace ap::fx
