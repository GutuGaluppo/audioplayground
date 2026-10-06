#include "ap/model/ProjectSerialization.h"

#include "ap/model/ClipEditing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

namespace ap::model
{
namespace
{
using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::string_view formatTag = "project";
constexpr std::size_t maxPreservedParameters = 256;
constexpr std::size_t maxParameterIdLength = 64;

// Thrown only inside this file and always converted to LoadError at the boundary.
struct Invalid
{
    std::string reason;
};

[[noreturn]] void invalid (std::string reason)
{
    throw Invalid {std::move (reason)};
}

// Rejects deeply nested input before the (recursive) JSON parser sees it.
bool exceedsNestingDepth (std::string_view text, int maxDepth)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (const char c : text)
    {
        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }

        if (c == '"')
            inString = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > maxDepth)
                return true;
        }
        else if (c == '}' || c == ']')
            --depth;
    }
    return false;
}

const Json& member (const Json& object, const char* key)
{
    const auto it = object.find (key);
    if (it == object.end())
        invalid (std::string ("missing \"") + key + "\"");
    return *it;
}

void requireExactKeys (const Json& object, std::initializer_list<const char*> keys, const char* what)
{
    if (!object.is_object())
        invalid (std::string (what) + " must be an object");
    if (object.size() != keys.size())
        invalid (std::string (what) + " has unexpected or missing fields");
    for (const auto* key : keys)
        if (!object.contains (key))
            invalid (std::string (what) + " is missing \"" + key + "\"");
}

double finiteNumber (const Json& value, const char* what)
{
    if (!value.is_number())
        invalid (std::string (what) + " must be a number");
    const auto number = value.get<double>();
    if (!std::isfinite (number))
        invalid (std::string (what) + " must be finite");
    return number;
}

double numberInRange (const Json& value, double min, double max, const char* what)
{
    const auto number = finiteNumber (value, what);
    if (number < min || number > max)
        invalid (std::string (what) + " is out of range");
    return number;
}

std::uint64_t positiveInteger (const Json& value, const char* what)
{
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() > 0))
        invalid (std::string (what) + " must be a positive integer");
    const auto number = value.get<std::uint64_t>();
    if (number == 0)
        invalid (std::string (what) + " must be a positive integer");
    return number;
}

bool boolean (const Json& value, const char* what)
{
    if (!value.is_boolean())
        invalid (std::string (what) + " must be true or false");
    return value.get<bool>();
}

std::string name (const Json& value, std::size_t maxBytes, const char* what)
{
    if (!value.is_string())
        invalid (std::string (what) + " must be text");
    const auto& raw = value.get_ref<const std::string&>();
    if (raw.size() > maxBytes * 4) // generous: sanitising trims, but refuse absurd input
        invalid (std::string (what) + " is too long");
    auto clean = sanitiseName (raw, maxBytes);
    if (!clean)
        invalid (std::string (what) + " must not be empty");
    return *clean;
}

std::string timestamp (const Json& value, const char* what)
{
    if (!value.is_string())
        invalid (std::string (what) + " must be text");
    const auto& text = value.get_ref<const std::string&>();
    const bool plausible = !text.empty() && text.size() <= maxTimestampLength
                        && std::all_of (text.begin(), text.end(),
                                        [] (char c)
                                        {
                                            return (c >= '0' && c <= '9') || c == '-' || c == ':' || c == 'T'
                                                || c == 'Z' || c == '.' || c == '+';
                                        });
    if (!plausible)
        invalid (std::string (what) + " is not a timestamp");
    return text;
}

bool isParameterIdShape (std::string_view id)
{
    if (id.empty() || id.size() > maxParameterIdLength || id.front() == '.' || id.back() == '.')
        return false;
    return std::all_of (
        id.begin(), id.end(), [] (char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'; });
}

const char* instrumentName (InstrumentKind instrument) noexcept
{
    switch (instrument)
    {
    case InstrumentKind::synth:
        return "synth";
    case InstrumentKind::sampler:
        return "sampler";
    case InstrumentKind::drums:
        return "drums";
    }
    return "synth";
}

core::Ticks ticks (const Json& value, core::Ticks min, core::Ticks max, const char* what)
{
    if (!value.is_number_integer())
        invalid (std::string (what) + " must be a whole number");
    const auto number = value.get<std::int64_t>();
    if (number < min || number > max)
        invalid (std::string (what) + " is out of range");
    return number;
}

std::uint64_t idOrZero (const Json& value, const char* what)
{
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() == 0))
        invalid (std::string (what) + " must be an id or 0");
    return value.get<std::uint64_t>();
}

