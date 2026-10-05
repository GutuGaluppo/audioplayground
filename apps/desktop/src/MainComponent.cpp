#include "MainComponent.h"

#include "ap/core/BuildInfo.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace ap::desktop
{
namespace
{
const juce::Colour background{0xff111214};
const juce::Colour surface{0xff1a1b1e};
const juce::Colour text{0xffe8e8ea};
const juce::Colour textMuted{0xff8a8b90};
const juce::Colour meterOk{0xff4cc38a};
const juce::Colour meterHot{0xfff2c94c};
const juce::Colour danger{0xffff5c5c};

constexpr int meterRefreshHz = 30;
constexpr float meterDecayPerFrame = 0.85f;
} // namespace

MainComponent::MainComponent (AudioDeviceHost& hostToUse, engine::Engine& engineToUse)
    : host (hostToUse), engine (engineToUse)
{
    toneButton.onClick = [this] { toggleTone(); };
    settingsButton.onClick = [this] { showAudioSettings(); };

    statusLabel.setColour (juce::Label::textColourId, textMuted);
    statusLabel.setJustificationType (juce::Justification::centred);

    errorBanner.setColour (juce::Label::backgroundColourId, danger.withAlpha (0.15f));
    errorBanner.setColour (juce::Label::textColourId, text);
    errorBanner.setJustificationType (juce::Justification::centred);

    for (auto* child :
         std::initializer_list<juce::Component*>{&toneButton, &settingsButton, &statusLabel, &errorBanner})
        addAndMakeVisible (child);

    host.onStatusChanged = [this] { refreshStatus(); };

    setWantsKeyboardFocus (true);
    setSize (960, 600);
    refreshStatus();
    startTimerHz (meterRefreshHz);
}

MainComponent::~MainComponent()
{
    host.onStatusChanged = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    const auto version = ap::core::buildInfo().versionString;
    auto header = getLocalBounds().removeFromTop (getHeight() / 3).removeFromBottom (72);

    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f));
    g.drawText ("Audio Playground", header.removeFromTop (30), juce::Justification::centred);

    g.setColour (textMuted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String ("v") + juce::String (version.data(), version.size()), header.removeFromTop (20),
                juce::Justification::centred);

    g.setColour (surface);
    g.fillRoundedRectangle (meterBounds.toFloat(), 3.0f);

    const auto fraction = juce::jlimit (0.0f, 1.0f, displayedPeak);
    g.setColour (fraction > 0.7f ? meterHot : meterOk);
    g.fillRoundedRectangle (
        meterBounds.toFloat().withWidth (static_cast<float> (meterBounds.getWidth()) * fraction), 3.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    errorBanner.setBounds (area.removeFromTop (errorBanner.getText().isEmpty() ? 0 : 36));

    area.removeFromTop (getHeight() / 3);
    auto buttons = area.removeFromTop (36).withSizeKeepingCentre (380, 36);
    toneButton.setBounds (buttons.removeFromLeft (180));
    settingsButton.setBounds (buttons.removeFromRight (180));

    area.removeFromTop (24);
    meterBounds = area.removeFromTop (8).withSizeKeepingCentre (380, 8);

    area.removeFromTop (16);
    statusLabel.setBounds (area.removeFromTop (24));
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        toggleTone();
        return true;
    }
    return false;
}

void MainComponent::timerCallback()
{
    displayedPeak = std::max (engine.consumeOutputPeak(), displayedPeak * meterDecayPerFrame);
    repaint (meterBounds);
}

void MainComponent::toggleTone()
{
    engine.setTestToneEnabled (!engine.isTestToneEnabled());
    refreshStatus();
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (host.getDeviceManager(), 0,
                                                                          0, // inputs: none until recording
                                                                          1, 2,  // outputs
                                                                          false, // MIDI in
                                                                          false, // MIDI out
                                                                          true,  // stereo pairs
                                                                          false);
    selector->setSize (520, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = background;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::refreshStatus()
{
    toneButton.setButtonText (engine.isTestToneEnabled() ? "Stop test tone" : "Play test tone");

    const auto status = host.getStatus();

    if (status.deviceName.isEmpty())
    {
        statusLabel.setText ("No audio output", juce::dontSendNotification);
        errorBanner.setText ("Audio device unavailable. Choose another output device in Audio settings.",
                             juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText (status.deviceName + "  ·  " + juce::String (status.sampleRate / 1000.0, 1)
                                 + " kHz  ·  " + juce::String (status.bufferSize) + " samples  ·  "
                                 + juce::String (status.outputLatencyMs, 1) + " ms",
                             juce::dontSendNotification);
        errorBanner.setText (status.error, juce::dontSendNotification);
    }

    resized();
    repaint();
}

} // namespace ap::desktop
