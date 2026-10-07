#include "ap/instruments/FactoryKit.h"

#include "ap/dsp/StateVariableFilter.h"

#include <algorithm>
#include <array>
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

// Shape of a kit: the same pad roles as the classic kit, with different tuning, lengths and colour.
struct Style
{
    double kickStart, kickEnd, kickGlide, kickDecay, kickLength, kickClick;
    double snareTone, snareCut, snareDecay, snareNoise;
    double hatCut, hatDecay, openDecay;
    double clapCut, clapTail;
    double tomBase, tomSweep, tomDecay;
    double crashCut, crashDecay;
    double bassHz, bassDecay;
    double drive;     // pre-clip gain; 1 = none
    int crushStep;    // hold each sample this many times (sample-rate reduction); 1 = none
    int crushLevels;  // amplitude levels (bit reduction); 0 = none
};

constexpr std::array<Style, factoryKitCount> styles {{
    {}, // kit 0 is the hand-written classic kit below
    // 808: long sub kick, boomy toms, soft snare, sizzly hats.
    {120.0, 38.0, 0.05, 0.75, 1.3, 0.12, 190.0, 2500.0, 0.16, 0.5, 9500.0, 0.028, 0.32, 1100.0, 0.16, 70.0,
     2.0, 0.45, 6000.0, 0.9, 43.0, 1.2, 1.0, 1, 0},
    // Lo-fi: the classic shape, dusty and crunchy (crushed in post).
    {150.0, 52.0, 0.04, 0.22, 0.5, 0.2, 210.0, 1500.0, 0.14, 0.5, 6500.0, 0.035, 0.18, 1300.0, 0.1, 100.0,
     1.5, 0.2, 4500.0, 0.5, 60.0, 0.5, 1.4, 4, 48},
    // Acoustic-ish: round kick thump, noisy snare, washy cymbals.
    {110.0, 62.0, 0.03, 0.2, 0.45, 0.35, 200.0, 1200.0, 0.2, 0.9, 7000.0, 0.05, 0.4, 1000.0, 0.14, 85.0, 1.35,
     0.3, 4000.0, 1.4, 65.0, 0.4, 1.0, 1, 0},
    // Electro: tight, hard kick, bright zappy toms, clipped hats.
    {230.0, 55.0, 0.02, 0.16, 0.4, 0.3, 330.0, 3000.0, 0.09, 0.6, 11000.0, 0.018, 0.12, 2200.0, 0.07, 160.0,
     4.0, 0.14, 9000.0, 0.45, 55.0, 0.3, 3.0, 1, 0},
}};

