#include "MainComponent.h"

#include "ap/core/BuildInfo.h"

namespace ap::desktop
{

MainComponent::MainComponent()
{
    setSize (960, 600);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111214));

    const auto version = ap::core::buildInfo().versionString;

    g.setColour (juce::Colour (0xffe8e8ea));
    g.setFont (juce::FontOptions (22.0f));
    g.drawText ("Audio Playground", getLocalBounds().withTrimmedBottom (28), juce::Justification::centred);

    g.setColour (juce::Colour (0xff8a8b90));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String ("v") + juce::String (version.data(), version.size()),
                getLocalBounds().withTrimmedTop (28), juce::Justification::centred);
}

} // namespace ap::desktop
