#include "FxTest.h"
#include "TempDirectory.h"
#include "TestAudio.h"
#include "TimelineHelpers.h"
#include "WavFile.h"
#include "ap/core/ScopedNoDenormals.h"
#include "ap/dsp/Loudness.h"
#include "ap/engine/Export.h"
#include "ap/io/WavExport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>

using namespace ap;

namespace
{
constexpr double fs = 48000.0;

std::vector<float> tone (double frequency, double rate, double seconds, double levelDb)
{
    return test::sine (frequency, rate, static_cast<std::size_t> (seconds * rate),
                       std::pow (10.0, levelDb / 20.0));
}

std::vector<char> bytes (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (in), {}};
}

// A project with one audio track holding a clip of `samples` at time 0, optionally with a reverb.
struct Song
{
    model::ProjectDocument doc;
    std::shared_ptr<instruments::SampleBuffer> source = std::make_shared<instruments::SampleBuffer>();

    Song (std::vector<float> samples, bool reverb)
    {
        doc.perform (model::AddTrack {model::TrackKind::audio});
        const auto track = doc.project().tracks.back().id;
        doc.perform (model::AddAsset {"audio/1-a.wav", "a.wav"});
        source->sampleRate = fs;
        const core::TempoMap map (120.0, {}, fs);
        model::Clip clip;
        clip.length = map.samplesToTicks (static_cast<core::Samples> (samples.size()));
        clip.asset = doc.project().assets.back().id;
        source->channels.push_back (std::move (samples));
        doc.perform (model::AddClip {track, clip});
        if (reverb)
        {
            auto state = model::defaultEffectState (params::EffectKind::reverb);
            state.enabled = true;
            doc.perform (model::SetTrackEffect {track, params::EffectKind::reverb, state});
        }
    }

    void into (engine::Engine& engine) const
    {
        engine::configureEngine (engine, doc.project(), 1, [this] (model::AssetId) { return source; });
    }

    [[nodiscard]] std::int64_t length() const
    {
        return core::TempoMap (120.0, {}, fs).ticksToSamples (engine::songEnd (doc.project()));
    }
};
} // namespace

// --- Loudness --------------------------------------------------------------------------------

TEST_CASE ("Integrated loudness of a -23 dBFS stereo sine is -23 LUFS (EBU Tech 3341)", "[export][loudness]")
{
    const double rate = GENERATE (44100.0, 48000.0, 96000.0);
    CAPTURE (rate);
    const auto sine = tone (1000.0, rate, 20.0, -23.0);
    const auto report = dsp::measureLoudness ({sine, sine}, rate);
    CHECK (report.integratedLufs == Catch::Approx (-23.0).margin (0.1));
    CHECK (report.samplePeakDb == Catch::Approx (-23.0).margin (0.05));
}

TEST_CASE ("Loudness gating ignores quiet passages and silence", "[export][loudness]")
{
    auto signal = tone (1000.0, fs, 3.0, -36.0);
    const auto loud = tone (1000.0, fs, 20.0, -23.0);
    signal.insert (signal.end(), loud.begin(), loud.end());
    const auto quiet = tone (1000.0, fs, 3.0, -36.0);
    signal.insert (signal.end(), quiet.begin(), quiet.end());
    signal.insert (signal.end(), static_cast<std::size_t> (5 * fs), 0.0f);
    CHECK (dsp::measureLoudness ({signal, signal}, fs).integratedLufs == Catch::Approx (-23.0).margin (0.1));

    const auto silent = dsp::measureLoudness ({std::vector<float> (48000, 0.0f)}, fs);
    CHECK (silent.integratedLufs == -70.0);
    CHECK (silent.samplePeakDb == -120.0);
}

TEST_CASE ("True peak sees the peaks between samples", "[export][loudness]")
{
    // fs/4 sampled 45 degrees off its peaks: samples reach 0.636, the waveform 0.9.
    std::vector<float> sine (48000);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = static_cast<float> (
            0.9 * std::sin (std::numbers::pi / 2.0 * static_cast<double> (i) + std::numbers::pi / 4.0));
    const auto report = dsp::measureLoudness ({sine}, fs);
    CHECK (report.samplePeakDb == Catch::Approx (20.0 * std::log10 (0.9 * std::sqrt (0.5))).margin (0.05));
    CHECK (report.truePeakDb == Catch::Approx (20.0 * std::log10 (0.9)).margin (0.3));
}

// --- WAV files -------------------------------------------------------------------------------

