#pragma once

#include "ap/engine/Engine.h"
#include "ap/engine/RenderGraph.h"
#include "ap/model/ClipEditing.h"
#include "ap/model/ProjectDocument.h"

#include <initializer_list>
#include <memory>
#include <utility>

namespace ap::test
{

// Hands the project's render graph to the engine, as the desktop Session does after every edit.
inline void publish (engine::Engine& engine, const model::Project& project,
                     const engine::AudioLookup& audio = {})
{
    engine.publishRenderGraph (
        std::make_unique<engine::RenderGraph> (engine::buildRenderGraph (project, 1, audio)));
}

struct PadSteps
{
    int pad = 0;
    std::initializer_list<int> steps;
};

// A project with a drum track holding one looping one-bar pattern clip that starts at bar 1.
inline model::Project drumPatternProject (std::initializer_list<PadSteps> rows, core::Ticks length)
{
    model::ProjectDocument doc;
    doc.perform (model::AddTrack {model::InstrumentKind::drums});
    auto clip = model::makePatternClip (0, length);
    for (const auto& row : rows)
        for (const int step : row.steps)
            clip = model::withDrumStep (clip, static_cast<std::size_t> (row.pad),
                                        static_cast<std::size_t> (step), true);
    doc.perform (model::AddClip {doc.project().tracks.front().id, clip});
    return doc.project();
}

} // namespace ap::test
