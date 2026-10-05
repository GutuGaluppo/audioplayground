#pragma once

#include "AudioDeviceHost.h"
#include "ap/engine/Engine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ap::desktop
{

// Temporary native diagnostics UI. Replaced by the WebView UI in Task 004.
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    MainComponent (AudioDeviceHost& host, engine::Engine& engine);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void toggleTone();
    void showAudioSettings();
    void refreshStatus();

    AudioDeviceHost& host;
    engine::Engine& engine;

    juce::TextButton toneButton;
    juce::TextButton settingsButton{"Audio settings..."};
    juce::Label statusLabel;
    juce::Label errorBanner;

    float displayedPeak = 0.0f;
    juce::Rectangle<int> meterBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ap::desktop
