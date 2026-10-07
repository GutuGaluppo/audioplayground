#include "ap/accompaniment/MusicalContext.h"

#include <algorithm>
#include <cmath>

namespace ap::accompaniment
{

bool sameTempoUpToOctave (double detected, double project, double tolerance) noexcept
{
    if (detected <= 0.0 || project <= 0.0)
        return false;
    for (const double factor : {1.0, 2.0, 0.5})
        if (std::abs (detected / (project * factor) - 1.0) <= tolerance)
            return true;
    return false;
}

double beatGridError (double beatSeconds, double bpm) noexcept
{
    const double period = 60.0 / bpm;
    double error = std::fmod (beatSeconds, period);
    if (error > 0.5 * period)
        error -= period;
    else if (error < -0.5 * period)
        error += period;
    return error;
}

ContextResult makeContext (const analysis::RhythmAnalysis& analysis, double projectBpm,
                           core::TimeSignature projectMeter, std::optional<double> firstBeatProjectSeconds,
                           const ContextSettings& s)
{
    ContextResult result;
    result.detectedBpm = analysis.bpm;

    if (analysis.onsetsPerSecond < s.minOnsetsPerSecond || !analysis.hasTempo()
        || analysis.bpmConfidence < s.minBpmConfidence)
    {
        result.verdict = Verdict::noRhythm;
        return result;
    }
    if (analysis.bpmConfidence < s.reliableBpmConfidence)
    {
        result.verdict = Verdict::tempoUncertain;
        return result;
    }
    if (!sameTempoUpToOctave (analysis.bpm, projectBpm, s.tempoTolerance))
    {
        result.verdict = Verdict::tempoMismatch;
        return result;
    }

    if (firstBeatProjectSeconds)
    {
        // Compare on the finer of the two grids, so a take counted in doubles or halves still lines up.
        const double gridBpm = std::max (projectBpm, analysis.bpm);
        if (std::abs (beatGridError (*firstBeatProjectSeconds, gridBpm)) > s.gridTolerance * 60.0 / gridBpm)
        {
            result.verdict = Verdict::offGrid;
            return result;
        }
    }

    auto& context = result.context;
    context.bpm = projectBpm;
    context.timeSignature = projectMeter;
    switch (analysis.feel)
    {
    case analysis::RhythmAnalysis::Feel::straight:
        context.feel = Feel::straight;
        break;
    case analysis::RhythmAnalysis::Feel::swing:
        context.feel = Feel::swing;
        break;
    default:
        context.feel = Feel::unknown;
        break;
    }

    const double onsetsPerBeat = analysis.onsetsPerSecond * 60.0 / projectBpm;
    context.density = onsetsPerBeat < s.sparseOnsetsPerBeat ? Density::sparse
                    : onsetsPerBeat >= s.denseOnsetsPerBeat ? Density::dense
                                                            : Density::medium;
    context.energy = analysis.rmsDb < s.softBelowDb   ? Energy::soft
                   : analysis.rmsDb > s.strongAboveDb ? Energy::strong
                                                      : Energy::medium;
    result.verdict = Verdict::ready;
    return result;
}

} // namespace ap::accompaniment
