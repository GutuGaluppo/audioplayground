#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/MusicalTime.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/engine/RenderGraph.h"
#include "ap/instruments/NoteEvent.h"

#include <array>
#include <cstdint>

namespace ap::engine
{

// Note events for one instrument in one block, kept sorted by offset. At the same offset, note
// offs come before note ons, so a repeated note retriggers instead of being cut.
class NoteEventList
{
public:
    static constexpr int capacity = 512;

    struct Item
    {
        int offset = 0;
        instruments::NoteEvent event;
    };

    void clear() noexcept AP_NONBLOCKING { count = 0; }

    // Returns false (and drops the event) if the list is full.
    bool add (int offset, const instruments::NoteEvent& event) noexcept AP_NONBLOCKING;

    [[nodiscard]] int size() const noexcept AP_NONBLOCKING { return count; }
    [[nodiscard]] const Item& operator[] (int index) const noexcept AP_NONBLOCKING
    {
        return items[static_cast<std::size_t> (index)];
    }

private:
    std::array<Item, capacity> items {};
    int count = 0;
};

// A track gain that moves linearly from `from` to `to` across one block, so volume, pan and mute
// changes never click.
struct GainRamp
{
    float fromLeft = 1.0f;
    float fromRight = 1.0f;
    float toLeft = 1.0f;
    float toRight = 1.0f;

    [[nodiscard]] float left (int index, int blockSize) const noexcept AP_NONBLOCKING
    {
        return fromLeft
             + (toLeft - fromLeft) * static_cast<float> (index + 1) / static_cast<float> (blockSize);
    }
    [[nodiscard]] float right (int index, int blockSize) const noexcept AP_NONBLOCKING
    {
        return fromRight
             + (toRight - fromRight) * static_cast<float> (index + 1) / static_cast<float> (blockSize);
    }
    [[nodiscard]] bool isSilent() const noexcept AP_NONBLOCKING
    {
        return fromLeft == 0.0f && fromRight == 0.0f && toLeft == 0.0f && toRight == 0.0f;
    }
};

// One stereo bus per track of the render graph, at unity gain (the engine applies the track's
// effects, then its volume and pan). Bus t's left channel starts at data + 2 * t * stride, its right
// channel stride samples later.
struct TrackBuses
{
    float* data = nullptr;
    int stride = 0;
    std::size_t count = 0;

    [[nodiscard]] float* left (std::size_t track) const noexcept AP_NONBLOCKING
    {
        return data + 2 * track * static_cast<std::size_t> (stride);
    }
    [[nodiscard]] float* right (std::size_t track) const noexcept AP_NONBLOCKING
    {
        return left (track) + stride;
    }
};

// Plays the timeline (ADR-007) on the audio thread: renders audio clips into their tracks' buses
// and turns note clips into sample-accurate note events for the instruments, which the engine
// renders.
//
// - Audio clips fade over 2 ms at their edges and wherever playback jumps (start, seek, loop,
//   stop); a jump also lets the audio that was playing fade out instead of cutting it.
// - Every note it starts it also ends: at the note's end, the clip's end or the loop boundary,
//   and immediately when playback jumps or stops.
class TimelinePlayer
{
public:
    static constexpr double fadeSeconds = 0.002;

    void prepare (double sampleRate) noexcept;

    // Once per block, before the transport advances.
    void beginBlock (const RenderGraph* graph, int numSamples) noexcept AP_NONBLOCKING;

    // For each contiguous stretch of musical time in the block (see Transport::advance).
    void segment (const TrackBuses& buses, int offset, int length, core::Samples start,
                  const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING;

    // Once per block, after the transport advanced.
    void endBlock (const TrackBuses& buses, bool playing,
                   const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING;

    [[nodiscard]] NoteEventList& events (model::InstrumentKind instrument) noexcept AP_NONBLOCKING
    {
        return eventLists[static_cast<std::size_t> (instrument)];
    }

    // Gain (volume x pan x mute/solo) of the graph's track at this index for this block.
    [[nodiscard]] GainRamp trackGain (std::size_t index) const noexcept AP_NONBLOCKING
    {
        return index < gainCount ? gains[index].ramp : GainRamp {};
    }

private:
    struct TrackGain
    {
        model::TrackId id;
        GainRamp ramp;
    };

    // Fade applied on top of the clip edge fades: none, in (after a jump) or out (the tail before
    // it). Expressed in whole samples so the result never depends on how blocks are split.
    struct Fade
    {
        enum class Kind : std::uint8_t
        {
            none,
            in,
            out
        };
        Kind kind = Kind::none;
        int remaining = 0; // samples of the fade left at the start of the range
    };

    void renderAudio (const TrackBuses& buses, int offset, int length, core::Samples start,
                      const core::TempoMap& tempoMap, Fade fade) noexcept AP_NONBLOCKING;
    void scheduleNotes (int offset, int length, core::Samples start,
                        const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING;
    void releaseDueNotes (int offset, int length, core::Samples start) noexcept AP_NONBLOCKING;
    void releaseAllNotes (int offset) noexcept AP_NONBLOCKING;
    void startNote (model::InstrumentKind instrument, int offset, const model::Note& note,
                    core::Samples offSample, core::Samples segmentEnd, int segmentOffset,
                    core::Samples segmentStart) noexcept AP_NONBLOCKING;
    void jumpFrom (core::Samples position, int offset) noexcept AP_NONBLOCKING;
    void renderTail (const TrackBuses& buses, int offset, int length,
                     const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING;

    const RenderGraph* graph = nullptr;
    double sampleRate = 48000.0;
    int fadeLength = 96;
    int blockSize = 0;

    std::array<NoteEventList, model::numInstrumentKinds> eventLists;

    // Timeline sample at which each note started by the timeline ends (-1: not playing).
    std::array<std::array<core::Samples, 128>, model::numInstrumentKinds> noteEnds {};

    std::array<TrackGain, model::Project::maxTracks> gains {};
    std::array<TrackGain, model::Project::maxTracks> previousGains {};
    std::size_t gainCount = 0;

    // Continuity of playback: where the next segment should start if nothing jumped.
    bool hasExpected = false;
    core::Samples expectedNext = 0;
    int fadeInRemaining = 0;

    // Audio that was playing before a jump, fading out.
    core::Samples tailPosition = 0;
    int tailRemaining = 0;
};

} // namespace ap::engine
