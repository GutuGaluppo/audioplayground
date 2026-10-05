#include "ap/instruments/FactoryKit.h"

#include "ap/dsp/StateVariableFilter.h"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace ap::instruments
{
namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;

// Deterministic white noise (xorshift), so the kit renders identically everywhere.
class Noise
{
public:
    explicit Noise (std::uint32_t seed)
        : state (seed)
    {
    }

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<double> (state) / 2147483648.0 - 1.0);
    }

private:
    std::uint32_t state;
};

struct Builder
{
    double rate;
    std::vector<float> samples;

    Builder (double sampleRate, double seconds)
        : rate (sampleRate)
        , samples (static_cast<std::size_t> (seconds * sampleRate), 0.0f)
    {
    }

    [[nodiscard]] double time (std::size_t n) const { return static_cast<double> (n) / rate; }

    // Decaying sine whose pitch glides exponentially from startHz to endHz.
    void sweptSine (double startHz, double endHz, double glideSeconds, double decaySeconds, double level)
    {
        double phase = 0.0;
        for (std::size_t n = 0; n < samples.size(); ++n)
        {
            const double t = time (n);
            const double hz = endHz + (startHz - endHz) * std::exp (-t / glideSeconds);
            phase += twoPi * hz / rate;
            samples[n] += static_cast<float> (level * std::sin (phase) * std::exp (-t / decaySeconds));
        }
    }

    // Filtered noise burst with an exponential decay.
    void noise (std::uint32_t seed, dsp::FilterMode mode, double cutoff, double q, double decaySeconds,
                double level, double attackSeconds = 0.0)
    {
        Noise source (seed);
        dsp::StateVariableFilter filter;
        filter.prepare (rate);
        filter.setMode (mode);
        filter.setCutoffAndQ (cutoff, q);
        for (std::size_t n = 0; n < samples.size(); ++n)
        {
            const double t = time (n);
            const double attack = attackSeconds > 0.0 ? std::min (1.0, t / attackSeconds) : 1.0;
            const double envelope = attack * std::exp (-t / decaySeconds);
            samples[n] += static_cast<float> (level * envelope) * filter.process (source.next());
        }
    }

    void squareTone (double hz, double decaySeconds, double level)
    {
        for (std::size_t n = 0; n < samples.size(); ++n)
        {
            const double t = time (n);
            const double square = std::sin (twoPi * hz * t) >= 0.0 ? 1.0 : -1.0;
            samples[n] += static_cast<float> (level * square * std::exp (-t / decaySeconds));
        }
    }

    std::unique_ptr<SampleBuffer> finish()
    {
        // Short fade at the very end so truncation never clicks, and gentle soft clip.
        const auto fade = std::min<std::size_t> (samples.size(), static_cast<std::size_t> (0.005 * rate));
        for (std::size_t i = 0; i < fade; ++i)
            samples[samples.size() - 1 - i] *= static_cast<float> (i) / static_cast<float> (fade);
        for (auto& s : samples)
            s = std::tanh (s);

        auto buffer = std::make_unique<SampleBuffer>();
        buffer->sampleRate = rate;
        buffer->channels.push_back (std::move (samples));
        return buffer;
    }
};
} // namespace

std::unique_ptr<SampleBuffer> makeFactorySound (std::size_t pad, double sampleRate)
{
    using dsp::FilterMode;
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    switch (pad)
    {
    case 0: // Kick
    {
        Builder b (rate, 0.6);
        b.sweptSine (160.0, 48.0, 0.035, 0.28, 0.95);
        b.noise (1, FilterMode::bandPass, 3500.0, 1.0, 0.004, 0.25);
        return b.finish();
    }
    case 1: // Snare
    {
        Builder b (rate, 0.35);
        b.sweptSine (230.0, 180.0, 0.02, 0.08, 0.5);
        b.noise (2, FilterMode::highPass, 1800.0, 0.7, 0.12, 0.55);
        return b.finish();
    }
    case 2: // Closed hat
    {
        Builder b (rate, 0.12);
        b.noise (3, FilterMode::highPass, 8000.0, 0.8, 0.03, 0.6);
        return b.finish();
    }
    case 3: // Open hat
    {
        Builder b (rate, 0.6);
        b.noise (4, FilterMode::highPass, 7500.0, 0.8, 0.22, 0.5);
        return b.finish();
    }
    case 4: // Clap: three quick bursts and a tail
    {
        Builder b (rate, 0.4);
        for (int burst = 0; burst < 3; ++burst)
        {
            Builder hit (rate, 0.4);
            hit.noise (5u + static_cast<std::uint32_t> (burst), FilterMode::bandPass, 1400.0, 1.2,
                       burst == 2 ? 0.12 : 0.008, 0.9);
            const auto offset = static_cast<std::size_t> (burst * 0.011 * rate);
            for (std::size_t n = 0; n + offset < b.samples.size(); ++n)
                b.samples[n + offset] += hit.samples[n];
        }
        return b.finish();
    }
    case 5: // Low tom
    case 6: // Mid tom
    case 7: // High tom
    {
        const double base = pad == 5 ? 95.0 : pad == 6 ? 135.0 : 190.0;
        Builder b (rate, 0.5);
        b.sweptSine (base * 1.6, base, 0.05, 0.22, 0.85);
        return b.finish();
    }
    case 8: // Rim
    {
        Builder b (rate, 0.08);
        b.noise (9, FilterMode::bandPass, 2600.0, 4.0, 0.012, 1.6);
        b.sweptSine (1700.0, 1600.0, 0.01, 0.015, 0.3);
        return b.finish();
    }
    case 9: // Cowbell
    {
        Builder b (rate, 0.45);
        b.squareTone (540.0, 0.12, 0.25);
        b.squareTone (800.0, 0.12, 0.25);
        dsp::StateVariableFilter filter;
        filter.prepare (rate);
        filter.setMode (FilterMode::bandPass);
        filter.setCutoffAndQ (900.0, 1.5);
        for (auto& s : b.samples)
            s = filter.process (s) * 1.6f;
        return b.finish();
    }
    case 10: // Shaker
    {
        Builder b (rate, 0.15);
        b.noise (11, FilterMode::highPass, 6000.0, 0.7, 0.04, 0.5, 0.015);
        return b.finish();
    }
    case 11: // Crash
    {
        Builder b (rate, 1.8);
        b.noise (12, FilterMode::highPass, 5000.0, 0.6, 0.6, 0.45);
        return b.finish();
    }
    case 12: // Perc low
    {
        Builder b (rate, 0.25);
        b.sweptSine (420.0, 300.0, 0.01, 0.06, 0.7);
        return b.finish();
    }
    case 13: // Perc high
    {
        Builder b (rate, 0.2);
        b.sweptSine (1100.0, 900.0, 0.008, 0.04, 0.6);
        return b.finish();
    }
    case 14: // Bass hit
    {
        Builder b (rate, 0.8);
        b.sweptSine (70.0, 55.0, 0.05, 0.35, 0.8);
        b.sweptSine (140.0, 110.0, 0.05, 0.12, 0.2);
        return b.finish();
    }
    default: // Zap
    {
        Builder b (rate, 0.3);
        b.sweptSine (2400.0, 120.0, 0.03, 0.12, 0.6);
        return b.finish();
    }
    }
}

} // namespace ap::instruments
