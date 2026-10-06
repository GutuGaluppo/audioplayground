#include "ap/engine/TimelinePlayer.h"

#include <algorithm>
#include <cmath>

namespace ap::engine
{
namespace
{
using instruments::NoteEvent;

int priority (NoteEvent::Type type) noexcept AP_NONBLOCKING
{
    switch (type)
    {
    case NoteEvent::Type::allNotesOff:
        return 0;
    case NoteEvent::Type::noteOff:
        return 1;
    case NoteEvent::Type::noteOn:
        return 2;
    }
    return 2;
}

NoteEvent noteOff (std::uint8_t pitch) noexcept AP_NONBLOCKING
{
    return {NoteEvent::Type::noteOff, pitch, 0.0f};
}

// Calls fn (timelineTick, note, maxLength) for every note of the clip that starts in the timeline
// range [from, to) (already clipped to the clip). maxLength is how long the note may sound before
// the loop boundary cuts it.
template <typename Fn>
void forEachNoteStart (const NoteClipRender& clip, core::Ticks from, core::Ticks to,
                       Fn&& fn) noexcept AP_NONBLOCKING
{
    const auto& notes = clip.notes;
    const auto byStart
        = [] (const model::Note& note, core::Ticks t) noexcept AP_NONBLOCKING { return note.start < t; };
    const auto c0 = clip.contentOffset + (from - clip.start);
    const auto c1 = clip.contentOffset + (to - clip.start);

    if (clip.loopLength <= 0)
    {
        for (auto it = std::lower_bound (notes.begin(), notes.end(), c0, byStart);
             it != notes.end() && it->start < c1; ++it)
            fn (clip.start + it->start - clip.contentOffset, *it, it->length);
        return;
    }

    const auto loop = clip.loopLength;
    for (auto k = c0 / loop; k * loop < c1; ++k)
    {
        const auto base = k * loop;
        const auto lo = std::max (c0, base) - base;
        const auto hi = std::min (c1, base + loop) - base;
        for (auto it = std::lower_bound (notes.begin(), notes.end(), lo, byStart);
             it != notes.end() && it->start < hi; ++it)
            fn (clip.start + base + it->start - clip.contentOffset, *it, loop - it->start);
    }
}
} // namespace

bool NoteEventList::add (int offset, const NoteEvent& event) noexcept AP_NONBLOCKING
{
    if (count >= capacity)
        return false;

    // Insertion from the back: events mostly arrive in order, so this is usually O(1).
    auto position = count++;
    while (position > 0)
    {
        const auto& before = items[static_cast<std::size_t> (position - 1)];
        const bool after = before.offset > offset
                        || (before.offset == offset && priority (before.event.type) > priority (event.type));
        if (!after)
            break;
        items[static_cast<std::size_t> (position)] = before;
        --position;
    }
    items[static_cast<std::size_t> (position)] = {offset, event};
    return true;
}

void TimelinePlayer::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    fadeLength = std::max (1, static_cast<int> (std::lround (fadeSeconds * sampleRate)));
    for (auto& ends : noteEnds)
        ends.fill (-1);
    for (auto& list : eventLists)
        list.clear();
    graph = nullptr;
    gainCount = 0;
    hasExpected = false;
    fadeInRemaining = 0;
    tailRemaining = 0;
}

void TimelinePlayer::beginBlock (const RenderGraph* currentGraph, int numSamples) noexcept AP_NONBLOCKING
{
    graph = currentGraph;
    blockSize = std::max (1, numSamples);
    for (auto& list : eventLists)
        list.clear();

    // Each track's gain ramps from where it ended last block (matched by id, so reordering or
    // deleting tracks never makes another track jump) to its new target.
    const auto previousCount = gainCount;
    std::copy_n (gains.begin(), previousCount, previousGains.begin());
    gainCount = 0;
    if (graph == nullptr)
        return;

    for (const auto& track : graph->tracks)
    {
        if (gainCount >= gains.size())
            break;
        const float left = track.audible ? track.leftGain : 0.0f;
        const float right = track.audible ? track.rightGain : 0.0f;
        GainRamp ramp {left, right, left, right};
        for (std::size_t i = 0; i < previousCount; ++i)
            if (previousGains[i].id == track.id)
            {
                ramp.fromLeft = previousGains[i].ramp.toLeft;
                ramp.fromRight = previousGains[i].ramp.toRight;
                break;
            }
        gains[gainCount++] = {track.id, ramp};
    }
}

