#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/AudioRing.h"
#include "ap/core/MusicalTime.h"
#include "ap/core/RealtimeSafety.h"

#include <atomic>
#include <cstdint>

namespace ap::engine
{

// Captures the device input while the transport plays, for audio recording (Task 015).
//
// The audio thread copies input frames into a lock-free ring; a disk thread reads them out (see
// ring()). A take is one contiguous stretch of musical time: it starts at the first played block
// after start() and ends at stop(), or earlier when playback jumps (loop wrap, seek), the ring
// overflows (the disk is too slow) or the device restarts. Audio is never captured across a jump,
// so a take always maps to one place on the timeline.
//
// Threading: start(), stop(), finish() and the getters are for the message thread (the getters
// are lock-free and may be read anywhere). capture() runs on the audio thread. deviceStopped()
// is called while the audio callback is not running.
class InputCapture
{
public:
    enum class State : std::uint8_t
    {
        idle,      // nothing to do
        waiting,   // armed: capture begins with the next played block
        capturing, // frames are flowing into the ring
        ended      // no more frames will be written; the ring may still hold unread ones
    };

    enum class EndReason : std::uint8_t
    {
        none,
        stopped,         // stop() was called
        jumped,          // playback jumped (loop wrap or seek)
        overflow,        // the ring was full: the reader fell behind
        noInput,         // the device has no open input
        deviceRestarted, // the device stopped or changed
    };

    static constexpr double ringSeconds = 20.0;

    // Allocates the ring for the sample rate if needed and arms the capture. Only call when idle
    // (after finish()). Returns false if a take is still in progress.
    bool start (double sampleRate);

    // Ends the take. When this returns, the audio thread will not write another frame.
    void stop() noexcept;

    // Back to idle once the reader has drained the ring.
    void finish() noexcept;

    [[nodiscard]] State getState() const noexcept AP_NONBLOCKING
    {
        return stateOf (status.load (std::memory_order_seq_cst));
    }
    [[nodiscard]] EndReason getEndReason() const noexcept
    {
        return static_cast<EndReason> (status.load (std::memory_order_seq_cst) >> 8);
    }

    // Valid once the state has left `waiting`: the transport position of the first captured
    // frame (negative during a count-in) and the number of input channels (1 or 2).
    [[nodiscard]] core::Samples getStartPosition() const noexcept
    {
        return startPosition.load (std::memory_order_acquire);
    }
    [[nodiscard]] int getNumChannels() const noexcept { return numChannels.load (std::memory_order_acquire); }
    [[nodiscard]] double getSampleRate() const noexcept { return rate; }

    // The reader side of the ring (one consumer thread).
    [[nodiscard]] core::AudioRing& ring() noexcept { return frames; }

    // Audio thread: one contiguous segment of played time; input frames [offset, offset + length).
    void capture (core::InputBlock input, int offset, int length,
                  core::Samples position) noexcept AP_NONBLOCKING;

    // The device stopped: a take in progress ends.
    void deviceStopped() noexcept;

private:
    // State and end reason share one atomic so they always change together.
    [[nodiscard]] static constexpr std::uint16_t pack (State s, EndReason reason = EndReason::none) noexcept
    {
        return static_cast<std::uint16_t> (static_cast<unsigned> (s) | (static_cast<unsigned> (reason) << 8));
    }
    [[nodiscard]] static constexpr State stateOf (std::uint16_t packed) noexcept
    {
        return static_cast<State> (packed & 0xff);
    }

    void end (EndReason reason) noexcept AP_NONBLOCKING;

    core::AudioRing frames;
    double rate = 0.0;

    std::atomic<std::uint16_t> status {pack (State::idle)};
    std::atomic<bool> audioThreadInside {false};
    std::atomic<core::Samples> startPosition {0};
    std::atomic<int> numChannels {0};
    core::Samples nextPosition = 0; // audio thread
};

// Where a recorded take belongs on the timeline. The frame captured at transport position p was
// played when the musician heard position p - latency, so the take is shifted earlier by the
// round-trip latency. The clip starts on a whole tick at or after time zero; the sub-tick rest
// (and anything recorded before zero, e.g. during a count-in) is skipped with the source offset,
// so the audio lands on the exact sample.
struct TakePlacement
{
    core::Ticks start = 0;
    core::Ticks length = 0;        // 0 when no recorded audio reaches past time zero
    core::Flicks sourceOffset = 0; // into the recorded file
};

[[nodiscard]] TakePlacement placeTake (core::Samples capturePosition, std::int64_t capturedFrames,
                                       core::Samples latency, const core::TempoMap& tempoMap) noexcept;

} // namespace ap::engine
