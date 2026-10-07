#pragma once

#include "ap/engine/Engine.h"
#include "ap/host/DocumentEditor.h"
#include "ap/host/IntentApplier.h"

#include <string>
#include <string_view>
#include <vector>

namespace ap::web
{

// The browser's host: the same core and the same intent code as the desktop app (ADR-016), with
// nothing platform-specific but this class. It runs inside an AudioWorklet: intents arrive as JSON
// text from the page, audio is pulled one block at a time, and events leave as JSON text.
//
// Not in the browser yet (a notice says so): saving and opening projects, importing and recording
// audio, the sampler, exporting, the accompaniment suggestions, device choice.
class WebHost
{
public:
    static constexpr std::size_t maxIntentBytes = 1u << 20; // a page cannot send more than this per message
    static constexpr double eventRateHz = 30.0;             // meters and playhead

    WebHost (double sampleRate, int blockSize);

    // A UI message {"type": ..., "payload": {...}}. False if it was malformed and dropped.
    bool handleIntent (std::string_view json);

    // Renders `frames` frames of stereo audio. Any frame count up to maxFrames().
    void process (float* left, float* right, int frames);
    [[nodiscard]] int maxFrames() const noexcept { return maxBlock; }

    // Everything the UI should hear about since the last call, as one JSON array text (empty
    // events: "[]"). The text stays valid until the next call.
    [[nodiscard]] const std::string& takeEvents();

    // What the browser reports about its output, shown in the status bar.
    void setOutputLatency (double milliseconds);

private:
    void emit (const bridge::Event& event);
    void notice (int level, std::string_view message);
    void sendAll();
    void sendStatus();
    void sendTransportState();
    void sendParameters (bool everything);
    void sendDrumPads();
    void onProjectChanged();
    void pulse();

    double sampleRate;
    int maxBlock;
    double outputLatencyMs = 0.0;
    engine::Engine engine;
    host::DocumentEditor editor;
    host::IntentApplier applier;

    std::vector<std::string> pending; // events as JSON text, in order
    std::string out;
    std::array<float, params::numParameters> sentParameters {};
    std::int64_t framesSincePulse = 0;
    bridge::TransportPosition lastPosition;
    bool lastPlaying = false;
};

} // namespace ap::web
