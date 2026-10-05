#include "ap/model/Commands.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace ap::model
{
namespace
{
template <typename... Fns> struct Overloaded : Fns...
{
    using Fns::operator()...;
};

std::string defaultTrackName (const Project& project, TrackKind kind)
{
    const auto count = std::count_if (project.tracks.begin(), project.tracks.end(),
                                      [kind] (const Track& t) { return t.kind == kind; });
    return std::string (kind == TrackKind::audio ? "Audio " : "Instrument ") + std::to_string (count + 1);
}

// Applies a single-field track edit with the shared validate / capture / no-op logic.
template <typename Value, typename Validate>
ApplyResult applyTrackField (Project& project, TrackId id, Value Track::* field, Value& requested,
                             Value& previous, Validate&& normalise)
{
    auto* track = project.findTrack (id);
    if (track == nullptr)
        return ApplyResult::rejected;

    const std::optional<Value> value = normalise (requested);
    if (!value)
        return ApplyResult::rejected;

    requested = *value;
    if (track->*field == requested)
        return ApplyResult::unchanged;

    previous = track->*field;
    track->*field = requested;
    return ApplyResult::applied;
}

std::optional<float> finiteClamped (float value, float min, float max)
{
    if (!std::isfinite (value))
        return std::nullopt;
    return std::clamp (value, min, max);
}
} // namespace

ApplyResult apply (Command& command, Project& project)
{
    return std::visit (
        Overloaded {
            [&project] (SetTempo& c) -> ApplyResult
            {
                if (!core::isValidTempo (c.bpm))
                    return ApplyResult::rejected;
                if (c.bpm == project.tempoBpm)
                    return ApplyResult::unchanged;
                c.previous = project.tempoBpm;
                project.tempoBpm = c.bpm;
                return ApplyResult::applied;
            },
            [&project] (SetTimeSignature& c) -> ApplyResult
            {
                if (!c.signature.isValid())
                    return ApplyResult::rejected;
                if (c.signature == project.timeSignature)
                    return ApplyResult::unchanged;
                c.previous = project.timeSignature;
                project.timeSignature = c.signature;
                return ApplyResult::applied;
            },
            [&project] (RenameProject& c) -> ApplyResult
            {
                auto name = sanitiseName (c.name, Project::maxNameLength);
                if (!name)
                    return ApplyResult::rejected;
                c.name = std::move (*name);
                if (c.name == project.name)
                    return ApplyResult::unchanged;
                c.previous = std::exchange (project.name, c.name);
                return ApplyResult::applied;
            },
            [&project] (AddTrack& c) -> ApplyResult
            {
                if (project.tracks.size() >= Project::maxTracks)
                    return ApplyResult::rejected;

                const auto index = c.index.value_or (project.tracks.size());
                if (index > project.tracks.size())
                    return ApplyResult::rejected;

                std::string name;
                if (c.name.empty())
                    name = defaultTrackName (project, c.kind);
                else if (auto sanitised = sanitiseName (c.name, Track::maxNameLength))
                    name = std::move (*sanitised);
                else
                    return ApplyResult::rejected;

                Track track;
                track.id = TrackId {project.nextTrackId};
                track.kind = c.kind;
                track.name = std::move (name);

                c.created = track.id;
                c.previousNextTrackId = project.nextTrackId;
                c.index = index;

                project.tracks.insert (project.tracks.begin() + static_cast<std::ptrdiff_t> (index),
                                       std::move (track));
                ++project.nextTrackId;
                return ApplyResult::applied;
            },
            [&project] (RemoveTrack& c) -> ApplyResult
            {
                const auto index = project.indexOf (c.id);
                if (!index)
                    return ApplyResult::rejected;
                c.index = *index;
                c.removed = project.tracks[*index];
                project.tracks.erase (project.tracks.begin() + static_cast<std::ptrdiff_t> (*index));
                return ApplyResult::applied;
            },
            [&project] (MoveTrack& c) -> ApplyResult
            {
                const auto index = project.indexOf (c.id);
                if (!index || c.toIndex >= project.tracks.size())
                    return ApplyResult::rejected;
                if (*index == c.toIndex)
                    return ApplyResult::unchanged;
                c.fromIndex = *index;
                auto track = std::move (project.tracks[*index]);
                project.tracks.erase (project.tracks.begin() + static_cast<std::ptrdiff_t> (*index));
                project.tracks.insert (project.tracks.begin() + static_cast<std::ptrdiff_t> (c.toIndex),
                                       std::move (track));
                return ApplyResult::applied;
            },
            [&project] (RenameTrack& c) -> ApplyResult
            {
                return applyTrackField (project, c.id, &Track::name, c.name, c.previous,
                                        [] (const std::string& n)
                                        { return sanitiseName (n, Track::maxNameLength); });
            },
            [&project] (SetTrackVolume& c) -> ApplyResult
            {
                return applyTrackField (
                    project, c.id, &Track::volumeDb, c.volumeDb, c.previous,
                    [] (float v) { return finiteClamped (v, Track::minVolumeDb, Track::maxVolumeDb); });
            },
            [&project] (SetTrackPan& c) -> ApplyResult
            {
                return applyTrackField (project, c.id, &Track::pan, c.pan, c.previous,
                                        [] (float v) { return finiteClamped (v, -1.0f, 1.0f); });
            },
            [&project] (SetTrackMute& c) -> ApplyResult
            {
                return applyTrackField (project, c.id, &Track::muted, c.muted, c.previous,
                                        [] (bool v) { return std::optional<bool> (v); });
            },
            [&project] (SetTrackSolo& c) -> ApplyResult
            {
                return applyTrackField (project, c.id, &Track::soloed, c.soloed, c.previous,
                                        [] (bool v) { return std::optional<bool> (v); });
            },
            [&project] (SetParameter& c) -> ApplyResult
            {
                const auto index = static_cast<std::size_t> (c.id);
                if (index >= params::numParameters)
                    return ApplyResult::rejected;
                const auto& d = params::descriptor (c.id);
                const auto value = finiteClamped (c.value, d.min, d.max);
                if (!value)
                    return ApplyResult::rejected;
                c.value = *value;
                if (project.parameters[index] == c.value)
                    return ApplyResult::unchanged;
                c.previous = std::exchange (project.parameters[index], c.value);
                return ApplyResult::applied;
            },
        },
        command);
}

void revert (const Command& command, Project& project)
{
    std::visit (
        Overloaded {
            [&project] (const SetTempo& c) { project.tempoBpm = c.previous; },
            [&project] (const SetTimeSignature& c) { project.timeSignature = c.previous; },
            [&project] (const RenameProject& c) { project.name = c.previous; },
            [&project] (const AddTrack& c)
            {
                if (const auto index = project.indexOf (c.created))
                    project.tracks.erase (project.tracks.begin() + static_cast<std::ptrdiff_t> (*index));
                project.nextTrackId = c.previousNextTrackId;
            },
            [&project] (const RemoveTrack& c)
            {
                const auto index = std::min (c.index, project.tracks.size());
                project.tracks.insert (project.tracks.begin() + static_cast<std::ptrdiff_t> (index),
                                       c.removed);
            },
            [&project] (const MoveTrack& c)
            {
                if (const auto index = project.indexOf (c.id))
                {
                    auto track = std::move (project.tracks[*index]);
                    project.tracks.erase (project.tracks.begin() + static_cast<std::ptrdiff_t> (*index));
                    project.tracks.insert (project.tracks.begin() + static_cast<std::ptrdiff_t> (c.fromIndex),
                                           std::move (track));
                }
            },
            [&project] (const RenameTrack& c)
            {
                if (auto* t = project.findTrack (c.id))
                    t->name = c.previous;
            },
            [&project] (const SetTrackVolume& c)
            {
                if (auto* t = project.findTrack (c.id))
                    t->volumeDb = c.previous;
            },
            [&project] (const SetTrackPan& c)
            {
                if (auto* t = project.findTrack (c.id))
                    t->pan = c.previous;
            },
            [&project] (const SetTrackMute& c)
            {
                if (auto* t = project.findTrack (c.id))
                    t->muted = c.previous;
            },
            [&project] (const SetTrackSolo& c)
            {
                if (auto* t = project.findTrack (c.id))
                    t->soloed = c.previous;
            },
            [&project] (const SetParameter& c)
            { project.parameters[static_cast<std::size_t> (c.id)] = c.previous; },
        },
        command);
}

std::string_view describe (const Command& command) noexcept
{
    return std::visit (
        Overloaded {
            [] (const SetTempo&) { return std::string_view ("Change tempo"); },
            [] (const SetTimeSignature&) { return std::string_view ("Change time signature"); },
            [] (const RenameProject&) { return std::string_view ("Rename project"); },
            [] (const AddTrack&) { return std::string_view ("Add track"); },
            [] (const RemoveTrack&) { return std::string_view ("Delete track"); },
            [] (const MoveTrack&) { return std::string_view ("Move track"); },
            [] (const RenameTrack&) { return std::string_view ("Rename track"); },
            [] (const SetTrackVolume&) { return std::string_view ("Change volume"); },
            [] (const SetTrackPan&) { return std::string_view ("Change pan"); },
            [] (const SetTrackMute&) { return std::string_view ("Mute track"); },
            [] (const SetTrackSolo&) { return std::string_view ("Solo track"); },
            [] (const SetParameter& c) { return params::descriptor (c.id).name; },
        },
        command);
}

bool canMerge (const Command& previous, const Command& next) noexcept
{
    if (previous.index() != next.index())
        return false;

    return std::visit (
        [&next] (const auto& p) -> bool
        {
            using T = std::decay_t<decltype (p)>;
            const auto& n = std::get<T> (next);
            if constexpr (std::is_same_v<T, SetTempo> || std::is_same_v<T, SetTimeSignature>
                          || std::is_same_v<T, RenameProject>)
                return true;
            else if constexpr (std::is_same_v<T, SetParameter>)
                return p.id == n.id;
            else if constexpr (std::is_same_v<T, SetTrackVolume> || std::is_same_v<T, SetTrackPan>
                               || std::is_same_v<T, RenameTrack>)
                return p.id == n.id;
            else
                return false; // structural edits and toggles are always separate steps
        },
        previous);
}

void merge (Command& previous, const Command& next)
{
    std::visit (
        [&next] (auto& p)
        {
            using T = std::decay_t<decltype (p)>;
            const auto& n = std::get<T> (next);
            if constexpr (std::is_same_v<T, SetTempo>)
                p.bpm = n.bpm;
            else if constexpr (std::is_same_v<T, SetTimeSignature>)
                p.signature = n.signature;
            else if constexpr (std::is_same_v<T, RenameProject> || std::is_same_v<T, RenameTrack>)
                p.name = n.name;
            else if constexpr (std::is_same_v<T, SetTrackVolume>)
                p.volumeDb = n.volumeDb;
            else if constexpr (std::is_same_v<T, SetTrackPan>)
                p.pan = n.pan;
            else if constexpr (std::is_same_v<T, SetParameter>)
                p.value = n.value;
        },
        previous);
}

} // namespace ap::model
