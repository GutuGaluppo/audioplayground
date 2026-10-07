#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ap::dsp
{

// Waveform peaks for drawing (Task 016): the largest absolute sample of all channels in each
// slice of 1 / peaksPerSecond seconds, as 0..255 (linear, full scale = 255). Any slice that is not
// digital silence is at least 1, so quiet passages stay visible. Not for the audio thread.
inline constexpr double peaksPerSecond = 200.0;  // ~5 ms: one peak per pixel at the closest zoom
inline constexpr std::size_t maxPeaks = 130'000; // a bit over 10 minutes

// A coarse picture of a whole sound (a sampler's or a clip's overview): `points` values, each the
// largest absolute sample of its slice, 0..1. Empty input gives all zeros.
inline constexpr int overviewPoints = 512;
[[nodiscard]] std::vector<float> computeOverview (const std::vector<std::vector<float>>& channels,
                                                  int points = overviewPoints);

[[nodiscard]] std::vector<std::uint8_t> computePeaks (const std::vector<std::vector<float>>& channels,
                                                      double sampleRate);

} // namespace ap::dsp
