#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/MusicalTime.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/instruments/FactoryKit.h"
#include "ap/instruments/NoteEvent.h"
#include "ap/instruments/SampleSlot.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace ap::instruments
{

// 4x4 pad drum machine with a 16-step sequencer (guide §13.2).
//
// - Every pad plays the factory sound unless a user sample is loaded into it.
// - Steps are sixteenth notes, triggered sample-accurately inside each block.
// - Closed hat chokes open hat.
// - Threading: set*() any thread (atomics); loadPadSample/collectGarbage message thread;
//   trigger/handle/sequence/render audio thread.
class DrumMachine
{
public:
    static constexpr int numPads = 16;
    static constexpr int numSteps = 16;
    static constexpr int maxVoices = 32;
    static constexpr int firstMidiNote = 36; // GM kick; pads map to notes 36..51
    static constexpr core::Ticks ticksPerStep = core::ticksPerQuarterNote / 4;
    static constexpr float minVolumeDb = -60.0f;
    static constexpr float maxVolumeDb = 6.0f;

    void prepare (double sampleRate); // builds the factory kit (allocates)

    // --- Any thread -------------------------------------------------------------------------
    void setStepMask (int pad, std::uint16_t steps) noexcept;
    void setPad (int pad, float volumeDb, float pitchSemitones, bool muted) noexcept;

    // --- Message thread ---------------------------------------------------------------------
    void loadPadSample (int pad, std::unique_ptr<SampleBuffer> buffer) noexcept; // empty -> factory sound
    std::size_t collectGarbage() noexcept;

    // --- Audio thread -----------------------------------------------------------------------
    void trigger (int pad, float velocity, int delaySamples = 0) noexcept AP_NONBLOCKING;
    void handle (const NoteEvent& event) noexcept AP_NONBLOCKING;

    // Schedules the steps that fall inside one transport segment (see Transport::advance).
    void sequence (int offset, int length, core::Samples startSample,
                   const core::TempoMap& tempoMap) noexcept AP_NONBLOCKING;

    void render (core::AudioBlock output) noexcept AP_NONBLOCKING;

    [[nodiscard]] int activeVoiceCount() const noexcept AP_NONBLOCKING;

private:
    struct Voice
    {
        const SampleBuffer* buffer = nullptr;
        int pad = 0;
        double position = 0.0;
        double rate = 1.0;
        float gain = 0.0f;
        float fade = 1.0f;
        float fadeStep = 0.0f;
        int delay = 0; // samples to wait before starting (sample-accurate scheduling)
        std::uint64_t startedAt = 0;

        [[nodiscard]] bool isActive() const noexcept AP_NONBLOCKING { return buffer != nullptr; }
    };

    struct Pad
    {
        std::atomic<std::uint16_t> steps {0};
        std::atomic<float> volumeDb {0.0f};
        std::atomic<float> pitch {0.0f};
        std::atomic<bool> muted {false};
    };

    [[nodiscard]] const SampleBuffer* soundFor (int pad) const noexcept AP_NONBLOCKING;
    void fadeOutVoices (int pad) noexcept AP_NONBLOCKING;
    void renderVoices (core::AudioBlock output, int offset, int length) noexcept AP_NONBLOCKING;

    std::array<std::unique_ptr<SampleBuffer>, numPads> factory;
    std::array<SampleSlot, numPads> user;
    std::array<Pad, numPads> pads;
    std::array<Voice, maxVoices> voices {};
    double sampleRate = 48000.0;
    float fadeStep = 1.0f; // 3 ms
    std::uint64_t triggerCounter = 0;
};

} // namespace ap::instruments
