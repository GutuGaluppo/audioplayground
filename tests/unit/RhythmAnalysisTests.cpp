#include "TimelineHelpers.h"
#include "ap/analysis/RhythmAnalysis.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <cmath>
#include <numbers>

using namespace ap;
using analysis::analyseRhythm;
using analysis::RhythmAnalysis;

namespace
{
constexpr double fs = 48000.0;

// Deterministic noise in -1..1.
struct Noise
{
    std::uint32_t state = 12345;
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (static_cast<std::int32_t> (state)) / 2147483648.0f;
    }
};

// A percussive hit: a short noise burst over a low thump, decaying in ~60 ms.
void addHit (std::vector<float>& x, double atSeconds, float level, Noise& noise)
{
    const auto start = static_cast<std::size_t> (atSeconds * fs);
    const auto length = static_cast<std::size_t> (0.25 * fs);
    for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / fs;
        const float decay = static_cast<float> (std::exp (-t / 0.03));
        const float thump = static_cast<float> (std::sin (2.0 * std::numbers::pi * 80.0 * t));
        x[start + i] += level * decay * (0.6f * noise.next() + 0.4f * thump);
    }
}

instruments::SampleBuffer buffer (std::vector<float> x)
{
    instruments::SampleBuffer b;
    b.sampleRate = fs;
    b.channels.push_back (std::move (x));
    return b;
}

// Quarter-note hits at `bpm`, the first of each bar stronger, with optional timing jitter (seconds).
instruments::SampleBuffer quarterNotes (double bpm, double seconds, double jitter = 0.0, double offset = 0.2)
{
    std::vector<float> x (static_cast<std::size_t> (seconds * fs), 0.0f);
    Noise noise;
    Noise jitterNoise {777};
    const double beat = 60.0 / bpm;
    int index = 0;
    for (double t = offset; t < seconds - 0.3; t += beat, ++index)
        addHit (x, t + jitter * static_cast<double> (jitterNoise.next()), index % 4 == 0 ? 0.8f : 0.5f,
                noise);
    return buffer (std::move (x));
}

// Eighth-note pairs: a hit on every beat and one `offBeat` of a beat later (0.5 straight, 0.667 swing).
instruments::SampleBuffer eighths (double bpm, double seconds, double offBeat)
{
    std::vector<float> x (static_cast<std::size_t> (seconds * fs), 0.0f);
    Noise noise;
    const double beat = 60.0 / bpm;
    for (double t = 0.2; t < seconds - 0.5; t += beat)
    {
        addHit (x, t, 0.8f, noise);
        addHit (x, t + offBeat * beat, 0.4f, noise);
    }
    return buffer (std::move (x));
}
} // namespace

TEST_CASE ("A steady pulse is found at its tempo, with high confidence", "[analysis]")
{
    const double bpm = GENERATE (70.0, 90.0, 96.0, 120.0, 145.0, 170.0);
    CAPTURE (bpm);
    const auto result = analyseRhythm (quarterNotes (bpm, 16.0));
    if (bpm <= 150.0)
        CHECK (result.bpm == Catch::Approx (bpm).epsilon (0.02));
    else // a fast pulse is as plausible at half its speed: the matcher accepts either (ADR-011)
        CHECK ((result.bpm == Catch::Approx (bpm).epsilon (0.02)
                || result.bpm == Catch::Approx (bpm / 2).epsilon (0.02)));
    CAPTURE (result.onsetContrast);
    CHECK (result.bpmConfidence > 0.7);
    CHECK (result.hasTempo());
    CHECK (result.onsetSeconds.size() >= 15);
    CHECK (result.onsetsPerSecond > 0.5);
}

TEST_CASE ("The beat grid lines up with where the hits are", "[analysis]")
{
    const auto result = analyseRhythm (quarterNotes (100.0, 12.0, 0.0, 0.35));
    REQUIRE (result.beatSeconds.size() > 10);
    CHECK (result.beatSeconds.front() == Catch::Approx (0.35).margin (0.02));
    CHECK (result.beatSeconds[4] - result.beatSeconds[0] == Catch::Approx (4 * 0.6).epsilon (0.01));
}

