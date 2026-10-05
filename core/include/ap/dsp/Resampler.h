#pragma once

#include <cstddef>
#include <vector>

namespace ap::dsp
{

// Offline, high-quality sample-rate conversion (windowed-sinc, Kaiser window).
// Used off the audio thread when imported audio does not match the engine rate (ADR-006).
//
// Quality (verified in tests): flat to within 0.01 dB up to 0.35 x the lower rate (15 kHz at
// 44.1 kHz), -0.3 dB at 0.45 x; content above the lower Nyquist attenuated by more than 85 dB.
// Cost: a few milliseconds per second of audio.
[[nodiscard]] std::vector<float> resample (const std::vector<float>& input, double fromRate, double toRate);

} // namespace ap::dsp
