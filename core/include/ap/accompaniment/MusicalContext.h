#pragma once

#include "ap/analysis/RhythmAnalysis.h"
#include "ap/core/MusicalTime.h"

#include <optional>

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
    tempoMismatch,  // a clear pulse at a tempo that does not fit the project's
    offGrid         // the tempo fits but the beats fall between the project's: out of time with it
};

struct ContextSettings
{
    double minOnsetsPerSecond = 0.5;    // below this there is no rhythm to accompany
    double minBpmConfidence = 0.5;      // below this: noRhythm
    double reliableBpmConfidence = 0.7; // below this (and above the one before): tempoUncertain
    double tempoTolerance = 0.04;       // how close counts as the same tempo (also at 2x and 1/2x)
    double gridTolerance = 0.15;        // of a beat: how far the take's beats may sit from the project's
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

// Signed distance in seconds from a beat at `beatSeconds` (project time) to the nearest beat of a
// grid at `bpm` that starts at 0. Within half a beat either way.
[[nodiscard]] double beatGridError (double beatSeconds, double bpm) noexcept;

// Turns an analysis into a context at the project's tempo and meter. `firstBeatProjectSeconds` is
// where the take's first detected beat falls in the project (the take's position plus the beat's
// time inside it); when given, a take whose beats miss the project's grid is refused (offGrid),
// because a groove laid on the grid would clash with it.
[[nodiscard]] ContextResult makeContext (const analysis::RhythmAnalysis& analysis, double projectBpm,
                                         core::TimeSignature projectMeter,
                                         std::optional<double> firstBeatProjectSeconds = std::nullopt,
                                         const ContextSettings& settings = {});

// True when `detected` is the same tempo as `project`, or twice or half of it (within tolerance).
[[nodiscard]] bool sameTempoUpToOctave (double detected, double project, double tolerance) noexcept;

} // namespace ap::accompaniment
