#include "ap/model/Project.h"

#include <algorithm>

namespace ap::model
{

const Track* Project::findTrack (TrackId id) const noexcept
{
    const auto it = std::find_if (tracks.begin(), tracks.end(), [id] (const Track& t) { return t.id == id; });
    return it == tracks.end() ? nullptr : &*it;
}

Track* Project::findTrack (TrackId id) noexcept
{
    const auto it = std::find_if (tracks.begin(), tracks.end(), [id] (const Track& t) { return t.id == id; });
    return it == tracks.end() ? nullptr : &*it;
}

std::optional<std::size_t> Project::indexOf (TrackId id) const noexcept
{
    const auto it = std::find_if (tracks.begin(), tracks.end(), [id] (const Track& t) { return t.id == id; });
    if (it == tracks.end())
        return std::nullopt;
    return static_cast<std::size_t> (it - tracks.begin());
}

const Asset* Project::findAsset (AssetId id) const noexcept
{
    const auto it = std::find_if (assets.begin(), assets.end(), [id] (const Asset& a) { return a.id == id; });
    return it == assets.end() ? nullptr : &*it;
}

const Track* Project::findInstrumentTrack (InstrumentKind instrument) const noexcept
{
    const auto it = std::find_if (tracks.begin(), tracks.end(), [instrument] (const Track& t)
                                  { return t.kind == TrackKind::instrument && t.instrument == instrument; });
    return it == tracks.end() ? nullptr : &*it;
}

std::optional<Project::ClipLocation> Project::locate (ClipId id) const noexcept
{
    for (std::size_t t = 0; t < tracks.size(); ++t)
    {
        const auto& clips = tracks[t].clips;
        for (std::size_t c = 0; c < clips.size(); ++c)
            if (clips[c].id == id)
                return ClipLocation {t, c};
    }
    return std::nullopt;
}

const Clip* Project::findClip (ClipId id) const noexcept
{
    const auto location = locate (id);
    return location ? &tracks[location->track].clips[location->clip] : nullptr;
}

bool isSafeAssetPath (std::string_view path) noexcept
{
    constexpr std::string_view prefix = "audio/";
    if (path.size() <= prefix.size() || path.size() > 200 || path.substr (0, prefix.size()) != prefix)
        return false;

    const auto file = path.substr (prefix.size());
    if (file.front() == '.' || file.find ("..") != std::string_view::npos)
        return false;

    return std::all_of (file.begin(), file.end(),
                        [] (char c)
                        {
                            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                                || c == '-' || c == '_' || c == '.';
                        });
}

std::optional<std::string> sanitiseName (std::string_view input, std::size_t maxBytes)
{
    std::string result;
    result.reserve (std::min (input.size(), maxBytes));

    for (const char c : input)
    {
        const auto byte = static_cast<unsigned char> (c);
        if (byte < 0x20 || byte == 0x7f)
            result.push_back (' '); // tabs, newlines and other controls become spaces
        else
            result.push_back (c);
    }

    // Trim surrounding whitespace.
    const auto first = result.find_first_not_of (' ');
    if (first == std::string::npos)
        return std::nullopt;
    result = result.substr (first, result.find_last_not_of (' ') - first + 1);

    if (result.size() > maxBytes)
    {
        auto cut = maxBytes;
        // Step back over UTF-8 continuation bytes so a code point is never split.
        while (cut > 0 && (static_cast<unsigned char> (result[cut]) & 0xC0u) == 0x80u)
            --cut;
        result.resize (cut);
        const auto last = result.find_last_not_of (' ');
        if (last == std::string::npos)
            return std::nullopt;
        result.resize (last + 1);
    }

    return result;
}

} // namespace ap::model