std::unique_ptr<SampleBuffer> makeStyledSound (std::size_t pad, double rate, const Style& st,
                                               std::uint32_t seedOffset)
{
    using dsp::FilterMode;
    const auto seed = [seedOffset] (std::uint32_t n) { return n + seedOffset; };
    Builder b (rate, 0.5);

    switch (pad)
    {
    case 0: // Kick
        b = Builder (rate, st.kickLength);
        b.sweptSine (st.kickStart, st.kickEnd, st.kickGlide, st.kickDecay, 0.95);
        b.noise (seed (1), FilterMode::bandPass, 3500.0, 1.0, 0.004, st.kickClick);
        break;
    case 1: // Snare
        b = Builder (rate, 0.4);
        b.sweptSine (st.snareTone * 1.2, st.snareTone, 0.02, 0.08, 0.5);
        b.noise (seed (2), FilterMode::highPass, st.snareCut, 0.7, st.snareDecay, st.snareNoise);
        break;
    case 2: // Closed hat
        b = Builder (rate, 0.15);
        b.noise (seed (3), FilterMode::highPass, st.hatCut, 0.8, st.hatDecay, 0.6);
        break;
    case 3: // Open hat
        b = Builder (rate, 0.7);
        b.noise (seed (4), FilterMode::highPass, st.hatCut * 0.95, 0.8, st.openDecay, 0.5);
        break;
    case 4: // Clap
    {
        b = Builder (rate, 0.45);
        for (int burst = 0; burst < 3; ++burst)
        {
            Builder hit (rate, 0.45);
            hit.noise (seed (5u + static_cast<std::uint32_t> (burst)), FilterMode::bandPass, st.clapCut, 1.2,
                       burst == 2 ? st.clapTail : 0.008, 0.9);
            const auto offset = static_cast<std::size_t> (burst * 0.011 * rate);
            for (std::size_t n = 0; n + offset < b.samples.size(); ++n)
                b.samples[n + offset] += hit.samples[n];
        }
        break;
    }
    case 5:
    case 6:
    case 7: // Toms
    {
        const double base = st.tomBase * (pad == 5 ? 1.0 : pad == 6 ? 1.42 : 2.0);
        b = Builder (rate, 0.7);
        b.sweptSine (base * st.tomSweep, base, 0.05, st.tomDecay, 0.85);
        break;
    }
    case 8: // Rim
        b = Builder (rate, 0.1);
        b.noise (seed (9), FilterMode::bandPass, 2600.0, 4.0, 0.012, 1.6);
        b.sweptSine (1700.0, 1600.0, 0.01, 0.015, 0.3);
        break;
    case 9: // Cowbell
    {
        b = Builder (rate, 0.5);
        b.squareTone (540.0, 0.14, 0.25);
        b.squareTone (800.0, 0.14, 0.25);
        dsp::StateVariableFilter filter;
        filter.prepare (rate);
        filter.setMode (FilterMode::bandPass);
        filter.setCutoffAndQ (900.0, 1.5);
        for (auto& s : b.samples)
            s = filter.process (s) * 1.6f;
        break;
    }
    case 10: // Shaker
        b = Builder (rate, 0.18);
        b.noise (seed (11), FilterMode::highPass, st.hatCut * 0.75, 0.7, 0.04, 0.5, 0.015);
        break;
    case 11: // Crash
        b = Builder (rate, 2.2);
        b.noise (seed (12), FilterMode::highPass, st.crashCut, 0.6, st.crashDecay, 0.45);
        break;
    case 12: // Perc low
        b = Builder (rate, 0.3);
        b.sweptSine (420.0, 300.0, 0.01, 0.07, 0.7);
        break;
    case 13: // Perc high
        b = Builder (rate, 0.22);
        b.sweptSine (1100.0, 900.0, 0.008, 0.045, 0.6);
        break;
    case 14: // Bass hit
        b = Builder (rate, 1.4);
        b.sweptSine (st.bassHz * 1.3, st.bassHz, 0.05, st.bassDecay, 0.8);
        b.sweptSine (st.bassHz * 2.6, st.bassHz * 2.0, 0.05, st.bassDecay * 0.3, 0.2);
        break;
    default: // Zap
        b = Builder (rate, 0.35);
        b.sweptSine (2400.0, 120.0, 0.03, 0.14, 0.6);
        break;
    }

    // Colour: drive into the soft clip, then optional sample-rate and bit reduction (lo-fi).
    for (auto& s : b.samples)
        s = static_cast<float> (st.drive) * s;
    if (st.crushStep > 1)
        for (std::size_t n = 0; n < b.samples.size(); ++n)
            if (n % static_cast<std::size_t> (st.crushStep) != 0)
                b.samples[n] = b.samples[n - n % static_cast<std::size_t> (st.crushStep)];
    if (st.crushLevels > 0)
    {
        const auto levels = static_cast<float> (st.crushLevels);
        for (auto& s : b.samples)
            s = std::round (s * levels) / levels;
    }
    return b.finish();
}
} // namespace

std::unique_ptr<SampleBuffer> makeFactorySound (std::size_t pad, double sampleRate, std::size_t kit)
{
    using dsp::FilterMode;
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    if (kit > 0 && kit < factoryKitCount)
        return makeStyledSound (pad, rate, styles[kit], static_cast<std::uint32_t> (kit) * 100u);


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
