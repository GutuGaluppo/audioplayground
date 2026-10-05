#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace ap::test
{

struct WavData
{
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels; // planar
};

// Minimal 32-bit float WAV I/O for test fixtures only. Not used by the application, which
// must treat audio files as untrusted input (ADR-006).
void writeFloatWav (const std::filesystem::path& path, const WavData& data);
[[nodiscard]] std::optional<WavData> readFloatWav (const std::filesystem::path& path);

} // namespace ap::test
