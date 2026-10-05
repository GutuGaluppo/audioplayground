#include "ap/engine/RenderGraph.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::engine
{

PanGains constantPowerPan (float pan) noexcept
{
    const float clamped = std::isfinite (pan) ? std::clamp (pan, -1.0f, 1.0f) : 0.0f;
    const float angle = (clamped + 1.0f) * std::numbers::pi_v<float> / 4.0f;
    return {std::cos (angle), std::sin (angle)};
}

RenderGraph buildRenderGraph (const model::Project& project, std::uint64_t projectVersion)
{
    RenderGraph graph;
    graph.projectVersion = projectVersion;
    graph.tracks.reserve (project.tracks.size());

    const bool anySolo = std::any_of (project.tracks.begin(), project.tracks.end(),
                                      [] (const model::Track& t) { return t.soloed; });

    for (const auto& track : project.tracks)
    {
        TrackRender render;
        render.id = track.id;
        render.audible
            = !track.muted && (!anySolo || track.soloed) && track.volumeDb > model::Track::minVolumeDb;

        if (render.audible)
        {
            const float volume = std::pow (10.0f, track.volumeDb / 20.0f);
            const auto pan = constantPowerPan (track.pan);
            render.leftGain = volume * pan.left;
            render.rightGain = volume * pan.right;
        }

        graph.tracks.push_back (render);
    }

    return graph;
}

} // namespace ap::engine
