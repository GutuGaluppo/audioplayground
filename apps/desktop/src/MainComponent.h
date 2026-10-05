#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ap::desktop
{

// Placeholder content until the WebView UI lands (Task 004).
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint (juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ap::desktop
