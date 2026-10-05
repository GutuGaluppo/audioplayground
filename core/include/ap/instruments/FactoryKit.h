#pragma once

#include "ap/instruments/SampleBuffer.h"

#include <array>
#include <memory>
#include <string_view>

namespace ap::instruments
{

// The built-in drum kit, synthesised in code at the engine's sample rate. Owning these sounds
// means pads work the moment the app opens, with no sample files to ship or license.
// Deterministic: the same rate always produces bit-identical sounds.
inline constexpr std::size_t factoryKitSize = 16;

inline constexpr std::array<std::string_view, factoryKitSize> factoryKitNames {
    "Kick", "Snare",   "Closed Hat", "Open Hat", "Clap",     "Low Tom",   "Mid Tom", "High Tom",
    "Rim",  "Cowbell", "Shaker",     "Crash",    "Perc Low", "Perc High", "Bass",    "Zap"};

// Not real-time safe (allocates); call from prepare().
[[nodiscard]] std::unique_ptr<SampleBuffer> makeFactorySound (std::size_t pad, double sampleRate);

} // namespace ap::instruments
