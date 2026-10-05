#pragma once

#include "ap/engine/Engine.h"
#include "ap/io/ProjectFiles.h"
#include "ap/model/ProjectDocument.h"

#include <functional>
#include <juce_events/juce_events.h>
#include <optional>
#include <string>

namespace ap::desktop
{

// The open project, its history and where it lives on disk, kept in sync with the engine.
// Message thread only. All project changes go through perform/undo/redo (ADR-003).
class Session final : private juce::Timer
{
public:
    // scratchFolder holds autosaves of projects that have never been saved.
    Session (engine::Engine& engine, io::ProjectFolder scratchFolder);
    ~Session() override;

    bool perform (model::Command command, model::ProjectDocument::GestureId gesture = 0);
    bool undo();
    bool redo();

    [[nodiscard]] const model::ProjectDocument& document() const noexcept { return doc; }
    [[nodiscard]] const model::Project& project() const noexcept { return doc.project(); }

    // --- Persistence -----------------------------------------------------------------------
    [[nodiscard]] bool isDirty() const noexcept { return doc.version() != savedVersion; }
    [[nodiscard]] const std::optional<io::ProjectFolder>& location() const noexcept { return folder; }

    // Where the project's audio files live right now: its folder, or the scratch folder while
    // it has never been saved.
    [[nodiscard]] const io::ProjectFolder& assetRoot() const noexcept { return folder ? *folder : scratch; }

    void newProject();
    [[nodiscard]] std::optional<std::string> save(); // needs a location
    [[nodiscard]] std::optional<std::string> saveAs (const io::ProjectFolder& target);
    [[nodiscard]] std::optional<std::string> open (const io::ProjectFolder& source);

    // Restores the newest autosave if it is newer than the last save of that project (or if the
    // project was never saved). Returns a message for the user when something was recovered.
    std::optional<std::string> recoverFromAutosave (std::optional<io::ProjectFolder> lastProject);

    // Removes autosaves and imported audio of the never-saved project (after "Don't Save" or a
    // successful save). Only ever touches the app's own scratch folder.
    void discardScratchAutosaves();

    // Writes an autosave now if there are unsaved changes since the last one.
    void autosaveIfNeeded();

    // Called after every change to the project or its saved state.
    std::function<void()> onChanged;

private:
    void syncEngine();
    bool afterChange (bool changed);
    void notify();
    void timerCallback() override;
    void loadInto (model::LoadedProject loaded, std::optional<io::ProjectFolder> location, bool markDirty);

    engine::Engine& engine;
    io::ProjectFolder scratch;
    model::ProjectDocument doc;
    model::ProjectMetadata metadata;
    std::optional<io::ProjectFolder> folder;
    std::uint64_t savedVersion = 0;
    std::uint64_t autosavedVersion = 0;
    int ticksSinceAutosave = 0;
};

} // namespace ap::desktop
