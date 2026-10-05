#include "Golden.h"
#include "TestAudio.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"
#include "ap/instruments/Synth.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ap;
using instruments::NoteEvent;
using instruments::Synth;
using params::ParamId;

namespace
{
constexpr double fs = 48000.0;

struct SynthRig
{
    Synth synth;
    params::ParameterStore parameters;
    test::TestBuffer buffer {2, 512};

    SynthRig() { synth.prepare (fs); }

    std::vector<float> run (int numSamples)
    {
        std::vector<float> out;
        out.reserve (static_cast<std::size_t> (numSamples));
        for (int done = 0; done < numSamples; done += 512)
        {
            buffer.fill (0.0f);
            synth.render (buffer.block(), parameters);
            const auto& left = buffer.channel (0);
            out.insert (out.end(), left.begin(), left.begin() + std::min (512, numSamples - done));
        }
        return out;
    }
};

float rms (const std::vector<float>& x)
{
    double sum = 0.0;
    for (const float s : x)
        sum += static_cast<double> (s) * static_cast<double> (s);
    return static_cast<float> (std::sqrt (sum / static_cast<double> (x.size())));
}

NoteEvent on (int note, float velocity = 1.0f)
{
    return {NoteEvent::Type::noteOn, static_cast<std::uint8_t> (note), velocity};
}

NoteEvent off (int note)
{
    return {NoteEvent::Type::noteOff, static_cast<std::uint8_t> (note), 0.0f};
}
} // namespace

TEST_CASE ("Synth plays in tune", "[synth]")
{
    SynthRig rig;
    rig.parameters.set (ParamId::synthWaveform, 0.0f); // sine
    rig.parameters.set (ParamId::synthDetune, 0.0f);
    rig.parameters.set (ParamId::synthCutoff, 18000.0f);

    rig.synth.handle (on (69)); // A4
    const auto audio = rig.run (static_cast<int> (fs));

    int crossings = 0;
    for (std::size_t i = static_cast<std::size_t> (fs / 2); i + 1 < audio.size(); ++i)
        if (audio[i] < 0.0f && audio[i + 1] >= 0.0f)
            ++crossings;
    CHECK (std::abs (crossings - 220) <= 1); // 440 Hz over half a second

    rig.parameters.set (ParamId::synthPitch, 12.0f);
    const auto octaveUp = rig.run (static_cast<int> (fs));
    crossings = 0;
    for (std::size_t i = static_cast<std::size_t> (fs / 2); i + 1 < octaveUp.size(); ++i)
        if (octaveUp[i] < 0.0f && octaveUp[i + 1] >= 0.0f)
            ++crossings;
    CHECK (std::abs (crossings - 440) <= 1);
}

TEST_CASE ("Synth voices release to silence and free themselves", "[synth]")
{
    SynthRig rig;
    rig.parameters.set (ParamId::synthRelease, 0.05f);

    rig.synth.handle (on (60));
    rig.synth.handle (on (64));
    (void)rig.run (4800);
    CHECK (rig.synth.activeVoiceCount() == 2);
    CHECK (rms (rig.run (4800)) > 0.01f);

    rig.synth.handle (off (60));
    rig.synth.handle (off (64));
    (void)rig.run (static_cast<int> (fs * 0.2));
    CHECK (rig.synth.activeVoiceCount() == 0);
    CHECK (rms (rig.run (4800)) == 0.0f);
}

TEST_CASE ("Synth never exceeds 16 voices and stays finite", "[synth]")
{
    SynthRig rig;
    for (int note = 30; note < 90; ++note)
        rig.synth.handle (on (note));

    const auto audio = rig.run (9600);
    CHECK (rig.synth.activeVoiceCount() == Synth::maxVoices);
    for (const float s : audio)
        REQUIRE (std::isfinite (s));
}

TEST_CASE ("Synth retriggers a repeated note instead of stacking voices", "[synth]")
{
    SynthRig rig;
    rig.synth.handle (on (60));
    (void)rig.run (512);
    rig.synth.handle (on (60));
    (void)rig.run (512);
    CHECK (rig.synth.activeVoiceCount() == 1);
}

TEST_CASE ("Synth treats velocity 0 as note off and handles all-notes-off", "[synth]")
{
    SynthRig rig;
    rig.parameters.set (ParamId::synthRelease, 0.01f);

    rig.synth.handle (on (60));
    rig.synth.handle (on (60, 0.0f));
    (void)rig.run (4800);
    CHECK (rig.synth.activeVoiceCount() == 0);

    for (int note = 60; note < 66; ++note)
        rig.synth.handle (on (note));
    rig.synth.handle ({NoteEvent::Type::allNotesOff, 0, 0.0f});
    (void)rig.run (4800);
    CHECK (rig.synth.activeVoiceCount() == 0);
}

TEST_CASE ("Synth brightness control darkens the sound", "[synth]")
{
    const auto renderWithCutoff = [] (float cutoff)
    {
        SynthRig rig;
        rig.parameters.set (ParamId::synthCutoff, cutoff);
        rig.synth.handle (on (48));
        (void)rig.run (4800); // let smoothing settle
        return rms (rig.run (9600));
    };

    CHECK (renderWithCutoff (200.0f) < renderWithCutoff (8000.0f) * 0.6f);
}

TEST_CASE ("Synth chord through the engine matches the golden file", "[synth][golden]")
{
    engine::Engine engine;
    for (const int note : {48, 55, 60, 64})
        REQUIRE (engine.sendNoteFromUi (on (note, 0.8f)));

    const auto audio = engine::renderOffline (engine, {48000.0, 2, 24000, 512});
    const auto result
        = test::compareWithGolden ("synth_chord_48k", {48000.0, audio}, test::crossPlatformTolerance);
    INFO (result.message);
    CHECK (result.passed);
}