TEST_CASE ("Human timing wobble lowers confidence a little but keeps the tempo", "[analysis]")
{
    const auto result = analyseRhythm (quarterNotes (100.0, 20.0, 0.012));
    CHECK (result.bpm == Catch::Approx (100.0).epsilon (0.03));
    CHECK (result.bpmConfidence > 0.5);
}

TEST_CASE ("Material without a pulse gets no tempo to trust", "[analysis]")
{
    SECTION ("silence")
    {
        const auto result
            = analyseRhythm (buffer (std::vector<float> (static_cast<std::size_t> (10 * fs), 0.0f)));
        CHECK_FALSE (result.hasTempo());
        CHECK (result.onsetsPerSecond == 0.0);
        CHECK (result.rmsDb < -100.0);
    }
    SECTION ("a sustained chord")
    {
        std::vector<float> x (static_cast<std::size_t> (12 * fs));
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / fs;
            const double swell = std::min (1.0, t / 2.0); // fades in over 2 s
            x[i] = static_cast<float> (swell * 0.2
                                       * (std::sin (2.0 * std::numbers::pi * 220.0 * t)
                                          + std::sin (2.0 * std::numbers::pi * 277.18 * t)
                                          + std::sin (2.0 * std::numbers::pi * 329.63 * t)));
        }
        const auto result = analyseRhythm (buffer (std::move (x)));
        CAPTURE (result.onsetContrast, result.onsetsPerSecond, result.bpmConfidence);
        CHECK (result.onsetsPerSecond < 0.5);
        CHECK (result.bpmConfidence < 0.5);
    }
    SECTION ("steady noise")
    {
        std::vector<float> x (static_cast<std::size_t> (12 * fs));
        Noise noise;
        for (auto& sample : x)
            sample = 0.1f * noise.next();
        const auto result = analyseRhythm (buffer (std::move (x)));
        CHECK (result.bpmConfidence < 0.5);
    }
    SECTION ("hits at random times")
    {
        std::vector<float> x (static_cast<std::size_t> (20 * fs), 0.0f);
        Noise noise;
        Noise when {99};
        for (double t = 0.3; t < 19.5; t += 0.2 + 0.7 * (0.5 + 0.5 * static_cast<double> (when.next())))
            addHit (x, t, 0.7f, noise);
        const auto result = analyseRhythm (buffer (std::move (x)));
        CHECK (result.bpmConfidence < 0.7);
    }
    SECTION ("a take too short to hold a pulse")
    {
        const auto result = analyseRhythm (quarterNotes (120.0, 1.5));
        CHECK_FALSE (result.hasTempo());
    }
}

TEST_CASE ("Straight and swung eighths are told apart", "[analysis]")
{
    CHECK (analyseRhythm (eighths (100.0, 20.0, 0.5)).feel == RhythmAnalysis::Feel::straight);
    CHECK (analyseRhythm (eighths (100.0, 20.0, 2.0 / 3.0)).feel == RhythmAnalysis::Feel::swing);
    CHECK (analyseRhythm (quarterNotes (100.0, 20.0)).feel
           == RhythmAnalysis::Feel::unknown); // nothing between beats
}

TEST_CASE ("A drum machine groove is found at its tempo", "[analysis]")
{
    constexpr int kick = 0, snare = 1, closedHat = 2;
    engine::Engine engine;
    test::publish (engine,
                   test::drumPatternProject (
                       {{kick, {0, 6, 8}}, {snare, {4, 12}}, {closedHat, {0, 2, 4, 6, 8, 10, 12, 14}}},
                       8 * 4 * core::ticksPerQuarterNote));
    engine.getTransport().setTempo (96.0);
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 1, static_cast<std::int64_t> (fs * 20.0), 512});

    instruments::SampleBuffer take;
    take.sampleRate = fs;
    take.channels = {audio[0]};
    const auto result = analyseRhythm (take);
    CHECK (result.bpm == Catch::Approx (96.0).epsilon (0.03));
    CHECK (result.bpmConfidence > 0.6);
    CHECK (result.feel == RhythmAnalysis::Feel::straight);
}

