#include "AudioDeviceHost.h"

#include "ap/core/AudioBlock.h"

namespace ap::desktop
{
namespace
{
constexpr int numInputChannels = 0;
constexpr int numOutputChannels = 2;
} // namespace

AudioDeviceHost::AudioDeviceHost (engine::Engine& engineToUse)
    : engine (engineToUse)
{
    deviceManager.addChangeListener (this);
}

AudioDeviceHost::~AudioDeviceHost()
{
    cancelPendingUpdate();
    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioDeviceHost::initialise (const juce::XmlElement* savedState)
{
    const auto error = deviceManager.initialise (numInputChannels, numOutputChannels, savedState, true);
    setError (error);

    // Saved settings may name an input: remember it, but keep the microphone closed until recording.
    preferredInput = deviceManager.getAudioDeviceSetup().inputDeviceName;
    if (preferredInput.isNotEmpty())
        setInputEnabled (false);

    deviceManager.addAudioCallback (this);

    // Every MIDI keyboard just works, including ones plugged in later.
    deviceManager.addMidiInputDeviceCallback ({}, this);
    enableAllMidiInputs();
    midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableAllMidiInputs(); });

    notifyStatusChanged();
}

void AudioDeviceHost::enableAllMidiInputs()
{
    for (const auto& input : juce::MidiInput::getAvailableDevices())
        if (!deviceManager.isMidiInputDeviceEnabled (input.identifier))
            deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
}

void AudioDeviceHost::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // JUCE serialises MIDI callbacks from all devices, so this is the queue's single producer.
    using instruments::NoteEvent;
    if (message.isNoteOn())
        engine.sendNoteFromMidi ({NoteEvent::Type::noteOn,
                                  static_cast<std::uint8_t> (message.getNoteNumber()),
                                  message.getFloatVelocity()});
    else if (message.isNoteOff())
        engine.sendNoteFromMidi (
            {NoteEvent::Type::noteOff, static_cast<std::uint8_t> (message.getNoteNumber()), 0.0f});
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine.sendNoteFromMidi ({NoteEvent::Type::allNotesOff, 0, 0.0f});
}

juce::String AudioDeviceHost::setInputEnabled (bool enabled)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    if (!enabled)
    {
        if (setup.inputDeviceName.isNotEmpty())
            preferredInput = setup.inputDeviceName;
        if (setup.inputDeviceName.isEmpty() && setup.inputChannels.isZero())
            return {};
        setup.inputDeviceName = {};
        setup.inputChannels.clear();
        setup.useDefaultInputChannels = false;
        return deviceManager.setAudioDeviceSetup (setup, true);
    }

    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr)
        return "No audio device is available.";

    const auto inputs = type->getDeviceNames (true);
    juce::String name = setup.inputDeviceName;
    if (name.isEmpty())
        name = inputs.contains (preferredInput) ? preferredInput : inputs[type->getDefaultDeviceIndex (true)];
    if (name.isEmpty())
        return "No microphone or audio input was found. Connect one, then try again.";

    setup.inputDeviceName = name;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true); // JUCE keeps only the channels the device has
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        return "Could not open the audio input. " + error;
    if (getStatus().numInputChannels == 0)
        return "The audio input \"" + name + "\" could not be opened.";
    return {};
}

std::unique_ptr<juce::XmlElement> AudioDeviceHost::createStateXml() const
{
    return deviceManager.createStateXml();
}

AudioDeviceHost::Status AudioDeviceHost::getStatus() const
{
    Status status;

    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->isOpen())
    {
        status.running = device->isPlaying();
        status.deviceName = device->getName();
        status.sampleRate = device->getCurrentSampleRate();
        status.bufferSize = device->getCurrentBufferSizeSamples();

        if (status.sampleRate > 0.0)
            status.outputLatencyMs
                = 1000.0 * static_cast<double> (device->getOutputLatencyInSamples() + status.bufferSize)
                / status.sampleRate;

        status.numInputChannels = device->getActiveInputChannels().countNumberOfSetBits();
        if (status.numInputChannels > 0)
        {
            status.inputName = deviceManager.getAudioDeviceSetup().inputDeviceName;
            status.roundTripLatencySamples = device->getInputLatencyInSamples()
                                           + device->getOutputLatencyInSamples() + 2 * status.bufferSize;
        }
    }

    const juce::ScopedLock lock (errorLock);
    status.error = lastError;
    return status;
}

void AudioDeviceHost::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                        int numInputChannelsInUse,
                                                        float* const* outputChannelData,
                                                        int numOutputChannelsInUse, int numSamples,
                                                        const juce::AudioIODeviceCallbackContext&)
{
    engine.process (core::AudioBlock {outputChannelData, numOutputChannelsInUse, numSamples},
                    core::InputBlock {inputChannelData, numInputChannelsInUse, numSamples});
}

void AudioDeviceHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    engine.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    setError ({});
    notifyStatusChanged();
}

void AudioDeviceHost::audioDeviceStopped()
{
    engine.releaseResources();
    notifyStatusChanged();
}

void AudioDeviceHost::audioDeviceError (const juce::String& errorMessage)
{
    setError (errorMessage);
    notifyStatusChanged();
}

void AudioDeviceHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    notifyStatusChanged();
}

void AudioDeviceHost::setError (const juce::String& message)
{
    const juce::ScopedLock lock (errorLock);
    lastError = message;
}

void AudioDeviceHost::notifyStatusChanged()
{
    // Device callbacks may arrive on driver threads. AsyncUpdater is thread-safe, coalesces
    // bursts, and is cancelled automatically when this object is destroyed.
    triggerAsyncUpdate();
}

void AudioDeviceHost::handleAsyncUpdate()
{
    if (onStatusChanged)
        onStatusChanged();
}

} // namespace ap::desktop
