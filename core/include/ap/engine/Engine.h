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
#include "ap/fx/Limiter.h"
#include "ap/instruments/DrumMachine.h"
#include "ap/instruments/Sampler.h"
#include "ap/instruments/Synth.h"
#include "ap/params/Parameters.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace ap::engine
{

// A live note (keyboard, MIDI), stamped with when the player heard the music while playing it:
// the output latency is subtracted. Every live note is logged, so notes can be recorded while the
// transport plays and captured afterwards (retroactive capture) whether it played or not.
struct PlayedNote
{
    core::Samples clock = 0; // engine sample clock: counts every rendered sample, never jumps
    bool playing = false;    // the transport was playing (ticks is valid)
    core::Ticks ticks = 0;   // timeline position, may be negative during a count-in
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

    // Played-note log. latency: the device's output latency in samples (the engine adds its own,
    // getOutputLatency), so a note is stamped where the player heard the music, not where the
    // engine was rendering. Any thread.
    void setRecordingLatency (core::Samples latency) noexcept
    {
        recordingLatency.store (std::max<core::Samples> (0, latency), std::memory_order_relaxed);
    }
    // Message thread only (the single consumer). Drain it regularly: when full, new notes are not
    // logged (they still play).
    [[nodiscard]] std::optional<PlayedNote> popPlayedNote() noexcept { return playedNotes.pop(); }
    // The engine sample clock now (see PlayedNote::clock), minus the output latency.
    [[nodiscard]] core::Samples getHeardClock() const noexcept
    {
        return clock.load (std::memory_order_relaxed) - recordingLatency.load (std::memory_order_relaxed)
             - getOutputLatency();
    }

    // Audio recording: the device input captured while the transport plays (see InputCapture).
    [[nodiscard]] InputCapture& getInputCapture() noexcept { return inputCapture; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }

    // Render structure (message thread). Snapshots are swapped in at the next block boundary;
    // retired ones are freed by collectGarbage(), which must be called periodically.
    // Gives every track its effect chain (kept across graphs, created on first use) before
    // publishing.
    void publishRenderGraph (std::unique_ptr<RenderGraph> next);
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

    // Delay from the timeline (and live notes) to the output, in samples at the prepared rate:
    // two chain latencies (a track's, then a bus's; ADR-010) plus the master limiter's lookahead. Offline
    // rendering and recording compensate it.
    [[nodiscard]] int getOutputLatency() const noexcept
    {
        return outputLatency.load (std::memory_order_relaxed);
    }

    // Master limiter gain reduction since the last call, in dB (<= 0). Resets it.
    [[nodiscard]] float consumeLimiterReductionDb() noexcept { return limiter.consumeGainReductionDb(); }

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
    // Fixed delay of a stereo signal, in place. N samples exactly.
    template <std::size_t N> struct StereoDelay
    {
        std::array<std::array<float, N>, 2> line {};
        std::size_t position = 0;

        void reset() noexcept
        {
            line[0].fill (0.0f);
            line[1].fill (0.0f);
            position = 0;
        }

        void process (float* left, float* right, int numSamples) noexcept AP_NONBLOCKING
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float delayedLeft = line[0][position];
                const float delayedRight = line[1][position];
                line[0][position] = left[i];
                line[1][position] = right[i];
                left[i] = delayedLeft;
                right[i] = delayedRight;
                position = (position + 1) % N;
            }
        }
    };

    void processBlock (core::AudioBlock output, core::InputBlock input) noexcept AP_NONBLOCKING;
    void meterInput (core::InputBlock input) noexcept AP_NONBLOCKING;
    void routeLiveNote (const instruments::NoteEvent& note) noexcept AP_NONBLOCKING;
    void handleNote (model::InstrumentKind instrument,
                     const instruments::NoteEvent& note) noexcept AP_NONBLOCKING;
    void renderInstrument (model::InstrumentKind instrument, int numSamples) noexcept AP_NONBLOCKING;
    void mixTracks (const TrackBuses& buses, const TrackBuses& sends, int numSamples) noexcept AP_NONBLOCKING;
    void processBuses (const TrackBuses& sends, int numSamples) noexcept AP_NONBLOCKING;
    void mixOutput (core::AudioBlock output) noexcept AP_NONBLOCKING;
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
    std::vector<float> busStorage;      // 2 channels x maxBlock per instrument, allocated in prepare()
    std::vector<float> trackBusStorage; // 2 channels x maxBlock per track (Project::maxTracks)
    // Signal paths (ADR-010). A track goes through its chain; its send goes on through a bus chain.
    // Everything that does not go through a bus chain is delayed by one chain latency, and sources
    // that belong to no track (metronome, test tone, an instrument without a track) by two, so
    // every path takes exactly two chain latencies and nothing ever moves in time.
    std::vector<float> directBus;    // 2 x maxBlock
    std::vector<float> trackMix;     // 2 x maxBlock: the tracks, after chain and fader
    std::vector<float> busReturn;    // 2 x maxBlock: the buses, after chain and fader
    std::vector<float> busStorageIn; // 2 x maxBlock per bus (Bus::maxBuses): what the sends collected
    StereoDelay<TrackChain::latency()> trackDelay;
    StereoDelay<2 * TrackChain::latency()> directDelay;

    // Gains at the end of the previous block, so send and bus changes ramp instead of clicking.
    struct SendGainState
    {
        model::TrackId track;
        std::array<float, model::Bus::maxBuses> gain {};
    };
    std::array<SendGainState, model::Project::maxTracks> sendState {};
    struct BusGainState
    {
        model::BusId bus;
        float left = 0.0f;
        float right = 0.0f;
    };
    std::array<BusGainState, model::Bus::maxBuses> busState {};
    fx::Limiter limiter;
    std::atomic<int> outputLatency {0};

    // Effect chains by track id. Message thread (publish) and prepare(); never the audio thread.
    std::mutex chainLock;
    std::map<std::uint64_t, std::shared_ptr<TrackChain>> chains;
    std::map<std::uint64_t, std::shared_ptr<TrackChain>> busChains;
    double chainRate = 48000.0; // guarded by chainLock
    int chainBlock = 512;       // guarded by chainLock

    std::atomic<core::Samples> recordingLatency {0};
    core::SpscQueue<PlayedNote, 1024> playedNotes;
    std::atomic<core::Samples> clock {0}; // written by the audio thread only

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
