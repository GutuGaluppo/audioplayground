#include "ap/io/WavExport.h"

#include "FileHandles.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <system_error>

namespace ap::io
{
namespace
{
static_assert (std::endian::native == std::endian::little, "WAV data is written in native byte order");

void put16 (std::uint8_t* at, std::uint32_t value)
{
    at[0] = static_cast<std::uint8_t> (value);
    at[1] = static_cast<std::uint8_t> (value >> 8);
}

void put32 (std::uint8_t* at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        at[i] = static_cast<std::uint8_t> (value >> (8 * i));
}

// Small, fast, seeded generator for the dither (quality of randomness is not critical here).
struct Xorshift
{
    std::uint32_t state;
    float uniform() // [0, 1)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state >> 8) / static_cast<float> (1u << 24);
    }
};

constexpr std::size_t framesPerChunk = 4096;

[[nodiscard]] std::uint32_t bytesPerSampleOf (WavFormat format) noexcept
{
    return format == WavFormat::pcm16 ? 2 : format == WavFormat::pcm24 ? 3 : 4;
}

// Checks the render and builds the 44-byte header, or says why it cannot be a WAV file.
std::optional<IoError> makeHeader (const std::vector<std::vector<float>>& channels, std::uint32_t sampleRate,
                                   WavFormat format, std::array<std::uint8_t, 44>& header)
{
    const auto numChannels = channels.size();
    if (numChannels < 1 || numChannels > 2 || sampleRate < 8000 || sampleRate > 384000)
        return IoError {"The export format is not supported."};
    const auto frames = channels.front().size();
    for (const auto& channel : channels)
        if (channel.size() != frames)
            return IoError {"The export format is not supported."};

    const std::uint32_t bytesPerSample = bytesPerSampleOf (format);
    const std::uint64_t dataBytes = static_cast<std::uint64_t> (frames) * numChannels * bytesPerSample;
    if (dataBytes > 0xFFFF'FFFFull - 64)
        return IoError {"The export is too long for a WAV file."};

    const auto blockAlign = static_cast<std::uint32_t> (numChannels) * bytesPerSample;
    header.fill (0);
    std::memcpy (header.data(), "RIFF", 4);
    put32 (header.data() + 4, static_cast<std::uint32_t> (36 + dataBytes));
    std::memcpy (header.data() + 8, "WAVEfmt ", 8);
    put32 (header.data() + 16, 16);
    put16 (header.data() + 20, format == WavFormat::float32 ? 3 : 1);
    put16 (header.data() + 22, static_cast<std::uint32_t> (numChannels));
    put32 (header.data() + 24, sampleRate);
    put32 (header.data() + 28, sampleRate * blockAlign);
    put16 (header.data() + 32, blockAlign);
    put16 (header.data() + 34, bytesPerSample * 8);
    std::memcpy (header.data() + 36, "data", 4);
    put32 (header.data() + 40, static_cast<std::uint32_t> (dataBytes));
    return std::nullopt;
}

// Frames [start, end) of the render as interleaved WAV samples, appended to `out`.
void convertFrames (const std::vector<std::vector<float>>& channels, std::size_t start, std::size_t end,
                    WavFormat format, Xorshift& random, std::vector<std::uint8_t>& out)
{
    const auto blockAlign = channels.size() * bytesPerSampleOf (format);
    const auto first = out.size();
    out.resize (first + (end - start) * blockAlign);
    auto* at = out.data() + first;
    for (auto i = start; i < end; ++i)
        for (const auto& channel : channels)
        {
            const float x = std::isfinite (channel[i]) ? channel[i] : 0.0f;
            if (format == WavFormat::float32)
            {
                std::memcpy (at, &x, 4);
                at += 4;
            }
            else if (format == WavFormat::pcm24)
            {
                const auto v = static_cast<std::int32_t> (
                    std::lround (std::clamp (x * 8388608.0f, -8388608.0f, 8388607.0f)));
                const auto u = static_cast<std::uint32_t> (v);
                at[0] = static_cast<std::uint8_t> (u);
                at[1] = static_cast<std::uint8_t> (u >> 8);
                at[2] = static_cast<std::uint8_t> (u >> 16);
                at += 3;
            }
            else
            {
                const float dither = random.uniform() - random.uniform(); // triangular, +/-1 LSB
                const auto v = static_cast<std::int32_t> (
                    std::lround (std::clamp (x * 32768.0f + dither, -32768.0f, 32767.0f)));
                put16 (at, static_cast<std::uint32_t> (v));
                at += 2;
            }
        }
}
} // namespace

std::variant<std::vector<std::uint8_t>, IoError> encodeWav (const std::vector<std::vector<float>>& channels,
                                                            std::uint32_t sampleRate, WavFormat format,
                                                            std::uint32_t ditherSeed)
{
    std::array<std::uint8_t, 44> header {};
    if (const auto error = makeHeader (channels, sampleRate, format, header))
        return *error;

    std::vector<std::uint8_t> bytes;
    bytes.reserve (header.size() + channels.front().size() * channels.size() * bytesPerSampleOf (format));
    bytes.insert (bytes.end(), header.begin(), header.end());
    Xorshift random {ditherSeed != 0 ? ditherSeed : 1u};
    const auto frames = channels.front().size();
    for (std::size_t start = 0; start < frames; start += framesPerChunk)
        convertFrames (channels, start, std::min (frames, start + framesPerChunk), format, random, bytes);
    return bytes;
}

std::optional<IoError> writeWav (const fs::path& path, const std::vector<std::vector<float>>& channels,
                                 std::uint32_t sampleRate, WavFormat format, std::uint32_t ditherSeed)
{
    std::array<std::uint8_t, 44> header {};
    if (const auto error = makeHeader (channels, sampleRate, format, header))
        return error;
    const auto frames = channels.front().size();

    auto temporary = path;
    temporary += ".exporting";
    std::FILE* file = detail::openFile (temporary, "wb");
    if (file == nullptr)
        return IoError {"Could not create the export file. Check the folder can be written to."};

    bool ok = std::fwrite (header.data(), 1, header.size(), file) == header.size();
    Xorshift random {ditherSeed != 0 ? ditherSeed : 1u};
    std::vector<std::uint8_t> chunk;
    for (std::size_t start = 0; ok && start < frames; start += framesPerChunk)
    {
        chunk.clear();
        convertFrames (channels, start, std::min (frames, start + framesPerChunk), format, random, chunk);
        ok = std::fwrite (chunk.data(), 1, chunk.size(), file) == chunk.size();
    }

    ok = ok && detail::flushToDisk (file);
    ok = std::fclose (file) == 0 && ok;
    std::error_code ec;
    if (!ok)
    {
        fs::remove (temporary, ec);
        return IoError {"Could not write the export. The disk may be full."};
    }
    fs::rename (temporary, path, ec);
    if (ec)
    {
        fs::remove (temporary, ec);
        return IoError {"Could not save the export file."};
    }
    return std::nullopt;
}

} // namespace ap::io
