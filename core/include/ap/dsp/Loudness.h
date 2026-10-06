#pragma once

#include <vector>

namespace ap::dsp
{

struct LoudnessReport
{
    double integratedLufs = -70.0; // EBU R128 / ITU-R BS.1770-4 integrated loudness (-70 = silence)
    double samplePeakDb = -120.0;  // dBFS
    double truePeakDb = -120.0;    // dBTP, 4x oversampled estimate
};

// Measures a finished render (Task 025). Channels are planar and of equal length; mono or stereo
// (both weighted 1.0). Not real-time: allocates and filters the whole signal.
//
// Integrated loudness: K-weighting (coefficients for any sample rate, as in libebur128), 400 ms
// blocks with 75 % overlap, absolute gate at -70 LUFS and relative gate 10 LU below the
// ungated level.
[[nodiscard]] LoudnessReport measureLoudness (const std::vector<std::vector<float>>& channels,
                                              double sampleRate);

} // namespace ap::dsp
