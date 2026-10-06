#include "ap/io/Recordings.h"

#include "FileHandles.h"
#include "ap/model/Project.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>

namespace ap::io
{
namespace
{
static_assert (std::endian::native == std::endian::little, "WAV data is written in native byte order");

constexpr std::size_t headerBytes = 44;
constexpr std::uint16_t formatIeeeFloat = 3;
constexpr std::uint16_t bitsPerSample = 32;
constexpr std::string_view journalFormat = "audio-playground.take";
constexpr std::size_t maxJournalBytes = 4096;

void put16 (std::uint8_t* at, std::uint16_t value)
{
    at[0] = static_cast<std::uint8_t> (value);
    at[1] = static_cast<std::uint8_t> (value >> 8);
}

void put32 (std::uint8_t* at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        at[i] = static_cast<std::uint8_t> (value >> (8 * i));
}

std::uint16_t get16 (const std::uint8_t* at)
{
    return static_cast<std::uint16_t> (at[0] | (at[1] << 8));
}

std::uint32_t get32 (const std::uint8_t* at)
{
    return static_cast<std::uint32_t> (at[0]) | (static_cast<std::uint32_t> (at[1]) << 8)
         | (static_cast<std::uint32_t> (at[2]) << 16) | (static_cast<std::uint32_t> (at[3]) << 24);
}

std::array<std::uint8_t, headerBytes> makeHeader (int channels, std::uint32_t rate, std::uint64_t dataBytes)
{
    const auto data
        = static_cast<std::uint32_t> (std::min<std::uint64_t> (dataBytes, RecordingWriter::maxDataBytes));
    const auto blockAlign = static_cast<std::uint16_t> (channels * bitsPerSample / 8);

    std::array<std::uint8_t, headerBytes> header {};
    std::memcpy (header.data(), "RIFF", 4);
    put32 (header.data() + 4, 36 + data);
    std::memcpy (header.data() + 8, "WAVEfmt ", 8);
    put32 (header.data() + 16, 16);
    put16 (header.data() + 20, formatIeeeFloat);
    put16 (header.data() + 22, static_cast<std::uint16_t> (channels));
    put32 (header.data() + 24, rate);
    put32 (header.data() + 28, rate * blockAlign);
    put16 (header.data() + 32, blockAlign);
    put16 (header.data() + 34, bitsPerSample);
    std::memcpy (header.data() + 36, "data", 4);
    put32 (header.data() + 40, data);
    return header;
}

bool writeSizes (std::FILE* file, std::uint64_t dataBytes)
{
    std::array<std::uint8_t, 4> riff {};
    std::array<std::uint8_t, 4> data {};
    put32 (riff.data(), static_cast<std::uint32_t> (36 + dataBytes));
    put32 (data.data(), static_cast<std::uint32_t> (dataBytes));
    return std::fseek (file, 4, SEEK_SET) == 0 && std::fwrite (riff.data(), 1, 4, file) == 4
        && std::fseek (file, 40, SEEK_SET) == 0 && std::fwrite (data.data(), 1, 4, file) == 4
        && std::fseek (file, 0, SEEK_END) == 0;
}
} // namespace

// --- RecordingWriter -------------------------------------------------------------------------

RecordingWriter::~RecordingWriter()
{
    close();
}

std::optional<IoError> RecordingWriter::open (const fs::path& path, int numChannels, double sampleRate)
{
    close();
    const auto rate = static_cast<std::uint32_t> (sampleRate);
    if (numChannels < 1 || numChannels > 2 || static_cast<double> (rate) != sampleRate || rate < 8000
        || rate > 384000)
        return IoError {"The recording format is not supported."};

    file = detail::openFile (path, "wb");
    if (file == nullptr)
        return IoError {"Could not create the recording file."};

    channels = numChannels;
    frames = 0;
    failed = false;
    const auto header = makeHeader (channels, rate, 0);
    if (std::fwrite (header.data(), 1, header.size(), file) != header.size() || std::fflush (file) != 0)
    {
        std::fclose (file);
        file = nullptr;
        return IoError {"Could not write the recording file."};
    }
    return std::nullopt;
}

bool RecordingWriter::write (const float* const* source, std::size_t numFrames)
{
    if (file == nullptr || failed)
        return false;
    const auto bytesPerFrame = static_cast<std::uint64_t> (channels) * sizeof (float);
    if ((frames + numFrames) * bytesPerFrame > maxDataBytes)
    {
        failed = true;
        return false;
    }

    interleaved.resize (numFrames * static_cast<std::size_t> (channels));
    for (std::size_t i = 0; i < numFrames; ++i)
        for (int ch = 0; ch < channels; ++ch)
            interleaved[i * static_cast<std::size_t> (channels) + static_cast<std::size_t> (ch)]
                = source[ch][i];

    if (std::fwrite (interleaved.data(), sizeof (float), interleaved.size(), file) != interleaved.size())
    {
        failed = true;
        return false;
    }
    frames += numFrames;
    return true;
}

bool RecordingWriter::commit()
{
    if (file == nullptr || failed)
        return false;
    if (!writeSizes (file, frames * static_cast<std::uint64_t> (channels) * sizeof (float))
        || std::fflush (file) != 0)
        failed = true;
    return !failed;
}

bool RecordingWriter::close()
{
    if (file == nullptr)
        return !failed;
    const bool committed = commit() && detail::flushToDisk (file);
    const bool closed = std::fclose (file) == 0;
    file = nullptr;
    return committed && closed;
}

// --- Recovery --------------------------------------------------------------------------------

std::optional<RecordingInfo> repairRecording (const fs::path& path)
{
    std::error_code ec;
    if (!fs::is_regular_file (fs::symlink_status (path, ec)) || ec)
        return std::nullopt;
    const auto size = fs::file_size (path, ec);
    if (ec || size < headerBytes)
        return std::nullopt;

    std::FILE* file = detail::openFile (path, "r+b");
    if (file == nullptr)
        return std::nullopt;

    std::array<std::uint8_t, headerBytes> header {};
    std::optional<RecordingInfo> result;
    if (std::fread (header.data(), 1, header.size(), file) == header.size())
    {
        const auto channels = get16 (header.data() + 22);
        const auto rate = get32 (header.data() + 24);
        const bool ours
            = std::memcmp (header.data(), "RIFF", 4) == 0
           && std::memcmp (header.data() + 8, "WAVEfmt ", 8) == 0 && get32 (header.data() + 16) == 16
           && get16 (header.data() + 20) == formatIeeeFloat && (channels == 1 || channels == 2)
           && rate >= 8000 && rate <= 384000 && get16 (header.data() + 34) == bitsPerSample
           && get16 (header.data() + 32) == channels * 4 && std::memcmp (header.data() + 36, "data", 4) == 0;
        if (ours)
        {
            const std::uint64_t bytesPerFrame = channels * 4ULL;
            const auto frames
                = std::min<std::uint64_t> (size - headerBytes, RecordingWriter::maxDataBytes) / bytesPerFrame;
            if (writeSizes (file, frames * bytesPerFrame) && detail::flushToDisk (file))
                result = RecordingInfo {frames, channels, static_cast<double> (rate)};
        }
    }
    std::fclose (file);
    if (result)
    {
        // Drop a trailing partial frame so the file length matches the header.
        fs::resize_file (path, headerBytes + result->frames * get16 (header.data() + 32), ec);
        if (ec)
            return std::nullopt;
    }
    return result;
}

// --- Journal ---------------------------------------------------------------------------------

fs::path journalPathFor (const fs::path& recording)
{
    auto path = recording;
    path += std::string (RecordingJournal::extension);
    return path;
}

std::optional<IoError> writeJournal (const fs::path& path, const RecordingJournal& journal)
{
    const nlohmann::ordered_json json = {
        {"format", journalFormat}, {"version", 1},           {"file", journal.file},
        {"track", journal.track},  {"start", journal.start}, {"sourceOffset", journal.sourceOffset},
        {"name", journal.name},
    };
    return writeFileAtomically (path, json.dump (2) + "\n");
}

std::optional<RecordingJournal> readJournal (const fs::path& path)
{
    auto contents = readFileLimited (path, maxJournalBytes);
    if (std::holds_alternative<IoError> (contents))
        return std::nullopt;

    const auto& text = std::get<std::string> (contents);
    const auto json = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (!json.is_object() || json.size() != 7)
        return std::nullopt;

    const auto integer
        = [&json] (const char* key, std::int64_t min, std::int64_t max) -> std::optional<std::int64_t>
    {
        const auto it = json.find (key);
        if (it == json.end() || !it->is_number_integer())
            return std::nullopt;
        const auto value = it->get<std::int64_t>();
        return value >= min && value <= max ? std::optional (value) : std::nullopt;
    };
    const auto textField = [&json] (const char* key) -> std::optional<std::string>
    {
        const auto it = json.find (key);
        return it != json.end() && it->is_string() ? std::optional (it->get<std::string>()) : std::nullopt;
    };

    const auto format = textField ("format");
    const auto version = integer ("version", 1, 1);
    const auto file = textField ("file");
    const auto track = integer ("track", 1, std::numeric_limits<std::int64_t>::max());
    const auto start = integer ("start", 0, model::maxTimelineTicks - model::Clip::minLength);
    const auto offset = integer ("sourceOffset", 0, 3600LL * core::flicksPerSecond);
    const auto name = textField ("name");
    if (!format || *format != journalFormat || !version || !file || !model::isSafeAssetPath (*file) || !track
        || !start || !offset || !name)
        return std::nullopt;

    const auto cleanName = model::sanitiseName (*name, model::Asset::maxNameLength);
    return RecordingJournal {*file, static_cast<std::uint64_t> (*track), *start, *offset,
                             cleanName.value_or ("Take")};
}

std::vector<fs::path> listJournals (const ProjectFolder& folder)
{
    std::vector<fs::path> journals;
    std::error_code ec;
    for (fs::directory_iterator it (folder.audioDirectory(), ec), end; !ec && it != end; it.increment (ec))
    {
        const auto& path = it->path();
        if (path.extension() == RecordingJournal::extension && it->is_regular_file (ec)
            && !it->is_symlink (ec))
            journals.push_back (path);
    }
    std::sort (journals.begin(), journals.end());
    return journals;
}

} // namespace ap::io
