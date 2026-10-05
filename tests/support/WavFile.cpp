#include "WavFile.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ap::test
{
namespace
{
constexpr std::uint16_t formatIeeeFloat = 3;
constexpr std::uint16_t bitsPerSample = 32;

template <typename T> void writeLE (std::ostream& out, T value)
{
    std::array<char, sizeof (T)> bytes {};
    std::memcpy (bytes.data(), &value,
                 sizeof (T)); // fixture hosts (macOS/Windows/Linux CI) are little-endian
    out.write (bytes.data(), static_cast<std::streamsize> (bytes.size()));
}

template <typename T> bool readLE (std::istream& in, T& value)
{
    std::array<char, sizeof (T)> bytes {};
    if (!in.read (bytes.data(), static_cast<std::streamsize> (bytes.size())))
        return false;
    std::memcpy (&value, bytes.data(), sizeof (T));
    return true;
}
} // namespace

void writeFloatWav (const std::filesystem::path& path, const WavData& data)
{
    const auto numChannels = static_cast<std::uint16_t> (data.channels.size());
    const auto numFrames = data.channels.empty() ? std::size_t {0} : data.channels.front().size();
    const auto sampleRate = static_cast<std::uint32_t> (data.sampleRate);
    const auto blockAlign = static_cast<std::uint16_t> (numChannels * bitsPerSample / 8);
    const auto dataBytes = static_cast<std::uint32_t> (numFrames * blockAlign);

    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error ("Cannot write " + path.string());

    out.write ("RIFF", 4);
    writeLE<std::uint32_t> (out, 4 + (8 + 16) + (8 + dataBytes));
    out.write ("WAVE", 4);

    out.write ("fmt ", 4);
    writeLE<std::uint32_t> (out, 16);
    writeLE<std::uint16_t> (out, formatIeeeFloat);
    writeLE<std::uint16_t> (out, numChannels);
    writeLE<std::uint32_t> (out, sampleRate);
    writeLE<std::uint32_t> (out, sampleRate * blockAlign);
    writeLE<std::uint16_t> (out, blockAlign);
    writeLE<std::uint16_t> (out, bitsPerSample);

    out.write ("data", 4);
    writeLE<std::uint32_t> (out, dataBytes);
    for (std::size_t frame = 0; frame < numFrames; ++frame)
        for (const auto& channel : data.channels)
            writeLE<float> (out, channel[frame]);
}

std::optional<WavData> readFloatWav (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (!in)
        return std::nullopt;

    std::array<char, 4> id {};
    std::uint32_t size = 0;
    if (!in.read (id.data(), 4) || std::memcmp (id.data(), "RIFF", 4) != 0 || !readLE (in, size)
        || !in.read (id.data(), 4) || std::memcmp (id.data(), "WAVE", 4) != 0)
        return std::nullopt;

    std::uint16_t format = 0, numChannels = 0, bits = 0;
    std::uint32_t sampleRate = 0;
    bool haveFormat = false;

    while (in.read (id.data(), 4) && readLE (in, size))
    {
        if (std::memcmp (id.data(), "fmt ", 4) == 0)
        {
            std::uint32_t byteRate = 0;
            std::uint16_t blockAlign = 0;
            if (size < 16 || !readLE (in, format) || !readLE (in, numChannels) || !readLE (in, sampleRate)
                || !readLE (in, byteRate) || !readLE (in, blockAlign) || !readLE (in, bits))
                return std::nullopt;
            in.ignore (static_cast<std::streamsize> (size - 16));
            haveFormat = true;
        }
        else if (std::memcmp (id.data(), "data", 4) == 0)
        {
            if (!haveFormat || format != formatIeeeFloat || bits != bitsPerSample || numChannels == 0)
                return std::nullopt;

            const auto numFrames = size / (numChannels * sizeof (float));
            WavData data;
            data.sampleRate = sampleRate;
            data.channels.assign (numChannels, std::vector<float> (numFrames));
            for (std::size_t frame = 0; frame < numFrames; ++frame)
                for (auto& channel : data.channels)
                    if (!readLE (in, channel[frame]))
                        return std::nullopt;
            return data;
        }
        else
        {
            in.ignore (static_cast<std::streamsize> (size + (size & 1u)));
        }
    }

    return std::nullopt;
}

} // namespace ap::test
