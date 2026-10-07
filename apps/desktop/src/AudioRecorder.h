#pragma once

#include "Session.h"
#include "ap/engine/Engine.h"
#include "ap/io/Recordings.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace ap::desktop
{

// Records the device input into an audio clip on the armed track (Task 015).
//
// The engine captures the input into a lock-free ring while the transport plays; a disk thread
// writes it to a WAV file in the project's audio folder as it arrives. When the take ends it
// becomes an asset and a clip in one undo step, placed where the musician heard the music (the
// round-trip latency is compensated). Overlapping takes all play (overdub, ADR-007).
//
// Crash safety: the WAV header is committed every half second, and a journal next to the file
// says where the take belongs, so recoverInterrupted() can put it back on the timeline at the next
// start. Message thread only (apart from the private disk thread).
class AudioRecorder
{
public:
    static constexpr double maxTakeSeconds = 10.0 * 60.0; // what SampleLoader accepts

    AudioRecorder (Session& session, engine::Engine& engine);
    // Abandons a take in progress: the file is completed and its journal kept, so it is recovered
    // at the next start rather than lost.
    ~AudioRecorder();

    // The audio track takes are recorded onto (invalid = none: recording records notes only).
    void setArmedTrack (model::TrackId track) noexcept { armed = track; }
    [[nodiscard]] model::TrackId getArmedTrack() const noexcept { return armed; }

    // Starts a take on the armed track; it begins with the next played block. latency: the
    // device's round-trip latency in samples (the engine's own output latency is added). Returns a message
    // for the user on failure.
    [[nodiscard]] std::optional<std::string> start (core::Samples latency);

    // Ends the take and adds it to the timeline. Returns true if a clip was added.
    bool stop();

    [[nodiscard]] bool isRecording() const noexcept { return recording; }

    // Call regularly while recording: records where the take belongs once it has begun, and ends
    // the take when the engine ended it (loop wrap, device change...) or it reached its limit.
    void poll();

    // Puts takes interrupted by a crash back on the timeline (one undo step each). Returns a message
    // for the user when something was recovered.
    std::optional<std::string> recoverInterrupted();

    // Errors and explanations for the user.
    std::function<void (const std::string&)> onNotice;

    // A finished take is now a clip (called after the undo step exists).
    std::function<void (model::ClipId)> onTakeAdded;

private:
    struct Take
    {
        io::fs::path file;
        std::string relativePath;
        std::string name;
        model::TrackId track;
        core::Samples latency = 0;
        bool journalWritten = false;
    };

    void writerLoop();
    void endWriter();
    void notice (const std::string& message);
    [[nodiscard]] model::TrackId targetTrack (model::ProjectDocument::Group& group,
                                              model::TrackId wanted) const;

    Session& session;
    engine::Engine& engine;
    model::TrackId armed;
    bool recording = false;
    Take take;

    // Disk thread. The writer belongs to it until it is joined.
    std::thread writerThread;
    io::RecordingWriter writer;
    std::atomic<bool> stopWriter {false};
    std::atomic<bool> writerFailed {false};
    std::atomic<std::uint64_t> framesOnDisk {0};
};

} // namespace ap::desktop
