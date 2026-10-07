#pragma once

#include "ap/analysis/RhythmAnalysis.h"
#include "ap/core/MusicalTime.h"

namespace ap::accompaniment
{

enum class Feel
{
    unknown,
    straight,
    swing
};
enum class Density
{
    sparse,
    medium,
    dense
};
enum class Energy
{
    soft,
    medium,
    strong
};

// What the music is like, in words a musician would use (ADR-011 §3). No genre guesses.
struct MusicalContext
{
    double bpm = 120.0; // the tempo the accompaniment will play at: the project's
    core::TimeSignature timeSignature;
    Feel feel = Feel::unknown;
    Density density = Density::medium;
    Energy energy = Energy::medium;
};

// Why a take can or cannot get a suggestion. Not suggesting is better than a bad suggestion.
enum class Verdict
{
    ready,          // a context could be built
    noRhythm,       // no clear pulse: pads, long chords, speech, rubato, silence
    tempoUncertain, // some pulse, but not sure enough of the tempo: ask for tap tempo / a BPM
    tempoMismatch   // a clear pulse at a tempo that does not fit the project's
};

struct ContextSettings
{
    double minOnsetsPerSecond = 0.5;    // below this there is no rhythm to accompany
    double minBpmConfidence = 0.5;      // below this: noRhythm
    double reliableBpmConfidence = 0.7; // below this (and above the one before): tempoUncertain
    double tempoTolerance = 0.04;       // how close counts as the same tempo (also at 2x and 1/2x)
    double sparseOnsetsPerBeat = 0.75;  // below: sparse
    double denseOnsetsPerBeat = 2.25;   // at or above: dense
    double softBelowDb = -30.0;         // RMS of the whole take
    double strongAboveDb = -18.0;
};

struct ContextResult
{
    Verdict verdict = Verdict::noRhythm;
    MusicalContext context;
    double detectedBpm = 0.0; // the take's own estimate, for messages ("about 87 BPM")
};

// Turns an analysis into a context at the project's tempo and meter.
[[nodiscard]] ContextResult makeContext (const analysis::RhythmAnalysis& analysis, double projectBpm,
                                         core::TimeSignature projectMeter,
                                         const ContextSettings& settings = {});

// True when `detected` is the same tempo as `project`, or twice or half of it (within tolerance).
[[nodiscard]] bool sameTempoUpToOctave (double detected, double project, double tolerance) noexcept;

} // namespace ap::accompaniment
