#pragma once

#include "ap/model/Project.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace ap::model
{

// Pure clip math (guide §14). Every edit is non-destructive: trimming or splitting only changes
// which part of the content is visible, never the notes or the audio file.

// Validates a clip for a track of the given kind and brings it to canonical form: notes sorted by
// start then pitch, duplicates (same start and pitch) removed, velocities clamped, loop offset
// wrapped. Returns nullopt if the clip is invalid (it is never silently repaired into something
// else). The clip id is not checked.
[[nodiscard]] std::optional<Clip> normaliseClip (Clip clip, const Track& track, const Project& project);

// Moves the clip so it starts at newStart (clamped to the timeline).
[[nodiscard]] Clip moveClip (const Clip& clip, core::Ticks newStart) noexcept;

// Moves the left edge, keeping the right edge and what plays under it in place. Clamped so the
// clip keeps at least Clip::minLength and cannot reveal content before its beginning (audio
// before the start of the file, or notes before content time 0 when not looping).
[[nodiscard]] Clip trimClipStart (const Clip& clip, core::Ticks newStart, double tempoBpm) noexcept;

// Moves the right edge (clamped to at least Clip::minLength and to the timeline).
[[nodiscard]] Clip resizeClip (const Clip& clip, core::Ticks newEnd) noexcept;

// Splits at a timeline position strictly inside the clip (each part at least Clip::minLength).
// The right part has no id yet. Playing both parts sounds exactly like the original.
[[nodiscard]] std::optional<std::pair<Clip, Clip>> splitClip (const Clip& clip, core::Ticks at,
                                                              double tempoBpm);

// Content time (ticks from the content start, after looping) at a timeline position inside the
// clip.
[[nodiscard]] core::Ticks contentTimeAt (const Clip& clip, core::Ticks timelinePosition) noexcept;

// Note edits. A note is identified by its start and pitch (unique within a clip).
[[nodiscard]] Clip withNote (const Clip& clip, const Note& note);
[[nodiscard]] Clip withoutNote (const Clip& clip, core::Ticks start, std::uint8_t pitch);
[[nodiscard]] const Note* findNote (const Clip& clip, core::Ticks start, std::uint8_t pitch) noexcept;

// Drum pattern steps: sixteenth-note positions in the first pattern length of the content,
// played on pad notes 36..51.
[[nodiscard]] bool hasDrumStep (const Clip& clip, std::size_t pad, std::size_t step) noexcept;
[[nodiscard]] Clip withDrumStep (const Clip& clip, std::size_t pad, std::size_t step, bool on);

// Replaces the whole pattern: pads[pad] has bit `step` set when that pad plays on that step. Notes
// outside the 16 drum pad pitches are kept.
[[nodiscard]] Clip withDrumPattern (const Clip& clip,
                                    const std::array<std::uint16_t, DrumKit::numPads>& pads);

// A new, empty drum pattern clip: its content is one pattern long and loops for the whole clip.
[[nodiscard]] Clip makePatternClip (core::Ticks start, core::Ticks length);

// What the app opens with the very first time (plan §3.7, "first sound in under 5 s"): a drum track
// with a four-bar beat and an empty synth track to play over it. Valid by construction (built
// through commands).
[[nodiscard]] Project starterProject();

} // namespace ap::model
