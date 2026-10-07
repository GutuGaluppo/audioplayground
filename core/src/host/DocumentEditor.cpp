#include "ap/host/DocumentEditor.h"

#include "ap/engine/Export.h"

namespace ap::host
{

DocumentEditor::DocumentEditor (engine::Engine& engineToUse, model::Project initial)
    : engine (engineToUse)
    , doc (std::move (initial))
{
    sync();
}

void DocumentEditor::sync()
{
    engine::configureEngine (engine, doc.project(), doc.version(), audioLookup);
}

bool DocumentEditor::afterChange (bool changed)
{
    if (changed)
    {
        sync();
        if (onChanged)
            onChanged();
    }
    return changed;
}

bool DocumentEditor::perform (model::Command command, GestureId gesture)
{
    return afterChange (doc.perform (std::move (command), gesture));
}

bool DocumentEditor::performGroup (std::string_view description,
                                   const std::function<void (model::ProjectDocument::Group&)>& edit,
                                   GestureId gesture)
{
    return afterChange (doc.performGroup (description, edit, gesture));
}

bool DocumentEditor::undo()
{
    return afterChange (doc.undo());
}

bool DocumentEditor::redo()
{
    return afterChange (doc.redo());
}

void DocumentEditor::reset (model::Project project)
{
    doc.reset (std::move (project));
    sync();
    if (onChanged)
        onChanged();
}

void DocumentEditor::refresh()
{
    sync();
}

void DocumentEditor::setAudioLookup (engine::AudioLookup lookup)
{
    audioLookup = std::move (lookup);
    sync();
}

} // namespace ap::host
