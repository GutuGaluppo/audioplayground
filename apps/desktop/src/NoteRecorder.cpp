#include "NoteRecorder.h"

#include <algorithm>
#include <limits>

namespace ap::desktop
{

NoteRecorder::NoteRecorder (Session& sessionToUse, engine::Engine& engineToUse)
    : session (sessionToUse)
    , engine (engineToUse)
{
}

NoteRecorder::~NoteRecorder()
{
    engine.setNoteRecording (false);
}

void NoteRecorder::start()
{
    if (recording)
        return;
    while (engine.popRecordedNote()) // stale notes from an earlier take
    {
    }
    for (auto& notes : held)
        notes.fill (std::nullopt);
    for (auto& notes : played)
        notes.clear();
    recording = true;
    engine.setNoteRecording (true);
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

void NoteRecorder::poll()
{
    while (const auto recorded = engine.popRecordedNote())
    {
        if (!recording)
            continue;
        const auto instrument = static_cast<std::size_t> (recorded->instrument);
        const auto& event = recorded->event;
        if (instrument >= numInstruments || event.note > 127)
            continue;

        if (event.type == instruments::NoteEvent::Type::noteOn && event.velocity > 0.0f)
        {
            close (instrument, event.note, recorded->ticks); // a retriggered key ends the previous note
            held[instrument][event.note]
                = Held {recorded->ticks, std::clamp (event.velocity, model::Note::minVelocity, 1.0f)};
        }
        else
            close (instrument, event.note, recorded->ticks);
    }
}

bool NoteRecorder::stop (core::Ticks stopPosition)
{
    if (!recording)
        return false;

    engine.setNoteRecording (false);
    poll();
    recording = false;

    const auto bar = session.project().timeSignature.ticksPerBar();
    struct Take
    {
        model::InstrumentKind instrument;
        model::Clip clip;
    };
    std::vector<Take> takes;

    for (std::size_t k = 0; k < numInstruments; ++k)
    {
        for (std::size_t pitch = 0; pitch < 128; ++pitch)
            close (k, static_cast<std::uint8_t> (pitch),
                   std::max (stopPosition, held[k][pitch] ? held[k][pitch]->start + 1 : 0));

        // Notes played during the count-in that last into bar 1 start at bar 1; earlier ones are dropped.
        std::vector<model::Note> notes;
        for (auto note : played[k])
        {
            const auto end = note.start + note.length;
            if (end <= 0)
                continue;
            note.start = std::max<core::Ticks> (0, note.start);
            note.length = end - note.start;
            notes.push_back (note);
        }
        played[k].clear();
        if (notes.empty())
            continue;

        core::Ticks first = std::numeric_limits<core::Ticks>::max();
        core::Ticks last = 0;
        for (const auto& note : notes)
        {
            first = std::min (first, note.start);
            last = std::max (last, note.start + note.length);
        }

        model::Clip clip;
        clip.start = first / bar * bar;
        clip.length = std::max<core::Ticks> (bar, (last - clip.start + bar - 1) / bar * bar);
        clip.length = std::min (clip.length, model::maxTimelineTicks - clip.start);
        for (auto& note : notes)
            note.start -= clip.start;
        clip.notes = std::move (notes);
        takes.push_back ({static_cast<model::InstrumentKind> (k), std::move (clip)});
    }

    if (takes.empty())
        return false;

    return session.performGroup (
        "Record notes",
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

} // namespace ap::desktop