Clip parseClip (const Json& json, const Track& track, const Project& project)
{
    Clip clip;
    if (track.kind == TrackKind::audio)
    {
        requireExactKeys (json, {"id", "start", "length", "asset", "sourceOffset"}, "audio clip");
        clip.asset = AssetId {positiveInteger (json["asset"], "clip asset")};
        clip.sourceOffset
            = ticks (json["sourceOffset"], 0, std::numeric_limits<std::int64_t>::max(), "clip source offset");
    }
    else
    {
        requireExactKeys (json, {"id", "start", "length", "contentOffset", "loopLength", "notes"},
                          "note clip");
        clip.contentOffset = ticks (json["contentOffset"], 0, maxTimelineTicks, "clip content offset");
        clip.loopLength = ticks (json["loopLength"], 0, maxTimelineTicks, "clip loop length");

        const auto& notes = json["notes"];
        if (!notes.is_array())
            invalid ("clip notes must be a list");
        if (notes.size() > Clip::maxNotes)
            invalid ("too many notes in a clip");
        clip.notes.reserve (notes.size());
        for (const auto& note : notes)
        {
            if (!note.is_array() || note.size() != 4)
                invalid ("a note must be [start, length, pitch, velocity]");
            Note parsed;
            parsed.start = ticks (note[0], 0, maxTimelineTicks - 1, "note start");
            parsed.length = ticks (note[1], 1, maxTimelineTicks, "note length");
            parsed.pitch = static_cast<std::uint8_t> (ticks (note[2], 0, 127, "note pitch"));
            parsed.velocity = static_cast<float> (
                numberInRange (note[3], static_cast<double> (Note::minVelocity), 1.0, "note velocity"));
            clip.notes.push_back (parsed);
        }
    }

    clip.id = ClipId {positiveInteger (json["id"], "clip id")};
    clip.start = ticks (json["start"], 0, maxTimelineTicks, "clip start");
    clip.length = ticks (json["length"], Clip::minLength, maxTimelineTicks, "clip length");

    const auto id = clip.id;
    auto normalised = normaliseClip (std::move (clip), track, project);
    if (!normalised)
        invalid ("a clip is not valid");
    normalised->id = id;
    return *normalised;
}

std::string_view effectValueKey (const params::ParameterDescriptor& descriptor)
{
    return descriptor.id.substr (descriptor.id.find ('.') + 1);
}

// Missing effects and values take their defaults (files written before effects existed load
// unchanged); anything unknown or out of range is refused.
TrackEffects parseEffects (const Json& json)
{
    if (!json.is_object())
        invalid ("track effects must be an object");
    auto effects = defaultTrackEffects();
    for (const auto& [id, value] : json.items())
    {
        std::size_t e = 0;
        while (e < params::numEffects && params::effectDescriptors[e].id != id)
            ++e;
        if (e == params::numEffects)
            invalid ("unknown track effect");
        if (!value.is_object())
            invalid ("a track effect must be an object");

        const auto& descriptor = params::effectDescriptors[e];
        auto& state = effects[e];
        for (const auto& [key, field] : value.items())
        {
            if (key == "enabled")
            {
                state.enabled = boolean (field, "effect enabled");
                continue;
            }
            std::size_t i = 0;
            while (i < descriptor.numParameters && effectValueKey (descriptor.parameters[i]) != key)
                ++i;
            if (i == descriptor.numParameters)
                invalid ("unknown effect value");
            const auto& d = descriptor.parameters[i];
            state.values[i] = static_cast<float> (numberInRange (
                field, static_cast<double> (d.min), static_cast<double> (d.max), "effect value"));
        }
    }
    return effects;
}

