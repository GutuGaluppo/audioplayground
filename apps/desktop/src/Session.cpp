#include "Session.h"

namespace ap::desktop
{

Session::Session (engine::Engine& engineToUse) : engine (engineToUse)
{
    syncEngine();
}

bool Session::perform (model::Command command, model::ProjectDocument::GestureId gesture)
{
    return afterChange (doc.perform (std::move (command), gesture));
}

bool Session::undo()
{
    return afterChange (doc.undo());
}

bool Session::redo()
{
    return afterChange (doc.redo());
}

bool Session::afterChange (bool changed)
{
    if (changed)
    {
        syncEngine();
        if (onChanged)
            onChanged();
    }
    return changed;
}

void Session::syncEngine()
{
    // Lock-free setters; the audio thread picks the values up on its next block.
    const auto& project = doc.project();
    engine.getTransport().setTempo (project.tempoBpm);
    engine.getTransport().setTimeSignature (project.timeSignature);

    for (std::size_t i = 0; i < params::numParameters; ++i)
        engine.getParameters().set (static_cast<params::ParamId> (i), project.parameters[i]);
}

} // namespace ap::desktop
