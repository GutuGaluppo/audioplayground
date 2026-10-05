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

// Compares rendered audio with tests/golden/<name>.wav.
// Set AP_UPDATE_GOLDENS=1 to (re)write the reference instead. Review the diff and listen to the
// new file before committing it.
[[nodiscard]] GoldenResult compareWithGolden (const std::string& name, const WavData& rendered,
                                              float maxAbsoluteError = 1.0e-6f);

} // namespace ap::test
