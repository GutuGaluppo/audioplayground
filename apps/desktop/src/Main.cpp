#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace ap::desktop
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : DocumentWindow (name, juce::Colour (0xff111214), allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
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
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ap::desktop

START_JUCE_APPLICATION (ap::desktop::Application)