Track parseTrack (const Json& json, const Project& project)
{
    if (!json.is_object())
        invalid ("track must be an object");

    Track track;
    const auto& kind = member (json, "kind");
    if (kind == "audio")
    {
        track.kind = TrackKind::audio;
        if (json.contains ("effects"))
            requireExactKeys (
                json, {"id", "kind", "name", "volumeDb", "pan", "muted", "soloed", "clips", "effects"},
                "track");
        else
            requireExactKeys (json, {"id", "kind", "name", "volumeDb", "pan", "muted", "soloed", "clips"},
                              "track");
    }
    else if (kind == "instrument")
    {
        track.kind = TrackKind::instrument;
        if (json.contains ("effects"))
            requireExactKeys (json,
                              {"id", "kind", "instrument", "name", "volumeDb", "pan", "muted", "soloed",
                               "clips", "effects"},
                              "track");
        else
            requireExactKeys (
                json, {"id", "kind", "instrument", "name", "volumeDb", "pan", "muted", "soloed", "clips"},
                "track");
        const auto& instrument = json["instrument"];
        if (instrument == "synth")
            track.instrument = InstrumentKind::synth;
        else if (instrument == "sampler")
            track.instrument = InstrumentKind::sampler;
        else if (instrument == "drums")
            track.instrument = InstrumentKind::drums;
        else
            invalid ("track instrument must be \"synth\", \"sampler\" or \"drums\"");
    }
    else
        invalid ("track kind must be \"audio\" or \"instrument\"");

    track.id = TrackId {positiveInteger (json["id"], "track id")};
    track.name = name (json["name"], Track::maxNameLength, "track name");
    track.volumeDb
        = static_cast<float> (numberInRange (json["volumeDb"], static_cast<double> (Track::minVolumeDb),
                                             static_cast<double> (Track::maxVolumeDb), "track volume"));
    track.pan = static_cast<float> (numberInRange (json["pan"], -1.0, 1.0, "track pan"));
    track.muted = boolean (json["muted"], "track mute");
    track.soloed = boolean (json["soloed"], "track solo");
    if (json.contains ("effects"))
        track.effects = parseEffects (json["effects"]);

    const auto& clips = json["clips"];
    if (!clips.is_array())
        invalid ("track clips must be a list");
    if (clips.size() > Track::maxClips)
        invalid ("too many clips on a track");
    for (const auto& clip : clips)
        track.clips.push_back (parseClip (clip, track, project));
    std::sort (track.clips.begin(), track.clips.end(), [] (const Clip& a, const Clip& b)
               { return a.start != b.start ? a.start < b.start : a.id < b.id; });
    return track;
}

