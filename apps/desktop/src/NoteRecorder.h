#pragma once

#include "Session.h"
#include "ap/engine/Engine.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace ap::desktop
{

using NotesPerInstrument = std::array<std::vector<model::Note>, model::numInstrumentKinds>;

// Adds a note clip for each instrument that has notes (in timeline ticks), on that instrument's
// track, creating the track if needed. Each clip spans whole bars around its notes; notes before 0
// are cut at 0 (count-in). All of it is one undo step named description (a string literal).
// Returns true if anything was added.
bool addNoteClips (Session& session, std::string_view description, NotesPerInstrument notes);

// Turns the notes played while recording into note clips (see addNoteClips). One recording is one
// undo step. Message thread only; the host feeds it every played note from the engine's log.
class NoteRecorder
{
public:
    explicit NoteRecorder (Session& session);

    void start();
    // Stops recording and adds what was played. stopPosition closes notes still held. Feed the
    // notes logged so far first. Returns true if a clip was added.
    bool stop (core::Ticks stopPosition);
    [[nodiscard]] bool isRecording() const noexcept { return recording; }

    void handle (const engine::PlayedNote& note);

private:
    struct Held
    {
        core::Ticks start = 0;
        float velocity = 1.0f;
    };

    static constexpr std::size_t numInstruments = model::numInstrumentKinds;

    void close (std::size_t instrument, std::uint8_t pitch, core::Ticks end);

    Session& session;
    bool recording = false;
    std::array<std::array<std::optional<Held>, 128>, numInstruments> held {};
    NotesPerInstrument played;
};

} // namespace ap::desktop
