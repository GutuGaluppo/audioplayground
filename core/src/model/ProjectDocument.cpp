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

    auto* last = undoStack.empty() ? nullptr : &undoStack.back();
    if (gesture != 0 && last != nullptr && last->gesture == gesture
        && canMerge (last->commands.back(), command))
    {
        merge (last->commands.back(), command);
    }
    else
    {
        Entry entry;
        entry.commands.push_back (std::move (command));
        entry.gesture = gesture;
        record (std::move (entry));
    }

    ++changeCounter;
    return true;
}

const Command* ProjectDocument::Group::perform (Command command)
{
    if (rejected)
        return nullptr;

    switch (apply (command, doc.current))
    {
    case ApplyResult::applied:
        applied.push_back (std::move (command));
        return &applied.back();
    case ApplyResult::unchanged:
        return nullptr;
    case ApplyResult::rejected:
        rejected = true;
        return nullptr;
    }
    return nullptr;
}

bool ProjectDocument::performGroup (std::string_view description, const std::function<void (Group&)>& edit,
                                    GestureId gesture)
{
    Group group (*this);
    edit (group);

    if (group.rejected)
    {
        for (auto it = group.applied.rbegin(); it != group.applied.rend(); ++it)
            revert (*it, current);
        return false;
    }
    if (group.applied.empty())
        return false;

    redoStack.clear();
    Entry entry;
    entry.commands = std::move (group.applied);
    entry.description = description;
    entry.gesture = gesture;
    record (std::move (entry));
    ++changeCounter;
    return true;
}

void ProjectDocument::record (Entry entry)
{
    undoStack.push_back (std::move (entry));
    if (undoStack.size() > maxUndoSteps)
        undoStack.pop_front();
}

std::string_view ProjectDocument::describeEntry (const Entry& entry) noexcept
{
    if (!entry.description.empty())
        return entry.description;
    return entry.commands.empty() ? std::string_view {} : describe (entry.commands.back());
}

bool ProjectDocument::undo()
{
    if (undoStack.empty())
        return false;

    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    for (auto it = entry.commands.rbegin(); it != entry.commands.rend(); ++it)
        revert (*it, current);
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
    for (std::size_t i = 0; i < entry.commands.size(); ++i)
    {
        if (apply (entry.commands[i], current) != ApplyResult::applied)
        {
            for (std::size_t j = i; j-- > 0;)
                revert (entry.commands[j], current);
            redoStack.clear();
            return false;
        }
    }

    undoStack.push_back (std::move (entry));
    ++changeCounter;
    return true;
}

std::string_view ProjectDocument::undoDescription() const noexcept
{
    return undoStack.empty() ? std::string_view {} : describeEntry (undoStack.back());
}

std::string_view ProjectDocument::redoDescription() const noexcept
{
    return redoStack.empty() ? std::string_view {} : describeEntry (redoStack.back());
}

void ProjectDocument::reset (Project project)
{
    current = std::move (project);
    undoStack.clear();
    redoStack.clear();
    ++changeCounter;
}

} // namespace ap::model
