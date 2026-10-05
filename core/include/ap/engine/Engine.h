#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/core/SnapshotExchange.h"
#include "ap/core/SpscQueue.h"
#include "ap/dsp/LinearSmoothedValue.h"
#include "ap/dsp/SineOscillator.h"
#include "ap/engine/Metronome.h"
#include "ap/engine/RenderGraph.h"
#include "ap/engine/Transport.h"
#include "ap/instruments/Sampler.h"
#include "ap/instruments/Synth.h"
#include "ap/params/Parameters.h"

#include <atomic>
#include <memory>

namespace ap::engine
{

// Headless audio engine. Owns everything that runs inside the audio callback.
//
// Threading:
// - prepare() / releaseResources(): called while the audio callback is stopped.
// - process(): audio thread only. Real-time safe (no allocation, locks or I/O).
// - set*() and getters: any thread, lock-free.
class Engine
{
public:
    static constexpr float minToneFrequencyHz = 20.0f;
    static constexpr float maxToneFrequencyHz = 20000.0f;

    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources() noexcept;

    void process (core::AudioBlock output) noexcept AP_NONBLOCKING;

    void setTestToneEnabled (bool enabled) noexcept;
    void setTestToneFrequency (float hz) noexcept;

    // Thread-safe controls; see Transport and Metronome for details.
    [[nodiscard]] Transport& getTransport() noexcept { return transport; }
    [[nodiscard]] const Transport& getTransport() const noexcept { return transport; }
    [[nodiscard]] params::ParameterStore& getParameters() noexcept { return parameters; }
    [[nodiscard]] const params::ParameterStore& getParameters() const noexcept { return parameters; }
    [[nodiscard]] Metronome& getMetronome() noexcept { return metronome; }
    [[nodiscard]] const Metronome& getMetronome() const noexcept { return metronome; }

    [[nodiscard]] bool isTestToneEnabled() const noexcept;

    // Which instrument live notes (keyboard, MIDI) play. Switching releases the notes held on the
    // previous instrument, so nothing can get stuck.
    enum class LiveInstrument : std::uint8_t
    {
        synth = 0,
        sampler = 1
    };
    void setLiveInstrument (LiveInstrument instrument) noexcept
    {
        liveInstrument.store (instrument, std::memory_order_relaxed);
    }
    [[nodiscard]] LiveInstrument getLiveInstrument() const noexcept
    {
        return liveInstrument.load (std::memory_order_relaxed);
    }

    // Message thread: replaces the sampler's sample (lock-free; see Sampler).
    void loadSamplerSample (std::unique_ptr<instruments::SampleBuffer> buffer) noexcept
    {
        sampler.loadSample (std::move (buffer));
    }
    [[nodiscard]] std::uint64_t getSamplerAssetId() const noexcept { return sampler.loadedAssetId(); }

    // Live notes for the current instrument. Each source has its own single-producer queue: call
    // sendNoteFromUi only from the message thread and sendNoteFromMidi only from the MIDI
    // callback thread. Returns false if the queue is full (the note is dropped, never blocks).
    bool sendNoteFromUi (const instruments::NoteEvent& event) noexcept { return uiNotes.push (event); }
    bool sendNoteFromMidi (const instruments::NoteEvent& event) noexcept { return midiNotes.push (event); }

    // Render structure (message thread). Snapshots are swapped in at the next block boundary;
    // retired ones are freed by collectGarbage(), which must be called periodically.
    void publishRenderGraph (std::unique_ptr<RenderGraph> next) noexcept
    {
        graphs.publish (std::move (next));
    }
    std::size_t collectGarbage() noexcept { return graphs.collectGarbage() + sampler.collectGarbage(); }

    // Project version of the graph the audio thread is currently rendering (0 = none yet).
    [[nodiscard]] std::uint64_t getRenderedGraphVersion() const noexcept
    {
        return renderedGraphVersion.load (std::memory_order_acquire);
    }

    // Highest absolute output sample since the last call. Resets the meter.
    [[nodiscard]] float consumeOutputPeak() noexcept;

    // Number of non-finite samples replaced with silence since start (should always be 0).
    [[nodiscard]] int getNonFiniteSampleCount() const noexcept;

private:
    void renderTestTone (core::AudioBlock output) noexcept AP_NONBLOCKING;
    void finaliseOutput (core::AudioBlock output) noexcept AP_NONBLOCKING;
    void updatePeak (float blockPeak) noexcept AP_NONBLOCKING;

    double sampleRate = 48000.0;
    bool prepared = false;

    params::ParameterStore parameters;
    core::SnapshotExchange<RenderGraph> graphs;
    const RenderGraph* currentGraph = nullptr; // audio thread
    std::atomic<std::uint64_t> renderedGraphVersion {0};
    Transport transport;
    Metronome metronome;
    instruments::Synth synth;
    instruments::Sampler sampler;
    std::atomic<LiveInstrument> liveInstrument {LiveInstrument::synth};
    LiveInstrument routedInstrument = LiveInstrument::synth; // audio thread
    core::SpscQueue<instruments::NoteEvent, 256> uiNotes;
    core::SpscQueue<instruments::NoteEvent, 256> midiNotes;

    dsp::SineOscillator toneOscillator;
    dsp::LinearSmoothedValue toneGain;
    dsp::LinearSmoothedValue toneFrequency;

    std::atomic<bool> toneEnabled {false};
    std::atomic<float> toneFrequencyHz {440.0f};

    std::atomic<float> outputPeak {0.0f};
    std::atomic<int> nonFiniteSamples {0};
};

} // namespace ap::engine
