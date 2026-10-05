#include "Golden.h"
#include "ap/engine/Engine.h"
#include "ap/engine/OfflineRenderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <stdexcept>

using ap::engine::Engine;
using ap::engine::renderOffline;
using ap::engine::RenderSettings;

namespace
{
void configureReferenceTone (Engine& engine)
{
    engine.setTestToneFrequency (1000.0f);
    engine.getParameters().set (ap::params::ParamId::toneLevel, -12.0f);
    engine.setTestToneEnabled (true);
}
} // namespace

TEST_CASE ("Offline render of the reference tone matches the golden file", "[render][golden]")
{
    Engine engine;
    configureReferenceTone (engine);

    const RenderSettings settings {48000.0, 2, 12000, 512}; // 250 ms
    const auto audio = renderOffline (engine, settings);

    const auto result = ap::test::compareWithGolden ("tone_1k_-12dB_48k", {settings.sampleRate, audio});
    INFO (result.message);
    CHECK (result.passed);
}

TEST_CASE ("Offline render is bit-identical across runs", "[render][determinism]")
{
    const RenderSettings settings {44100.0, 2, 22050, 256};

    Engine first;
    configureReferenceTone (first);
    Engine second;
    configureReferenceTone (second);

    CHECK (renderOffline (first, settings) == renderOffline (second, settings));
}

TEST_CASE ("Offline render does not depend on block size", "[render][determinism]")
{
    const int blockSize = GENERATE (1, 17, 64, 128, 333, 1024, 4096);
    CAPTURE (blockSize);

    Engine reference;
    configureReferenceTone (reference);
    const auto expected = renderOffline (reference, {48000.0, 2, 9600, 512});

    Engine engine;
    configureReferenceTone (engine);
    CHECK (renderOffline (engine, {48000.0, 2, 9600, blockSize}) == expected);
}

TEST_CASE ("Offline render validates its settings", "[render]")
{
    Engine engine;
    CHECK_THROWS_AS (renderOffline (engine, {0.0, 2, 100, 512}), std::invalid_argument);
    CHECK_THROWS_AS (renderOffline (engine, {48000.0, 0, 100, 512}), std::invalid_argument);
    CHECK_THROWS_AS (renderOffline (engine, {48000.0, 2, -1, 512}), std::invalid_argument);
    CHECK_THROWS_AS (renderOffline (engine, {48000.0, 2, 100, 0}), std::invalid_argument);
    CHECK (renderOffline (engine, {48000.0, 2, 0, 512}).at (0).empty());
}
