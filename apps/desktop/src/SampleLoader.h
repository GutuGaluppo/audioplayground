#pragma once

#include "Session.h"
#include "ap/engine/Engine.h"

#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>
#include <vector>

namespace ap::desktop
{

// Imports audio files into the project and keeps the sampler loaded with the project's sample.
// Decoding, copying and resampling run on a background thread; results are applied on the
// message thread. Imported files are untrusted input: format, size, length and channel count are
// checked before anything is decoded.
class SampleLoader
{
public:
    struct State
    {
        std::string name;
        bool loaded = false;
        bool missing = false;
        bool loading = false;
        double durationSeconds = 0.0;
        std::vector<float> overview; // peak per segment, 0..1
    };

    static constexpr std::int64_t maxFileBytes = 1024LL * 1024 * 1024;
    static constexpr double maxDurationSeconds = 10.0 * 60.0;
    static constexpr int overviewPoints = 512;

    SampleLoader (Session& session, engine::Engine& engine);
    ~SampleLoader();

    // Asks for a file, copies it into the project and loads it into the sampler.
    void chooseAndImport();

    // Call when the project changes (open, undo...) or the device sample rate changes.
    void sync (double engineSampleRate);

    [[nodiscard]] const State& getState() const noexcept { return state; }

    std::function<void()> onStateChanged;
    std::function<void (const std::string&)> onError;

private:
    struct Decoded;
    void startLoad (model::AssetId asset, juce::File file, std::string name);
    void apply (std::uint64_t generation, std::shared_ptr<Decoded> result);
    void setState (State next);

    Session& session;
    engine::Engine& engine;
    juce::ThreadPool pool {
        juce::ThreadPoolOptions {}.withThreadName ("Audio import").withNumberOfThreads (1)};
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    std::uint64_t generation = 0;
    model::AssetId requestedAsset;
    double loadedRate = 0.0;
    double engineRate = 48000.0;
    State state;
};

} // namespace ap::desktop
