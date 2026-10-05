#include "AppPaths.h"
#include "AudioDeviceHost.h"
#include "ProjectActions.h"
#include "SampleLoader.h"
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

// Present while the app runs; still present at launch means the last session did not exit
// cleanly, so autosaves may hold work that was never saved.
class SessionLock
{
public:
    explicit SessionLock (juce::File lockFile)
        : file (std::move (lockFile))
        , previousSessionCrashed (file.existsAsFile())
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText ("running");
    }

    ~SessionLock() { file.deleteFile(); }

    SessionLock (const SessionLock&) = delete;
    SessionLock& operator= (const SessionLock&) = delete;

    const juce::File file;
    const bool previousSessionCrashed;
};
} // namespace

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, WebUiHost* content)
        : DocumentWindow (name, juce::Colour (0xff111214), allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
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
        lock = std::make_unique<SessionLock> (appDataDirectory().getChildFile ("session.lock"));

        audioHost = std::make_unique<AudioDeviceHost> (engine);
        const auto savedState = settings.getUserSettings()->getXmlValue (audioDeviceStateKey);
        audioHost->initialise (savedState.get());

        session = std::make_unique<Session> (
            engine, io::ProjectFolder {toPath (appDataDirectory().getChildFile ("Unsaved"))});
        actions = std::make_unique<ProjectActions> (*session, *settings.getUserSettings());

        samples = std::make_unique<SampleLoader> (*session, engine);
        auto* ui = new WebUiHost (*audioHost, engine, *session, *actions, *samples);
        actions->onNotice = [ui] (ProjectActions::NoticeLevel level, const std::string& message)
        { ui->showNotice (level, message); };
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), ui);

        actions->restoreLastSession (lock->previousSessionCrashed);
        samples->sync (audioHost->getStatus().sampleRate);
    }

    void shutdown() override
    {
        if (audioHost != nullptr)
            if (const auto state = audioHost->createStateXml())
                settings.getUserSettings()->setValue (audioDeviceStateKey, state.get());

        settings.saveIfNeeded();

        // Order matters: the window references the actions, session and host, which reference
        // the engine.
        mainWindow.reset();
        samples.reset();
        actions.reset();
        session.reset();
        audioHost.reset();
        lock.reset(); // clean exit: no recovery next time
    }

    void systemRequestedQuit() override
    {
        if (actions == nullptr)
        {
            quit();
            return;
        }

        actions->confirmClose ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

private:
    engine::Engine engine;
    juce::ApplicationProperties settings;
    std::unique_ptr<SessionLock> lock;
    std::unique_ptr<AudioDeviceHost> audioHost;
    std::unique_ptr<Session> session;
    std::unique_ptr<ProjectActions> actions;
    std::unique_ptr<SampleLoader> samples;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ap::desktop

START_JUCE_APPLICATION (ap::desktop::Application)