TEST_CASE ("Exported WAV files hold the audio in the chosen format", "[export][io]")
{
    test::TempDirectory dir;
    const auto sine = tone (440.0, fs, 0.25, -6.0);
    std::vector<float> loud (100, 1.5f); // beyond full scale

    SECTION ("32-bit float is exact")
    {
        REQUIRE_FALSE (io::writeWav (dir.path() / "a.wav", {sine, sine}, 48000, io::WavFormat::float32));
        const auto read = test::readFloatWav (dir.path() / "a.wav");
        REQUIRE (read);
        CHECK (read->sampleRate == 48000.0);
        CHECK (read->channels[0] == sine);
    }
    SECTION ("24-bit rounds to the nearest step and clips at full scale")
    {
        REQUIRE_FALSE (io::writeWav (dir.path() / "b.wav", {loud}, 48000, io::WavFormat::pcm24));
        const auto data = bytes (dir.path() / "b.wav");
        REQUIRE (data.size() == 44 + 300);
        CHECK (static_cast<unsigned char> (data[44]) == 0xff);
        CHECK (static_cast<unsigned char> (data[46]) == 0x7f); // 8388607
    }
    SECTION ("16-bit is dithered: within a step of the signal, never plain silence, and repeatable")
    {
        std::vector<float> silence (sine.size(), 0.0f);
        REQUIRE_FALSE (io::writeWav (dir.path() / "c.wav", {sine, silence}, 48000, io::WavFormat::pcm16));
        REQUIRE_FALSE (io::writeWav (dir.path() / "d.wav", {sine, silence}, 48000, io::WavFormat::pcm16));
        const auto data = bytes (dir.path() / "c.wav");
        CHECK (data == bytes (dir.path() / "d.wav"));
        REQUIRE (data.size() == 44 + sine.size() * 4);

        int nonZero = 0;
        for (std::size_t i = 0; i < sine.size(); ++i)
        {
            std::int16_t left = 0;
            std::int16_t right = 0;
            std::memcpy (&left, data.data() + 44 + 4 * i, 2);
            std::memcpy (&right, data.data() + 46 + 4 * i, 2);
            REQUIRE (std::abs (static_cast<float> (left) - sine[i] * 32768.0f) <= 1.5f);
            REQUIRE (std::abs (right) <= 1);
            nonZero += right != 0 ? 1 : 0;
        }
        CHECK (nonZero > 1000); // TPDF: about half the samples of silence are +/-1
    }
    SECTION ("Refused formats leave nothing behind")
    {
        CHECK (io::writeWav (dir.path() / "e.wav", {sine, sine, sine}, 48000, io::WavFormat::pcm16));
        CHECK (io::writeWav (dir.path() / "f.wav", {sine, loud}, 48000, io::WavFormat::pcm16));
        CHECK (std::filesystem::is_empty (dir.path()));
    }
}

// --- Rendering -------------------------------------------------------------------------------

TEST_CASE ("A song renders from time 0 to its end, and on through the effect tails", "[export][render]")
{
    const core::ScopedNoDenormals noDenormals;
    const auto clipAudio = test::noise (24000, 0.5f)[0];

    Song dry (clipAudio, false);
    engine::Engine dryEngine;
    dry.into (dryEngine);
    const auto plain = engine::renderSong (dryEngine, {fs, dry.length()});
    REQUIRE (plain);
    REQUIRE ((*plain)[0].size() == static_cast<std::size_t> (dry.length()));
    for (std::size_t i = 200; i + 200 < clipAudio.size(); i += 101) // inside the 2 ms edge fades
        REQUIRE ((*plain)[0][i] == Catch::Approx (clipAudio[i]).margin (1.0e-6));

    Song wet (clipAudio, true);
    engine::Engine wetEngine;
    wet.into (wetEngine);
    const auto tail = engine::renderSong (wetEngine, {fs, wet.length()});
    REQUIRE (tail);
    const auto length = (*tail)[0].size();
    CHECK (length > static_cast<std::size_t> (wet.length() + 24000)); // the reverb rings on
    CHECK (length < static_cast<std::size_t> (wet.length() + 10 * 48000));
    CHECK (std::abs ((*tail)[0].back()) < 1.0e-3f);

    // The tail is capped.
    engine::Engine capped;
    wet.into (capped);
    engine::SongRenderSettings settings {fs, wet.length()};
    settings.maxTailSeconds = 0.1;
    CHECK ((*engine::renderSong (capped, settings))[0].size()
           <= static_cast<std::size_t> (wet.length() + 4800));
}

TEST_CASE ("An export equals what real-time playback produces", "[export][render][determinism]")
{
    const core::ScopedNoDenormals noDenormals;
    Song song (test::noise (36000, 0.4f)[0], true);

    engine::Engine offline;
    song.into (offline);
    const auto exported = engine::renderSong (offline, {fs, song.length()});
    REQUIRE (exported);

    // Real time: a device with uneven callbacks, latency at the front.
    engine::Engine live;
    song.into (live);
    live.prepare (fs, 1024);
    live.getTransport().requestPlay();
    const auto latency = static_cast<std::size_t> (live.getOutputLatency());
    std::vector<float> played;
    const std::array<int, 4> sizes {64, 333, 1024, 7};
    for (std::size_t b = 0; played.size() < (*exported)[0].size() + latency; ++b)
    {
        test::TestBuffer buffer (2, sizes[b % sizes.size()]);
        live.process (buffer.block());
        played.insert (played.end(), buffer.channel (0).begin(), buffer.channel (0).end());
    }
    for (std::size_t i = 0; i < (*exported)[0].size(); ++i)
        REQUIRE ((*exported)[0][i] == played[i + latency]);
}

TEST_CASE ("Rendering a song can be cancelled", "[export][render]")
{
    Song song (test::noise (96000, 0.4f)[0], false);
    engine::Engine engine;
    song.into (engine);
    int calls = 0;
    CHECK_FALSE (
        engine::renderSong (engine, {fs, song.length()}, [&calls] (double) { return ++calls < 10; }));
    CHECK (calls == 10);
}
