#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/dsp/EqBand.h"
#include "ap/dsp/LinearSmoothedValue.h"

#include <array>

namespace ap::fx
{

struct EqualizerSettings
{
    static constexpr std::size_t numBands = 3; // the architecture allows more later (guide §12.2)
    static constexpr float minFrequencyHz = 20.0f;
    static constexpr float maxFrequencyHz = 20000.0f;
    static constexpr float minQ = 0.3f;
    static constexpr float maxQ = 10.0f;
    static constexpr float maxGainDb = 18.0f;

    struct Band
    {
        float frequencyHz = 1000.0f;
        float q = 0.7071f;
        float gainDb = 0.0f;

        bool operator== (const Band&) const = default;
    };

    // Low shelf, mid bell, high shelf.
    std::array<Band, numBands> bands {Band {120.0f, 0.7071f, 0.0f}, Band {1000.0f, 1.0f, 0.0f},
                                      Band {8000.0f, 0.7071f, 0.0f}};

    bool operator== (const EqualizerSettings&) const = default;
};

// Three-band parametric equaliser (guide §12.2, Task 019): low shelf, mid bell and high shelf,
// stereo. Frequency (log domain), Q and gain glide over 20 ms, so moving a band never clicks;
// with every gain at 0 dB the output equals the input. Settings are clamped to safe ranges.
class Equalizer
{
public:
    static constexpr double smoothingSeconds = 0.02;

    void prepare (double sampleRate);
    void reset() noexcept AP_NONBLOCKING;
    void set (const EqualizerSettings& settings) noexcept AP_NONBLOCKING;

    // In place. Mono blocks use the left channel's filters; channels beyond two pass through.
    void process (core::AudioBlock block) noexcept AP_NONBLOCKING;

private:
    struct Smoothed
    {
        dsp::LinearSmoothedValue logFrequency;
        dsp::LinearSmoothedValue q;
        dsp::LinearSmoothedValue gainDb;
    };

    void apply (std::size_t band) noexcept AP_NONBLOCKING;

    static constexpr std::array<dsp::EqShape, EqualizerSettings::numBands> shapes {
        dsp::EqShape::lowShelf, dsp::EqShape::bell, dsp::EqShape::highShelf};

    std::array<Smoothed, EqualizerSettings::numBands> smoothed;
    std::array<std::array<dsp::EqBand, EqualizerSettings::numBands>, 2> filters; // [channel][band]
};

} // namespace ap::fx
