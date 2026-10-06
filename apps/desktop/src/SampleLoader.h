#pragma once

#include "Session.h"
#include "ap/engine/Engine.h"

#include <array>
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ap::desktop
{

// Imports audio files into the project and keeps everything that plays audio files loaded: the
// sampler, the 16 drum pads and every asset used by an audio clip. Decoding and resampling run on
// a background thread; results are applied on the message thread. Imported files are untrusted
// input: format, size, length and channel count are checked before anything is decoded.
class SampleLoader
{
public:
    // Slot 0 is the sampler; slots 1..16 are drum pads 0..15.
    static constexpr std::size_t samplerSlot = 0;
    static constexpr std::size_t numSlots = 1 + model::DrumKit::numPads;
    [[nodiscard]] static constexpr std::size_t padSlot (std::size_t pad) noexcept { return 1 + pad; }

    struct State
    {
        std::string name;
        bool loaded = false;
        bool missing = false;
        bool loading = false;
        double durationSeconds = 0.0;
        std::vector<float> overview; // peak per segment, 0..1 (sampler only)
    };

    static constexpr std::int64_t maxFileBytes = 1024LL * 1024 * 1024;
    static constexpr double maxDurationSeconds = 10.0 * 60.0;
    static constexpr int overviewPoints = 512;

    SampleLoader (Session& session, engine::Engine& engine);
    ~SampleLoader();

    // Asks for a file, copies it into the project and assigns it to the slot.
    void chooseAndImport (std::size_t slot);

    // Asks for a file, copies it into the project and adds it as an audio clip at the given
    // timeline position (one undo step).
    void chooseAndImportClip (model::TrackId track, core::Ticks position);

    // Call when the project changes (open, undo...) or the device sample rate changes.
    void sync (double engineSampleRate);

    [[nodiscard]] const State& getState (std::size_t slot = samplerSlot) const noexcept
    {
        return slots[slot].state;
    }

    // Audio used by clips, by asset id (only assets that some audio clip uses).
    struct ClipAudio
    {
        State state;
        std::shared_ptr<const instruments::SampleBuffer> buffer; // null until loaded
        double loadedRate = 0.0;
        std::uint64_t generation = 0;
    };
    [[nodiscard]] const std::map<std::uint64_t, ClipAudio>& getClipAudio() const noexcept
    {
        return clipAudio;
    }

    std::function<void (std::size_t slot)> onStateChanged;
    std::function<void()> onClipAudioChanged;
    std::function<void (const std::string&)> onError;

    struct Decoded; // a background decode's result (defined in the .cpp)

private:
    struct Slot
    {
        model::AssetId requested;
        double loadedRate = 0.0;
        std::uint64_t generation = 0;
        State state;
    };

    [[nodiscard]] model::AssetId wantedAsset (std::size_t slot) const;
    void assign (std::size_t slot, model::AssetId asset);
    struct Copied
    {
        juce::File file;
        std::string relativePath;
        std::string displayName;
    };

    void syncSlot (std::size_t slot);
    void syncClipAudio();
    void startLoad (std::size_t slot, model::AssetId asset, juce::File file, std::string name);
    void startClipLoad (model::AssetId asset, juce::File file, std::string name);
    void apply (std::size_t slot, std::uint64_t generation, std::shared_ptr<Decoded> result);
    void applyClip (std::uint64_t generation, std::shared_ptr<Decoded> result);
    void finishClipImport (model::TrackId track, core::Ticks position, Copied copied,
                           std::shared_ptr<Decoded> result);
    void chooseFile (std::function<void (Copied)> onCopied);
    template <typename Done>
    void decodeInBackground (juce::File file, std::string name, model::AssetId asset, bool overview,
                             Done done);
    [[nodiscard]] std::optional<juce::File> resolve (model::AssetId asset, std::string& name) const;
    void publish (std::size_t slot, std::unique_ptr<instruments::SampleBuffer> buffer);
    void setState (std::size_t slot, State next);
    void error (const std::string& message);

    Session& session;
    engine::Engine& engine;
    juce::ThreadPool pool {
        juce::ThreadPoolOptions {}.withThreadName ("Audio import").withNumberOfThreads (1)};
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    std::array<Slot, numSlots> slots;
    std::map<std::uint64_t, ClipAudio> clipAudio;
    std::uint64_t clipGeneration = 0;
    double engineRate = 48000.0;
};

} // namespace ap::desktop
