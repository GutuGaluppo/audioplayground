#pragma once

#include "ap/core/MusicalTime.h"
#include "ap/io/ProjectFiles.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ap::io
{

// Writes a recording to disk while it is being made: 32-bit float WAV, 1 or 2 channels.
//
// The header is rewritten by commit(), which also hands the data to the operating system, so a
// crash (or a killed process) loses at most what was written since the last commit; and
// repairRecording() recovers even that from the file length. Not for the audio thread: the disk
// thread calls it with frames read from the capture ring.
class RecordingWriter
{
public:
    RecordingWriter() = default;
    ~RecordingWriter();
    RecordingWriter (const RecordingWriter&) = delete;
    RecordingWriter& operator= (const RecordingWriter&) = delete;

    [[nodiscard]] std::optional<IoError> open (const fs::path& path, int numChannels, double sampleRate);

    // Planar input; only the first numChannels (from open) channels are read.
    [[nodiscard]] bool write (const float* const* channels, std::size_t numFrames);

    // Updates the header to the frames written so far and flushes to the OS.
    [[nodiscard]] bool commit();

    // commit() and close. Returns false if anything failed since open().
    bool close();

    [[nodiscard]] bool isOpen() const noexcept { return file != nullptr; }
    [[nodiscard]] std::uint64_t framesWritten() const noexcept { return frames; }

    // Largest take: the 4 GiB WAV limit with room to spare, and well above anything the app loads.
    static constexpr std::uint64_t maxDataBytes = 2'000'000'000ULL;

private:
    std::FILE* file = nullptr;
    int channels = 0;
    std::uint64_t frames = 0;
    bool failed = false;
    std::vector<float> interleaved;
};

struct RecordingInfo
{
    std::uint64_t frames = 0;
    int numChannels = 0;
    double sampleRate = 0.0;
};

// Makes a recording that was interrupted by a crash readable again: sets the header's sizes from
// the file's actual length (whole frames only). Only accepts files with the exact layout
// RecordingWriter produces; returns nullopt for anything else.
[[nodiscard]] std::optional<RecordingInfo> repairRecording (const fs::path& path);

// Written next to a recording while it is in progress, so a take interrupted by a crash can be put
// back on the timeline at the next start. Removed once the take is in the project.
struct RecordingJournal
{
    static constexpr std::string_view extension = ".journal";

    std::string file; // project-relative asset path of the WAV ("audio/12-take.wav")
    std::uint64_t track = 0;
    core::Ticks start = 0;
    core::Flicks sourceOffset = 0;
    std::string name; // shown to the user, e.g. "Take 3"

    bool operator== (const RecordingJournal&) const = default;
};

[[nodiscard]] fs::path journalPathFor (const fs::path& recording);
[[nodiscard]] std::optional<IoError> writeJournal (const fs::path& path, const RecordingJournal& journal);
// Strict: anything unexpected (unsafe path, out-of-range values, wrong types) is refused.
[[nodiscard]] std::optional<RecordingJournal> readJournal (const fs::path& path);
// Journals in the project's audio folder (regular files only), oldest name first.
[[nodiscard]] std::vector<fs::path> listJournals (const ProjectFolder& folder);

} // namespace ap::io
