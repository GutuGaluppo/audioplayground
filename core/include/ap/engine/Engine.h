#pragma once

#include "ap/core/AudioBlock.h"
#include "ap/core/RealtimeSafety.h"
#include "ap/core/SnapshotExchange.h"
#include "ap/core/SpscQueue.h"
#include "ap/dsp/LinearSmoothedValue.h"
#include "ap/dsp/SineOscillator.h"
#include "ap/engine/InputCapture.h"
#include "ap/engine/Metronome.h"
#include "ap/engine/RenderGraph.h"
#include "ap/engine/TimelinePlayer.h"
#include "ap/engine/Transport.h"
#include "ap/instruments/DrumMachine.h"
#include "ap/instruments/Sampler.h"
#include "ap/instruments/Synth.h"
#include "ap/params/Parameters.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace ap::engine
{

// A live note played while recording, stamped with the timeline position the player heard when
// playing it (the output latency is subtracted).
struct RecordedNote
{
    core::Ticks ticks = 0;
    instruments::NoteEvent event;
    model::InstrumentKind instrument = model::InstrumentKind::synth;
};

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
    static constexpr int maxOutputChannels = 8;

    // maxBlockSize sizes the internal buffers; larger blocks are processed in pieces.
    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources() noexcept;

    // input: the device input for the same block (may be empty). It is only metered and, while
    // recording, captured; it never reaches the output (no input monitoring).
    void process (core::AudioBlock output, core::InputBlock input = {}) noexcept AP_NONBLOCKING;

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
        sampler = 1,
        drums = 2
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

    // Note recording (any thread). While enabled and playing, live notes are queued for the
    // message thread, which builds the clip. latency: output latency in samples, so a note is
    // placed where the player heard the music, not where the engine was rendering.
    void setNoteRecording (bool enabled) noexcept
    {
        recordingNotes.store (enabled, std::memory_order_relaxed);
    }
    [[nodiscard]] bool isRecordingNotes() const noexcept
    {
        return recordingNotes.load (std::memory_order_relaxed);
    }
    void setRecordingLatency (core::Samples latency) noexcept
    {
        recordingLatency.store (std::max<core::Samples> (0, latency), std::memory_order_relaxed);
    }
    // Message thread only.
    [[nodiscard]] std::optional<RecordedNote> popRecordedNote() noexcept { return recordedNotes.pop(); }

    // Audio recording: the device input captured while the transport plays (see InputCapture).
    [[nodiscard]] InputCapture& getInputCapture() noexcept { return inputCapture; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }

    // Render structure (message thread). Snapshots are swapped in at the next block boundary;
    // retired ones are freed by collectGarbage(), which must be called periodically.
    void publishRenderGraph (std::unique_ptr<RenderGraph> next) noexcept
    {
        graphs.publish (std::move (next));
    }
    std::size_t collectGarbage() noexcept
    {
        return graphs.collectGarbage() + sampler.collectGarbage() + drums.collectGarbage();
    }

    // Pattern and pad settings are lock-free setters; see DrumMachine for the threading rules.
    [[nodiscard]] instruments::DrumMachine& getDrums() noexcept { return drums; }

    // Project version of the graph the audio thread is currently rendering (0 = none yet).
    [[nodiscard]] std::uint64_t getRenderedGraphVersion() const noexcept
    {
        return renderedGraphVersion.load (std::memory_order_acquire);
    }

    // Highest absolute output sample since the last call. Resets the meter.
    [[nodiscard]] float consumeOutputPeak() noexcept;
    // Same for the device input (0 while no input is open).
    [[nodiscard]] float consumeInputPeak() noexcept
    {
        return inputPeak.exchange (0.0f, std::memory_order_relaxed);
    }

    // Number of non-finite samples replaced with silence since start (should always be 0).
    [[nodiscard]] int getNonFiniteSampleCount() const noexcept;

private:
    void processBlock (core::AudioBlock output, core::InputBlock input) noexcept AP_NONBLOCKING;
    void meterInput (core::InputBlock input) noexcept AP_NONBLOCKING;
    void routeLiveNote (const instruments::NoteEvent& note) noexcept AP_NONBLOCKING;
    void handleNote (model::InstrumentKind instrument,
                     const instruments::NoteEvent& note) noexcept AP_NONBLOCKING;
    void renderInstrument (model::InstrumentKind instrument, core::AudioBlock output) noexcept AP_NONBLOCKING;
    void renderTestTone (core::AudioBlock output) noexcept AP_NONBLOCKING;
    void finaliseOutput (core::AudioBlock output) noexcept AP_NONBLOCKING;
    static void updatePeak (std::atomic<float>& peak, float blockPeak) noexcept AP_NONBLOCKING;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool prepared = false;

    params::ParameterStore parameters;
    core::SnapshotExchange<RenderGraph> graphs;
    const RenderGraph* currentGraph = nullptr; // audio thread
    std::atomic<std::uint64_t> renderedGraphVersion {0};
    Transport transport;
    Metronome metronome;
    instruments::Synth synth;
    instruments::Sampler sampler;
    instruments::DrumMachine drums;
    std::atomic<LiveInstrument> liveInstrument {LiveInstrument::synth};
    LiveInstrument routedInstrument = LiveInstrument::synth; // audio thread
    core::SpscQueue<instruments::NoteEvent, 256> uiNotes;
    core::SpscQueue<instruments::NoteEvent, 256> midiNotes;

    TimelinePlayer timeline;
    std::vector<float> busStorage; // 2 channels x maxBlock per instrument, allocated in prepare()

    std::atomic<bool> recordingNotes {false};
    std::atomic<core::Samples> recordingLatency {0};
    core::SpscQueue<RecordedNote, 1024> recordedNotes;

    dsp::SineOscillator toneOscillator;
    dsp::LinearSmoothedValue toneGain;
    dsp::LinearSmoothedValue toneFrequency;

    std::atomic<bool> toneEnabled {false};
    std::atomic<float> toneFrequencyHz {440.0f};

    InputCapture inputCapture;

    std::atomic<float> outputPeak {0.0f};
    std::atomic<float> inputPeak {0.0f};
    std::atomic<int> nonFiniteSamples {0};
};

} // namespace ap::engine
