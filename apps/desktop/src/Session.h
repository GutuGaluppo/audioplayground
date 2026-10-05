#pragma once

#include "ap/engine/Engine.h"
#include "ap/model/ProjectDocument.h"

#include <functional>

namespace ap::desktop
{

// The open project and its history, kept in sync with the engine. Message thread only.
// All project changes go through perform/undo/redo (ADR-003).
class Session
{
public:
    explicit Session (engine::Engine& engine);

    bool perform (model::Command command, model::ProjectDocument::GestureId gesture = 0);
    bool undo();
    bool redo();

    [[nodiscard]] const model::ProjectDocument& document() const noexcept { return doc; }
    [[nodiscard]] const model::Project& project() const noexcept { return doc.project(); }

    // Called after every change (perform, undo, redo).
    std::function<void()> onChanged;

private:
    void syncEngine();
    bool afterChange (bool changed);

    engine::Engine& engine;
    model::ProjectDocument doc;
};

} // namespace ap::desktop
