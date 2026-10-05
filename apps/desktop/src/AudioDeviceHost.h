#pragma once

#include "ap/engine/Engine.h"

#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace ap::desktop
{

// Connects the platform audio device to the headless engine.
//
// Opens outputs only. Inputs stay closed until recording needs them, so the microphone
// permission is never requested at launch (guide §25).
class AudioDeviceHost final
    : private juce::AudioIODeviceCallback
    , private juce::MidiInputCallback
    , private juce::ChangeListener
    , private juce::AsyncUpdater
{
public:
    struct Status
    {
        bool running = false;
        juce::String deviceName;
        double sampleRate = 0.0;
        int bufferSize = 0;
        double outputLatencyMs = 0.0;
        juce::String error;
    };

    explicit AudioDeviceHost (engine::Engine& engine);
    ~AudioDeviceHost() override;

    // Message thread. Restores saved device settings, falling back to the system default.
    void initialise (const juce::XmlElement* savedState);

    [[nodiscard]] std::unique_ptr<juce::XmlElement> createStateXml() const;
    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    [[nodiscard]] Status getStatus() const;

    // Called on the message thread whenever the device starts, stops, changes or fails.
    std::function<void()> onStatusChanged;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void enableAllMidiInputs();

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void notifyStatusChanged();
    void handleAsyncUpdate() override;
    void setError (const juce::String& message);

    engine::Engine& engine;
    juce::AudioDeviceManager deviceManager;
    juce::MidiDeviceListConnection midiDevicesChanged;

    mutable juce::CriticalSection errorLock; // never taken on the audio thread
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceHost)
};

} // namespace ap::desktop
