#include "Spectrum.h"
#include "ap/dsp/AdsrEnvelope.h"
#include "ap/dsp/PolyBlepOscillator.h"
#include "ap/dsp/StateVariableFilter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

using namespace ap::dsp;
using ap::test::inharmonicPowerDb;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
constexpr double fs = 48000.0;

std::vector<float> render (Waveform waveform, double frequency, int numSamples)
{
    PolyBlepOscillator osc;
    osc.prepare (fs);
    osc.setWaveform (waveform);
    osc.setFrequency (frequency);
    std::vector<float> out (static_cast<std::size_t> (numSamples));
    for (auto& s : out)
        s = osc.next();
    return out;
}

std::vector<double> naive (Waveform waveform, double frequency, int numSamples)
{
    std::vector<double> out (static_cast<std::size_t> (numSamples));
    for (int n = 0; n < numSamples; ++n)
    {
        const double t = std::fmod (frequency * n / fs, 1.0);
        out[static_cast<std::size_t> (n)] = waveform == Waveform::saw    ? 2.0 * t - 1.0
                                          : waveform == Waveform::square ? (t < 0.5 ? 1.0 : -1.0)
                                                                         : 1.0 - 4.0 * std::abs (t - 0.5);
    }
    return out;
}

double sineGainAt (StateVariableFilter& filter, double frequency)
{
    filter.reset();
    double peak = 0.0;
    const int n = static_cast<int> (fs);
    for (int i = 0; i < n; ++i)
    {
        const float y
            = filter.process (static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * i / fs)));
        if (i > n / 2)
            peak = std::max (peak, static_cast<double> (std::abs (y)));
    }
    return peak;
}
} // namespace

TEST_CASE ("Oscillator aliasing is far below a naive oscillator", "[dsp][oscillator][quality]")
{
    constexpr int n = 16384;
    const double frequency = GENERATE (440.0, 1234.5, 3517.0, 7919.0);
    CAPTURE (frequency);

    // Measured: saw/square improve 13-29 dB, triangle 10-28 dB; thresholds leave a small margin.
    for (const auto waveform : {Waveform::saw, Waveform::square})
    {
        CAPTURE (static_cast<int> (waveform));
        CHECK (inharmonicPowerDb (render (waveform, frequency, n), frequency, fs)
               < inharmonicPowerDb (naive (waveform, frequency, n), frequency, fs) - 12.0);
    }

    CHECK (inharmonicPowerDb (render (Waveform::triangle, frequency, n), frequency, fs)
           < inharmonicPowerDb (naive (Waveform::triangle, frequency, n), frequency, fs) - 8.0);

    // Limited by the analysis window's side lobes (~-92 dB), not by the oscillator.
    CHECK (inharmonicPowerDb (render (Waveform::sine, frequency, n), frequency, fs) < -85.0);
}

TEST_CASE ("Oscillator output is bounded, centred and at the right pitch", "[dsp][oscillator]")
{
    const auto waveform = GENERATE (Waveform::sine, Waveform::triangle, Waveform::saw, Waveform::square);
    CAPTURE (static_cast<int> (waveform));

    const auto samples = render (waveform, 1000.0, static_cast<int> (fs));
    double sum = 0.0;
    float peak = 0.0f;
    int crossings = 0;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        sum += static_cast<double> (samples[i]);
        peak = std::max (peak, std::abs (samples[i]));
        if (i > 0 && samples[i - 1] < 0.0f && samples[i] >= 0.0f)
            ++crossings;
    }

    CHECK (peak <= 1.1f);                                                 // PolyBLEP overshoot stays small
    CHECK (std::abs (sum / static_cast<double> (samples.size())) < 0.01); // no DC
    CHECK (std::abs (crossings - 1000) <= 1);
}

TEST_CASE ("Oscillator clamps frequencies near Nyquist without blowing up", "[dsp][oscillator]")
{
    for (const double frequency : {0.0, 23999.0, 1.0e9, -50.0})
    {
        const auto samples = render (Waveform::saw, frequency, 1000);
        for (const float s : samples)
            REQUIRE ((std::isfinite (s) && std::abs (s) <= 2.0f));
    }
}

TEST_CASE ("State variable filter has the textbook response", "[dsp][filter]")
{
    StateVariableFilter filter;
    filter.prepare (fs);

    for (const double cutoff : {100.0, 1000.0, 10000.0})
    {
        CAPTURE (cutoff);
        filter.setCutoffAndQ (cutoff, std::numbers::sqrt2 / 2.0);

        filter.setMode (FilterMode::lowPass);
        CHECK_THAT (sineGainAt (filter, cutoff), WithinRel (std::numbers::sqrt2 / 2.0, 0.01)); // -3 dB
        CHECK (sineGainAt (filter, cutoff * 4.0 < 20000.0 ? cutoff * 4.0 : 20000.0) < 0.1);

        filter.setMode (FilterMode::highPass);
        CHECK_THAT (sineGainAt (filter, cutoff), WithinRel (std::numbers::sqrt2 / 2.0, 0.01));
        CHECK (sineGainAt (filter, cutoff / 4.0) < 0.1);

        filter.setMode (FilterMode::bandPass);
        CHECK_THAT (sineGainAt (filter, cutoff),
                    WithinRel (std::numbers::sqrt2 / 2.0, 0.01)); // = 1/Q at centre
    }
}

