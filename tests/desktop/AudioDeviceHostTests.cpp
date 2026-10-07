#include "AudioDeviceHost.h"

#include <catch2/catch_test_macros.hpp>

using namespace ap;

// These never open a real audio device (CI machines have none and a developer's should stay
// quiet): they check what the device menu relies on when nothing is open.

TEST_CASE ("The device list is safe to ask for before any device is open", "[audio-device]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    engine::Engine engine;
    desktop::AudioDeviceHost host {engine};

    const auto list = host.getDevices();
    CHECK (list.output.isEmpty());
    CHECK (list.sampleRates.empty());
    CHECK (list.bufferSizes.empty());
    CHECK (list.sampleRate == 0.0);
    CHECK (list.bufferSize == 0);

    const auto status = host.getStatus();
    CHECK_FALSE (status.running);
    CHECK (status.deviceName.isEmpty());
}

TEST_CASE ("Choosing a device that does not exist is refused with a message", "[audio-device]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    engine::Engine engine;
    desktop::AudioDeviceHost host {engine};

    CHECK (host.setOutputDevice (juce::String::fromUTF8 ("No such output \xE2\x80\x94 \xF0\x9F\x8E\xB5"))
               .isNotEmpty());
    CHECK (host.setOutputDevice ("").isNotEmpty());
    CHECK (host.setPreferredInput ("No such input").isNotEmpty());
    CHECK (host.getDevices().output.isEmpty()); // nothing was opened by the attempts
}
