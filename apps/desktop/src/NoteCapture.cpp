#include "NoteCapture.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ap::desktop
{

NoteCapture::NoteCapture (Session& sessionToUse)
    : session (sessionToUse)
{
}

void NoteCapture::clear()
{
    history.clear();
    for (auto& notes : held)
        notes.fill (std::nullopt);
    heldCount = 0;
}

void NoteCapture::close (std::size_t instrument, std::uint8_t pitch, const engine::PlayedNote& at)
{
    auto& note = held[instrument][pitch];
    if (!note)
        return;
    note->off = std::max (at.clock, note->on + 1);
    note->playing = note->playing && at.playing;
    note->offTicks = at.ticks;
    history.push_back (*note);
    if (history.size() > maxRemembered)
        history.pop_front();
    note.reset();
    --heldCount;
}

void NoteCapture::handle (const engine::PlayedNote& note)
{
    const auto instrument = static_cast<std::size_t> (note.instrument);
    const auto& event = note.event;
    if (instrument >= held.size() || event.note > 127)
        return;

    using Type = instruments::NoteEvent::Type;
    if (event.type == Type::allNotesOff)
    {
        for (std::size_t pitch = 0; pitch < 128; ++pitch)
            close (instrument, static_cast<std::uint8_t> (pitch), note);
    }
    else if (event.type == Type::noteOn && event.velocity > 0.0f)
    {
        close (instrument, event.note, note); // a retriggered key ends the previous note
        Played played;
        played.on = note.clock;
        played.playing = note.playing;
        played.onTicks = note.ticks;
        played.pitch = event.note;
        played.velocity = std::clamp (event.velocity, model::Note::minVelocity, 1.0f);
        played.instrument = note.instrument;
        held[instrument][event.note] = played;
        ++heldCount;
    }
    else
        close (instrument, event.note, note);
}

bool NoteCapture::capture (core::Samples now, core::Ticks playhead, double sampleRate)
{
    const auto& project = session.project();
    const core::TempoMap map (project.tempoBpm, project.timeSignature, sampleRate);

    // Notes still held end now. Their timeline end follows from the time they have been held.
    std::vector<Played> notes (history.begin(), history.end());
    for (const auto& perInstrument : held)
        for (const auto& note : perInstrument)
            if (note)
            {
                auto closed = *note;
                closed.off = std::max (now, closed.on + 1);
                closed.offTicks = closed.onTicks + map.samplesToTicks (closed.off - closed.on);
                notes.push_back (closed);
            }
    if (notes.empty())
        return false;

    // The last phrase: split wherever nothing sounded for phraseGapSeconds.
    std::sort (notes.begin(), notes.end(), [] (const Played& a, const Played& b) { return a.on < b.on; });
    const auto gap = static_cast<core::Samples> (phraseGapSeconds * map.getSampleRate());
    std::size_t first = 0;
    core::Samples soundingUntil = notes.front().off;
    for (std::size_t i = 1; i < notes.size(); ++i)
    {
        if (notes[i].on - soundingUntil >= gap)
            first = i;
        soundingUntil = std::max (soundingUntil, notes[i].off);
    }
    const std::vector<Played> phrase (notes.begin() + static_cast<std::ptrdiff_t> (first), notes.end());

    // Played along with the transport, in one continuous pass (no loop wrap or seek): the sample
    // clock and the timeline advanced together, so their difference stayed (nearly) constant.
    const bool alongTransport
        = std::all_of (phrase.begin(), phrase.end(), [] (const Played& p) { return p.playing; });
    bool continuous = alongTransport;
    if (alongTransport)
    {
        const auto drift = [&map] (const Played& p) { return p.on - map.ticksToSamples (p.onTicks); };
        const auto [low, high]
            = std::minmax_element (phrase.begin(), phrase.end(),
                                   [&] (const Played& a, const Played& b) { return drift (a) < drift (b); });
        continuous
            = drift (*high) - drift (*low) <= 2 * static_cast<core::Samples> (map.getSamplesPerTick()) + 1;
    }

    const auto bar = project.timeSignature.ticksPerBar();
    const auto barStart = std::max<core::Ticks> (0, playhead) / bar * bar;
    NotesPerInstrument clips;
    for (const auto& p : phrase)
    {
        model::Note note;
        note.pitch = p.pitch;
        note.velocity = p.velocity;
        if (continuous)
        {
            note.start = p.onTicks;
            note.length = std::max<core::Ticks> (1, p.offTicks - p.onTicks);
        }
        else
        {
            note.start = barStart + map.samplesToTicks (p.on - phrase.front().on);
            note.length = std::max<core::Ticks> (1, map.samplesToTicks (p.off - p.on));
        }
        clips[static_cast<std::size_t> (p.instrument)].push_back (note);
    }

    if (!addNoteClips (session, "Capture", std::move (clips)))
        return false;
    clear();
    return true;
}

} // namespace ap::desktop
