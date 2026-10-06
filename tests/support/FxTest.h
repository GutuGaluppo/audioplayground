#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace ap::test
{

using Stereo = std::array<std::vector<float>, 2>;

// Deterministic white noise in [-1, 1).
struct Noise
{
    std::uint32_t state = 12345;
    float next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state >> 8) / static_cast<float> (1u << 23) - 1.0f;
    }
};

inline Stereo noise (std::size_t length, float level, std::uint32_t seed = 12345)
{
    Noise source {seed};
    Stereo audio {std::vector<float> (length), std::vector<float> (length)};
    for (std::size_t i = 0; i < length; ++i)
        for (auto& channel : audio)
            channel[i] = level * source.next();
    return audio;
}

inline std::vector<float> sine (double frequency, double sampleRate, std::size_t length,
                                double amplitude = 0.5)
{
    std::vector<float> out (length);
    for (std::size_t i = 0; i < length; ++i)
        out[i] = static_cast<float> (
            amplitude * std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / sampleRate));
    return out;
}

// Runs a stereo effect (anything with process (core::AudioBlock)) in blocks; before each block,
// configure (blockIndex) may change its settings.
template <typename Effect, typename Configure>
Stereo process (Effect& effect, Stereo audio, int blockSize, Configure configure)
{
    const auto length = static_cast<int> (audio[0].size());
    for (int start = 0, index = 0; start < length; start += blockSize, ++index)
    {
        configure (index);
        const int n = std::min (blockSize, length - start);
        std::array<float*, 2> channels {audio[0].data() + start, audio[1].data() + start};
        effect.process ({channels.data(), 2, n});
    }
    return audio;
}

template <typename Effect> Stereo process (Effect& effect, Stereo audio, int blockSize = 256)
{
    return process (effect, std::move (audio), blockSize, [] (int) {});
}

inline double rms (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (auto i = from; i < to; ++i)
        sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);
    return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, to - from)));
}

inline double toDb (double ratio)
{
    return 20.0 * std::log10 (ratio);
}

// Steady-state gain in dB of a prepared, configured effect at a frequency (second half of 1 s).
template <typename Effect> double gainAtDb (Effect& effect, double frequency, double sampleRate)
{
    const auto length = static_cast<std::size_t> (sampleRate);
    const auto input = sine (frequency, sampleRate, length);
    const auto out = process (effect, Stereo {input, input});
    return toDb (rms (out[0], length / 2, length) / rms (input, length / 2, length));
}

inline float maxStep (const std::vector<float>& signal)
{
    float step = 0.0f;
    for (std::size_t i = 1; i < signal.size(); ++i)
        step = std::max (step, std::abs (signal[i] - signal[i - 1]));
    return step;
}

} // namespace ap::test
