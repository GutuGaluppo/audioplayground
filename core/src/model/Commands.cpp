#include "ap/model/Commands.h"

#include "ap/model/ClipEditing.h"

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

std::string defaultTrackName (const Project& project, TrackKind kind, InstrumentKind instrument)
{
    if (kind == TrackKind::instrument)
    {
        switch (instrument)
        {
        case InstrumentKind::synth:
            return "Synth";
        case InstrumentKind::sampler:
            return "Sampler";
        case InstrumentKind::drums:
            return "Drums";
        }
    }
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

bool isValidInstrument (InstrumentKind instrument) noexcept
{
    return static_cast<std::size_t> (instrument) < numInstrumentKinds;
}

void insertSorted (Track& track, Clip clip)
{
    const auto it
        = std::lower_bound (track.clips.begin(), track.clips.end(), clip, [] (const Clip& a, const Clip& b)
                            { return a.start != b.start ? a.start < b.start : a.id < b.id; });
    track.clips.insert (it, std::move (clip));
}

void eraseClip (Project& project, ClipId id)
{
    if (const auto location = project.locate (id))
    {
        auto& clips = project.tracks[location->track].clips;
        clips.erase (clips.begin() + static_cast<std::ptrdiff_t> (location->clip));
    }
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
                if (c.kind == TrackKind::instrument
                    && (!isValidInstrument (c.instrument)
                        || project.findInstrumentTrack (c.instrument) != nullptr))
                    return ApplyResult::rejected;

                const auto index = c.index.value_or (project.tracks.size());
                if (index > project.tracks.size())
                    return ApplyResult::rejected;

                std::string name;
                if (c.name.empty())
                    name = defaultTrackName (project, c.kind, c.instrument);
                else if (auto sanitised = sanitiseName (c.name, Track::maxNameLength))
                    name = std::move (*sanitised);
                else
                    return ApplyResult::rejected;

                Track track;
                track.id = TrackId {project.nextTrackId};
                track.kind = c.kind;
                track.instrument = c.kind == TrackKind::instrument ? c.instrument : InstrumentKind::synth;
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
            [&project] (SetTrackEffect& c) -> ApplyResult
            {
                auto* track = project.findTrack (c.id);
                const auto value = normaliseEffect (c.effect, c.value);
                if (track == nullptr || !value)
                    return ApplyResult::rejected;
                c.value = *value;
                auto& slot = track->effects[static_cast<std::size_t> (c.effect)];
                if (slot == c.value)
                    return ApplyResult::unchanged;
                c.previous = slot;
                slot = c.value;
                return ApplyResult::applied;
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
            [&project] (AddAsset& c) -> ApplyResult
            {
                if (project.assets.size() >= Project::maxAssets || !isSafeAssetPath (c.relativePath))
                    return ApplyResult::rejected;
                auto name = sanitiseName (c.name, Asset::maxNameLength);
                if (!name)
                    return ApplyResult::rejected;
                c.name = std::move (*name);
                c.created = AssetId {project.nextAssetId};
                c.previousNextAssetId = project.nextAssetId;
                project.assets.push_back ({c.created, c.relativePath, c.name});
                ++project.nextAssetId;
                return ApplyResult::applied;
            },
            [&project] (SetSamplerAsset& c) -> ApplyResult
            {
                if (c.asset.isValid() && project.findAsset (c.asset) == nullptr)
                    return ApplyResult::rejected;
                if (c.asset == project.samplerAsset)
                    return ApplyResult::unchanged;
                c.previous = std::exchange (project.samplerAsset, c.asset);
                return ApplyResult::applied;
            },
            [&project] (SetDrumPad& c) -> ApplyResult
            {
                if (c.pad >= DrumKit::numPads || !std::isfinite (c.value.volumeDb)
                    || !std::isfinite (c.value.pitch))
                    return ApplyResult::rejected;
                if (c.value.sample.isValid() && project.findAsset (c.value.sample) == nullptr)
                    return ApplyResult::rejected;
                c.value.volumeDb = std::clamp (c.value.volumeDb, DrumPad::minVolumeDb, DrumPad::maxVolumeDb);
                c.value.pitch = std::clamp (c.value.pitch, -DrumPad::maxPitch, DrumPad::maxPitch);
                if (c.value == project.drums.pads[c.pad])
                    return ApplyResult::unchanged;
                c.previous = std::exchange (project.drums.pads[c.pad], c.value);
                return ApplyResult::applied;
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
            [&project] (AddClip& c) -> ApplyResult
            {
                auto* track = project.findTrack (c.track);
                if (track == nullptr || track->clips.size() >= Track::maxClips)
                    return ApplyResult::rejected;
                auto clip = normaliseClip (c.clip, *track, project);
                if (!clip)
                    return ApplyResult::rejected;
                clip->id = ClipId {project.nextClipId};
                c.clip = *clip;
                c.created = clip->id;
                c.previousNextClipId = project.nextClipId;
                insertSorted (*track, std::move (*clip));
                ++project.nextClipId;
                return ApplyResult::applied;
            },
            [&project] (RemoveClip& c) -> ApplyResult
            {
                const auto location = project.locate (c.id);
                if (!location)
                    return ApplyResult::rejected;
                c.track = project.tracks[location->track].id;
                c.removed = project.tracks[location->track].clips[location->clip];
                eraseClip (project, c.id);
                return ApplyResult::applied;
            },
            [&project] (SetClip& c) -> ApplyResult
            {
                const auto location = project.locate (c.id);
                auto* target = project.findTrack (c.track);
                if (!location || target == nullptr)
                    return ApplyResult::rejected;
                const auto& source = project.tracks[location->track];
                const bool changesTrack = source.id != target->id;
                if (target->kind != source.kind || (changesTrack && target->clips.size() >= Track::maxClips))
                    return ApplyResult::rejected;

                auto value = normaliseClip (c.value, *target, project);
                if (!value)
                    return ApplyResult::rejected;
                value->id = c.id;
                c.value = *value;

                const auto& current = source.clips[location->clip];
                if (!changesTrack && current == c.value)
                    return ApplyResult::unchanged;

                c.previousTrack = source.id;
                c.previous = current;
                eraseClip (project, c.id);
                insertSorted (*target, c.value);
                return ApplyResult::applied;
            },
            [&project] (SplitClip& c) -> ApplyResult
            {
                const auto location = project.locate (c.id);
                if (!location)
                    return ApplyResult::rejected;
                auto& track = project.tracks[location->track];
                if (track.clips.size() >= Track::maxClips)
                    return ApplyResult::rejected;
                auto parts = splitClip (track.clips[location->clip], c.at, project.tempoBpm);
                if (!parts)
                    return ApplyResult::rejected;

                c.track = track.id;
                c.original = track.clips[location->clip];
                c.created = ClipId {project.nextClipId};
                c.previousNextClipId = project.nextClipId;
                parts->second.id = c.created;
                track.clips[location->clip] = std::move (parts->first); // same start and id: stays sorted
                insertSorted (track, std::move (parts->second));
                ++project.nextClipId;
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
            [&project] (const SetTrackEffect& c)
            {
                if (auto* t = project.findTrack (c.id))
                    t->effects[static_cast<std::size_t> (c.effect)] = c.previous;
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
            [&project] (const AddAsset& c)
            {
                std::erase_if (project.assets, [&c] (const Asset& a) { return a.id == c.created; });
                project.nextAssetId = c.previousNextAssetId;
            },
            [&project] (const SetSamplerAsset& c) { project.samplerAsset = c.previous; },
            [&project] (const SetDrumPad& c) { project.drums.pads[c.pad] = c.previous; },
            [&project] (const SetParameter& c)
            { project.parameters[static_cast<std::size_t> (c.id)] = c.previous; },
            [&project] (const AddClip& c)
            {
                eraseClip (project, c.created);
                project.nextClipId = c.previousNextClipId;
            },
            [&project] (const RemoveClip& c)
            {
                if (auto* track = project.findTrack (c.track))
                    insertSorted (*track, c.removed);
            },
            [&project] (const SetClip& c)
            {
                eraseClip (project, c.id);
                if (auto* track = project.findTrack (c.previousTrack))
                    insertSorted (*track, c.previous);
            },
            [&project] (const SplitClip& c)
            {
                eraseClip (project, c.created);
                eraseClip (project, c.id);
                if (auto* track = project.findTrack (c.track))
                    insertSorted (*track, c.original);
                project.nextClipId = c.previousNextClipId;
            },
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
            [] (const SetTrackEffect& c)
            { return params::effectDescriptors[static_cast<std::size_t> (c.effect)].name; },
            [] (const SetTrackMute&) { return std::string_view ("Mute track"); },
            [] (const SetTrackSolo&) { return std::string_view ("Solo track"); },
            [] (const SetParameter& c) { return params::descriptor (c.id).name; },
            [] (const AddAsset&) { return std::string_view ("Import audio"); },
            [] (const SetSamplerAsset&) { return std::string_view ("Change sample"); },
            [] (const SetDrumPad&) { return std::string_view ("Change pad"); },
            [] (const AddClip&) { return std::string_view ("Add clip"); },
            [] (const RemoveClip&) { return std::string_view ("Delete clip"); },
            [] (const SetClip& c)
            {
                switch (c.edit)
                {
                case ClipEdit::move:
                    return std::string_view ("Move clip");
                case ClipEdit::resize:
                    return std::string_view ("Resize clip");
                case ClipEdit::notes:
                    return std::string_view ("Edit notes");
                case ClipEdit::loop:
                    return std::string_view ("Loop clip");
                }
                return std::string_view ("Edit clip");
            },
            [] (const SplitClip&) { return std::string_view ("Split clip"); },
        },
        command);
}

bool canMerge (const Command& previous, const Command& next) noexcept
{
    // Editing a clip in the same gesture that created it (e.g. painting a new drum pattern) folds
    // into the creation.
    if (const auto* added = std::get_if<AddClip> (&previous))
        if (const auto* edit = std::get_if<SetClip> (&next))
            return added->created == edit->id;

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
            else if constexpr (std::is_same_v<T, SetTrackEffect>)
                return p.id == n.id && p.effect == n.effect;
            else if constexpr (std::is_same_v<T, SetClip>)
                return p.id == n.id && p.edit == n.edit;
            else if constexpr (std::is_same_v<T, SetDrumPad>)
                return p.pad == n.pad;
            else
                return false; // structural edits and toggles are always separate steps
        },
        previous);
}

void merge (Command& previous, const Command& next)
{
    if (auto* added = std::get_if<AddClip> (&previous))
        if (const auto* edit = std::get_if<SetClip> (&next))
        {
            added->track = edit->track;
            added->clip = edit->value;
            return;
        }

    std::visit (
        [&next] (auto& p)
        {
            using T = std::decay_t<decltype (p)>;
            const auto* n = std::get_if<T> (&next);
            if (n == nullptr)
                return;
            if constexpr (std::is_same_v<T, SetTempo>)
                p.bpm = n->bpm;
            else if constexpr (std::is_same_v<T, SetTimeSignature>)
                p.signature = n->signature;
            else if constexpr (std::is_same_v<T, RenameProject> || std::is_same_v<T, RenameTrack>)
                p.name = n->name;
            else if constexpr (std::is_same_v<T, SetTrackVolume>)
                p.volumeDb = n->volumeDb;
            else if constexpr (std::is_same_v<T, SetTrackPan>)
                p.pan = n->pan;
            else if constexpr (std::is_same_v<T, SetTrackEffect>)
                p.value = n->value;
            else if constexpr (std::is_same_v<T, SetParameter>)
                p.value = n->value;
            else if constexpr (std::is_same_v<T, SetClip>)
            {
                p.track = n->track;
                p.value = n->value;
            }
            else if constexpr (std::is_same_v<T, SetDrumPad>)
                p.value = n->value;
        },
        previous);
}

} // namespace ap::model
