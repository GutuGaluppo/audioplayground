#include "TestAudio.h"
#include "ap/instruments/Sampler.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace ap;
using instruments::NoteEvent;
using instruments::SampleBuffer;
using instruments::Sampler;
using params::ParamId;

namespace
{
constexpr double fs = 48000.0;

std::unique_ptr<SampleBuffer> makeSine (int frames, double frequency = 1000.0, std::uint64_t assetId = 1)
{
    auto buffer = std::make_unique<SampleBuffer>();
    buffer->assetId = assetId;
    buffer->sampleRate = fs;
    buffer->channels.assign (1, std::vector<float> (static_cast<std::size_t> (frames)));
    for (int n = 0; n < frames; ++n)
        buffer->channels[0][static_cast<std::size_t> (n)]
            = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * frequency * n / fs));
    return buffer;
}

struct SamplerRig
{
    Sampler sampler;
    params::ParameterStore parameters;
    test::TestBuffer buffer {2, 256};

    SamplerRig() { sampler.prepare (fs); }

    std::vector<float> run (int numSamples)
    {
        std::vector<float> out;
        for (int done = 0; done < numSamples; done += 256)
        {
            buffer.fill (0.0f);
            sampler.render (buffer.block(), parameters);
            const auto& left = buffer.channel (0);
            out.insert (out.end(), left.begin(), left.begin() + std::min (256, numSamples - done));
        }
        return out;
    }

    void play (int note, float velocity = 1.0f)
    {
        sampler.handle ({NoteEvent::Type::noteOn, static_cast<std::uint8_t> (note), velocity});
    }
};

float maxStep (const std::vector<float>& x)
{
    float step = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i)
        step = std::max (step, std::abs (x[i] - x[i - 1]));
    return step;
}

int soundingLength (const std::vector<float>& x)
{
    for (auto i = static_cast<int> (x.size()) - 1; i >= 0; --i)
        if (std::abs (x[static_cast<std::size_t> (i)]) > 1.0e-6f)
            return i + 1;
    return 0;
}
} // namespace

TEST_CASE ("Sampler is silent until a sample is loaded", "[sampler]")
{
    SamplerRig rig;
    rig.play (60);
    CHECK (soundingLength (rig.run (1024)) == 0);
    CHECK (rig.sampler.loadedAssetId() == 0);
}

TEST_CASE ("Sampler plays the root note bit-exactly", "[sampler]")
{
    SamplerRig rig;
    auto source = makeSine (4800);
    const auto expected = source->channels[0];
    rig.sampler.loadSample (std::move (source));
    (void)rig.run (256); // swap in

    rig.play (Sampler::rootNote);
    const auto out = rig.run (4800);
    for (std::size_t i = 0; i < expected.size(); ++i)
        REQUIRE (out[i] == expected[i]);
    CHECK (rig.sampler.loadedAssetId() == 1);
}

TEST_CASE ("Sampler transposes by changing the playback rate", "[sampler]")
{
    SamplerRig rig;
    rig.sampler.loadSample (makeSine (9600));
    (void)rig.run (256);

    rig.play (Sampler::rootNote + 12);
    CHECK (std::abs (soundingLength (rig.run (20000)) - 4800) <= 2);

    rig.parameters.set (ParamId::samplerPitch, -12.0f);
    rig.play (Sampler::rootNote);
    CHECK (std::abs (soundingLength (rig.run (25000)) - 19200) <= 2);
}

TEST_CASE ("Sampler trims start and end without clicks", "[sampler]")
{
    SamplerRig rig;
    rig.sampler.loadSample (makeSine (48000, 200.0));
    rig.parameters.set (ParamId::samplerStart, 25.0f);
    rig.parameters.set (ParamId::samplerEnd, 50.0f);
    (void)rig.run (256);

    rig.play (Sampler::rootNote);
    const auto out = rig.run (20000);
    CHECK (std::abs (soundingLength (out) - 12000) <= 2);
    CHECK (std::abs (out[0]) < 0.01f);
    // A 200 Hz sine at 0.5 moves < 0.014 per sample; anything bigger would be a click.
    CHECK (maxStep (out) < 0.02f);
}

TEST_CASE ("Gate mode stops on note off; one-shot plays through", "[sampler]")
{
    SamplerRig rig;
    rig.sampler.loadSample (makeSine (48000, 200.0));
    (void)rig.run (256);

    rig.play (Sampler::rootNote);
    (void)rig.run (2400);
    rig.sampler.handle ({NoteEvent::Type::noteOff, static_cast<std::uint8_t> (Sampler::rootNote), 0.0f});
    CHECK (soundingLength (rig.run (48000)) > 40000); // one-shot ignores note off

    rig.parameters.set (ParamId::samplerMode, 1.0f);
    rig.play (Sampler::rootNote);
    (void)rig.run (2400);
    rig.sampler.handle ({NoteEvent::Type::noteOff, static_cast<std::uint8_t> (Sampler::rootNote), 0.0f});
    const auto tail = rig.run (4800);
    CHECK (soundingLength (tail) <= static_cast<int> (0.03 * fs) + 256);
    CHECK (maxStep (tail) < 0.02f);
}

TEST_CASE ("Sampler never drops a note and caps sounding voices", "[sampler]")
{
    SamplerRig rig;
    rig.sampler.loadSample (makeSine (48000, 200.0));
    (void)rig.run (256);

    for (int i = 0; i < 20; ++i)
        rig.play (40 + i);
    (void)rig.run (256);
    CHECK (rig.sampler.activeVoiceCount() == Sampler::maxVoices);

    const auto out = rig.run (256);
    for (const float s : out)
        REQUIRE (std::isfinite (s));
}

TEST_CASE ("Swapping the sample while voices play fades them and frees the old buffer later", "[sampler]")
{
    SamplerRig rig;
    rig.sampler.loadSample (makeSine (48000, 200.0, 1));
    (void)rig.run (256);
    rig.play (Sampler::rootNote);
    (void)rig.run (1024);

    rig.sampler.loadSample (makeSine (48000, 300.0, 2));
    const auto during = rig.run (4800);
    CHECK (rig.sampler.loadedAssetId() == 2);
    CHECK (maxStep (during) < 0.03f); // the old voice fades, no click

    // The old buffer is retired once its voice is gone; the message thread frees it.
    (void)rig.run (512);
    CHECK (rig.sampler.collectGarbage() == 1);

    // The new sample plays.
    rig.play (Sampler::rootNote);
    CHECK (soundingLength (rig.run (1024)) > 0);
}

TEST_CASE ("Sampler ignores invalid buffers", "[sampler]")
{
    SamplerRig rig;
    auto empty = std::make_unique<SampleBuffer>();
    rig.sampler.loadSample (std::move (empty));
    (void)rig.run (256);
    rig.play (Sampler::rootNote);
    CHECK (soundingLength (rig.run (1024)) == 0);
    CHECK (rig.sampler.loadedAssetId() == 0);
    CHECK (rig.sampler.collectGarbage() == 1);
}