LoadedProject parseV1 (const Json& root)
{
    requireExactKeys (root,
                      {"format", "schemaVersion", "name", "createdAt", "updatedAt", "tempo", "timeSignature",
                       "exportSampleRate", "nextTrackId", "nextClipId", "tracks", "parameters", "assets",
                       "nextAssetId", "samplerAsset", "drums"},
                      "project");

    LoadedProject loaded;
    auto& project = loaded.project;

    project.name = name (root["name"], Project::maxNameLength, "project name");
    loaded.metadata.createdAt = timestamp (root["createdAt"], "createdAt");
    loaded.metadata.updatedAt = timestamp (root["updatedAt"], "updatedAt");

    project.tempoBpm = numberInRange (root["tempo"], core::minTempoBpm, core::maxTempoBpm, "tempo");

    const auto& signature = root["timeSignature"];
    if (!signature.is_array() || signature.size() != 2 || !signature[0].is_number_integer()
        || !signature[1].is_number_integer())
        invalid ("time signature must be two whole numbers");
    project.timeSignature = {signature[0].get<int>(), signature[1].get<int>()};
    if (!project.timeSignature.isValid())
        invalid ("time signature is not supported");

    project.exportSampleRate
        = numberInRange (root["exportSampleRate"], 8000.0, 384000.0, "export sample rate");

    const auto& assets = root["assets"];
    if (!assets.is_array())
        invalid ("assets must be a list");
    if (assets.size() > Project::maxAssets)
        invalid ("too many assets");

    std::set<std::uint64_t> assetIds;
    std::uint64_t highestAssetId = 0;
    for (const auto& json : assets)
    {
        requireExactKeys (json, {"id", "path", "name"}, "asset");
        Asset asset;
        asset.id = AssetId {positiveInteger (json["id"], "asset id")};
        if (!json["path"].is_string() || !isSafeAssetPath (json["path"].get_ref<const std::string&>()))
            invalid ("asset path is not allowed");
        asset.relativePath = json["path"].get<std::string>();
        asset.name = name (json["name"], Asset::maxNameLength, "asset name");
        if (!assetIds.insert (asset.id.value).second)
            invalid ("two assets share the same id");
        highestAssetId = std::max (highestAssetId, asset.id.value);
        project.assets.push_back (std::move (asset));
    }

    project.nextAssetId = positiveInteger (root["nextAssetId"], "nextAssetId");
    if (project.nextAssetId <= highestAssetId)
        invalid ("nextAssetId must be greater than every asset id");

    const auto& tracks = root["tracks"];
    if (!tracks.is_array())
        invalid ("tracks must be a list");
    if (tracks.size() > Project::maxTracks)
        invalid ("too many tracks");

    std::set<std::uint64_t> ids;
    std::set<std::uint64_t> clipIds;
    std::uint64_t highestId = 0;
    std::uint64_t highestClipId = 0;
    for (const auto& json : tracks)
    {
        auto track = parseTrack (json, project);
        if (!ids.insert (track.id.value).second)
            invalid ("two tracks share the same id");
        if (track.kind == TrackKind::instrument && project.findInstrumentTrack (track.instrument) != nullptr)
            invalid ("two tracks play the same instrument");
        for (const auto& clip : track.clips)
        {
            if (!clipIds.insert (clip.id.value).second)
                invalid ("two clips share the same id");
            highestClipId = std::max (highestClipId, clip.id.value);
        }
        highestId = std::max (highestId, track.id.value);
        project.tracks.push_back (std::move (track));
    }

    project.nextTrackId = positiveInteger (root["nextTrackId"], "nextTrackId");
    if (project.nextTrackId <= highestId)
        invalid ("nextTrackId must be greater than every track id");

    project.nextClipId = positiveInteger (root["nextClipId"], "nextClipId");
    if (project.nextClipId <= highestClipId)
        invalid ("nextClipId must be greater than every clip id");

    project.samplerAsset = AssetId {idOrZero (root["samplerAsset"], "samplerAsset")};
    if (project.samplerAsset.isValid() && project.findAsset (project.samplerAsset) == nullptr)
        invalid ("samplerAsset refers to a missing asset");

    const auto& drums = root["drums"];
    requireExactKeys (drums, {"pads"}, "drums");
    const auto& pads = drums["pads"];
    if (!pads.is_array() || pads.size() != DrumKit::numPads)
        invalid ("the drum kit must have 16 pads");
    for (std::size_t i = 0; i < DrumKit::numPads; ++i)
    {
        const auto& json = pads[i];
        requireExactKeys (json, {"sample", "volumeDb", "pitch", "muted"}, "drum pad");
        auto& pad = project.drums.pads[i];
        pad.sample = AssetId {idOrZero (json["sample"], "drum pad sample")};
        if (pad.sample.isValid() && project.findAsset (pad.sample) == nullptr)
            invalid ("drum pad refers to a missing asset");
        pad.volumeDb
            = static_cast<float> (numberInRange (json["volumeDb"], static_cast<double> (DrumPad::minVolumeDb),
                                                 static_cast<double> (DrumPad::maxVolumeDb), "pad volume"));
        pad.pitch
            = static_cast<float> (numberInRange (json["pitch"], -static_cast<double> (DrumPad::maxPitch),
                                                 static_cast<double> (DrumPad::maxPitch), "pad pitch"));
        pad.muted = boolean (json["muted"], "pad mute");
    }

    const auto& parameters = root["parameters"];
    if (!parameters.is_object())
        invalid ("parameters must be an object");
    for (const auto& [id, value] : parameters.items())
    {
        if (!isParameterIdShape (id))
            invalid ("parameter id is malformed");

        if (const auto known = params::findParamId (id))
        {
            const auto& d = params::descriptor (*known);
            project.parameters[static_cast<std::size_t> (*known)] = static_cast<float> (numberInRange (
                value, static_cast<double> (d.min), static_cast<double> (d.max), "parameter value"));
        }
        else
        {
            if (project.preservedParameters.size() >= maxPreservedParameters)
                invalid ("too many parameters");
            project.preservedParameters.emplace_back (id, finiteNumber (value, "parameter value"));
        }
    }

    return loaded;
}
} // namespace

