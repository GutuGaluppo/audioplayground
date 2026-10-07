#pragma once

#include "ap/engine/Engine.h"

#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <vector>

namespace ap::desktop
{

// Connects the platform audio device to the headless engine.
//
// Opens outputs only. Inputs stay closed until a track is armed for recording, so the microphone
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
        int numInputChannels = 0; // open input channels (0 = input closed)
        juce::String inputName;
        // What recording compensates: input + output latency reported by the device, plus one
        // buffer each way (the input of a callback was captured during the previous one).
        std::int64_t roundTripLatencySamples = 0;
        juce::String error;
    };

    explicit AudioDeviceHost (engine::Engine& engine);
    ~AudioDeviceHost() override;

    // Message thread. Restores saved device settings, falling back to the system default.
    void initialise (const juce::XmlElement* savedState);

    [[nodiscard]] std::unique_ptr<juce::XmlElement> createStateXml() const;
    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    [[nodiscard]] Status getStatus() const;

    // Message thread. Opens (or closes) up to two channels of the input device; the device restarts.
    // Returns an error message for the user, or an empty string.
    juce::String setInputEnabled (bool enabled);

    // What the device menu offers: the current device type's devices and the current device's
    // sample rates and buffer sizes.
    struct DeviceList
    {
        std::vector<juce::String> outputs;
        std::vector<juce::String> inputs;
        juce::String output;         // the open output device ("" = none)
        juce::String preferredInput; // the input opened when a track is armed ("" = the default)
        std::vector<double> sampleRates;
        std::vector<int> bufferSizes;
        double sampleRate = 0.0;
        int bufferSize = 0;
    };
    [[nodiscard]] DeviceList getDevices() const;

    // Message thread. Each returns an error message for the user, or an empty string. The device
    // restarts; the input stays closed (it opens when a track is armed).
    juce::String setOutputDevice (const juce::String& name);
    juce::String setSampleRate (double rate);
    juce::String setBufferSize (int size);
    // Which input a later arm opens; if the input is open now it switches to the new one.
    juce::String setPreferredInput (const juce::String& name);

    // Called when the open device stopped (unplugged, driver gone) and the system default took over (the
    // message says which), so the user is told instead of finding silence.
    std::function<void (const juce::String&)> onDeviceRecovered;

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
    void recoverLostDevice();

    engine::Engine& engine;
    juce::AudioDeviceManager deviceManager;
    juce::MidiDeviceListConnection midiDevicesChanged;

    mutable juce::CriticalSection errorLock; // never taken on the audio thread
    juce::String lastError;
    bool recoveryAttempted = false; // one try per loss, so a failing default cannot loop
    juce::String preferredInput;    // input device to reopen (from the saved settings or the dialog)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceHost)
};

} // namespace ap::desktop
