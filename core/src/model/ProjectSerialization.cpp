#include "ap/model/ProjectSerialization.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <initializer_list>
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

Track parseTrack (const Json& json)
{
    requireExactKeys (json, {"id", "kind", "name", "volumeDb", "pan", "muted", "soloed"}, "track");

    Track track;
    track.id = TrackId {positiveInteger (json["id"], "track id")};

    const auto& kind = json["kind"];
    if (kind == "audio")
        track.kind = TrackKind::audio;
    else if (kind == "instrument")
        track.kind = TrackKind::instrument;
    else
        invalid ("track kind must be \"audio\" or \"instrument\"");

    track.name = name (json["name"], Track::maxNameLength, "track name");
    track.volumeDb
        = static_cast<float> (numberInRange (json["volumeDb"], static_cast<double> (Track::minVolumeDb),
                                             static_cast<double> (Track::maxVolumeDb), "track volume"));
    track.pan = static_cast<float> (numberInRange (json["pan"], -1.0, 1.0, "track pan"));
    track.muted = boolean (json["muted"], "track mute");
    track.soloed = boolean (json["soloed"], "track solo");
    return track;
}

LoadedProject parseV1 (const Json& root)
{
    requireExactKeys (root,
                      {"format", "schemaVersion", "name", "createdAt", "updatedAt", "tempo", "timeSignature",
                       "exportSampleRate", "nextTrackId", "tracks", "parameters", "assets", "nextAssetId",
                       "samplerAsset", "drums"},
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

    const auto& tracks = root["tracks"];
    if (!tracks.is_array())
        invalid ("tracks must be a list");
    if (tracks.size() > Project::maxTracks)
        invalid ("too many tracks");

    std::set<std::uint64_t> ids;
    std::uint64_t highestId = 0;
    for (const auto& json : tracks)
    {
        auto track = parseTrack (json);
        if (!ids.insert (track.id.value).second)
            invalid ("two tracks share the same id");
        highestId = std::max (highestId, track.id.value);
        project.tracks.push_back (std::move (track));
    }

    project.nextTrackId = positiveInteger (root["nextTrackId"], "nextTrackId");
    if (project.nextTrackId <= highestId)
        invalid ("nextTrackId must be greater than every track id");

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

    const auto& samplerAsset = root["samplerAsset"];
    if (!samplerAsset.is_number_unsigned()
        && !(samplerAsset.is_number_integer() && samplerAsset.get<std::int64_t>() == 0))
        invalid ("samplerAsset must be an asset id or 0");
    project.samplerAsset = AssetId {samplerAsset.get<std::uint64_t>()};
    if (project.samplerAsset.isValid() && project.findAsset (project.samplerAsset) == nullptr)
        invalid ("samplerAsset refers to a missing asset");

    const auto& drums = root["drums"];
    requireExactKeys (drums, {"pads", "steps"}, "drums");
    const auto& pads = drums["pads"];
    const auto& steps = drums["steps"];
    if (!pads.is_array() || pads.size() != DrumKit::numPads || !steps.is_array()
        || steps.size() != DrumKit::numPads)
        invalid ("the drum kit must have 16 pads and 16 step rows");
    for (std::size_t i = 0; i < DrumKit::numPads; ++i)
    {
        const auto& json = pads[i];
        requireExactKeys (json, {"sample", "volumeDb", "pitch", "muted"}, "drum pad");
        auto& pad = project.drums.pads[i];
        const auto& sample = json["sample"];
        if (!sample.is_number_unsigned() && !(sample.is_number_integer() && sample.get<std::int64_t>() == 0))
            invalid ("drum pad sample must be an asset id or 0");
        pad.sample = AssetId {sample.get<std::uint64_t>()};
        if (pad.sample.isValid() && project.findAsset (pad.sample) == nullptr)
            invalid ("drum pad refers to a missing asset");
        pad.volumeDb
            = static_cast<float> (numberInRange (json["volumeDb"], static_cast<double> (DrumPad::minVolumeDb),
                                                 static_cast<double> (DrumPad::maxVolumeDb), "pad volume"));
        pad.pitch
            = static_cast<float> (numberInRange (json["pitch"], -static_cast<double> (DrumPad::maxPitch),
                                                 static_cast<double> (DrumPad::maxPitch), "pad pitch"));
        pad.muted = boolean (json["muted"], "pad mute");

        if (!steps[i].is_number_unsigned()
            && !(steps[i].is_number_integer() && steps[i].get<std::int64_t>() == 0))
            invalid ("drum steps must be whole numbers");
        const auto mask = steps[i].get<std::uint64_t>();
        if (mask > 0xFFFF)
            invalid ("drum steps are out of range");
        project.drums.steps[i] = static_cast<std::uint16_t> (mask);
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

    auto tracks = OrderedJson::array();
    for (const auto& track : project.tracks)
    {
        OrderedJson json;
        json["id"] = track.id.value;
        json["kind"] = track.kind == TrackKind::audio ? "audio" : "instrument";
        json["name"] = track.name;
        json["volumeDb"] = track.volumeDb;
        json["pan"] = track.pan;
        json["muted"] = track.muted;
        json["soloed"] = track.soloed;
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
    auto steps = OrderedJson::array();
    for (std::size_t i = 0; i < DrumKit::numPads; ++i)
    {
        const auto& pad = project.drums.pads[i];
        OrderedJson json;
        json["sample"] = pad.sample.value;
        json["volumeDb"] = pad.volumeDb;
        json["pitch"] = pad.pitch;
        json["muted"] = pad.muted;
        pads.push_back (std::move (json));
        steps.push_back (project.drums.steps[i]);
    }
    OrderedJson drums;
    drums["pads"] = std::move (pads);
    drums["steps"] = std::move (steps);
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
