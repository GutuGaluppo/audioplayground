#pragma once

#include "ap/engine/Engine.h"
#include "ap/host/DocumentEditor.h"
#include "ap/host/IntentApplier.h"
#include "ap/host/Snapshots.h"
#include "ap/model/ProjectSerialization.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
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

    // Saving and opening: the page keeps the files (IndexedDB); the core turns the project into
    // text and back, with the same strict parser the desktop uses for untrusted project files.
    // `timestamp` is the page's clock as ISO 8601 UTC (the module has no clock).
    [[nodiscard]] std::string exportProject (std::string_view timestamp);
    // Replaces the open project (and its undo history) with the one in `json`. Returns an empty
    // string, or a message for the user and the open project untouched. `dirty`: the project has
    // changes that are not saved (a recovered autosave); `hasLocation`: it belongs to a saved file.
    [[nodiscard]] std::string importProject (std::string_view json, bool dirty, bool hasLocation);
    // Audio files. The page decodes a file (the browser does the decoding) and hands over the
    // samples: begin, write each channel into audioChannel(), end. `use` says what the audio is
    // for; the project edit and the audio arrive together, in one undo step.
    enum class AudioUse
    {
        existing = 0, // the project already lists this file (opening a song): just load it
        clip = 1,     // a new clip on audio track `a` at tick `b`
        relink = 2,   // the file for asset `a`, which was missing
        sampler = 3,  // the sampler's sample
        pad = 4       // drum pad `a`'s sample
    };
    static constexpr double maxAudioSeconds = 600.0;
    static constexpr std::size_t maxAudioBytes = 256u * 1024u * 1024u;
    // Empty on success, else a message for the user. At most one file at a time.
    [[nodiscard]] std::string beginAudio (std::string_view path, double rate, int channels, int frames);
    [[nodiscard]] float* audioChannel (int index) noexcept;
    [[nodiscard]] std::string endAudio (AudioUse use, int a, int b, std::string_view name);
    // The page could not provide the file for `path` (not stored any more, or it will not decode).
    void audioMissing (std::string_view path);
    void forgetAudio (std::string_view path);

    // The page saved the project: it is clean now.
    void projectSaved (bool hasLocation);
    // The file the project belonged to is gone: it has changes nothing stores.
    void projectUnsaved();

private:
    void emit (const bridge::Event& event);
    void notice (int level, std::string_view message);
    void sendAll();
    void sendStatus();
    void sendTransportState();
    void sendParameters (bool everything);
    void sendDrumPads();
    void onProjectChanged();
    void sendProjectState();
    void resetProject (model::Project project, bool dirty, bool hasLocation);
    void pulse();

    struct LoadedAudio
    {
        std::shared_ptr<const instruments::SampleBuffer> buffer;
        double durationSeconds = 0.0;
        std::vector<float> overview;
        std::vector<std::uint8_t> peaks;
        std::uint64_t generation = 0;
        std::size_t bytes = 0;
    };
    struct PendingAudio
    {
        std::string path;
        double rate = 0.0;
        std::vector<std::vector<float>> channels;
    };
    [[nodiscard]] std::shared_ptr<const instruments::SampleBuffer> audioFor (model::AssetId id) const;
    [[nodiscard]] host::AssetAudio assetAudio (model::AssetId id) const;
    [[nodiscard]] std::size_t audioBytes() const noexcept;
    void sendAssetEvents (bool everything);
    void sendSamplerState();
    void syncSlots();
    void unregisterAudio (const std::string& path);

    double sampleRate;
    int maxBlock;
    double outputLatencyMs = 0.0;
    engine::Engine engine;
    host::DocumentEditor editor;
    host::IntentApplier applier;

    std::map<std::string, LoadedAudio> audio; // by the path the project uses
    std::set<std::string> missingAudio;       // paths the page could not provide
    std::optional<PendingAudio> incoming;
    std::uint64_t audioGeneration = 0;
    std::map<std::uint64_t, std::uint64_t> sentPeaks; // asset -> generation already sent
    std::optional<bridge::ProjectAssets> sentProjectAssets;
    std::string samplerLoaded;
    std::uint64_t samplerGeneration = 0;
    std::array<std::string, model::DrumKit::numPads> padLoaded;
    std::array<std::uint64_t, model::DrumKit::numPads> padGeneration {};
    std::optional<bridge::SamplerState> sentSampler;

    std::vector<std::string> pending; // events as JSON text, in order
    std::string out;
    std::array<float, params::numParameters> sentParameters {};
    std::int64_t framesSincePulse = 0;
    bridge::TransportPosition lastPosition;
    bool lastPlaying = false;
    bool loading = false;
    bool hasLocation = false;
    std::uint64_t savedVersion = 0;
    model::ProjectMetadata metadata;
};

} // namespace ap::web
