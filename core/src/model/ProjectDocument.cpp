#include "ap/model/ProjectDocument.h"

#include <utility>

namespace ap::model
{

ProjectDocument::ProjectDocument (Project initial)
    : current (std::move (initial))
{
}

bool ProjectDocument::perform (Command command, GestureId gesture)
{
    if (apply (command, current) != ApplyResult::applied)
        return false;

    redoStack.clear();

    if (gesture != 0 && !undoStack.empty() && undoStack.back().gesture == gesture
        && canMerge (undoStack.back().command, command))
    {
        merge (undoStack.back().command, command);
    }
    else
    {
        undoStack.push_back ({std::move (command), gesture});
        if (undoStack.size() > maxUndoSteps)
            undoStack.pop_front();
    }

    ++changeCounter;
    return true;
}

bool ProjectDocument::undo()
{
    if (undoStack.empty())
        return false;

    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    revert (entry.command, current);
    entry.gesture = 0; // a redone step never merges with a new gesture
    redoStack.push_back (std::move (entry));
    ++changeCounter;
    return true;
}

bool ProjectDocument::redo()
{
    if (redoStack.empty())
        return false;

    auto entry = std::move (redoStack.back());
    redoStack.pop_back();

    // Re-applying against the exact state it was recorded on must succeed; if it ever does not,
    // drop the redo history rather than corrupt the project.
    if (apply (entry.command, current) != ApplyResult::applied)
    {
        redoStack.clear();
        return false;
    }

    undoStack.push_back (std::move (entry));
    ++changeCounter;
    return true;
}

std::string_view ProjectDocument::undoDescription() const noexcept
{
    return undoStack.empty() ? std::string_view {} : describe (undoStack.back().command);
}

std::string_view ProjectDocument::redoDescription() const noexcept
{
    return redoStack.empty() ? std::string_view {} : describe (redoStack.back().command);
}

void ProjectDocument::reset (Project project)
{
    current = std::move (project);
    undoStack.clear();
    redoStack.clear();
    ++changeCounter;
}

} // namespace ap::model
