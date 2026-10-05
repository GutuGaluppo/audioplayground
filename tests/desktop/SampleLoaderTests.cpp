#include "SampleLoader.h"
#include "TempDirectory.h"
#include "TestAudio.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace ap;
using ap::test::TempDirectory;

namespace
{
juce::File toFile (const std::filesystem::path& path)
{
    return juce::File (juce::String::fromUTF8 (reinterpret_cast<const char*> (path.u8string().c_str())));
}

void writeWav (const juce::File& file, double rate, int channels, int frames)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (rate)
                             .withNumChannels (channels)
                             .withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);
    REQUIRE (writer != nullptr);

    juce::AudioBuffer<float> buffer (channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < frames; ++n)
            buffer.setSample (
                ch, n, static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 440.0 * n / rate)));
    REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, frames));
}

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juce;
    TempDirectory temp;
    engine::Engine engine;
    desktop::Session session {engine, io::ProjectFolder {temp.path() / "Unsaved"}};
    desktop::SampleLoader loader {session, engine};
    std::vector<std::string> errors;
    io::ProjectFolder folder {temp.path() / "Song.playground"};

    Fixture()
    {
        engine.prepare (48000.0, 256);
        loader.onError = [this] (const std::string& message) { errors.push_back (message); };
    }

    // Points the sampler at a file inside the project folder and saves, like a real project.
    model::AssetId addAsset (const std::string& file)
    {
        REQUIRE (session.perform (model::AddAsset {"audio/" + file, file}));
        const auto id = session.project().assets.back().id;
        REQUIRE (session.perform (model::SetSamplerAsset {id}));
        REQUIRE_FALSE (session.saveAs (folder).has_value());
        return id;
    }

    void waitForLoad()
    {
        for (int i = 0; i < 200 && loader.getState().loading; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    void processBlock()
    {
        test::TestBuffer buffer (2, 256);
        engine.process (buffer.block());
    }
};
} // namespace

TEST_CASE ("SampleLoader decodes, resamples and hands the sample to the engine", "[samples]")
{
    Fixture f;
    writeWav (toFile (f.folder.audioDirectory() / "1-tone.wav"), 44100.0, 2, 22050);
    const auto id = f.addAsset ("1-tone.wav");

    f.loader.sync (48000.0);
    f.waitForLoad();

    const auto& state = f.loader.getState();
    CHECK (state.loaded);
    CHECK_FALSE (state.missing);
    CHECK (std::abs (state.durationSeconds - 0.5) < 1.0e-6);
    REQUIRE (state.overview.size() == desktop::SampleLoader::overviewPoints);
    CHECK (*std::max_element (state.overview.begin(), state.overview.end()) > 0.45f);
    CHECK (f.errors.empty());

    f.processBlock();
    CHECK (f.engine.getSamplerAssetId() == id.value);
}

TEST_CASE ("SampleLoader reports a missing file without crashing", "[samples]")
{
    Fixture f;
    f.addAsset ("1-gone.wav");
    f.loader.sync (48000.0);

    CHECK (f.loader.getState().missing);
    CHECK_FALSE (f.loader.getState().loaded);
    REQUIRE (f.errors.size() == 1);
    CHECK (f.errors[0].find ("missing") != std::string::npos);
}

TEST_CASE ("SampleLoader rejects corrupt and truncated files safely", "[samples][security]")
{
    Fixture f;
    const auto audio = toFile (f.folder.audioDirectory());
    audio.createDirectory();

    SECTION ("random bytes with a .wav name")
    {
        juce::MemoryBlock junk (4096);
        juce::Random random (42);
        random.fillBitsRandomly (junk.getData(), junk.getSize());
        REQUIRE (audio.getChildFile ("1-junk.wav").replaceWithData (junk.getData(), junk.getSize()));
        f.addAsset ("1-junk.wav");
    }

    SECTION ("a WAV header that promises far more data than the file holds")
    {
        writeWav (audio.getChildFile ("1-cut.wav"), 48000.0, 1, 48000);
        juce::MemoryBlock data;
        REQUIRE (audio.getChildFile ("1-cut.wav").loadFileAsData (data));
        data.setSize (200); // header + a few samples
        REQUIRE (audio.getChildFile ("1-cut.wav").replaceWithData (data.getData(), data.getSize()));
        f.addAsset ("1-cut.wav");
    }

    f.loader.sync (48000.0);
    f.waitForLoad();
    f.processBlock();

    // Either rejected with a message, or (truncated) loaded as the audio that is actually there.
    const auto& state = f.loader.getState();
    CHECK ((state.missing || state.loaded));
    CHECK ((state.loaded || !f.errors.empty()));
    for (const float peak : state.overview)
        CHECK (std::isfinite (peak));
}

TEST_CASE ("Clearing the sampler asset silences it", "[samples]")
{
    Fixture f;
    writeWav (toFile (f.folder.audioDirectory() / "1-tone.wav"), 48000.0, 1, 4800);
    f.addAsset ("1-tone.wav");
    f.loader.sync (48000.0);
    f.waitForLoad();
    f.processBlock();
    REQUIRE (f.engine.getSamplerAssetId() != 0);

    REQUIRE (f.session.perform (model::SetSamplerAsset {model::AssetId {}}));
    f.loader.sync (48000.0);
    f.processBlock();
    CHECK (f.engine.getSamplerAssetId() == 0);
    CHECK_FALSE (f.loader.getState().loaded);
}
