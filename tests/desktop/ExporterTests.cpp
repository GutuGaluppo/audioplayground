#include "Exporter.h"
#include "TempDirectory.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace ap;

namespace
{
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juce;
    test::TempDirectory temp;
    engine::Engine engine;
    desktop::Session session {engine, io::ProjectFolder {temp.path() / "Unsaved"}};
    desktop::SampleLoader loader {session, engine};
    desktop::Exporter exporter {session, loader};
    std::optional<desktop::Exporter::Result> result;

    Fixture()
    {
        engine.prepare (48000.0, 256);
        loader.sync (48000.0);
        exporter.onFinished = [this] (const desktop::Exporter::Result& r) { result = r; };
    }

    void addMelody (int bars = 1)
    {
        REQUIRE (session.perform (model::AddTrack {model::InstrumentKind::synth}));
        model::Clip clip;
        clip.length = bars * 4 * core::ticksPerQuarterNote; // a bar is 2 s at 120 BPM
        clip.loopLength = 4 * core::ticksPerQuarterNote;
        clip.notes = {{0, 960, 60, 0.8f}, {960, 960, 64, 0.8f}, {1920, 1920, 67, 0.8f}};
        REQUIRE (session.perform (model::AddClip {session.project().tracks.back().id, clip}));
    }

    void waitForResult()
    {
        for (int i = 0; i < 1000 && !result; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        REQUIRE (result.has_value());
    }

    [[nodiscard]] juce::File file (const char* name) const
    {
        return juce::File (juce::String (temp.path().string())).getChildFile (name);
    }
};
} // namespace

TEST_CASE ("Export renders the song to a WAV file in the chosen format and rate", "[export]")
{
    Fixture f;
    f.addMelody();
    const auto target = f.file ("song.wav");
    REQUIRE_FALSE (f.exporter.start (target, {io::WavFormat::pcm16, 44100}).has_value());
    CHECK (f.exporter.isRunning());
    f.waitForResult();
    INFO (f.result->error);
    REQUIRE (f.result->error.empty());
    CHECK_FALSE (f.exporter.isRunning());

    juce::AudioFormatManager formats;
    formats.registerFormat (new juce::WavAudioFormat(), true);
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (target));
    REQUIRE (reader != nullptr);
    CHECK (reader->sampleRate == 44100.0);
    CHECK (reader->bitsPerSample == 16);
    CHECK (reader->numChannels == 2);
    const double seconds = static_cast<double> (reader->lengthInSamples) / 44100.0;
    CHECK (seconds >= 2.0); // the whole bar...
    CHECK (seconds < 4.0);  // ...plus the synth's release, not ten seconds of silence
    CHECK (f.result->seconds == seconds);
    CHECK (f.result->loudness.integratedLufs > -40.0);
    CHECK (f.result->loudness.truePeakDb <= 0.0);
    CHECK_FALSE (target.getSiblingFile ("song.wav.exporting").exists());
}

TEST_CASE ("Export refuses an empty song and can be cancelled", "[export]")
{
    Fixture f;
    CHECK (f.exporter.start (f.file ("empty.wav"), {}).has_value());

    f.addMelody (600); // 20 minutes: still rendering when the cancel arrives
    REQUIRE_FALSE (f.exporter.start (f.file ("cancelled.wav"), {}).has_value());
    CHECK (f.exporter.start (f.file ("second.wav"), {}).has_value()); // one at a time
    f.exporter.cancel();
    f.waitForResult();
    CHECK (f.result->error == "cancelled");
    CHECK_FALSE (f.file ("cancelled.wav").exists());
}
