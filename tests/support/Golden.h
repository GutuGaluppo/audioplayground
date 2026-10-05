#pragma once

#include "WavFile.h"

#include <string>
#include <vector>

namespace ap::test
{

struct GoldenResult
{
    bool passed = false;
    std::string message;
};

// Tolerance for renders that use transcendental functions (sin, tan, exp, tanh) inside recursive
// DSP: each platform's maths library rounds slightly differently and filters accumulate it.
// Measured: 1.5e-5 between macOS and Linux/Windows for the synth chord. 1e-4 is -80 dBFS:
// inaudible, yet any intentional change to the sound exceeds it by orders of magnitude.
inline constexpr float crossPlatformTolerance = 1.0e-4f;

// Compares rendered audio with tests/golden/<name>.wav.
// Set AP_UPDATE_GOLDENS=1 to (re)write the reference instead. Review the diff and listen to the
// new file before committing it.
[[nodiscard]] GoldenResult compareWithGolden (const std::string& name, const WavData& rendered,
                                              float maxAbsoluteError = 1.0e-6f);

} // namespace ap::test