TEST_CASE ("Low-pass passes DC and resonance peaks at Q", "[dsp][filter]")
{
    StateVariableFilter filter;
    filter.prepare (fs);
    filter.setMode (FilterMode::lowPass);

    filter.setCutoffAndQ (1000.0, 0.7071);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
        y = filter.process (1.0f);
    CHECK_THAT (static_cast<double> (y), WithinAbs (1.0, 1.0e-4));

    filter.setCutoffAndQ (1000.0, 10.0);
    CHECK_THAT (sineGainAt (filter, 1000.0), WithinRel (10.0, 0.03));
}

TEST_CASE ("State variable filter stays stable under extreme per-sample modulation", "[dsp][filter]")
{
    StateVariableFilter filter;
    filter.prepare (fs);
    std::mt19937 random (7);
    std::uniform_real_distribution<double> unit (0.0, 1.0);

    for (const auto mode : {FilterMode::lowPass, FilterMode::highPass, FilterMode::bandPass})
    {
        filter.setMode (mode);
        filter.reset();
        float peak = 0.0f;
        for (int i = 0; i < 10 * 48000; ++i)
        {
            filter.setCutoffAndQ (20.0 * std::pow (1000.0, unit (random)), 0.5 + 19.5 * unit (random));
            const float y = filter.process (static_cast<float> (unit (random) * 2.0 - 1.0));
            REQUIRE (std::isfinite (y));
            peak = std::max (peak, std::abs (y));
        }
        CHECK (peak < 100.0f);
    }

    filter.setCutoffAndQ (std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()); // ignored safely
    CHECK (std::isfinite (filter.process (1.0f)));
}

TEST_CASE ("ADSR follows its segments", "[dsp][envelope]")
{
    AdsrEnvelope env;
    env.prepare (fs);
    env.setSettings ({0.01f, 0.1f, 0.5f, 0.2f});

    CHECK_FALSE (env.isActive());
    CHECK (env.next() == 0.0f);

    env.noteOn();
    int samplesToPeak = 0;
    while (env.getStage() == AdsrEnvelope::Stage::attack)
    {
        (void)env.next();
        ++samplesToPeak;
    }
    CHECK (std::abs (samplesToPeak - 480) <= 1); // 10 ms
    CHECK (env.getLevel() == 1.0f);

    for (int i = 0; i < static_cast<int> (0.2 * fs); ++i)
        (void)env.next();
    CHECK (env.getStage() == AdsrEnvelope::Stage::sustain);
    CHECK (env.getLevel() == 0.5f);

    env.noteOff();
    int releaseSamples = 0;
    while (env.isActive())
    {
        (void)env.next();
        ++releaseSamples;
    }
    // Release time is defined from full level to silence (-80 dB); from sustain 0.5 it is shorter.
    const double expected = 0.2 * fs * std::log (0.5 / static_cast<double> (AdsrEnvelope::silence))
                          / std::log (1.0 / static_cast<double> (AdsrEnvelope::silence));
    CHECK (std::abs (releaseSamples - static_cast<int> (expected)) <= 2);
    CHECK (env.next() == 0.0f);
}

TEST_CASE ("ADSR never jumps on retrigger or early release", "[dsp][envelope]")
{
    AdsrEnvelope env;
    env.prepare (fs);
    env.setSettings ({0.05f, 0.1f, 0.6f, 0.1f});

    const auto maxStepOver = [&env] (int samples)
    {
        float previous = env.getLevel();
        float maxStep = 0.0f;
        for (int i = 0; i < samples; ++i)
        {
            const float level = env.next();
            maxStep = std::max (maxStep, std::abs (level - previous));
            previous = level;
        }
        return maxStep;
    };

    // A click is a discontinuity; smooth segments move far less than this per sample.
    constexpr float maxSmoothStep = 0.002f;
    env.noteOn();
    CHECK (maxStepOver (1000) < maxSmoothStep); // mid-attack
    env.noteOff();                              // release from mid-attack
    CHECK (maxStepOver (200) < maxSmoothStep);
    env.noteOn(); // retrigger from mid-release
    CHECK (maxStepOver (5000) < maxSmoothStep);
}

TEST_CASE ("ADSR enforces a minimum segment time and sanitises settings", "[dsp][envelope]")
{
    AdsrEnvelope env;
    env.prepare (fs);
    env.setSettings ({0.0f, NAN, 7.0f, -1.0f});

    env.noteOn();
    int samples = 0;
    while (env.getStage() == AdsrEnvelope::Stage::attack)
    {
        (void)env.next();
        ++samples;
    }
    CHECK (samples >= 47); // 1 ms floor at 48 kHz, never an instant jump
    for (int i = 0; i < 1000; ++i)
        REQUIRE (std::isfinite (env.next()));
    CHECK (env.getLevel() <= 1.0f); // sustain clamped to 1
}
