#include "AudioRecorder.h"
#include "TempDirectory.h"
#include "TestAudio.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace ap;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 480; // 10 ms

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juce;
    test::TempDirectory temp;
    engine::Engine engine;
    desktop::Session session {engine, io::ProjectFolder {temp.path() / "Unsaved"}};
    std::optional<desktop::AudioRecorder> recorder {std::in_place, session, engine};
    test::TestBuffer output {2, block};
    std::vector<float> input = std::vector<float> (block);
    int counter = 0;
    std::vector<std::string> notices;

    Fixture()
    {
        engine.prepare (rate, block);
        recorder->onNotice = [this] (const std::string& message) { notices.push_back (message); };
    }

    model::TrackId addAudioTrack()
    {
        REQUIRE (session.perform (model::AddTrack {model::TrackKind::audio}));
        return session.project().tracks.back().id;
    }

    // Input samples count up (scaled to stay in range), so the file tells which frames it holds.
    void run (int blocks)
    {
        const float* pointer = input.data();
        for (int b = 0; b < blocks; ++b)
        {
            for (auto& sample : input)
                sample = static_cast<float> (counter++) * 1.0e-7f;
            engine.process (output.block(), {&pointer, 1, block});
            recorder->poll();
        }
    }

    [[nodiscard]] io::fs::path assetPath (const model::Asset& asset) const
    {
        return *io::resolveAssetPath (session.assetRoot(), asset.relativePath);
    }
};

std::vector<float> readWav (const io::fs::path& path)
{
    juce::AudioFormatManager formats;
    formats.registerFormat (new juce::WavAudioFormat(), true);
    const std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (juce::File (juce::String (path.string()))));
    REQUIRE (reader != nullptr);
    CHECK (reader->sampleRate == rate);
    CHECK (reader->usesFloatingPointData);
    juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels),
                                     static_cast<int> (reader->lengthInSamples));
    REQUIRE (reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, false));
    return {buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples()};
}
} // namespace

TEST_CASE ("A recorded take becomes a latency-compensated clip on the armed track, as one undo step",
           "[recording]")
{
    Fixture f;
    const auto track = f.addAudioTrack();
    f.recorder->setArmedTrack (track);
    const core::Samples latency = 1234;

    f.run (5); // stopped: not recorded
    REQUIRE_FALSE (f.recorder->start (latency).has_value());
    f.engine.getTransport().requestPlay();
    f.run (150); // 1.5 s

    // While recording, a journal says where the take belongs.
    const auto journals = io::listJournals (f.session.assetRoot());
    REQUIRE (journals.size() == 1);

    f.engine.getTransport().requestStop();
    const auto versionBefore = f.session.document().version();
    REQUIRE (f.recorder->stop());
    CHECK_FALSE (f.recorder->isRecording());
    CHECK (io::listJournals (f.session.assetRoot()).empty());

    const auto& project = f.session.project();
    REQUIRE (project.assets.size() == 1);
    CHECK (project.assets[0].name == "Take 1");
    REQUIRE (project.tracks[0].clips.size() == 1);
    const auto& clip = project.tracks[0].clips[0];
    CHECK (clip.asset == project.assets[0].id);

    // The file holds exactly what was played, from the first played block.
    const auto samples = readWav (f.assetPath (project.assets[0]));
    REQUIRE (samples.size() == 150 * block);
    CHECK (samples.front() == static_cast<float> (5 * block) * 1.0e-7f);
    CHECK (samples.back() == static_cast<float> (155 * block - 1) * 1.0e-7f);

    // Frame 0 was captured at position 0, so it belongs `latency` samples earlier: before the start.
    const core::TempoMap map (project.tempoBpm, project.timeSignature, rate);
    CHECK (clip.start == 0);
    CHECK (core::flicksToFrames (clip.sourceOffset, rate) == latency);
    CHECK (map.ticksToSamples (clip.end()) <= 150 * block - latency);

    // One undo step removes the clip and the asset.
    CHECK (f.session.document().version() == versionBefore + 1);
    REQUIRE (f.session.undo());
    CHECK (f.session.project().assets.empty());
    CHECK (f.session.project().tracks[0].clips.empty());
}

TEST_CASE ("Takes recorded over each other all stay on the track (overdub)", "[recording]")
{
    Fixture f;
    f.recorder->setArmedTrack (f.addAudioTrack());
    for (int take = 0; take < 2; ++take)
    {
        f.engine.getTransport().requestSeek (0);
        REQUIRE_FALSE (f.recorder->start (0).has_value());
        f.engine.getTransport().requestPlay();
        f.run (60);
        f.engine.getTransport().requestStop();
        f.run (1);
        REQUIRE (f.recorder->stop());
    }
    const auto& project = f.session.project();
    CHECK (project.tracks[0].clips.size() == 2);
    REQUIRE (project.assets.size() == 2);
    CHECK (project.assets[1].name == "Take 2");
    CHECK (project.assets[0].relativePath != project.assets[1].relativePath);
}