std::string serialiseProject (const Project& project, const ProjectMetadata& metadata)
{
    OrderedJson root;
    root["format"] = formatTag;
    root["schemaVersion"] = currentSchemaVersion;
    root["name"] = project.name;
    root["createdAt"] = metadata.createdAt;
    root["updatedAt"] = metadata.updatedAt;
    root["tempo"] = project.tempoBpm;
    root["timeSignature"] = {project.timeSignature.numerator, project.timeSignature.denominator};
    root["exportSampleRate"] = project.exportSampleRate;
    root["nextTrackId"] = project.nextTrackId;
    root["nextClipId"] = project.nextClipId;

    auto tracks = OrderedJson::array();
    for (const auto& track : project.tracks)
    {
        OrderedJson json;
        json["id"] = track.id.value;
        json["kind"] = track.kind == TrackKind::audio ? "audio" : "instrument";
        if (track.kind == TrackKind::instrument)
            json["instrument"] = instrumentName (track.instrument);
        json["name"] = track.name;
        json["volumeDb"] = track.volumeDb;
        json["pan"] = track.pan;
        json["muted"] = track.muted;
        json["soloed"] = track.soloed;

        auto clips = OrderedJson::array();
        for (const auto& clip : track.clips)
        {
            OrderedJson c;
            c["id"] = clip.id.value;
            c["start"] = clip.start;
            c["length"] = clip.length;
            if (track.kind == TrackKind::audio)
            {
                c["asset"] = clip.asset.value;
                c["sourceOffset"] = clip.sourceOffset;
            }
            else
            {
                c["contentOffset"] = clip.contentOffset;
                c["loopLength"] = clip.loopLength;
                auto notes = OrderedJson::array();
                for (const auto& note : clip.notes)
                    notes.push_back ({note.start, note.length, note.pitch, note.velocity});
                c["notes"] = std::move (notes);
            }
            clips.push_back (std::move (c));
        }
        json["clips"] = std::move (clips);

        // Effects by id; a value's key is the part of its parameter id after the effect's
        // ("eq.lowGain" -> "lowGain"). Both are persistence contracts (schema/parameters.json).
        auto effects = OrderedJson::object();
        for (std::size_t e = 0; e < params::numEffects; ++e)
        {
            const auto& descriptor = params::effectDescriptors[e];
            const auto& state = track.effects[e];
            OrderedJson effect;
            effect["enabled"] = state.enabled;
            for (std::size_t i = 0; i < descriptor.numParameters; ++i)
                effect[std::string (effectValueKey (descriptor.parameters[i]))] = state.values[i];
            effects[std::string (descriptor.id)] = std::move (effect);
        }
        json["effects"] = std::move (effects);
        tracks.push_back (std::move (json));
    }
    root["tracks"] = std::move (tracks);

    auto parameters = OrderedJson::object();
    for (std::size_t i = 0; i < params::numParameters; ++i)
        parameters[std::string (params::descriptors[i].id)] = project.parameters[i];
    for (const auto& [id, value] : project.preservedParameters)
        parameters[id] = value;
    root["parameters"] = std::move (parameters);

    auto assets = OrderedJson::array();
    for (const auto& asset : project.assets)
    {
        OrderedJson json;
        json["id"] = asset.id.value;
        json["path"] = asset.relativePath;
        json["name"] = asset.name;
        assets.push_back (std::move (json));
    }
    root["assets"] = std::move (assets);
    root["nextAssetId"] = project.nextAssetId;
    root["samplerAsset"] = project.samplerAsset.value;

    auto pads = OrderedJson::array();
    for (std::size_t i = 0; i < DrumKit::numPads; ++i)
    {
        const auto& pad = project.drums.pads[i];
        OrderedJson json;
        json["sample"] = pad.sample.value;
        json["volumeDb"] = pad.volumeDb;
        json["pitch"] = pad.pitch;
        json["muted"] = pad.muted;
        pads.push_back (std::move (json));
    }
    OrderedJson drums;
    drums["pads"] = std::move (pads);
    root["drums"] = std::move (drums);

    return root.dump (2) + "\n";
}

LoadResult parseProject (std::string_view text)
{
    if (text.size() > maxProjectFileBytes)
        return LoadError {LoadError::Code::tooLarge, "The project file is too large to open."};

    if (exceedsNestingDepth (text, maxJsonNestingDepth))
        return LoadError {LoadError::Code::tooDeeplyNested, "The project file is not a valid project."};

    const auto root = Json::parse (text.begin(), text.end(), nullptr, false);
    if (root.is_discarded())
        return LoadError {LoadError::Code::notJson, "The project file is damaged or is not a project."};

    try
    {
        if (!root.is_object() || root.value ("format", "") != formatTag)
            invalid ("not a project file");

        const auto& version = member (root, "schemaVersion");
        if (!version.is_number_integer() || version.get<std::int64_t>() < 1)
            invalid ("unknown file version");

        if (version.get<std::int64_t>() > currentSchemaVersion)
            return LoadError {
                LoadError::Code::newerVersion,
                "This project was saved by a newer version of the app. Update the app to open it."};

        // Future versions add migrations here (pure vN -> vN+1 on the JSON) before parseV1.
        return parseV1 (root);
    }
    catch (const Invalid& error)
    {
        return LoadError {LoadError::Code::invalid, "The project file is not valid (" + error.reason + ")."};
    }
    catch (const nlohmann::json::exception&)
    {
        return LoadError {LoadError::Code::invalid, "The project file is not valid."};
    }
}

std::string currentTimestampUtc()
{
    const auto now = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    std::tm utc {};
#if defined(_WIN32)
    gmtime_s (&utc, &now);
#else
    gmtime_r (&now, &utc);
#endif
    char buffer[32] {};
    std::strftime (buffer, sizeof (buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

} // namespace ap::model
