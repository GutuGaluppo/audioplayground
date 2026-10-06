#include "ap/dsp/Peaks.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using ap::dsp::computePeaks;

TEST_CASE ("Peaks hold the loudest sample of every channel in each slice", "[dsp][peaks]")
{
    constexpr double rate = 48000.0; // 240 frames per peak
    std::vector<std::vector<float>> channels (2, std::vector<float> (1000, 0.0f));
    channels[0][10] = 0.5f;
    channels[1][250] = -1.0f;
    channels[0][500] = 0.001f; // quiet but not silent
    channels[1][999] = std::numeric_limits<float>::quiet_NaN();

    const auto peaks = computePeaks (channels, rate);
    REQUIRE (peaks.size() == 5); // ceil (1000 / 240)
    CHECK (peaks[0] == 128);     // ceil (0.5 * 255)
    CHECK (peaks[1] == 255);
    CHECK (peaks[2] == 1);
    CHECK (peaks[3] == 0);
    CHECK (peaks[4] == 0); // NaN is not a peak
}

TEST_CASE ("Peaks cover the whole file at any rate, up to the limit", "[dsp][peaks]")
{
    for (const double rate : {22050.0, 44100.0, 96000.0})
    {
        const auto frames = static_cast<std::size_t> (rate * 3.0); // 3 s
        const auto peaks = computePeaks ({std::vector<float> (frames, 0.25f)}, rate);
        CHECK (peaks.size() == 600);
        CHECK (peaks.back() == 64);
    }
    CHECK (computePeaks ({}, 48000.0).empty());
    CHECK (computePeaks ({std::vector<float> (10, 1.0f)}, 0.0).empty());

    const std::vector<std::vector<float>> longFile (
        1, std::vector<float> (8000 * 700, 0.0f)); // 11+ min at 8 kHz
    CHECK (computePeaks (longFile, 8000.0).size() == ap::dsp::maxPeaks);
}
