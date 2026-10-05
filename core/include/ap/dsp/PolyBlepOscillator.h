#pragma once

#include "ap/core/RealtimeSafety.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace ap::dsp
{

enum class Waveform : std::uint8_t
{
    sine,
    triangle,
    saw,
    square
};

// Band-limited oscillator. Naive saw/square/triangle waves alias badly (audible as inharmonic
// "whistles" on high notes); PolyBLEP corrects each discontinuity in the waveform and PolyBLAMP
// each corner of the triangle, which removes most aliasing at very low CPU cost.
//
// Phase is double precision in [0, 1).
class PolyBlepOscillator
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        reset();
    }

    void reset (double startPhase = 0.0) noexcept AP_NONBLOCKING
    {
        phase = startPhase - std::floor (startPhase);
    }

    void setWaveform (Waveform newWaveform) noexcept AP_NONBLOCKING { waveform = newWaveform; }

    // Frequencies are clamped to just below Nyquist so the corrections stay valid.
    void setFrequency (double hz) noexcept AP_NONBLOCKING
    {
        increment = std::clamp (hz / sampleRate, 0.0, 0.49);
    }

    [[nodiscard]] float next() noexcept AP_NONBLOCKING
    {
        const double t = phase;
        const double dt = increment;
        double value = 0.0;

        switch (waveform)
        {
        case Waveform::sine:
            value = std::sin (2.0 * std::numbers::pi * t);
            break;

        case Waveform::saw:
            value = 2.0 * t - 1.0 - polyBlep (t, dt);
            break;

        case Waveform::square:
            value = (t < 0.5 ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (wrap (t + 0.5), dt);
            break;

        case Waveform::triangle:
            // Corners at t = 0 (valley) and t = 0.5 (peak). The 4 * dt scale was verified
            // against the measured alias floor (tests: "Oscillator aliasing").
            value = 1.0 - 4.0 * std::abs (t - 0.5);
            value += 4.0 * dt * (polyBlamp (t, dt) - polyBlamp (wrap (t + 0.5), dt));
            break;
        }

        phase = wrap (phase + dt);
        return static_cast<float> (value);
    }

    [[nodiscard]] double getPhase() const noexcept AP_NONBLOCKING { return phase; }

private:
    static double wrap (double x) noexcept AP_NONBLOCKING { return x - std::floor (x); }

    // Two-sample polynomial approximation of the band-limited step residual.
    static double polyBlep (double t, double dt) noexcept AP_NONBLOCKING
    {
        if (dt <= 0.0)
            return 0.0;
        if (t < dt)
        {
            const double x = t / dt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }

    // Integrated PolyBLEP: band-limited ramp residual, for slope discontinuities.
    static double polyBlamp (double t, double dt) noexcept AP_NONBLOCKING
    {
        if (dt <= 0.0)
            return 0.0;
        if (t < dt)
        {
            const double x = t / dt - 1.0;
            return -x * x * x / 3.0;
        }
        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt + 1.0;
            return x * x * x / 3.0;
        }
        return 0.0;
    }

    double sampleRate = 48000.0;
    double phase = 0.0;
    double increment = 0.0;
    Waveform waveform = Waveform::saw;
};

} // namespace ap::dsp
