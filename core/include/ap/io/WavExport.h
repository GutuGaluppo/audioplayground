#pragma once

#include "ap/io/ProjectFiles.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ap::io
{

enum class WavFormat : std::uint8_t
{
    pcm16, // with TPDF dither
    pcm24,
    float32
};

// Writes a finished render as a WAV file (Task 025). Planar channels of equal length (1 or 2).
// The file appears complete or not at all (temporary file, flushed, renamed). PCM formats clip at
// full scale; 16-bit adds triangular (TPDF) dither of +/-1 LSB before rounding, from a seeded
// generator so the same render always produces the same file.
[[nodiscard]] std::optional<IoError> writeWav (const fs::path& path,
                                               const std::vector<std::vector<float>>& channels,
                                               std::uint32_t sampleRate, WavFormat format,
                                               std::uint32_t ditherSeed = 0x2545f491u);

} // namespace ap::io
