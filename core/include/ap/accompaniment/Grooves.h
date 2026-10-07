#pragma once

#include "ap/accompaniment/MusicalContext.h"
#include "ap/model/Project.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ap::accompaniment
{

// What an accompaniment is for. Only drums exist in the MVP; the matcher is written against this
// so that bass or harmony can be added without a new API (ADR-011 §14).
enum class Kind
{
    drums
};

// One curated groove (ADR-011 §4): a bar of the built-in drum kit and what the matcher scores.
// The metadata is data (presets/grooves.json), independent of any sound.
struct Groove
{
    std::string id;
    std::string name;
    Kind kind = Kind::drums;
    core::TimeSignature timeSignature;
    Feel feel = Feel::straight;
    Density density = Density::medium;
    Energy energy = Energy::medium;
    double minBpm = 60.0;
    double maxBpm = 160.0;
    std::vector<std::string> character;
    std::string variationGroup;
    std::array<std::uint16_t, model::DrumKit::numPads> pattern {}; // bit n of a pad = step n
};

struct GrooveLibrary
{
    std::vector<Groove> grooves;

    [[nodiscard]] const Groove* find (std::string_view id) const noexcept;
};

// Parses the library file. Every problem found is appended to `problems` (with the groove's id);
// a groove with a problem is left out, the others are kept.
[[nodiscard]] GrooveLibrary parseGrooves (std::string_view json,
                                          std::vector<std::string>* problems = nullptr);

// The library shipped with the app (presets/grooves.json, embedded at build time).
[[nodiscard]] const GrooveLibrary& factoryGrooves();

// How much each aspect counts (ADR-011 §5). They need not sum to 1; the score is normalised.
struct MatchWeights
{
    double tempo = 0.35;
    double timeSignature = 0.25;
    double feel = 0.15;
    double density = 0.10;
    double energy = 0.10;
    double character = 0.05;
    double minimumScore = 0.55; // below this a groove is not offered at all
};

struct Suggestion
{
    std::string grooveId;
    double score = 0.0; // 0..1
    std::vector<std::string> reasons;
};

// The grooves for `kind` that fit the context, best first, never one listed in `exclude`. Fully
// deterministic: equal scores are ordered by id. A groove in another time signature is never
// offered.
[[nodiscard]] std::vector<Suggestion> findAccompaniment (Kind kind, const MusicalContext& context,
                                                         const GrooveLibrary& library,
                                                         std::span<const std::string> exclude = {},
                                                         const MatchWeights& weights = {});

// The groove as a drum pattern clip of `length` ticks starting at `start`, looping every bar.
// `contentOffset` shifts where in the pattern the clip begins.
[[nodiscard]] model::Clip grooveClip (const Groove& groove, core::Ticks start, core::Ticks length);

} // namespace ap::accompaniment
