#include "Golden.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace ap::test
{

GoldenResult compareWithGolden (const std::string& name, const WavData& rendered, float maxAbsoluteError)
{
    const auto path = std::filesystem::path (AP_GOLDEN_DIR) / (name + ".wav");

    if (const char* update = std::getenv ("AP_UPDATE_GOLDENS");
        update != nullptr && std::string (update) == "1")
    {
        writeFloatWav (path, rendered);
        return {true, "updated " + path.string()};
    }

    const auto reference = readFloatWav (path);
    if (!reference)
        return {false,
                "missing or unreadable golden file " + path.string() + " (run with AP_UPDATE_GOLDENS=1)"};

    if (reference->sampleRate != rendered.sampleRate
        || reference->channels.size() != rendered.channels.size())
        return {false, "format mismatch with " + path.string()};

    float worstError = 0.0f;
    std::size_t worstChannel = 0, worstFrame = 0;

    for (std::size_t ch = 0; ch < rendered.channels.size(); ++ch)
    {
        const auto& actual = rendered.channels[ch];
        const auto& expected = reference->channels[ch];
        if (actual.size() != expected.size())
            return {false, "length mismatch on channel " + std::to_string (ch)};

        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            const float error = std::abs (actual[i] - expected[i]);
            if (!(error <= worstError)) // also catches NaN
            {
                worstError = std::isnan (error) ? INFINITY : error;
                worstChannel = ch;
                worstFrame = i;
            }
        }
    }

    std::ostringstream message;
    message << name << ": max abs error " << worstError << " at channel " << worstChannel << ", frame "
            << worstFrame << " (tolerance " << maxAbsoluteError << ")";
    return {worstError <= maxAbsoluteError, message.str()};
}

} // namespace ap::test
