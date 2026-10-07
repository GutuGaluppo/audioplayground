#pragma once

#include "ap/model/ProjectDocument.h"

#include <functional>
#include <string_view>

namespace ap::host
{

// Where intents from a UI change the project (ADR-003): the open project and its undo history,
// kept in sync with the engine. The desktop Session and the web host both implement it, so the same
// intent code (IntentApplier) runs in both.
class ProjectEditor
{
public:
    using GestureId = model::ProjectDocument::GestureId;

    virtual ~ProjectEditor() = default;

    [[nodiscard]] virtual const model::ProjectDocument& document() const noexcept = 0;
    [[nodiscard]] const model::Project& project() const noexcept { return document().project(); }

    // Each returns whether the project changed; a rejected or no-op command changes nothing.
    virtual bool perform (model::Command command, GestureId gesture = 0) = 0;
    // Several commands as one undo step.
    virtual bool performGroup (std::string_view description,
                               const std::function<void (model::ProjectDocument::Group&)>& edit,
                               GestureId gesture = 0) = 0;
    virtual bool undo() = 0;
    virtual bool redo() = 0;
};

} // namespace ap::host
