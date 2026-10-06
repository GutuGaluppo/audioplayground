#include "FxTest.h"
#include "TestAudio.h"
#include "TimelineHelpers.h"
#include "ap/core/ScopedNoDenormals.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/engine/TrackChain.h"
#include "ap/fx/Limiter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace ap;
using params::EffectKind;

namespace
{
constexpr double fs = 48000.0;
const float ceiling = std::pow (10.0f, fx::Limiter::ceilingDb / 20.0f);

model::EffectState on (EffectKind kind, std::initializer_list<std::pair<std::size_t, float>> values = {})
{
    auto state = model::defaultEffectState (kind);
    state.enabled = true;
    for (const auto& [index, value] : values)
        state.values[index] = value;
    return state;
}

std::size_t at (auto param)
{
    return static_cast<std::size_t> (param);
}

// Runs a chain over a stereo signal in 256-sample blocks with fixed effects.
test::Stereo run (engine::TrackChain& chain, const model::TrackEffects& effects, test::Stereo audio)
{
    const auto length = audio[0].size();
    for (std::size_t start = 0; start < length; start += 256)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (256, length - start));
        std::array<float*, 2> channels {audio[0].data() + start, audio[1].data() + start};
        chain.set (effects);
        chain.process ({channels.data(), 2, n});
    }
    return audio;
}
} // namespace

// --- Limiter ---------------------------------------------------------------------------------

TEST_CASE ("The limiter is a pure delay below its ceiling", "[fx][limiter]")
{
    fx::Limiter limiter;
    limiter.prepare (fs);
    const auto input = test::noise (9600, 0.5f);
    const auto out = test::process (limiter, input);
    const auto latency = static_cast<std::size_t> (limiter.latency());
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = latency; i < input[ch].size(); ++i)
            REQUIRE (out[ch][i] == input[ch][i - latency]);
}

TEST_CASE ("The limiter catches sudden peaks without overshoot", "[fx][limiter]")
{
    fx::Limiter limiter;
    limiter.prepare (fs);
    // Silence, then a full-scale square burst at +6 dB.
    test::Stereo audio {std::vector<float> (24000, 0.0f), std::vector<float> (24000, 0.0f)};
    for (std::size_t i = 4800; i < 9600; ++i)
        audio[0][i] = audio[1][i] = ((i / 40) % 2 == 0 ? 2.0f : -2.0f);
    const auto out = test::process (limiter, audio);
    float peak = 0.0f;
    for (const auto& channel : out)
        for (const float sample : channel)
            peak = std::max (peak, std::abs (sample));
    CHECK (peak <= ceiling * 1.0001f);
    CHECK (limiter.consumeGainReductionDb() < -6.0f);
    CHECK (limiter.consumeGainReductionDb() == 0.0f); // consumed
}

TEST_CASE ("The limiter sees peaks between samples (true peak)", "[fx][limiter]")
{
    fx::Limiter limiter;
    limiter.prepare (fs);
    // A sine at fs/4 sampled 45 degrees off its peaks: samples reach 0.67, the waveform 0.95.
    std::vector<float> sine (24000);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = static_cast<float> (
            0.95 * std::sin (std::numbers::pi / 2.0 * static_cast<double> (i) + std::numbers::pi / 4.0));
    const auto out = test::process (limiter, {sine, sine});
    const double gain = test::rms (out[0], 12000, 24000) / test::rms (sine, 12000, 24000);
    CHECK (gain == Catch::Approx (ceiling / 0.95f).epsilon (0.02));
}

// --- Track chain -----------------------------------------------------------------------------

TEST_CASE ("A chain with every effect off is its constant latency, nothing else", "[engine][chain]")
{
    engine::TrackChain chain;
    chain.prepare (fs, 256);
    const auto input = test::noise (4800, 0.5f);
    const auto out = run (chain, model::defaultTrackEffects(), input);
    const auto latency = static_cast<std::size_t> (engine::TrackChain::latency());
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        for (std::size_t i = 0; i < latency; ++i)
            REQUIRE (out[ch][i] == 0.0f);
        for (std::size_t i = latency; i < input[ch].size(); ++i)
            REQUIRE (out[ch][i] == input[ch][i - latency]);
    }
}

