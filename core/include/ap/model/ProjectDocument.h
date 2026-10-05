#pragma once

#include "ap/model/Commands.h"
#include "ap/model/Project.h"

#include <cstdint>
#include <deque>
#include <string_view>

namespace ap::model
{

// Owns the open project and its undo/redo history. Message thread only.
//
// Undo is command-based (guide §15): the history stores commands, never project copies.
class ProjectDocument
{
public:
    // Commands sent with the same non-zero gesture ID, in a row, for the same target become
    // one undo step (e.g. every value of a fader drag). 0 means "not part of a gesture".
    using GestureId = std::uint64_t;
    static constexpr std::size_t maxUndoSteps = 1000;

    explicit ProjectDocument (Project initial = {});

    [[nodiscard]] const Project& project() const noexcept { return current; }

    // Returns true if the project changed.
    bool perform (Command command, GestureId gesture = 0);
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
        Command command;
        GestureId gesture = 0;
    };

    Project current;
    std::deque<Entry> undoStack;
    std::deque<Entry> redoStack;
    std::uint64_t changeCounter = 0;
};

} // namespace ap::model
