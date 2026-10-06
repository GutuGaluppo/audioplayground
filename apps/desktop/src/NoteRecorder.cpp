#include "NoteRecorder.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace ap::desktop
{

bool addNoteClips (Session& session, std::string_view description, NotesPerInstrument notes)
{
    const auto bar = session.project().timeSignature.ticksPerBar();
    struct Take
    {
        model::InstrumentKind instrument;
        model::Clip clip;
    };
    std::vector<Take> takes;

    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        // Notes played during the count-in that last into bar 1 start at bar 1; earlier ones are dropped.
        std::vector<model::Note> kept;
        for (auto note : notes[k])
        {
            const auto end = std::min (note.start + note.length, model::maxTimelineTicks);
            if (end <= 0 || note.start >= model::maxTimelineTicks)
                continue;
            note.start = std::max<core::Ticks> (0, note.start);
            note.length = end - note.start;
            if (note.length > 0 && kept.size() < model::Clip::maxNotes)
                kept.push_back (note);
        }
        if (kept.empty())
            continue;

        core::Ticks first = std::numeric_limits<core::Ticks>::max();
        core::Ticks last = 0;
        for (const auto& note : kept)
        {
            first = std::min (first, note.start);
            last = std::max (last, note.start + note.length);
        }

        model::Clip clip;
        clip.start = first / bar * bar;
        clip.length = std::max<core::Ticks> (bar, (last - clip.start + bar - 1) / bar * bar);
        clip.length = std::min (clip.length, model::maxTimelineTicks - clip.start);
        for (auto& note : kept)
            note.start -= clip.start;
        std::sort (kept.begin(), kept.end(), [] (const model::Note& a, const model::Note& b)
                   { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
        kept.erase (std::unique (kept.begin(), kept.end(), [] (const model::Note& a, const model::Note& b)
                                 { return a.start == b.start && a.pitch == b.pitch; }),
                    kept.end());
        clip.notes = std::move (kept);
        takes.push_back ({static_cast<model::InstrumentKind> (k), std::move (clip)});
    }

    if (takes.empty())
        return false;

    return session.performGroup (
        description,
        [&takes] (model::ProjectDocument::Group& group)
        {
            for (auto& take : takes)
            {
                model::TrackId track;
                if (const auto* existing = group.project().findInstrumentTrack (take.instrument))
                    track = existing->id;
                else if (const auto* added = group.perform (model::AddTrack {take.instrument}))
                    track = std::get<model::AddTrack> (*added).created;
                (void)group.perform (model::AddClip {track, std::move (take.clip)});
            }
        });
}

NoteRecorder::NoteRecorder (Session& sessionToUse)
    : session (sessionToUse)
{
}

void NoteRecorder::start()
{
    if (recording)
        return;
    for (auto& notes : held)
        notes.fill (std::nullopt);
    for (auto& notes : played)
        notes.clear();
    recording = true;
}

void NoteRecorder::close (std::size_t instrument, std::uint8_t pitch, core::Ticks end)
{
    auto& note = held[instrument][pitch];
    if (!note)
        return;
    if (played[instrument].size() < model::Clip::maxNotes)
        played[instrument].push_back (
            {note->start, std::max<core::Ticks> (1, end - note->start), pitch, note->velocity});
    note.reset();
}

void NoteRecorder::handle (const engine::PlayedNote& note)
{
    if (!recording || !note.playing)
        return;
    const auto instrument = static_cast<std::size_t> (note.instrument);
    const auto& event = note.event;
    if (instrument >= numInstruments || event.note > 127)
        return;

    using Type = instruments::NoteEvent::Type;
    if (event.type == Type::allNotesOff)
    {
        for (std::size_t pitch = 0; pitch < 128; ++pitch)
            close (instrument, static_cast<std::uint8_t> (pitch), note.ticks);
    }
    else if (event.type == Type::noteOn && event.velocity > 0.0f)
    {
        close (instrument, event.note, note.ticks); // a retriggered key ends the previous note
        held[instrument][event.note]
            = Held {note.ticks, std::clamp (event.velocity, model::Note::minVelocity, 1.0f)};
    }
    else
        close (instrument, event.note, note.ticks);
}

bool NoteRecorder::stop (core::Ticks stopPosition)
{
    if (!recording)
        return false;
    recording = false;

    for (std::size_t k = 0; k < numInstruments; ++k)
        for (std::size_t pitch = 0; pitch < 128; ++pitch)
            close (k, static_cast<std::uint8_t> (pitch),
                   std::max (stopPosition, held[k][pitch] ? held[k][pitch]->start + 1 : 0));

    return addNoteClips (session, "Record notes", std::exchange (played, {}));
}

} // namespace ap::desktop
