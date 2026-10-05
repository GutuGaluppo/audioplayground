#include "ProjectActions.h"

#include "AppPaths.h"

namespace ap::desktop
{
namespace
{
constexpr auto lastProjectKey = "lastProject";

juce::File defaultProjectsDirectory()
{
    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (!music.isDirectory())
        music = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return music.getChildFile ("Audio Playground");
}

// Accepts the project folder itself or the project.json inside it.
std::optional<io::ProjectFolder> folderFromSelection (const juce::File& selection)
{
    if (selection.isDirectory())
        return io::ProjectFolder {toPath (selection)};
    if (selection.getFileName() == "project.json")
        return io::ProjectFolder {toPath (selection.getParentDirectory())};
    return std::nullopt;
}

juce::String withProjectExtension (juce::File file)
{
    const juce::String extension (io::projectFolderExtension.data(), io::projectFolderExtension.size());
    return file.hasFileExtension (extension) ? file.getFullPathName() : file.getFullPathName() + extension;
}
} // namespace

ProjectActions::ProjectActions (Session& sessionToUse, juce::PropertiesFile& settingsToUse)
    : session (sessionToUse)
    , settings (settingsToUse)
{
}

void ProjectActions::notice (NoticeLevel level, const std::string& message)
{
    if (onNotice)
        onNotice (level, message);
}

void ProjectActions::rememberLocation()
{
    if (const auto& location = session.location())
        settings.setValue (lastProjectKey, toJuceString (location->root));
    settings.saveIfNeeded();
}

void ProjectActions::newProject()
{
    confirmClose (
        [this]
        {
            session.discardScratchAutosaves();
            session.newProject();
            settings.removeValue (lastProjectKey);
        });
}

void ProjectActions::openProject()
{
    confirmClose (
        [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Open project", defaultProjectsDirectory(),
                                                           "*.playground;project.json");
            chooser->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::canSelectDirectories,
                [this] (const juce::FileChooser& fc)
                {
                    const auto selection = fc.getResult();
                    if (selection == juce::File())
                        return; // cancelled

                    const auto folder = folderFromSelection (selection);
                    if (!folder)
                    {
                        notice (NoticeLevel::error, "That is not a project. Choose a .playground folder.");
                        return;
                    }

                    if (const auto error = session.open (*folder))
                        notice (NoticeLevel::error, *error);
                    else
                        rememberLocation();
                });
        });
}

void ProjectActions::save()
{
    saveThen ({});
}

void ProjectActions::saveAs()
{
    saveAsThen ({});
}

void ProjectActions::saveThen (std::function<void()> afterSave)
{
    if (!session.location())
    {
        saveAsThen (std::move (afterSave));
        return;
    }

    if (const auto error = session.save())
    {
        notice (NoticeLevel::error, *error);
        return;
    }

    rememberLocation();
    if (afterSave)
        afterSave();
}

void ProjectActions::saveAsThen (std::function<void()> afterSave)
{
    const auto directory = defaultProjectsDirectory();
    directory.createDirectory();

    const auto suggested = directory.getChildFile (juce::File::createLegalFileName (session.project().name));
    chooser = std::make_unique<juce::FileChooser> ("Save project", suggested, "*.playground");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, afterSave = std::move (afterSave)] (const juce::FileChooser& fc)
                          {
                              const auto selection = fc.getResult();
                              if (selection == juce::File())
                                  return; // cancelled

                              const juce::File target (withProjectExtension (selection));
                              if (target.existsAsFile())
                              {
                                  notice (NoticeLevel::error,
                                          "A file with that name already exists. Choose another name.");
                                  return;
                              }

                              if (const auto error = session.saveAs (io::ProjectFolder {toPath (target)}))
                              {
                                  notice (NoticeLevel::error, *error);
                                  return;
                              }

                              rememberLocation();
                              if (afterSave)
                                  afterSave();
                          });
}

void ProjectActions::confirmClose (std::function<void()> proceed)
{
    if (!session.isDirty())
    {
        proceed();
        return;
    }

    const auto name = juce::String::fromUTF8 (session.project().name.c_str());
    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (
        juce::MessageBoxIconType::QuestionIcon, "Save changes to \"" + name + "\"?",
        "Your changes will be lost if you don't save them.", "Save", "Don't Save", "Cancel");

    juce::AlertWindow::showAsync (options,
                                  [this, proceed = std::move (proceed)] (int result)
                                  {
                                      if (result == 1) // Save
                                          saveThen (proceed);
                                      else if (result == 2) // Don't Save
                                      {
                                          if (!session.location())
                                              session.discardScratchAutosaves();
                                          proceed();
                                      }
                                      // 0: Cancel
                                  });
}

void ProjectActions::restoreLastSession (bool previousSessionCrashed)
{
    std::optional<io::ProjectFolder> lastProject;
    const auto stored = settings.getValue (lastProjectKey);
    if (stored.isNotEmpty() && juce::File (stored).isDirectory())
        lastProject = io::ProjectFolder {toPath (juce::File (stored))};

    if (lastProject)
    {
        if (const auto error = session.open (*lastProject))
        {
            notice (NoticeLevel::warning, "Could not reopen the last project. " + *error);
            lastProject.reset();
            settings.removeValue (lastProjectKey);
        }
    }

    if (previousSessionCrashed)
        if (const auto recovered = session.recoverFromAutosave (lastProject))
            notice (NoticeLevel::info, *recovered);
}

} // namespace ap::desktop
