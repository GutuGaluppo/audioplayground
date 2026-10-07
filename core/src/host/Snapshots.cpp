#include "ap/host/Snapshots.h"

#include "ap/dsp/Peaks.h"
#include "ap/host/Base64.h"
#include "ap/instruments/FactoryKit.h"

#include <algorithm>
#include <limits>

namespace ap::host
{
namespace
{
int toInt (std::uint64_t id)
{
    return static_cast<int> (
        std::min<std::uint64_t> (id, static_cast<std::uint64_t> (std::numeric_limits<int>::max())));
}

int toInt (core::Ticks ticks)
{
    return static_cast<int> (std::clamp<core::Ticks> (ticks, 0, model::maxTimelineTicks));
}

std::vector<bridge::TimelineEffect> effectsOf (const model::TrackEffects& effects)
{
    std::vector<bridge::TimelineEffect> out;
    for (std::size_t e = 0; e < params::numEffects; ++e)
    {
        const auto& state = effects[e];
        out.push_back (
            {state.enabled,
             std::vector<float> (state.values.begin(), state.values.begin()
                                                           + static_cast<std::ptrdiff_t> (
                                                               params::effectDescriptors[e].numParameters))});
    }
    return out;
}
} // namespace

bridge::TimelineState timelineState (const model::Project& project)
{
    using bridge::TimelineClip;
    using bridge::TimelineTrack;

    bridge::TimelineState event;
    for (const auto& track : project.tracks)
    {
        TimelineTrack t;
        t.id = toInt (track.id.value);
        t.kind = track.kind == model::TrackKind::audio ? 0 : 1 + static_cast<int> (track.instrument);
        t.name = model::sanitiseName (track.name, TimelineTrack::nameMaxLength).value_or ("Track");
        t.volumeDb = static_cast<double> (track.volumeDb);
        t.pan = static_cast<double> (track.pan);
        t.muted = track.muted;
        t.soloed = track.soloed;

        for (const auto& clip : track.clips)
        {
            TimelineClip c;
            c.id = toInt (clip.id.value);
            c.start = toInt (clip.start);
            c.length = std::max (1, toInt (clip.length));
            c.asset = toInt (clip.asset.value);
            c.sourceOffsetSeconds = std::clamp (static_cast<double> (clip.sourceOffset)
                                                    / static_cast<double> (core::flicksPerSecond),
                                                0.0, TimelineClip::sourceOffsetSecondsMax);
            c.contentOffset = toInt (clip.contentOffset);
            c.loopLength = toInt (clip.loopLength);
            for (const auto& note : clip.notes)
                c.notes.push_back ({toInt (note.start), std::max (1, toInt (note.length)), note.pitch,
                                    static_cast<double> (note.velocity)});
            t.clips.push_back (std::move (c));
        }
        t.effects = effectsOf (track.effects);
        for (const auto& send : track.sends)
            t.sends.push_back ({toInt (send.bus.value), static_cast<double> (send.levelDb)});
        event.tracks.push_back (std::move (t));
    }
    for (const auto& bus : project.buses)
    {
        bridge::TimelineBus b;
        b.id = toInt (bus.id.value);
        b.name = model::sanitiseName (bus.name, bridge::TimelineBus::nameMaxLength).value_or ("Bus");
        b.volumeDb = static_cast<double> (bus.volumeDb);
        b.pan = static_cast<double> (bus.pan);
        b.muted = bus.muted;
        for (const auto& effect : effectsOf (bus.effects))
            b.effects.push_back ({effect.enabled, effect.values});
        event.buses.push_back (std::move (b));
    }
    return event;
}

bridge::HistoryState historyState (const model::ProjectDocument& doc)
{
    bridge::HistoryState event;
    event.canUndo = doc.canUndo();
    event.canRedo = doc.canRedo();
    event.undoLabel
        = std::string (doc.undoDescription().substr (0, bridge::HistoryState::undoLabelMaxLength));
    event.redoLabel
        = std::string (doc.redoDescription().substr (0, bridge::HistoryState::redoLabelMaxLength));
    return event;
}

bridge::ParamValue paramValue (const model::Project& project, params::ParamId id)
{
    return {std::string (params::descriptor (id).id), static_cast<double> (project.parameter (id))};
}

bridge::DrumsKit drumsKit (const model::Project& project)
{
    return {static_cast<int> (project.drums.kit)};
}

bridge::InstrumentState instrumentState (const engine::Engine& engine)
{
    return {static_cast<int> (engine.getLiveInstrument())};
}

bridge::TimelineAsset timelineAsset (const model::Project& project, model::AssetId id,
                                     const AssetAudio& audio)
{
    const auto* asset = project.findAsset (id);
    bridge::TimelineAsset a;
    a.id = toInt (id.value);
    a.name = model::sanitiseName (asset != nullptr ? asset->name : audio.name,
                                  bridge::TimelineAsset::nameMaxLength)
                 .value_or ("Audio");
    a.loaded = audio.loaded;
    a.missing = audio.missing;
    a.loading = audio.loading;
    a.durationSeconds = std::clamp (audio.durationSeconds, 0.0, bridge::TimelineAsset::durationSecondsMax);
    a.overview = audio.overview;
    return a;
}

bridge::ProjectAssets projectAssets (const model::Project& project,
                                     const std::function<bool (model::AssetId)>& isMissing)
{
    using bridge::ProjectAsset;
    bridge::ProjectAssets event;
    for (const auto& asset : project.assets)
    {
        const auto use = project.assetUse (asset.id);
        ProjectAsset a;
        a.id = toInt (asset.id.value);
        a.name = model::sanitiseName (asset.name, ProjectAsset::nameMaxLength).value_or ("Audio");
        a.clips = static_cast<int> (std::min<std::size_t> (use.clips, ProjectAsset::clipsMax));
        a.pads = static_cast<int> (std::min<std::size_t> (use.pads, ProjectAsset::padsMax));
        a.sampler = use.sampler;
        a.missing = isMissing && isMissing (asset.id);
        event.assets.push_back (std::move (a));
    }
    return event;
}

std::optional<bridge::TimelinePeaks> timelinePeaks (model::AssetId id, const std::vector<std::uint8_t>& peaks)
{
    if (peaks.empty())
        return std::nullopt;
    bridge::TimelinePeaks event;
    event.asset = toInt (id.value);
    event.peaksPerSecond = dsp::peaksPerSecond;
    event.data = base64Encode (peaks.data(), peaks.size());
    if (event.data.size() > bridge::TimelinePeaks::dataMaxLength)
        return std::nullopt;
    return event;
}

bridge::DrumsPad drumsPad (const model::Project& project, std::size_t pad, const PadSample& sample)
{
    const auto& settings = project.drums.pads[pad];
    bridge::DrumsPad event;
    event.pad = static_cast<int> (pad);
    event.custom = settings.sample.isValid();
    event.name = event.custom
                   ? model::sanitiseName (sample.name, bridge::DrumsPad::nameMaxLength).value_or ("Sample")
                   : std::string (instruments::factoryKitNames[pad]);
    event.volumeDb = static_cast<double> (settings.volumeDb);
    event.pitch = static_cast<double> (settings.pitch);
    event.muted = settings.muted;
    event.missing = sample.missing;
    return event;
}

bridge::TransportState transportState (const model::Project& project, const engine::Engine& engine,
                                       const TransportExtras& extras)
{
    const auto& transport = engine.getTransport();

    bridge::TransportState event;
    event.playing = transport.getState().playing;
    event.bpm = project.tempoBpm;
    event.numerator = project.timeSignature.numerator;
    event.denominator = project.timeSignature.denominator;
    event.countInBars = transport.getCountInBars();
    event.metronomeEnabled = engine.getMetronome().isEnabled();
    event.recording = extras.recording;
    event.armedTrack = toInt (extras.armedTrack.value);
    event.captureAvailable = extras.captureAvailable;
    const auto loop = transport.getLoop();
    event.loopEnabled = loop.enabled;
    event.loopStart = toInt (loop.start);
    event.loopEnd = toInt (loop.end);
    return event;
}

bridge::TransportPosition transportPosition (const engine::Engine& engine)
{
    const auto& transport = engine.getTransport();
    const auto state = transport.getState();

    // Bars and beats depend only on the meter; the sample rate is irrelevant for this conversion.
    const core::TempoMap map (transport.getTempo(), transport.getTimeSignature(), 48000.0);
    const auto position = map.toBarBeatTick (state.positionTicks);

    bridge::TransportPosition event;
    event.bar = static_cast<int> (std::clamp<std::int64_t> (position.bar, bridge::TransportPosition::barMin,
                                                            bridge::TransportPosition::barMax));
    event.beat = position.beat;
    event.ticks = static_cast<int> (std::clamp<core::Ticks> (
        state.positionTicks, bridge::TransportPosition::ticksMin, bridge::TransportPosition::ticksMax));
    event.countingIn = state.countingIn;
    return event;
}

bridge::EngineMeters engineMeters (engine::Engine& engine)
{
    return {std::clamp (static_cast<double> (engine.consumeOutputPeak()), 0.0, 1.0),
            std::clamp (static_cast<double> (engine.consumeInputPeak()), 0.0, 1.0),
            std::clamp (static_cast<double> (engine.consumeLimiterReductionDb()), -60.0, 0.0)};
}

} // namespace ap::host
