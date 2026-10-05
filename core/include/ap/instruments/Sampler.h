#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/core/SnapshotExchange.h"
#include "ap/instruments/NoteEvent.h"
#include "ap/instruments/SampleBuffer.h"
#include "ap/params/Parameters.h"

#include <array>
#include <cstdint>
#include <memory>

namespace ap::instruments
{

// Plays one loaded sample chromatically (guide §13.3). C4 (note 60) plays at the original pitch.
//
// - 4-point Hermite interpolation when the playback rate is not 1.
// - Short fades wherever playback would otherwise start or stop mid-waveform (trimmed start/end,
//   voice stealing, gate release, sample swap), so nothing clicks.
// - Swapping the sample is lock-free; the previous buffer is released only after every voice
//   reading it has faded out.
class Sampler
{
public:
    static constexpr int maxVoices = 8; // sounding at once; extra slots let stolen voices fade out
    static constexpr int rootNote = 60;

    enum class Mode : std::uint8_t
    {
        oneShot = 0, // plays to the end regardless of note off
        gate = 1     // stops (with a short fade) on note off
    };

    ~Sampler();

    void prepare (double sampleRate) noexcept;

    // Message thread.
    void loadSample (std::unique_ptr<SampleBuffer> buffer) noexcept { samples.publish (std::move (buffer)); }
    std::size_t collectGarbage() noexcept { return samples.collectGarbage(); }

    // Audio thread.
    void handle (const NoteEvent& event) noexcept AP_NONBLOCKING;
    void render (core::AudioBlock output, const params::ParameterStore& parameters) noexcept AP_NONBLOCKING;

    [[nodiscard]] int activeVoiceCount() const noexcept AP_NONBLOCKING;
    [[nodiscard]] std::uint64_t loadedAssetId() const noexcept
    {
        return currentAssetId.load (std::memory_order_acquire);
    }

private:
    struct Voice
    {
        const SampleBuffer* buffer = nullptr;
        double position = 0.0; // in frames
        double rate = 1.0;
        std::uint8_t note = 0;
        float gain = 0.0f;
        float fade = 0.0f;     // current fade multiplier 0..1
        float fadeStep = 0.0f; // per sample; negative while fading out
        bool held = false;
        std::uint64_t startedAt = 0;

        [[nodiscard]] bool isActive() const noexcept AP_NONBLOCKING { return buffer != nullptr; }
    };

    void swapInPendingSample() noexcept AP_NONBLOCKING;
    void startFadeOut (Voice& voice) noexcept AP_NONBLOCKING;
    Voice& allocateVoice() noexcept AP_NONBLOCKING;

    core::SnapshotExchange<SampleBuffer> samples;
    SampleBuffer* current = nullptr; // audio thread owned
    SampleBuffer* fading = nullptr;  // previous buffer, kept until its voices finish
    std::atomic<std::uint64_t> currentAssetId {0};

    std::array<Voice, 2 * maxVoices> voices {};
    double sampleRate = 48000.0;
    float shortFadeStep = 1.0f;   // 2 ms
    float releaseFadeStep = 1.0f; // 30 ms (gate mode)
    std::uint64_t noteCounter = 0;

    // Read per block.
    std::int64_t startFrame = 0;
    std::int64_t endFrame = 0;
    double pitchSemitones = 0.0;
    float gain = 1.0f;
    Mode mode = Mode::oneShot;
};

} // namespace ap::instruments
