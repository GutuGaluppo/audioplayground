#pragma once

#include "ap/core/MusicalTime.h"
#include "ap/core/RealtimeSafety.h"

#include <algorithm>
#include <atomic>

namespace ap::engine
{

// Snapshot of the transport published by the audio thread for the UI (lock-free, ~30 Hz reads).
struct TransportState
{
    bool playing = false;
    core::Samples positionSamples = 0;
    core::Ticks positionTicks = 0;
    bool countingIn = false;
};

// Musical clock driven by the audio callback.
//
// Threading: request*() and set*() may be called from any thread; they only store atomics.
// prepare() is called while audio is stopped. advance() runs on the audio thread.
//
// Positions can be negative while counting in: playback content starts at 0, the metronome
// clicks through the count-in bar(s) before it.
class Transport
{
public:
    static constexpr int maxCountInBars = 4;

    void prepare (double sampleRate) noexcept;

    // --- Control (any thread) ---------------------------------------------------------------
    void requestPlay() noexcept { wantPlaying.store (true, std::memory_order_release); }
    void requestStop() noexcept { wantPlaying.store (false, std::memory_order_release); }
    void requestSeek (core::Ticks ticks) noexcept;

    void setTempo (double bpm) noexcept;
    void setTimeSignature (core::TimeSignature signature) noexcept;
    void setCountInBars (int bars) noexcept;
    // Loop region [start, end). Disabled when end <= start.
    void setLoop (bool enabled, core::Ticks start, core::Ticks end) noexcept;

    [[nodiscard]] TransportState getState() const noexcept;

    struct Loop
    {
        bool enabled = false;
        core::Ticks start = 0;
        core::Ticks end = 0;
    };
    [[nodiscard]] Loop getLoop() const noexcept
    {
        return {loopEnabled.load (std::memory_order_relaxed), loopStartTicks.load (std::memory_order_relaxed),
                loopEndTicks.load (std::memory_order_relaxed)};
    }
    [[nodiscard]] double getTempo() const noexcept { return tempoBpm.load (std::memory_order_relaxed); }
    [[nodiscard]] core::TimeSignature getTimeSignature() const noexcept;
    [[nodiscard]] int getCountInBars() const noexcept { return countInBars.load (std::memory_order_relaxed); }

    // --- Audio thread -----------------------------------------------------------------------
    // Advances the clock by numSamples. Calls segment (offset, length, startSample) for each
    // contiguous stretch of musical time inside the block; a loop wrap splits the block.
    // Not called while stopped.
    template <typename SegmentFn> void advance (int numSamples, SegmentFn&& segment) noexcept AP_NONBLOCKING;

    [[nodiscard]] const core::TempoMap& getTempoMap() const noexcept AP_NONBLOCKING { return tempoMap; }
    [[nodiscard]] bool isPlayingOnAudioThread() const noexcept AP_NONBLOCKING { return playing; }
    [[nodiscard]] core::Samples getPositionOnAudioThread() const noexcept AP_NONBLOCKING { return position; }

private:
    void syncParameters() noexcept AP_NONBLOCKING;
    void publish() noexcept AP_NONBLOCKING;

    // Requests and parameters (written by any thread).
    std::atomic<bool> wantPlaying {false};
    std::atomic<bool> seekPending {false};
    std::atomic<core::Ticks> seekTicks {0};
    std::atomic<double> tempoBpm {120.0};
    std::atomic<int> numerator {4};
    std::atomic<int> denominator {4};
    std::atomic<int> countInBars {0};
    std::atomic<bool> loopEnabled {false};
    std::atomic<core::Ticks> loopStartTicks {0};
    std::atomic<core::Ticks> loopEndTicks {0};

    // Published state (written by the audio thread).
    std::atomic<bool> publishedPlaying {false};
    std::atomic<core::Samples> publishedPosition {0};
    std::atomic<double> publishedSampleRate {48000.0};

    // Audio-thread state.
    double sampleRate = 48000.0;
    core::TempoMap tempoMap {120.0, {}, 48000.0};
    bool playing = false;
    core::Samples position = 0;
    core::Ticks playStartTicks = 0;
};

template <typename SegmentFn>
void Transport::advance (int numSamples, SegmentFn&& segment) noexcept AP_NONBLOCKING
{
    syncParameters();

    if (!playing || numSamples <= 0)
    {
        publish();
        return;
    }

    const bool looping = loopEnabled.load (std::memory_order_relaxed);
    const auto loopStart = tempoMap.ticksToSamples (loopStartTicks.load (std::memory_order_relaxed));
    const auto loopEnd = tempoMap.ticksToSamples (loopEndTicks.load (std::memory_order_relaxed));
    const bool loopActive = looping && loopEnd > loopStart;

    int offset = 0;
    while (offset < numSamples)
    {
        auto length = static_cast<core::Samples> (numSamples - offset);

        // Count-in (negative positions) never loops; inside the loop, stop at the loop end.
        if (loopActive && position >= loopStart && position < loopEnd)
            length = std::min (length, loopEnd - position);

        segment (offset, static_cast<int> (length), position);

        position += length;
        offset += static_cast<int> (length);

        if (loopActive && position >= loopEnd && position - length < loopEnd)
            position = loopStart;
    }

    publish();
}

} // namespace ap::engine
