#include "AudioDeviceHost.h"
#include "Session.h"
#include "WebUiHost.h"
#include "ap/engine/Engine.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace ap::desktop
{
namespace
{
constexpr auto audioDeviceStateKey = "audioDeviceState";

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Audio Playground";
    options.filenameSuffix = ".settings";
    options.folderName = "Audio Playground";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}
} // namespace

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, AudioDeviceHost& host, engine::Engine& engine, Session& session)
        : DocumentWindow (name, juce::Colour (0xff111214), allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new WebUiHost (host, engine, session), true);
        setResizable (true, true);
        setResizeLimits (720, 480, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // One instance owns the audio device and the open project.
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        settings.setStorageParameters (settingsOptions());

        audioHost = std::make_unique<AudioDeviceHost> (engine);
        const auto savedState = settings.getUserSettings()->getXmlValue (audioDeviceStateKey);
        audioHost->initialise (savedState.get());

        session = std::make_unique<Session> (engine);
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *audioHost, engine, *session);
    }

    void shutdown() override
    {
        if (audioHost != nullptr)
            if (const auto state = audioHost->createStateXml())
                settings.getUserSettings()->setValue (audioDeviceStateKey, state.get());

        settings.saveIfNeeded();

        // Order matters: the window references the session and host, which reference the engine.
        mainWindow.reset();
        session.reset();
        audioHost.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    engine::Engine engine;
    juce::ApplicationProperties settings;
    std::unique_ptr<AudioDeviceHost> audioHost;
    std::unique_ptr<Session> session;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ap::desktop

START_JUCE_APPLICATION (ap::desktop::Application)
