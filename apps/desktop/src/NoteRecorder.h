#pragma once

#include "Session.h"
#include "ap/engine/Engine.h"

#include <array>
#include <optional>
#include <vector>

namespace ap::desktop
{

// Turns the notes played while recording into a note clip on the track of the instrument they
// were played on, creating that track if needed. One recording is one undo step. The clip spans
// whole bars around what was played. Message thread only.
class NoteRecorder
{
public:
    NoteRecorder (Session& session, engine::Engine& engine);
    ~NoteRecorder();

    void start();
    // Stops recording and adds what was played. stopPosition closes notes still held.
    // Returns true if a clip was added.
    bool stop (core::Ticks stopPosition);
    [[nodiscard]] bool isRecording() const noexcept { return recording; }

    // Collects notes from the engine; call regularly while recording.
    void poll();

private:
    struct Held
    {
        core::Ticks start = 0;
        float velocity = 1.0f;
    };

    static constexpr std::size_t numInstruments = model::numInstrumentKinds;

    void close (std::size_t instrument, std::uint8_t pitch, core::Ticks end);

    Session& session;
    engine::Engine& engine;
    bool recording = false;
    std::array<std::array<std::optional<Held>, 128>, numInstruments> held {};
    std::array<std::vector<model::Note>, numInstruments> played;
};

} // namespace ap::desktop
