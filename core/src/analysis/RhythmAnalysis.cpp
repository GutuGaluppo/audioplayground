#include "ap/analysis/RhythmAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace ap::analysis
{
namespace
{

[[nodiscard]] bool cancelled (const std::atomic<bool>* cancel) noexcept
{
    return cancel != nullptr && cancel->load (std::memory_order_relaxed);
}

// In-place radix-2 complex FFT (size a power of two) with precomputed twiddles. Small, not
// real-time code: only the analysis uses it.
class Fft
{
public:
    explicit Fft (std::size_t size)
        : n (size)
        , cosTable (size / 2)
        , sinTable (size / 2)
        , bitReversed (size)
    {
        for (std::size_t i = 0; i < n / 2; ++i)
        {
            const double angle = -2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n);
            cosTable[i] = static_cast<float> (std::cos (angle));
            sinTable[i] = static_cast<float> (std::sin (angle));
        }
        std::size_t bits = 0;
        while ((std::size_t {1} << bits) < n)
            ++bits;
        for (std::size_t i = 0; i < n; ++i)
        {
            std::size_t reversed = 0;
            for (std::size_t bit = 0; bit < bits; ++bit)
                if (i & (std::size_t {1} << bit))
                    reversed |= std::size_t {1} << (bits - 1 - bit);
            bitReversed[i] = reversed;
        }
    }

    void transform (std::vector<float>& re, std::vector<float>& im) const
    {
        for (std::size_t i = 0; i < n; ++i)
            if (const auto j = bitReversed[i]; j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        for (std::size_t half = 1; half < n; half *= 2)
        {
            const auto step = n / (2 * half);
            for (std::size_t start = 0; start < n; start += 2 * half)
                for (std::size_t k = 0; k < half; ++k)
                {
                    const float wr = cosTable[k * step];
                    const float wi = sinTable[k * step];
                    const auto a = start + k;
                    const auto b = a + half;
                    const float tr = wr * re[b] - wi * im[b];
                    const float ti = wr * im[b] + wi * re[b];
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
    }

private:
    std::size_t n;
    std::vector<float> cosTable, sinTable;
    std::vector<std::size_t> bitReversed;
};

constexpr std::size_t fftSize = 1024;
// Spectral flux (summed over bins) a new note must exceed, whatever its surroundings.
constexpr float minPeakFlux = 4.0f;
constexpr double maxAnalysisHz = 8000.0;

// How much new sound appears in each hop: spectral flux on log magnitudes (the positive change of
// every frequency bin against the loudest of its neighbours in the previous frame, so a vibrato or
// a ringing note does not count as new, but a freshly struck note does). Works for drums and for
// plucked or struck instruments whose attack is only a little louder than the notes still ringing.
std::vector<float> transientStrength (const instruments::SampleBuffer& audio, std::size_t hop, double& rmsDb,
                                      const std::atomic<bool>* cancel)
{
    const auto frames = static_cast<std::size_t> (audio.frames());

    double sumSquares = 0.0;
    std::vector<float> mono (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for (const auto& channel : audio.channels)
            sum += channel[i];
        mono[i] = sum / static_cast<float> (audio.channels.size());
        sumSquares += static_cast<double> (mono[i]) * static_cast<double> (mono[i]);
    }
    rmsDb = frames > 0 ? 10.0 * std::log10 (sumSquares / static_cast<double> (frames) + 1.0e-12) : -120.0;
    if (frames < fftSize)
        return {};

    const Fft fft (fftSize);
    std::vector<float> window (fftSize);
    for (std::size_t i = 0; i < fftSize; ++i)
        window[i] = 0.5f
                  - 0.5f
                        * static_cast<float> (std::cos (2.0 * std::numbers::pi * static_cast<double> (i)
                                                        / static_cast<double> (fftSize)));

    const auto bins
        = std::min<std::size_t> (fftSize / 2, static_cast<std::size_t> (maxAnalysisHz / audio.sampleRate
                                                                        * static_cast<double> (fftSize)));
    const auto count = (frames - fftSize) / hop + 1;
    std::vector<float> re (fftSize), im (fftSize);
    std::vector<float> previous (bins, 0.0f), current (bins, 0.0f);
    std::vector<float> strength (count, 0.0f);

    for (std::size_t k = 0; k < count; ++k)
    {
        if (cancelled (cancel))
            return {};
        const auto begin = k * hop;
        for (std::size_t i = 0; i < fftSize; ++i)
        {
            re[i] = mono[begin + i] * window[i];
            im[i] = 0.0f;
        }
        fft.transform (re, im);

        for (std::size_t b = 0; b < bins; ++b)
            current[b] = std::log1p (200.0f * std::sqrt (re[b] * re[b] + im[b] * im[b])
                                     / static_cast<float> (fftSize));

        if (k > 0)
        {
            float flux = 0.0f;
            for (std::size_t b = 0; b < bins; ++b)
            {
                const float reference = std::max (
                    {previous[b > 0 ? b - 1 : 0], previous[b], previous[std::min (b + 1, bins - 1)]});
                flux += std::max (0.0f, current[b] - reference);
            }
            strength[k] = flux;
        }
        std::swap (previous, current);
    }
    return strength;
}

// Local maxima that stand clear of their surroundings and are not too close to a stronger one.
std::vector<std::size_t> pickOnsets (const std::vector<float>& s, std::size_t minSpacing, std::size_t radius)
{
    std::vector<std::size_t> picked;
    if (s.size() < 3)
        return picked;

    const float globalMax = *std::max_element (s.begin(), s.end());
    if (globalMax < minPeakFlux) // no sound ever appears: nothing to count
        return picked;

    // Running sum for the local mean over +/- radius frames.
    std::vector<double> cumulative (s.size() + 1, 0.0);
    for (std::size_t i = 0; i < s.size(); ++i)
        cumulative[i + 1] = cumulative[i] + static_cast<double> (s[i]);

    for (std::size_t k = 1; k + 1 < s.size(); ++k)
    {
        const auto from = k > radius ? k - radius : 0;
        const auto to = std::min (s.size(), k + radius + 1);
        const double mean = (cumulative[to] - cumulative[from]) / static_cast<double> (to - from);
        const float threshold
            = std::max (minPeakFlux, static_cast<float> (1.5 * mean) + 0.12f * globalMax + 0.5f);
        if (s[k] < threshold)
            continue;

        bool isPeak = true;
        for (std::size_t j = (k > 3 ? k - 3 : 0); j <= std::min (s.size() - 1, k + 3) && isPeak; ++j)
            isPeak = s[j] <= s[k] && (s[j] < s[k] || j >= k); // the first of equal neighbours wins
        if (!isPeak)
            continue;

        if (!picked.empty() && k - picked.back() < minSpacing)
        {
            if (s[k] > s[picked.back()])
                picked.back() = k;
            continue;
        }
        picked.push_back (k);
    }
    return picked;
}

struct Tempo
{
    double lagFrames = 0.0;
    double confidence = 0.0;
};

// Autocorrelation of the transient curve over the lags of the allowed tempo range, weighted by a
// preference for common tempos. Confidence is how much of the curve repeats at the chosen lag.
Tempo estimateTempo (const std::vector<float>& s, const RhythmAnalysisSettings& cfg,
                     const std::atomic<bool>* cancel)
{
    const auto n = s.size();
    const auto minLag = static_cast<std::size_t> (std::floor (60.0 / cfg.maxBpm / cfg.hopSeconds));
    const auto maxLag = static_cast<std::size_t> (std::ceil (60.0 / cfg.minBpm / cfg.hopSeconds));
    if (n < 4 * maxLag) // fewer than about four beats at the slowest tempo
        return {};

    // Smooth the curve over ~50 ms so that a hit played a little early or late still lines up with
    // its neighbours: this is what makes the estimate tolerate a human player.
    constexpr int kernelRadius = 2;
    std::vector<double> smooth (n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        for (int j = -kernelRadius; j <= kernelRadius; ++j)
        {
            const auto k = static_cast<std::ptrdiff_t> (i) + j;
            if (k >= 0 && k < static_cast<std::ptrdiff_t> (n))
                smooth[i] += static_cast<double> (s[static_cast<std::size_t> (k)])
                           * (kernelRadius + 1 - std::abs (j))
                           / static_cast<double> ((kernelRadius + 1) * (kernelRadius + 1));
        }

    const double mean = std::accumulate (smooth.begin(), smooth.end(), 0.0) / static_cast<double> (n);
    std::vector<double> centred (n);
    double energy = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        centred[i] = smooth[i] - mean;
        energy += centred[i] * centred[i];
    }
    if (energy <= 0.0)
        return {};

    std::vector<double> r (maxLag + 2, 0.0);
    for (std::size_t lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        if (cancelled (cancel))
            return {};
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i)
            sum += centred[i] * centred[i + lag];
        // Normalised by the energy of the overlapping part, so long lags are not penalised.
        r[lag] = sum / energy * static_cast<double> (n) / static_cast<double> (n - lag);
    }

    std::size_t best = 0;
    double bestScore = -1.0;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
    {
        if (!(r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]))
            continue; // only peaks
        const double bpm = 60.0 / (static_cast<double> (lag) * cfg.hopSeconds);
        const double octaves = std::log2 (bpm / cfg.preferredBpm) / cfg.preferenceWidthOctaves;
        const double score = r[lag] * std::exp (-0.5 * octaves * octaves);
        if (score > bestScore)
        {
            bestScore = score;
            best = lag;
        }
    }
    if (best == 0 || r[best] <= 0.0)
        return {};

    // Parabolic interpolation around the peak for a lag finer than one hop.
    const double a = r[best - 1];
    const double b = r[best];
    const double c = r[best + 1];
    const double denominator = a - 2.0 * b + c;
    const double offset = denominator < 0.0 ? 0.5 * (a - c) / denominator : 0.0;

    Tempo tempo;
    tempo.lagFrames = static_cast<double> (best) + std::clamp (offset, -0.5, 0.5);
    tempo.confidence = std::clamp ((r[best] - 0.15) / 0.5, 0.0, 1.0);
    return tempo;
}

// Position of the first beat: the offset (within one period) whose comb of beats collects the
// most transient strength.
double beatPhase (const std::vector<float>& s, double lag)
{
    double bestSum = -1.0;
    double bestPhase = 0.0;
    const auto steps = static_cast<std::size_t> (std::ceil (lag));
    for (std::size_t phase = 0; phase < steps; ++phase)
    {
        double sum = 0.0;
        for (double position = static_cast<double> (phase); position < static_cast<double> (s.size());
             position += lag)
        {
            const auto k = static_cast<std::size_t> (std::lround (position));
            if (k < s.size())
                sum += static_cast<double> (s[k]);
        }
        if (sum > bestSum)
        {
            bestSum = sum;
            bestPhase = static_cast<double> (phase);
        }
    }
    return bestPhase;
}

RhythmAnalysis::Feel estimateFeel (const std::vector<double>& onsets, double firstBeat, double period)
{
    std::vector<double> offBeat;
    for (const double t : onsets)
    {
        const double position = (t - firstBeat) / period;
        const double fraction = position - std::floor (position);
        if (fraction >= 0.3 && fraction <= 0.8)
            offBeat.push_back (fraction);
    }
    if (offBeat.size() < 4)
        return RhythmAnalysis::Feel::unknown;

    std::sort (offBeat.begin(), offBeat.end());
    const double median = offBeat[offBeat.size() / 2];
    if (std::abs (median - 0.5) < 0.06)
        return RhythmAnalysis::Feel::straight;
    if (std::abs (median - 2.0 / 3.0) < 0.07)
        return RhythmAnalysis::Feel::swing;
    return RhythmAnalysis::Feel::unknown;
}
} // namespace

RhythmAnalysis analyseRhythm (const instruments::SampleBuffer& audio, const RhythmAnalysisSettings& cfg,
                              const std::atomic<bool>* cancel)
{
    RhythmAnalysis result;
    if (!audio.isValid() || audio.sampleRate <= 0.0)
        return result;

    result.durationSeconds = static_cast<double> (audio.frames()) / audio.sampleRate;
    const auto hop = std::max<std::size_t> (
        1, static_cast<std::size_t> (std::lround (cfg.hopSeconds * audio.sampleRate)));
    const double hopSeconds = static_cast<double> (hop) / audio.sampleRate;
    auto settings = cfg;
    settings.hopSeconds = hopSeconds;
    const double frameCentreSeconds = 0.5 * static_cast<double> (fftSize) / audio.sampleRate;

    const auto strength = transientStrength (audio, hop, result.rmsDb, cancel);
    if (cancelled (cancel))
        return {};

    const auto spacing = std::max<std::size_t> (
        1, static_cast<std::size_t> (std::lround (cfg.minOnsetSpacingSeconds / hopSeconds)));
    const auto radius = static_cast<std::size_t> (std::lround (0.2 / hopSeconds));
    const auto onsets = pickOnsets (strength, spacing, radius);
    for (const auto k : onsets)
        result.onsetSeconds.push_back (static_cast<double> (k) * hopSeconds + frameCentreSeconds);
    if (!onsets.empty())
    {
        auto all = strength;
        std::vector<float> atOnsets;
        for (const auto k : onsets)
            atOnsets.push_back (strength[k]);
        std::nth_element (all.begin(), all.begin() + static_cast<std::ptrdiff_t> (all.size() / 2), all.end());
        std::nth_element (atOnsets.begin(),
                          atOnsets.begin() + static_cast<std::ptrdiff_t> (atOnsets.size() / 2),
                          atOnsets.end());
        result.onsetContrast = static_cast<double> (atOnsets[atOnsets.size() / 2])
                             / std::max (0.05, static_cast<double> (all[all.size() / 2]));
    }
    result.onsetsPerSecond = result.durationSeconds > 0.0
                               ? static_cast<double> (result.onsetSeconds.size()) / result.durationSeconds
                               : 0.0;
    if (result.onsetSeconds.size() < 4)
        return result; // too little to call a pulse

    const auto tempo = estimateTempo (strength, settings, cancel);
    if (cancelled (cancel))
        return {};
    if (tempo.lagFrames <= 0.0)
        return result;

    result.bpm = 60.0 / (tempo.lagFrames * hopSeconds);
    result.bpmConfidence = tempo.confidence;

    const double period = tempo.lagFrames * hopSeconds;
    const double firstBeat = beatPhase (strength, tempo.lagFrames) * hopSeconds + frameCentreSeconds;
    for (double t = firstBeat; t < result.durationSeconds; t += period)
        result.beatSeconds.push_back (t);
    result.feel = estimateFeel (result.onsetSeconds, firstBeat, period);
    return result;
}

} // namespace ap::analysis
