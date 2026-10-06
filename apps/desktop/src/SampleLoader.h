#pragma once

#include "Session.h"
#include "ap/engine/Engine.h"

#include <array>
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
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
        std::vector<float> overview;     // peak per segment, 0..1 (sampler and clips)
        std::vector<std::uint8_t> peaks; // detailed peaks (see dsp::computePeaks), clips only
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

    // Asks for a replacement for a missing audio file, copies it into the project and points the
    // asset at it (one undo step); every clip, the sampler and drum pads using it reload.
    void chooseAndRelink (model::AssetId asset);
    // Same with a given file (no dialog). Returns true if the asset now points at a copy of it.
    bool relink (model::AssetId asset, const juce::File& source);

    // Call when the project changes (open, undo...) or the device sample rate changes.
    void sync (double engineSampleRate);

    [[nodiscard]] const State& getState (std::size_t slot = samplerSlot) const noexcept
    {
        return slots[slot].state;
    }

    // The decoded sample a slot plays (null: none, or a drum pad's factory sound), shared so an
    // export can load the same audio into its own engine.
    [[nodiscard]] std::shared_ptr<const instruments::SampleBuffer> getSlotBuffer (std::size_t slot) const
    {
        return slots[slot].buffer;
    }

    // The rate decoded audio is converted to (the live engine's).
    [[nodiscard]] double getEngineRate() const noexcept { return engineRate; }

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
        std::shared_ptr<const instruments::SampleBuffer> buffer;
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
    [[nodiscard]] std::optional<Copied> copyIntoProject (const juce::File& source);
    bool finishRelink (model::AssetId asset, Copied copied);
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
