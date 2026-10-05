#pragma once

#include <cstdint>

namespace ap::instruments
{

struct NoteEvent
{
    enum class Type : std::uint8_t
    {
        noteOn,
        noteOff,
        allNotesOff
    };

    Type type = Type::noteOn;
    std::uint8_t note = 60; // MIDI note number, 0..127
    float velocity = 1.0f;  // 0..1 (note on only)
};

} // namespace ap::instruments
