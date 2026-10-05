#pragma once

#include "Session.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace ap::desktop
{

// New / Open / Save / Save As / Close with the platform dialogs, including the "save changes?"
// confirmation. Message thread only; every dialog is asynchronous.
class ProjectActions
{
public:
    enum class NoticeLevel
    {
        info = 0,
        warning = 1,
        error = 2
    };

    ProjectActions (Session& session, juce::PropertiesFile& settings);

    void newProject();
    void openProject();
    void save();
    void saveAs();

    // Asks to save unsaved changes, then calls proceed (unless the user cancels).
    void confirmClose (std::function<void()> proceed);

    // Reopens the last project (or recovers after a crash). Call once at startup.
    void restoreLastSession (bool previousSessionCrashed);

    std::function<void (NoticeLevel, const std::string&)> onNotice;

private:
    void saveAsThen (std::function<void()> afterSave);
    void saveThen (std::function<void()> afterSave);
    void rememberLocation();
    void notice (NoticeLevel level, const std::string& message);

    Session& session;
    juce::PropertiesFile& settings;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace ap::desktop
