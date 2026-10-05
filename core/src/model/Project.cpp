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
