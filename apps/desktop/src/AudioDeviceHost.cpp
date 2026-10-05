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
    }

    const juce::ScopedLock lock (errorLock);
    status.error = lastError;
    return status;
}

void AudioDeviceHost::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                        float* const* outputChannelData,
                                                        int numOutputChannelsInUse, int numSamples,
                                                        const juce::AudioIODeviceCallbackContext&)
{
    engine.process (core::AudioBlock {outputChannelData, numOutputChannelsInUse, numSamples});
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
