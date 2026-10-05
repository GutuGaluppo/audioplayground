#include "ap/core/MusicalTime.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace ap::core;

TEST_CASE ("TempoMap converts quarter notes exactly at common rates", "[time]")
{
    const TempoMap map (120.0, {4, 4}, 48000.0);
    CHECK (map.ticksToSamples (ticksPerQuarterNote) == 24000);
    CHECK (map.ticksToSamples (4 * ticksPerQuarterNote) == 96000);
    CHECK (map.samplesToTicks (24000) == ticksPerQuarterNote);
    CHECK (map.ticksToSamples (0) == 0);
}

TEST_CASE ("TempoMap tick -> sample -> tick round-trips", "[time][property]")
{
    const double tempo = GENERATE (20.0, 60.0, 87.5, 120.0, 133.0, 174.0, 300.0);
    const double rate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);
    CAPTURE (tempo, rate);

    const TempoMap map (tempo, {4, 4}, rate);
    REQUIRE (map.getSamplesPerTick() >= 1.0);

    for (Ticks t = -5000; t <= 2'000'000; t += 997)
    {
        const auto samples = map.ticksToSamples (t);
        REQUIRE (map.samplesToTicks (samples) == t);
        REQUIRE (map.tickAtOrBefore (samples) == t);
        REQUIRE (map.ticksToSamples (t + 1) > samples); // strictly monotonic
    }
}

TEST_CASE ("TempoMap tickAtOrBefore is consistent between tick boundaries", "[time][property]")
{
    const TempoMap map (133.0, {4, 4}, 44100.0);
    for (Samples s = -1000; s < 200000; s += 7)
    {
        const auto tick = map.tickAtOrBefore (s);
        REQUIRE (map.ticksToSamples (tick) <= s);
        REQUIRE (map.ticksToSamples (tick + 1) > s);
    }
}

TEST_CASE ("TempoMap stays exact far into a long session", "[time]")
{
    // Ten hours at 48 kHz, 120 BPM (2 s per bar of 4/4): 18 000 bars.
    const TempoMap map (120.0, {4, 4}, 48000.0);
    const Ticks tenHours = 18'000LL * 4 * ticksPerQuarterNote;
    CHECK (map.ticksToSamples (tenHours) == 10LL * 3600 * 48000);
}

TEST_CASE ("TempoMap reports bars and beats", "[time]")
{
    const Ticks quarter = ticksPerQuarterNote;

    SECTION ("4/4")
    {
        const TempoMap map (120.0, {4, 4}, 48000.0);
        CHECK (map.toBarBeatTick (0) == BarBeatTick {1, 1, 0});
        CHECK (map.toBarBeatTick (quarter) == BarBeatTick {1, 2, 0});
        CHECK (map.toBarBeatTick (4 * quarter + 10) == BarBeatTick {2, 1, 10});
    }

    SECTION ("3/4")
    {
        const TempoMap map (90.0, {3, 4}, 48000.0);
        CHECK (map.toBarBeatTick (3 * quarter) == BarBeatTick {2, 1, 0});
    }

    SECTION ("6/8 beats are eighth notes")
    {
        const TempoMap map (90.0, {6, 8}, 48000.0);
        CHECK (map.toBarBeatTick (quarter / 2) == BarBeatTick {1, 2, 0});
        CHECK (map.toBarBeatTick (3 * quarter) == BarBeatTick {2, 1, 0});
    }

    SECTION ("count-in positions are bar 0 and below")
    {
        const TempoMap map (120.0, {4, 4}, 48000.0);
        CHECK (map.toBarBeatTick (-quarter) == BarBeatTick {0, 4, 0});
        CHECK (map.toBarBeatTick (-4 * quarter) == BarBeatTick {0, 1, 0});
        CHECK (map.toBarBeatTick (-4 * quarter - 1) == BarBeatTick {-1, 4, quarter - 1});
    }
}

TEST_CASE ("TempoMap falls back to safe defaults for invalid input", "[time]")
{
    const TempoMap map (0.0, {0, 3}, -1.0);
    CHECK (map.getTempo() == 120.0);
    CHECK (map.getTimeSignature() == TimeSignature {4, 4});
    CHECK (map.getSampleRate() == 48000.0);

    CHECK_FALSE (isValidTempo (19.9));
    CHECK_FALSE (isValidTempo (300.1));
    CHECK_FALSE (TimeSignature {5, 6}.isValid());
    CHECK (TimeSignature {7, 8}.isValid());
}