TEST_CASE ("Switching chain effects crossfades, and a switched-off delay forgets its echoes",
           "[engine][chain]")
{
    const core::ScopedNoDenormals noDenormals;
    engine::TrackChain chain;
    chain.prepare (fs, 256);

    // EQ mid +12 dB at 1 kHz switched on mid-signal: no step beyond the boosted sine's own slope.
    auto effects = model::defaultTrackEffects();
    const auto sine = test::sine (1000.0, fs, 24000, 0.1);
    auto first = run (chain, effects,
                      test::Stereo {std::vector<float> (sine.begin(), sine.begin() + 12000),
                                    std::vector<float> (sine.begin(), sine.begin() + 12000)});
    effects[at (EffectKind::eq)] = on (EffectKind::eq, {{at (params::EqParam::midGain), 12.0f}});
    const auto second = run (chain, effects,
                             test::Stereo {std::vector<float> (sine.begin() + 12000, sine.end()),
                                           std::vector<float> (sine.begin() + 12000, sine.end())});
    first[0].insert (first[0].end(), second[0].begin(), second[0].end());
    CHECK (test::maxStep (first[0]) < 0.1f * 3.99f * 2.0f * 3.1416f * 1000.0f / 48000.0f * 1.1f);
    CHECK (test::toDb (test::rms (first[0], 20000, 24000) / test::rms (sine, 20000, 24000))
           == Catch::Approx (12.0).margin (0.3));

    // A delay with feedback, switched off once its echoes are flowing, then back on in silence.
    effects = model::defaultTrackEffects();
    effects[at (EffectKind::delay)] = on (EffectKind::delay, {{at (params::DelayParam::feedback), 90.0f},
                                                              {at (params::DelayParam::mix), 100.0f}});
    (void)run (chain, effects, test::noise (24000, 0.5f));
    effects[at (EffectKind::delay)].enabled = false;
    (void)run (chain, effects, test::noise (4800, 0.0f));
    effects[at (EffectKind::delay)].enabled = true;
    const auto silent = run (chain, effects, test::noise (24000, 0.0f));
    CHECK (test::rms (silent[0], 0, 24000) == 0.0);
}

// --- Engine ----------------------------------------------------------------------------------

TEST_CASE ("Track effects play in the engine, in time with the timeline", "[engine][chain]")
{
    const core::ScopedNoDenormals noDenormals;
    model::ProjectDocument doc;
    doc.perform (model::AddTrack {model::TrackKind::audio});
    const auto track = doc.project().tracks.back().id;
    doc.perform (model::AddAsset {"audio/1-a.wav", "a.wav"});
    model::Clip clip;
    clip.start = core::ticksPerQuarterNote; // 24000 samples at 120 BPM
    clip.length = 4 * core::ticksPerQuarterNote;
    clip.asset = doc.project().assets.back().id;
    doc.perform (model::AddClip {track, clip});
    // Low shelf -6 dB from 1 kHz: a constant (DC) source comes out at half its level.
    doc.perform (model::SetTrackEffect {track, EffectKind::eq,
                                        on (EffectKind::eq, {{at (params::EqParam::lowFreq), 1000.0f},
                                                             {at (params::EqParam::lowGain), -6.0f}})});

    engine::Engine engine;
    const auto source = std::make_shared<instruments::SampleBuffer>();
    source->sampleRate = fs;
    source->channels.push_back (std::vector<float> (10 * 48000, 0.5f));
    test::publish (engine, doc.project(), [&] (model::AssetId) { return source; });
    engine.getTransport().requestPlay();
    const auto audio = engine::renderOffline (engine, {fs, 2, 3 * 48000, 512});

    // Latency compensated: nothing before the clip, the clip from its first sample.
    for (std::size_t i = 0; i < 24000; ++i)
        REQUIRE (audio[0][i] == 0.0f);
    CHECK (audio[0][24000] != 0.0f);
    CHECK (audio[0][60000] == Catch::Approx (0.5f * std::pow (10.0f, -6.0f / 20.0f)).epsilon (0.01));
}

TEST_CASE ("Effect tails keep ringing after the transport stops", "[engine][chain]")
{
    const core::ScopedNoDenormals noDenormals;
    model::ProjectDocument doc;
    doc.perform (model::AddTrack {model::TrackKind::audio});
    const auto track = doc.project().tracks.back().id;
    doc.perform (model::AddAsset {"audio/1-a.wav", "a.wav"});
    model::Clip clip;
    clip.length = core::ticksPerQuarterNote / 4;
    clip.asset = doc.project().assets.back().id;
    doc.perform (model::AddClip {track, clip});
    doc.perform (model::SetTrackEffect {track, EffectKind::reverb,
                                        on (EffectKind::reverb, {{at (params::ReverbParam::mix), 100.0f}})});

    engine::Engine engine;
    const auto source = std::make_shared<instruments::SampleBuffer>();
    source->sampleRate = fs;
    source->channels.push_back (test::noise (6000, 0.5f)[0]);
    test::publish (engine, doc.project(), [&] (model::AssetId) { return source; });
    engine.prepare (fs, 512);
    engine.getTransport().requestPlay();
    test::TestBuffer buffer (2, 512);
    for (int b = 0; b < 20; ++b)
        engine.process (buffer.block());
    engine.getTransport().requestStop();
    for (int b = 0; b < 20; ++b)
        engine.process (buffer.block());
    CHECK (test::rms (buffer.channel (0), 0, 512) > 1.0e-3); // 0.2 s after stopping
}
