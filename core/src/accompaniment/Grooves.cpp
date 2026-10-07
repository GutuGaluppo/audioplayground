#include "ap/accompaniment/Grooves.h"

#include "ap/model/ClipEditing.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

namespace ap::accompaniment
{
extern const unsigned char embeddedGroovesData[];
extern const std::size_t embeddedGroovesSize;

namespace
{
using Json = nlohmann::json;

// The factory kit's pad slots, in order (instruments/FactoryKit.h), as the file names them.
constexpr std::array<std::string_view, model::DrumKit::numPads> padKeys {
    "kick", "snare",   "closedHat", "openHat", "clap",    "lowTom",   "midTom", "highTom",
    "rim",  "cowbell", "shaker",    "crash",   "percLow", "percHigh", "bass",   "zap"};

template <typename E>
std::optional<E> parseEnum (const Json& json, const char* key,
                            std::initializer_list<std::pair<std::string_view, E>> table)
{
    if (!json.contains (key) || !json[key].is_string())
        return std::nullopt;
    const auto& text = json[key].get_ref<const std::string&>();
    for (const auto& [name, value] : table)
        if (name == text)
            return value;
    return std::nullopt;
}

std::optional<Groove> parseGroove (const Json& json, std::string& problem)
{
    Groove groove;
    if (!json.is_object() || !json.contains ("id") || !json["id"].is_string())
    {
        problem = "a groove needs an id";
        return std::nullopt;
    }
    groove.id = json["id"].get<std::string>();

    const auto fail = [&] (const char* what)
    {
        problem = std::string (what);
        return std::optional<Groove> {};
    };

    if (!json.contains ("name") || !json["name"].is_string()
        || json["name"].get_ref<const std::string&>().empty())
        return fail ("missing name");
    groove.name = json["name"].get<std::string>();

    if (!json.contains ("kind") || json["kind"] != "drums")
        return fail ("unknown kind");

    if (!json.contains ("timeSignature") || !json["timeSignature"].is_array()
        || json["timeSignature"].size() != 2 || !json["timeSignature"][0].is_number_integer()
        || !json["timeSignature"][1].is_number_integer())
        return fail ("bad timeSignature");
    groove.timeSignature = {json["timeSignature"][0].get<int>(), json["timeSignature"][1].get<int>()};
    if (!groove.timeSignature.isValid())
        return fail ("invalid timeSignature");

    const auto feel = parseEnum<Feel> (json, "feel", {{"straight", Feel::straight}, {"swing", Feel::swing}});
    const auto density = parseEnum<Density> (
        json, "density",
        {{"sparse", Density::sparse}, {"medium", Density::medium}, {"dense", Density::dense}});
    const auto energy = parseEnum<Energy> (
        json, "energy", {{"soft", Energy::soft}, {"medium", Energy::medium}, {"strong", Energy::strong}});
    if (!feel || !density || !energy)
        return fail ("bad feel, density or energy");
    groove.feel = *feel;
    groove.density = *density;
    groove.energy = *energy;

    if (!json.contains ("minBpm") || !json.contains ("maxBpm") || !json["minBpm"].is_number()
        || !json["maxBpm"].is_number())
        return fail ("missing minBpm or maxBpm");
    groove.minBpm = json["minBpm"].get<double>();
    groove.maxBpm = json["maxBpm"].get<double>();
    if (!(groove.minBpm >= 20.0 && groove.maxBpm <= 300.0 && groove.minBpm < groove.maxBpm))
        return fail ("bad tempo range");

    if (json.contains ("character"))
    {
        if (!json["character"].is_array())
            return fail ("bad character");
        for (const auto& word : json["character"])
        {
            if (!word.is_string())
                return fail ("bad character");
            groove.character.push_back (word.get<std::string>());
        }
    }
    if (json.contains ("variationGroup"))
    {
        if (!json["variationGroup"].is_string())
            return fail ("bad variationGroup");
        groove.variationGroup = json["variationGroup"].get<std::string>();
    }

    if (!json.contains ("pattern") || !json["pattern"].is_object() || json["pattern"].empty())
        return fail ("missing pattern");
    for (const auto& [key, steps] : json["pattern"].items())
    {
        const auto pad = std::find (padKeys.begin(), padKeys.end(), key);
        if (pad == padKeys.end() || !steps.is_string())
            return fail ("unknown pad in pattern");
        const auto& text = steps.get_ref<const std::string&>();
        if (text.size() != model::DrumKit::numSteps
            || !std::all_of (text.begin(), text.end(), [] (char c) { return c == 'x' || c == '.'; }))
            return fail ("a pad needs 16 steps of x and .");
        std::uint16_t mask = 0;
        for (std::size_t step = 0; step < text.size(); ++step)
            if (text[step] == 'x')
                mask = static_cast<std::uint16_t> (mask | (1u << step));
        groove.pattern[static_cast<std::size_t> (pad - padKeys.begin())] = mask;
    }
    if (std::all_of (groove.pattern.begin(), groove.pattern.end(), [] (std::uint16_t m) { return m == 0; }))
        return fail ("an empty pattern");
    return groove;
}

// 0..1: how well `bpm` suits the groove's range. Inside is 1; it fades to 0 over a quarter of the
// range's edge (as a fraction of the tempo).
double tempoFit (const Groove& groove, double bpm)
{
    if (bpm >= groove.minBpm && bpm <= groove.maxBpm)
        return 1.0;
    const double edge = bpm < groove.minBpm ? groove.minBpm : groove.maxBpm;
    const double distance = std::abs (bpm - edge) / edge;
    return std::max (0.0, 1.0 - distance / 0.25);
}

double feelFit (Feel wanted, Feel groove)
{
    if (wanted == Feel::unknown)
        return groove == Feel::straight ? 0.6 : 0.3; // most music is straight: a safe default
    return wanted == groove ? 1.0 : 0.0;
}

template <typename E> double ordinalFit (E wanted, E groove)
{
    const int distance = std::abs (static_cast<int> (wanted) - static_cast<int> (groove));
    return distance == 0 ? 1.0 : distance == 1 ? 0.5 : 0.0;
}

const char* densityWord (Density d)
{
    return d == Density::sparse ? "sparse" : d == Density::medium ? "medium" : "dense";
}
const char* energyWord (Energy e)
{
    return e == Energy::soft ? "soft" : e == Energy::medium ? "medium" : "strong";
}
} // namespace

const Groove* GrooveLibrary::find (std::string_view id) const noexcept
{
    const auto it
        = std::find_if (grooves.begin(), grooves.end(), [id] (const Groove& g) { return g.id == id; });
    return it == grooves.end() ? nullptr : &*it;
}

GrooveLibrary parseGrooves (std::string_view json, std::vector<std::string>* problems)
{
    GrooveLibrary library;
    const auto report = [problems] (const std::string& text)
    {
        if (problems != nullptr)
            problems->push_back (text);
    };

    const auto root = Json::parse (json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains ("grooves") || !root["grooves"].is_array())
    {
        report ("(file): not a groove library");
        return library;
    }

    std::set<std::string> seen;
    for (const auto& entry : root["grooves"])
    {
        std::string problem;
        auto groove = parseGroove (entry, problem);
        const std::string id = entry.is_object() && entry.contains ("id") && entry["id"].is_string()
                                 ? entry["id"].get<std::string>()
                                 : std::string ("(no id)");
        if (groove && !seen.insert (groove->id).second)
        {
            problem = "duplicate id";
            groove.reset();
        }
        if (!groove)
        {
            report (id + ": " + problem);
            continue;
        }
        library.grooves.push_back (std::move (*groove));
    }
    return library;
}

const GrooveLibrary& factoryGrooves()
{
    static const GrooveLibrary library = parseGrooves (
        std::string_view (reinterpret_cast<const char*> (embeddedGroovesData), embeddedGroovesSize));
    return library;
}

std::vector<Suggestion> findAccompaniment (Kind kind, const MusicalContext& context,
                                           const GrooveLibrary& library, std::span<const std::string> exclude,
                                           const MatchWeights& w)
{
    std::vector<Suggestion> ranked;
    const double total = w.tempo + w.timeSignature + w.feel + w.density + w.energy + w.character;
    if (total <= 0.0)
        return ranked;

    for (const auto& groove : library.grooves)
    {
        if (groove.kind != kind || !(groove.timeSignature == context.timeSignature)
            || std::find (exclude.begin(), exclude.end(), groove.id) != exclude.end())
            continue;

        const double tempo = tempoFit (groove, context.bpm);
        const double feel = feelFit (context.feel, groove.feel);
        const double density = ordinalFit (context.density, groove.density);
        const double energy = ordinalFit (context.energy, groove.energy);
        const double score = (w.tempo * tempo + w.timeSignature + w.feel * feel + w.density * density
                              + w.energy * energy + w.character)
                           / total;
        if (score < w.minimumScore || tempo <= 0.0)
            continue; // a groove far outside its tempo range never sounds right

        Suggestion suggestion;
        suggestion.grooveId = groove.id;
        suggestion.score = score;
        if (tempo >= 1.0)
            suggestion.reasons.push_back (
                "sits well at " + std::to_string (static_cast<int> (std::lround (context.bpm))) + " BPM");
        if (feel >= 1.0)
            suggestion.reasons.push_back (groove.feel == Feel::swing ? "swings like your take"
                                                                     : "straight, like your take");
        if (density >= 1.0)
            suggestion.reasons.push_back (std::string (densityWord (groove.density))
                                          + " density, like your take");
        if (energy >= 1.0)
            suggestion.reasons.push_back (std::string (energyWord (groove.energy))
                                          + " energy, like your take");
        ranked.push_back (std::move (suggestion));
    }

    std::sort (ranked.begin(), ranked.end(), [] (const Suggestion& a, const Suggestion& b)
               { return a.score != b.score ? a.score > b.score : a.grooveId < b.grooveId; });
    return ranked;
}

model::Clip grooveClip (const Groove& groove, core::Ticks start, core::Ticks length)
{
    return model::withDrumPattern (model::makePatternClip (start, length), groove.pattern);
}

} // namespace ap::accompaniment
