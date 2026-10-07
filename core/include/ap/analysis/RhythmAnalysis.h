#pragma once

#include "ap/instruments/SampleBuffer.h"

#include <atomic>
#include <vector>

namespace ap::analysis
{

// What the rhythm of a recording looks like (ADR-011). Everything here is an estimate: when the
// signal gives no clear evidence the fields say so (bpmConfidence near 0, feel unknown) instead of
// guessing. The audio itself is only read, never changed.
struct RhythmAnalysis
{
    // Bumped when the algorithm changes, so a cached result can be recognised as stale.
    static constexpr int version = 1;

    enum class Feel
    {
        unknown,
        straight,
        swing
    };

    double durationSeconds = 0.0;
    double rmsDb = -120.0;        // loudness of the whole take, dBFS
    double onsetsPerSecond = 0.0; // note starts (transients) per second
    // How far the onsets stand out of the sound around them (median onset strength over median
    // strength): large for struck or plucked notes, small for a steady or beating tone.
    double onsetContrast = 0.0;
    std::vector<double> onsetSeconds;

    double bpm = 0.0;                // 0 = none found
    double bpmConfidence = 0.0;      // 0..1
    std::vector<double> beatSeconds; // where the beats fall, from the first one on
    Feel feel = Feel::unknown;

    [[nodiscard]] bool hasTempo() const noexcept { return bpm > 0.0; }
};

struct RhythmAnalysisSettings
{
    double minBpm = 50.0;
    double maxBpm = 220.0;
    // Tempo is searched with a mild preference for the range musicians play in, so a steady
    // quarter-note pulse wins over its double and half.
    double preferredBpm = 110.0;
    double preferenceWidthOctaves = 0.9;
    double hopSeconds = 0.01;
    double minOnsetSpacingSeconds = 0.05;
};

// Analyses a take: mono mix, a transient strength curve, onsets, tempo and beat grid, feel. Pure
// and deterministic; runs in time proportional to the length (well under a second for minutes of
// audio). Not real-time code: call it from a worker thread. If `cancel` becomes true it returns
// early with an empty result.
[[nodiscard]] RhythmAnalysis analyseRhythm (const instruments::SampleBuffer& audio,
                                            const RhythmAnalysisSettings& settings = {},
                                            const std::atomic<bool>* cancel = nullptr);

} // namespace ap::analysis