GainRamp TimelinePlayer::instrumentGain (model::InstrumentKind instrument) const noexcept AP_NONBLOCKING
{
    const int index = graph != nullptr ? graph->instrumentTrack[static_cast<std::size_t> (instrument)] : -1;
    if (index >= 0 && static_cast<std::size_t> (index) < gainCount)
        return gains[static_cast<std::size_t> (index)].ramp;
    return {};
}

void TimelinePlayer::segment (core::AudioBlock output, int offset, int length, core::Samples start,
                              const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING
{
    if (length <= 0)
        return;

    if (!hasExpected || start != expectedNext)
    {
        if (hasExpected)
            jumpFrom (expectedNext, offset);
        fadeInRemaining = fadeLength;
    }

    renderTail (output, offset, length, tempoMap);

    renderAudio (output, offset, length, start, tempoMap,
                 fadeInRemaining > 0 ? Fade {Fade::Kind::in, fadeInRemaining} : Fade {});
    fadeInRemaining = std::max (0, fadeInRemaining - length);

    releaseDueNotes (offset, length, start);
    scheduleNotes (offset, length, start, tempoMap);

    expectedNext = start + length;
    hasExpected = true;
}

void TimelinePlayer::endBlock (core::AudioBlock output, bool playing,
                               const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING
{
    if (playing)
        return;

    if (hasExpected)
    {
        jumpFrom (expectedNext, 0);
        hasExpected = false;
        fadeInRemaining = 0;
    }
    renderTail (output, 0, blockSize, tempoMap);
}

void TimelinePlayer::jumpFrom (core::Samples position, int offset) noexcept AP_NONBLOCKING
{
    tailPosition = position;
    tailRemaining = fadeLength;
    releaseAllNotes (offset);
}

void TimelinePlayer::renderTail (core::AudioBlock output, int offset, int length,
                                 const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING
{
    if (tailRemaining <= 0 || length <= 0)
        return;

    const int count = std::min (length, tailRemaining);
    renderAudio (output, offset, count, tailPosition, tempoMap, {Fade::Kind::out, tailRemaining});
    tailPosition += count;
    tailRemaining -= count;
}

void TimelinePlayer::renderAudio (core::AudioBlock output, int offset, int length, core::Samples start,
                                  const core::TempoMap& tempoMap, Fade jumpFade) noexcept AP_NONBLOCKING
{
    if (graph == nullptr || output.isEmpty() || length <= 0)
        return;

    const auto end = start + length;
    if (end <= 0)
        return;

    const auto fade = static_cast<core::Samples> (fadeLength);
    const bool stereo = output.numChannels >= 2;

    for (std::size_t t = 0; t < graph->tracks.size() && t < gainCount; ++t)
    {
        const auto& track = graph->tracks[t];
        const auto& ramp = gains[t].ramp;
        if (track.kind != model::TrackKind::audio || track.audioClips.empty() || ramp.isSilent())
            continue;

        for (const auto& clip : track.audioClips)
        {
            const auto clipStart = tempoMap.ticksToSamples (clip.start);
            if (clipStart >= end)
                break; // clips are sorted by start
            const auto clipEnd = tempoMap.ticksToSamples (clip.end);
            if (clipEnd <= start)
                continue;

            const auto* buffer = clip.buffer.get();
            if (buffer == nullptr || !buffer->isValid() || buffer->sampleRate != sampleRate)
                continue; // missing, still loading, or decoded for another device rate

            const auto frames = buffer->frames();
            const auto firstFrame = core::flicksToFrames (clip.sourceOffset, sampleRate);
            // Fade only edges that cut into the audio, so a clip that starts at the beginning of
            // its file keeps its attack intact.
            const bool fadeStart = firstFrame > 0;
            const bool fadeEnd = firstFrame + (clipEnd - clipStart) < frames;

            const auto from = std::max (start, clipStart);
            const auto to = std::min ({end, clipEnd, clipStart + frames - firstFrame});

            const float* left = buffer->channels[0].data();
            const float* right = buffer->channels.size() > 1 ? buffer->channels[1].data() : left;

            for (auto position = from; position < to; ++position)
            {
                const auto relative = position - start;
                const int index = offset + static_cast<int> (relative);

                float gain = 1.0f;
                if (jumpFade.kind != Fade::Kind::none)
                {
                    const auto due = std::max<core::Samples> (0, jumpFade.remaining - relative);
                    const float faded = static_cast<float> (due) / static_cast<float> (fade);
                    gain = jumpFade.kind == Fade::Kind::out ? faded : 1.0f - faded;
                }
                if (fadeStart && position - clipStart < fade)
                    gain *= static_cast<float> (position - clipStart + 1) / static_cast<float> (fade);
                if (fadeEnd && clipEnd - position <= fade)
                    gain *= static_cast<float> (clipEnd - position) / static_cast<float> (fade + 1);

                const auto frame = static_cast<std::size_t> (firstFrame + (position - clipStart));
                const float l = left[frame] * gain * ramp.left (index, blockSize);
                const float r = right[frame] * gain * ramp.right (index, blockSize);
                if (stereo)
                {
                    output.channels[0][index] += l;
                    output.channels[1][index] += r;
                }
                else
                    output.channels[0][index] += 0.5f * (l + r);
            }
        }
    }
}

void TimelinePlayer::releaseDueNotes (int offset, int length, core::Samples start) noexcept AP_NONBLOCKING
{
    const auto end = start + length;
    for (std::size_t k = 0; k < model::numInstrumentKinds; ++k)
        for (std::size_t pitch = 0; pitch < 128; ++pitch)
        {
            auto& noteEnd = noteEnds[k][pitch];
            if (noteEnd >= 0 && noteEnd < end)
            {
                const auto at = offset + static_cast<int> (std::max<core::Samples> (0, noteEnd - start));
                eventLists[k].add (at, noteOff (static_cast<std::uint8_t> (pitch)));
                noteEnd = -1;
            }
        }
}

void TimelinePlayer::releaseAllNotes (int offset) noexcept AP_NONBLOCKING
{
    for (std::size_t k = 0; k < model::numInstrumentKinds; ++k)
        for (std::size_t pitch = 0; pitch < 128; ++pitch)
            if (noteEnds[k][pitch] >= 0)
            {
                eventLists[k].add (offset, noteOff (static_cast<std::uint8_t> (pitch)));
                noteEnds[k][pitch] = -1;
            }
}

void TimelinePlayer::startNote (model::InstrumentKind instrument, int offset, const model::Note& note,
                                core::Samples offSample, core::Samples segmentEnd, int segmentOffset,
                                core::Samples segmentStart) noexcept AP_NONBLOCKING
{
    const auto k = static_cast<std::size_t> (instrument);
    auto& list = eventLists[k];
    if (list.size() >= NoteEventList::capacity / 2)
        return; // keep room for the note offs of notes already playing

    auto& noteEnd = noteEnds[k][note.pitch];
    if (noteEnd >= 0)
    {
        list.add (offset, noteOff (note.pitch));
        noteEnd = -1;
    }

    list.add (offset, {NoteEvent::Type::noteOn, note.pitch, note.velocity});
    if (offSample < segmentEnd)
        list.add (segmentOffset + static_cast<int> (offSample - segmentStart), noteOff (note.pitch));
    else
        noteEnd = offSample;
}

void TimelinePlayer::scheduleNotes (int offset, int length, core::Samples start,
                                    const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING
{
    if (graph == nullptr)
        return;

    const auto end = start + length;
    const auto from = std::max<core::Samples> (start, 0); // nothing plays during the count-in
    if (end <= from)
        return;

    const auto firstTick = tempoMap.tickAtOrBefore (from);
    const auto lastTick = tempoMap.tickAtOrBefore (end - 1) + 1;

    for (std::size_t k = 0; k < model::numInstrumentKinds; ++k)
    {
        const int trackIndex = graph->instrumentTrack[k];
        if (trackIndex < 0)
            continue;

        const auto instrument = static_cast<model::InstrumentKind> (k);
        for (const auto& clip : graph->tracks[static_cast<std::size_t> (trackIndex)].noteClips)
        {
            if (clip.start >= lastTick)
                break; // sorted by start
            if (clip.end <= firstTick)
                continue;

            const auto tickFrom = std::max (clip.start, firstTick);
            const auto tickTo = std::min (clip.end, lastTick);
            if (tickFrom >= tickTo)
                continue;

            forEachNoteStart (
                clip, tickFrom, tickTo,
                [&] (core::Ticks tick, const model::Note& note, core::Ticks maxLength) noexcept AP_NONBLOCKING
                {
                    const auto onSample = tempoMap.ticksToSamples (tick);
                    if (onSample < from || onSample >= end)
                        return;
                    const auto offTick = std::min (tick + std::min (note.length, maxLength), clip.end);
                    const auto offSample = std::max (onSample + 1, tempoMap.ticksToSamples (offTick));
                    startNote (instrument, offset + static_cast<int> (onSample - start), note, offSample, end,
                               offset, start);
                });
        }
    }
}

} // namespace ap::engine
