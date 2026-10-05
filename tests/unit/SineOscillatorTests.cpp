#include "ap/dsp/SineOscillator.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

TEST_CASE ("SineOscillator produces the requested frequency", "[dsp][oscillator]")
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 1000.0;

    ap::dsp::SineOscillator osc;
    osc.prepare (sampleRate);
    osc.setFrequency (frequency);

    int upwardCrossings = 0;
    float previous = osc.next();
    float peak = std::abs (previous);
    for (int i = 1; i < static_cast<int> (sampleRate); ++i)
    {
        const float current = osc.next();
        if (previous < 0.0f && current >= 0.0f)
            ++upwardCrossings;
        peak = std::max (peak, std::abs (current));
        previous = current;
    }

    CHECK (std::abs (upwardCrossings - static_cast<int> (frequency)) <= 1);
    CHECK (peak <= 1.0f);
    CHECK (peak > 0.999f);
}

TEST_CASE ("SineOscillator starts at zero phase after reset", "[dsp][oscillator]")
{
    ap::dsp::SineOscillator osc;
    osc.prepare (44100.0);
    osc.setFrequency (440.0);
    for (int i = 0; i < 100; ++i)
        (void)osc.next();

    osc.reset();
    CHECK (osc.next() == 0.0f);
}
