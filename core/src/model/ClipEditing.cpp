#include "ap/model/ClipEditing.h"

#include "ap/model/ProjectDocument.h"

#include <algorithm>
#include <cmath>

namespace ap::model
{
namespace
{
constexpr core::Flicks maxSourceOffset = 24LL * 60 * 60 * core::flicksPerSecond;

bool noteOrder (const Note& a, const Note& b) noexcept
{
    return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
}

core::Ticks positiveModulo (core::Ticks value, core::Ticks divisor) noexcept
{
    const auto result = value % divisor;
    return result < 0 ? result + divisor : result;
}

// Whole ticks of audio before sourceOffset (rounded down, so trimming never reaches before the
// start of the file).
core::Ticks flicksToTicksFloor (core::Flicks flicks, double tempoBpm) noexcept
{
    const double tempo = core::isValidTempo (tempoBpm) ? tempoBpm : 120.0;
    const double ticks = static_cast<double> (flicks) / static_cast<double> (core::flicksPerSecond) * tempo
                       * static_cast<double> (core::ticksPerQuarterNote) / 60.0;
    return static_cast<core::Ticks> (std::floor (ticks));
}
} // namespace

std::optional<Clip> normaliseClip (Clip clip, const Track& track, const Project& project)
{
    if (clip.start < 0 || clip.length < Clip::minLength || clip.start > maxTimelineTicks - clip.length)
        return std::nullopt;

    if (track.kind == TrackKind::audio)
    {
        const bool valid = clip.asset.isValid() && project.findAsset (clip.asset) != nullptr
                        && clip.sourceOffset >= 0 && clip.sourceOffset <= maxSourceOffset
                        && clip.notes.empty() && clip.contentOffset == 0 && clip.loopLength == 0;
        return valid ? std::optional<Clip> (std::move (clip)) : std::nullopt;
    }

    if (clip.asset.isValid() || clip.sourceOffset != 0 || clip.notes.size() > Clip::maxNotes)
        return std::nullopt;
    if (clip.loopLength != 0 && (clip.loopLength < Clip::minLength || clip.loopLength > maxTimelineTicks))
        return std::nullopt;
    if (clip.contentOffset < 0 || clip.contentOffset > maxTimelineTicks)
        return std::nullopt;
    if (clip.loopLength != 0)
        clip.contentOffset %= clip.loopLength;

    for (auto& note : clip.notes)
    {
        if (note.start < 0 || note.start >= maxTimelineTicks || note.length < 1
            || note.length > maxTimelineTicks || note.pitch > 127 || !std::isfinite (note.velocity))
            return std::nullopt;
        note.velocity = std::clamp (note.velocity, Note::minVelocity, 1.0f);
    }

    // Sort, then keep only the last of several notes with the same start and pitch.
    std::stable_sort (clip.notes.begin(), clip.notes.end(), noteOrder);
    std::vector<Note> unique;
    unique.reserve (clip.notes.size());
    for (std::size_t i = 0; i < clip.notes.size(); ++i)
    {
        const bool duplicated = i + 1 < clip.notes.size() && clip.notes[i + 1].start == clip.notes[i].start
                             && clip.notes[i + 1].pitch == clip.notes[i].pitch;
        if (!duplicated)
            unique.push_back (clip.notes[i]);
    }
    clip.notes = std::move (unique);
    return clip;
}

Clip moveClip (const Clip& clip, core::Ticks newStart) noexcept
{
    auto moved = clip;
    moved.start
        = std::clamp<core::Ticks> (newStart, 0, std::max<core::Ticks> (0, maxTimelineTicks - clip.length));
    return moved;
}

Clip trimClipStart (const Clip& clip, core::Ticks newStart, double tempoBpm) noexcept
{
    const auto end = clip.end();
    const auto latest = end - Clip::minLength;

    core::Ticks earliest = 0;
    if (clip.asset.isValid())
        earliest = clip.start - flicksToTicksFloor (clip.sourceOffset, tempoBpm);
    else if (clip.loopLength == 0)
        earliest = clip.start - clip.contentOffset;
    earliest = std::max<core::Ticks> (0, earliest);

    if (latest < earliest)
        return clip;

    const auto start = std::clamp (newStart, earliest, latest);
    const auto delta = start - clip.start;

    auto trimmed = clip;
    trimmed.start = start;
    trimmed.length = end - start;
    if (clip.asset.isValid())
        trimmed.sourceOffset
            = std::max<core::Flicks> (0, clip.sourceOffset + core::ticksToFlicks (delta, tempoBpm));
    else
    {
        trimmed.contentOffset = clip.contentOffset + delta;
        if (clip.loopLength != 0)
            trimmed.contentOffset = positiveModulo (trimmed.contentOffset, clip.loopLength);
    }
    return trimmed;
}

Clip resizeClip (const Clip& clip, core::Ticks newEnd) noexcept
{
    auto resized = clip;
    resized.length = std::clamp (newEnd - clip.start, Clip::minLength,
                                 std::max (Clip::minLength, maxTimelineTicks - clip.start));
    return resized;
}

std::optional<std::pair<Clip, Clip>> splitClip (const Clip& clip, core::Ticks at, double tempoBpm)
{
    if (at < clip.start + Clip::minLength || at > clip.end() - Clip::minLength)
        return std::nullopt;

    auto left = clip;
    left.length = at - clip.start;

    auto right = trimClipStart (clip, at, tempoBpm);
    right.id = {};
    if (right.start != at)
        return std::nullopt; // cannot happen for a valid clip; never produce overlapping parts

    return std::pair {std::move (left), std::move (right)};
}

core::Ticks contentTimeAt (const Clip& clip, core::Ticks timelinePosition) noexcept
{
    const auto content = clip.contentOffset + (timelinePosition - clip.start);
    return clip.loopLength != 0 ? positiveModulo (content, clip.loopLength) : content;
}

const Note* findNote (const Clip& clip, core::Ticks start, std::uint8_t pitch) noexcept
{
    const Note key {start, 1, pitch, 1.0f};
    const auto it = std::lower_bound (clip.notes.begin(), clip.notes.end(), key, noteOrder);
    return it != clip.notes.end() && it->start == start && it->pitch == pitch ? &*it : nullptr;
}

Clip withNote (const Clip& clip, const Note& note)
{
    auto edited = withoutNote (clip, note.start, note.pitch);
    const auto it = std::lower_bound (edited.notes.begin(), edited.notes.end(), note, noteOrder);
    edited.notes.insert (it, note);
    return edited;
}

Clip withoutNote (const Clip& clip, core::Ticks start, std::uint8_t pitch)
{
    auto edited = clip;
    std::erase_if (edited.notes,
                   [start, pitch] (const Note& n) { return n.start == start && n.pitch == pitch; });
    return edited;
}

bool hasDrumStep (const Clip& clip, std::size_t pad, std::size_t step) noexcept
{
    if (pad >= DrumKit::numPads || step >= DrumKit::numSteps)
        return false;
    return findNote (clip, static_cast<core::Ticks> (step) * DrumKit::ticksPerStep,
                     static_cast<std::uint8_t> (DrumKit::firstPadNote + pad))
        != nullptr;
}

Clip withDrumStep (const Clip& clip, std::size_t pad, std::size_t step, bool on)
{
    if (pad >= DrumKit::numPads || step >= DrumKit::numSteps)
        return clip;
    const auto start = static_cast<core::Ticks> (step) * DrumKit::ticksPerStep;
    const auto pitch = static_cast<std::uint8_t> (DrumKit::firstPadNote + pad);
    return on ? withNote (clip, Note {start, DrumKit::ticksPerStep, pitch, 1.0f})
              : withoutNote (clip, start, pitch);
}

Clip makePatternClip (core::Ticks start, core::Ticks length)
{
    Clip clip;
    clip.start = start;
    clip.length = length;
    clip.loopLength = DrumKit::patternLength;
    return clip;
}

Project starterProject()
{
    ProjectDocument doc;
    (void)doc.perform (RenameProject {"First song"});
    (void)doc.perform (AddTrack {InstrumentKind::drums});
    (void)doc.perform (AddTrack {InstrumentKind::synth});

    // Kick on 1 and the "and" of 2 and 3, snare on 2 and 4, closed hats on the eighths.
    constexpr std::size_t kick = 0;
    constexpr std::size_t snare = 1;
    constexpr std::size_t closedHat = 2;
    auto beat = makePatternClip (0, 4 * DrumKit::patternLength);
    for (const std::size_t step : {0u, 6u, 10u})
        beat = withDrumStep (beat, kick, step, true);
    for (const std::size_t step : {4u, 12u})
        beat = withDrumStep (beat, snare, step, true);
    for (std::size_t step = 0; step < DrumKit::numSteps; step += 2)
        beat = withDrumStep (beat, closedHat, step, true);
    (void)doc.perform (AddClip {doc.project().tracks.front().id, beat});
    return doc.project();
}

} // namespace ap::model
