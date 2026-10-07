#include "ap/accompaniment/MusicalContext.h"

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

ContextResult makeContext (const analysis::RhythmAnalysis& analysis, double projectBpm,
                           core::TimeSignature projectMeter, const ContextSettings& s)
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