namespace
{
// A synth playing `notes` (start tick, length, pitch) looped over `bars` bars, at `bpm`.
instruments::SampleBuffer renderSynth (double bpm, int bars, std::vector<model::Note> notes, double seconds)
{
    model::ProjectDocument doc;
    doc.perform (model::AddTrack {model::InstrumentKind::synth});
    model::Clip clip;
    clip.length = bars * 4 * core::ticksPerQuarterNote;
    clip.loopLength = 4 * core::ticksPerQuarterNote;
    clip.notes = std::move (notes);
    doc.perform (model::AddClip {doc.project().tracks.front().id, clip});

    engine::Engine engine;
    test::publish (engine, doc.project());
    engine.getTransport().setTempo (bpm);
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 1, static_cast<std::int64_t> (fs * seconds), 512});
    return buffer (audio[0]);
}
} // namespace

TEST_CASE ("A played riff on the synth is found at its tempo", "[analysis]")
{
    // Eighth-note arpeggio, a note every 480 ticks.
    std::vector<model::Note> riff;
    const std::uint8_t pitches[] = {57, 60, 64, 60, 57, 62, 65, 62};
    for (int i = 0; i < 8; ++i)
        riff.push_back ({i * 480, 400, pitches[i], 0.8f});
    const auto result = analyseRhythm (renderSynth (100.0, 12, riff, 20.0));
    CAPTURE (result.bpm, result.bpmConfidence, result.onsetsPerSecond, result.onsetContrast);
    CHECK ((result.bpm == Catch::Approx (100.0).epsilon (0.03)
            || result.bpm == Catch::Approx (200.0).epsilon (0.03)));
    CHECK (result.bpmConfidence > 0.6);
}

TEST_CASE ("Long held chords are not mistaken for a rhythm", "[analysis]")
{
    // One chord per bar, held: a note every 4 beats.
    const auto result = analyseRhythm (
        renderSynth (90.0, 12, {{0, 3600, 57, 0.7f}, {0, 3600, 60, 0.7f}, {0, 3600, 64, 0.7f}}, 20.0));
    CAPTURE (result.bpm, result.bpmConfidence, result.onsetsPerSecond, result.onsetContrast);
    CHECK (result.onsetsPerSecond < 0.6);
}

TEST_CASE ("Stereo takes are analysed like their mono mix, and the result is deterministic", "[analysis]")
{
    auto mono = quarterNotes (110.0, 12.0);
    auto stereo = mono;
    stereo.channels.push_back (mono.channels[0]);
    const auto a = analyseRhythm (stereo);
    const auto b = analyseRhythm (stereo);
    CHECK (a.bpm == b.bpm);
    CHECK (a.onsetSeconds == b.onsetSeconds);
    CHECK (a.bpm == Catch::Approx (analyseRhythm (mono).bpm));
}

TEST_CASE ("A cancelled analysis returns an empty result", "[analysis]")
{
    std::atomic<bool> cancel {true};
    const auto result = analyseRhythm (quarterNotes (100.0, 12.0), {}, &cancel);
    CHECK_FALSE (result.hasTempo());
    CHECK (result.onsetSeconds.empty());
}

TEST_CASE ("Invalid audio is refused without crashing", "[analysis]")
{
    CHECK_FALSE (analyseRhythm ({}).hasTempo());
    instruments::SampleBuffer bad;
    bad.sampleRate = 0.0;
    bad.channels = {{0.1f, 0.2f}};
    CHECK_FALSE (analyseRhythm (bad).hasTempo());
}

TEST_CASE ("Two minutes of audio are analysed quickly", "[analysis][!benchmark]")
{
    const auto take = quarterNotes (100.0, 120.0, 0.005);
    const auto start = std::chrono::steady_clock::now();
    const auto result = analyseRhythm (take);
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    INFO ("took " << seconds << " s");
    CHECK (result.bpm == Catch::Approx (100.0).epsilon (0.02));
    CHECK (seconds < 2.0); // generous for debug and sanitizer builds; ADR-011 asks for < 0.5 s in release
}
