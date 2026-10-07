#pragma once

#include "ap/engine/Engine.h"
#include "ap/engine/RenderGraph.h"
#include "ap/host/ProjectEditor.h"

#include <functional>

namespace ap::host
{

// The simplest project editor: a document whose every change is put into the engine. It has no
// files, timers or dialogs, so it runs anywhere: in the browser, and in tests of the intent code.
// (The desktop Session adds persistence and autosave on top of the same idea.)
class DocumentEditor final : public ProjectEditor
{
public:
    explicit DocumentEditor (engine::Engine& engine, model::Project initial = {});

    [[nodiscard]] const model::ProjectDocument& document() const noexcept override { return doc; }
    bool perform (model::Command command, GestureId gesture = 0) override;
    bool performGroup (std::string_view description,
                       const std::function<void (model::ProjectDocument::Group&)>& edit,
                       GestureId gesture = 0) override;
    bool undo() override;
    bool redo() override;

    // Replaces the project and its history (a new or opened project).
    void reset (model::Project project);

    // Where the render graph finds decoded audio for audio clips (none by default).
    void setAudioLookup (engine::AudioLookup lookup);

    // Called after every change to the project, once the engine has it.
    std::function<void()> onChanged;

private:
    bool afterChange (bool changed);
    void sync();

    engine::Engine& engine;
    engine::AudioLookup audioLookup;
    model::ProjectDocument doc;
};

} // namespace ap::host