TEST_CASE ("Recording audio needs an armed audio track", "[recording]")
{
    Fixture f;
    CHECK (f.recorder->start (0).has_value());
    REQUIRE (f.session.perform (model::AddTrack {model::InstrumentKind::synth}));
    f.recorder->setArmedTrack (f.session.project().tracks.back().id);
    CHECK (f.recorder->start (0).has_value());
    CHECK_FALSE (f.recorder->isRecording());
}

TEST_CASE ("A take whose track was deleted meanwhile lands on a new audio track", "[recording]")
{
    Fixture f;
    const auto track = f.addAudioTrack();
    f.recorder->setArmedTrack (track);
    REQUIRE_FALSE (f.recorder->start (0).has_value());
    f.engine.getTransport().requestPlay();
    f.run (50);
    REQUIRE (f.session.perform (model::RemoveTrack {track}));
    REQUIRE (f.recorder->stop());

    const auto& project = f.session.project();
    REQUIRE (project.tracks.size() == 1);
    CHECK (project.tracks[0].id != track);
    CHECK (project.tracks[0].kind == model::TrackKind::audio);
    CHECK (project.tracks[0].clips.size() == 1);
}

TEST_CASE ("A loop wrap ends the take and keeps it", "[recording]")
{
    Fixture f;
    f.recorder->setArmedTrack (f.addAudioTrack());
    f.engine.getTransport().setLoop (true, 0, core::ticksPerQuarterNote); // 0.5 s
    REQUIRE_FALSE (f.recorder->start (0).has_value());
    f.engine.getTransport().requestPlay();
    f.run (60);

    CHECK_FALSE (f.recorder->isRecording());
    REQUIRE (f.notices.size() == 1);
    REQUIRE (f.session.project().tracks[0].clips.size() == 1);
    CHECK (f.session.project().tracks[0].clips[0].length == core::ticksPerQuarterNote);
}

TEST_CASE ("Nothing is added when nothing was played after the song start", "[recording]")
{
    Fixture f;
    f.recorder->setArmedTrack (f.addAudioTrack());
    REQUIRE_FALSE (f.recorder->start (0).has_value());
    f.run (10); // never played
    CHECK_FALSE (f.recorder->stop());
    CHECK (f.session.project().assets.empty());
    std::error_code ec;
    CHECK (io::fs::is_empty (f.session.assetRoot().audioDirectory(), ec));
}

TEST_CASE ("A take interrupted by a crash is recovered at the next start", "[recording]")
{
    Fixture f;
    const auto track = f.addAudioTrack();
    f.recorder->setArmedTrack (track);
    REQUIRE_FALSE (f.recorder->start (0).has_value());
    f.engine.getTransport().requestPlay();
    f.run (100);        // 1 s
    f.recorder.reset(); // the app goes away mid-take

    // A crash leaves the header at the last commit: pretend nothing was committed.
    const auto journals = io::listJournals (f.session.assetRoot());
    REQUIRE (journals.size() == 1);
    auto wav = journals[0];
    wav.replace_extension(); // "x.wav.journal" -> "x.wav"
    {
        std::fstream file (wav, std::ios::in | std::ios::out | std::ios::binary);
        const std::uint32_t zero = 0;
        file.seekp (40);
        file.write (reinterpret_cast<const char*> (&zero), 4);
    }
    CHECK (f.session.project().assets.empty());

    f.recorder.emplace (f.session, f.engine);
    const auto message = f.recorder->recoverInterrupted();
    REQUIRE (message.has_value());
    CHECK (io::listJournals (f.session.assetRoot()).empty());

    const auto& project = f.session.project();
    REQUIRE (project.assets.size() == 1);
    REQUIRE (project.tracks[0].clips.size() == 1);
    CHECK (project.tracks[0].clips[0].start == 0);
    CHECK (readWav (f.assetPath (project.assets[0])).size() == 100 * block);

    // Recovering again finds nothing.
    CHECK_FALSE (f.recorder->recoverInterrupted().has_value());
}

TEST_CASE ("Malformed or unsafe journals are discarded without touching the project", "[recording]")
{
    Fixture f;
    const auto audio = f.session.assetRoot().audioDirectory();
    io::fs::create_directories (audio);
    std::ofstream (audio / "1-take.wav.journal") << "{ not json";
    REQUIRE_FALSE (
        io::writeJournal (audio / "2-take.wav.journal", {"audio/2-take.wav", 1, 0, 0, "Take"}).has_value());
    std::ofstream (audio / "2-take.wav") << "not a wav file";

    CHECK_FALSE (f.recorder->recoverInterrupted().has_value());
    CHECK (io::listJournals (f.session.assetRoot()).empty());
    CHECK (f.session.project().assets.empty());
    CHECK (f.session.project().tracks.empty());
}
