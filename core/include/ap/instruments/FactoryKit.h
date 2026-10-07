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

// Several kits share the same 16 pad slots (and names), so patterns keep their meaning when the kit
// changes. Kit 0 is the original sound set and must never change (it backs the golden files); the
// others are variations on it.
inline constexpr std::size_t factoryKitCount = 5;

inline constexpr std::array<std::string_view, factoryKitCount> factoryKitTitles {"Classic", "808", "Lo-fi",
                                                                                   "Acoustic", "Electro"};

// Not real-time safe (allocates); call from prepare() or the message thread.
[[nodiscard]] std::unique_ptr<SampleBuffer> makeFactorySound (std::size_t pad, double sampleRate,
                                                              std::size_t kit = 0);

} // namespace ap::instruments
