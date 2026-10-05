#pragma once

#include <filesystem>
#include <juce_core/juce_core.h>
#include <string>

namespace ap::desktop
{

// Per-user folder for app state (settings, autosaves of unsaved work, WebView data).
// macOS: ~/Library/Application Support/Audio Playground
// Windows: %APPDATA%\Audio Playground
inline juce::File appDataDirectory()
{
#if JUCE_MAC
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("Application Support");
#else
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#endif
    return base.getChildFile ("Audio Playground");
}

inline std::filesystem::path toPath (const juce::File& file)
{
    const auto* utf8 = file.getFullPathName().toRawUTF8();
    return std::filesystem::path (std::u8string (reinterpret_cast<const char8_t*> (utf8)));
}

inline juce::String toJuceString (const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return juce::String::fromUTF8 (reinterpret_cast<const char*> (utf8.c_str()),
                                   static_cast<int> (utf8.size()));
}

} // namespace ap::desktop
