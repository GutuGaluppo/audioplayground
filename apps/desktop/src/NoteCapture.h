#pragma once

#include "NoteRecorder.h"

#include <array>
#include <deque>
#include <optional>

namespace ap::desktop
{

// Retroactive capture (Task 014): remembers what was just played, with or without the transport
// running, so that pressing Capture turns it into note clips after the fact.
//
// Capture takes the last phrase: the notes after the last silence of at least phraseGapSeconds.
// A phrase played along with the transport keeps its place on the timeline. One played while
// stopped (or across a loop wrap or seek) keeps its rhythm and starts at the playhead's bar.
// Message thread only; the host feeds it every played note from the engine's log.
class NoteCapture
{
public:
    static constexpr double phraseGapSeconds = 4.0;
    static constexpr std::size_t maxRemembered = model::Clip::maxNotes;

    explicit NoteCapture (Session& session);

    void handle (const engine::PlayedNote& note);

    // Whether there is anything to capture.
    [[nodiscard]] bool hasNotes() const noexcept { return !history.empty() || heldCount > 0; }

    // Adds the last phrase as note clips (one undo step) and forgets everything played so far.
    // now: the engine's heard clock (closes notes still held); playhead: where a phrase played
    // while stopped goes; sampleRate: the engine's. Returns true if a clip was added.
    bool capture (core::Samples now, core::Ticks playhead, double sampleRate);

    // Forget everything (e.g. after a recording took the same notes).
    void clear();

private:
    struct Played
    {
        core::Samples on = 0;
        core::Samples off = 0;
        bool playing = false; // on and off were both played with the transport running
        core::Ticks onTicks = 0;
        core::Ticks offTicks = 0;
        std::uint8_t pitch = 0;
        float velocity = 1.0f;
        model::InstrumentKind instrument = model::InstrumentKind::synth;
    };

    void close (std::size_t instrument, std::uint8_t pitch, const engine::PlayedNote& at);

    Session& session;
    std::deque<Played> history; // in order of release
    std::array<std::array<std::optional<Played>, 128>, model::numInstrumentKinds> held {};
    std::size_t heldCount = 0;
};

} // namespace ap::desktop
