#pragma once

#include "ap/model/Commands.h"
#include "ap/model/Project.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string_view>
#include <vector>

namespace ap::model
{

// Owns the open project and its undo/redo history. Message thread only.
//
// Undo is command-based (guide §15): the history stores commands, never project copies.
class ProjectDocument
{
public:
    // Commands sent with the same non-zero gesture ID, in a row, for the same target become
    // one undo step (e.g. every value of a fader drag): they merge into the last command of the
    // previous step (see canMerge). 0 means "not part of a gesture".
    using GestureId = std::uint64_t;
    static constexpr std::size_t maxUndoSteps = 1000;

    explicit ProjectDocument (Project initial = {});

    [[nodiscard]] const Project& project() const noexcept { return current; }

    // Returns true if the project changed.
    bool perform (Command command, GestureId gesture = 0);

    // Several commands applied as one undo step, each seeing the project as the previous one left
    // it. Used for edits such as "create the drum track and its first clip".
    class Group
    {
    public:
        // Applies a command. Returns it with its captured state (e.g. created ids), or nullptr if it
        // was a no-op or was rejected. A rejected command cancels the whole group. The pointer is
        // valid until the next call.
        const Command* perform (Command command);
        [[nodiscard]] const Project& project() const noexcept { return doc.current; }

    private:
        friend class ProjectDocument;
        explicit Group (ProjectDocument& document) noexcept
            : doc (document)
        {
        }

        ProjectDocument& doc;
        std::vector<Command> applied;
        bool rejected = false;
    };

    // Runs edit; if every command it performed was valid, records them as one undo step named
    // description (a string literal). Otherwise reverts them all. Returns true if the project
    // changed. Later commands of the same gesture may merge into the group's last command.
    bool performGroup (std::string_view description, const std::function<void (Group&)>& edit,
                       GestureId gesture = 0);
    bool undo();
    bool redo();

    [[nodiscard]] bool canUndo() const noexcept { return !undoStack.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redoStack.empty(); }
    [[nodiscard]] std::string_view undoDescription() const noexcept;
    [[nodiscard]] std::string_view redoDescription() const noexcept;

    // Replaces the project (e.g. after opening a file) and clears the history.
    void reset (Project project);

    // Increments on every change, including undo and redo. Cheap change detection for observers.
    [[nodiscard]] std::uint64_t version() const noexcept { return changeCounter; }

private:
    struct Entry
    {
        std::vector<Command> commands; // applied in order, reverted in reverse order
        GestureId gesture = 0;
        std::string_view description; // empty: describe the command
    };

    void record (Entry entry);
    static std::string_view describeEntry (const Entry& entry) noexcept;

    Project current;
    std::deque<Entry> undoStack;
    std::deque<Entry> redoStack;
    std::uint64_t changeCounter = 0;
};

} // namespace ap::model
