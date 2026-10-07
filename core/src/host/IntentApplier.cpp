#include "ap/host/IntentApplier.h"

#include "ap/instruments/DrumMachine.h"
#include "ap/model/ClipEditing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <variant>

namespace ap::host
{
namespace
{
model::TrackId toTrack (int id)
{
    return model::TrackId {static_cast<std::uint64_t> (id)};
}

model::BusId toBus (int id)
{
    return model::BusId {static_cast<std::uint64_t> (id)};
}

model::ClipId toClip (int id)
{
    return model::ClipId {static_cast<std::uint64_t> (id)};
}

model::ProjectDocument::GestureId toGesture (int gesture)
{
    return static_cast<model::ProjectDocument::GestureId> (gesture);
}

core::Ticks secondsToTicks (double seconds, double tempoBpm)
{
    return static_cast<core::Ticks> (
        std::ceil (seconds * tempoBpm * static_cast<double> (core::ticksPerQuarterNote) / 60.0));
}
} // namespace

IntentApplier::IntentApplier (ProjectEditor& editorToUse, engine::Engine& engineToUse, Resync resyncToUse)
    : editor (editorToUse)
    , engine (engineToUse)
    , resync (std::move (resyncToUse))
{
}

bool IntentApplier::apply (const bridge::Intent& intent)
{
    return std::visit ([this] (const auto& typed) { return handle (typed); }, intent);
}

void IntentApplier::resyncTransport() const
{
    if (resync.transport)
        resync.transport();
}

void IntentApplier::resyncTimeline() const
{
    if (resync.timeline)
        resync.timeline();
}

void IntentApplier::stopTransport()
{
    engine.getTransport().requestStop();
    resyncTransport();
}

// --- Engine and transport --------------------------------------------------------------------

bool IntentApplier::handle (const bridge::ToneSetEnabled& intent)
{
    engine.setTestToneEnabled (intent.enabled);
    if (resync.status)
        resync.status();
    return true;
}

bool IntentApplier::handle (const bridge::TransportPlay&)
{
    engine.getTransport().requestPlay();
    return true;
}

bool IntentApplier::handle (const bridge::TransportStop&)
{
    stopTransport();
    return true;
}

bool IntentApplier::handle (const bridge::TransportReturnToStart&)
{
    engine.getTransport().requestSeek (0);
    return true;
}

bool IntentApplier::handle (const bridge::TransportSetTempo& intent)
{
    if (!editor.perform (model::SetTempo {intent.bpm}))
        resyncTransport(); // rejected or unchanged: resync the field
    return true;
}

bool IntentApplier::handle (const bridge::TransportSetCountIn& intent)
{
    engine.getTransport().setCountInBars (intent.bars);
    resyncTransport();
    return true;
}

bool IntentApplier::handle (const bridge::TransportSeek& intent)
{
    engine.getTransport().requestSeek (intent.ticks);
    return true;
}

bool IntentApplier::handle (const bridge::TransportSetLoop& intent)
{
    const bool valid = intent.end > intent.start;
    engine.getTransport().setLoop (intent.enabled && valid, intent.start, valid ? intent.end : intent.start);
    resyncTransport();
    return true;
}

bool IntentApplier::handle (const bridge::MetronomeSetEnabled& intent)
{
    engine.getMetronome().setEnabled (intent.enabled);
    resyncTransport();
    return true;
}

// --- Parameters and history ------------------------------------------------------------------

bool IntentApplier::handle (const bridge::ParamSet& intent)
{
    // The codec checked the envelope; the parameter system checks the ID and its own range.
    const auto id = params::findParamId (intent.id);
    if (!id)
        return true; // unknown parameter

    const auto value = static_cast<float> (intent.value);
    if (!params::isInRange (params::descriptor (*id), value))
        return true; // out of range

    // Value changes are sent back through the project-changed path; a rejected or no-op edit still
    // echoes the current value so a control never stays out of sync.
    if (!editor.perform (model::SetParameter {*id, value}, toGesture (intent.gesture)) && resync.parameter)
        resync.parameter (*id);
    return true;
}

bool IntentApplier::handle (const bridge::SynthSetPreset& intent)
{
    // The synth's parameters in a fixed order (the UI's preset files list them the same way).
    static constexpr std::array<std::string_view, 10> ids {
        "synth.waveform", "synth.pitch", "synth.detune",  "synth.cutoff",  "synth.resonance",
        "synth.attack",   "synth.decay", "synth.sustain", "synth.release", "synth.volume"};
    if (intent.values.size() != ids.size())
        return true;

    std::array<std::pair<params::ParamId, float>, ids.size()> changes;
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        const auto id = params::findParamId (ids[i]);
        const auto value = intent.values[i];
        if (!id || !params::isInRange (params::descriptor (*id), value))
            return true; // the whole preset is refused
        changes[i] = {*id, value};
    }

    editor.performGroup ("Apply synth preset",
                         [&changes] (model::ProjectDocument::Group& group)
                         {
                             for (const auto& [id, value] : changes)
                                 (void)group.perform (model::SetParameter {id, value});
                         });
    return true;
}

bool IntentApplier::handle (const bridge::EditUndo&)
{
    editor.undo();
    return true;
}

bool IntentApplier::handle (const bridge::EditRedo&)
{
    editor.redo();
    return true;
}

bool IntentApplier::handle (const bridge::ProjectRename& intent)
{
    if (!editor.perform (model::RenameProject {intent.name}) && resync.project)
        resync.project(); // rejected (e.g. empty): restore the shown name
    return true;
}

// --- Playing ---------------------------------------------------------------------------------

bool IntentApplier::handle (const bridge::NoteOn& intent)
{
    engine.sendNoteFromUi ({instruments::NoteEvent::Type::noteOn, static_cast<std::uint8_t> (intent.note),
                            static_cast<float> (intent.velocity)});
    return true;
}

bool IntentApplier::handle (const bridge::NoteOff& intent)
{
    engine.sendNoteFromUi (
        {instruments::NoteEvent::Type::noteOff, static_cast<std::uint8_t> (intent.note), 0.0f});
    return true;
}

bool IntentApplier::handle (const bridge::NoteAllOff&)
{
    engine.sendNoteFromUi ({instruments::NoteEvent::Type::allNotesOff, 0, 0.0f});
    return true;
}

bool IntentApplier::handle (const bridge::InstrumentSelect& intent)
{
    engine.setLiveInstrument (static_cast<engine::Engine::LiveInstrument> (intent.instrument));
    if (resync.instrument)
        resync.instrument();
    return true;
}

// --- Drums -----------------------------------------------------------------------------------

bool IntentApplier::handle (const bridge::DrumsTrigger& intent)
{
    // Pads play through the same lock-free note queue as the keyboard (GM drum notes).
    engine.sendNoteFromUi ({instruments::NoteEvent::Type::noteOn,
                            static_cast<std::uint8_t> (instruments::DrumMachine::firstMidiNote + intent.pad),
                            static_cast<float> (intent.velocity)});
    return true;
}

bool IntentApplier::handle (const bridge::DrumsSetPad& intent)
{
    const auto pad = static_cast<std::size_t> (intent.pad);
    auto settings = editor.project().drums.pads[pad];
    settings.volumeDb = static_cast<float> (intent.volumeDb);
    settings.pitch = static_cast<float> (intent.pitch);
    settings.muted = intent.muted;
    if (!editor.perform (model::SetDrumPad {pad, settings}, toGesture (intent.gesture)) && resync.drumPad)
        resync.drumPad (pad);
    return true;
}

bool IntentApplier::handle (const bridge::DrumsResetPad& intent)
{
    const auto pad = static_cast<std::size_t> (intent.pad);
    auto settings = editor.project().drums.pads[pad];
    settings.sample = {};
    editor.perform (model::SetDrumPad {pad, settings});
    return true;
}

bool IntentApplier::handle (const bridge::DrumsSetKit& intent)
{
    if (!editor.perform (model::SetDrumKit {static_cast<std::uint8_t> (intent.kit)}) && resync.drumKit)
        resync.drumKit();
    return true;
}

void IntentApplier::editDrumPattern (std::uint64_t clipId, std::uint64_t gesture, bool createIfMissing,
                                     const std::function<model::Clip (const model::Clip&)>& change)
{
    if (clipId != 0)
    {
        editClip (clipId, model::ClipEdit::notes, gesture, change);
        return;
    }

    // No clip given: the pattern at the playhead's bar, created (with the drum track if needed)
    // when there is none. A stroke that creates it stays one undo step.
    const auto& project = editor.project();
    const auto bar = project.timeSignature.ticksPerBar();
    const auto barStart
        = std::max<core::Ticks> (0, engine.getTransport().getState().positionTicks) / bar * bar;
    if (const auto* drums = project.findInstrumentTrack (model::InstrumentKind::drums))
        for (const auto& clip : drums->clips)
            if (clip.start <= barStart && barStart < clip.end())
            {
                editClip (clip.id.value, model::ClipEdit::notes, gesture, change);
                return;
            }

    if (!createIfMissing)
        return;

    auto pattern = change (model::makePatternClip (barStart, 4 * bar));
    editor.performGroup (
        "Add pattern",
        [&pattern] (model::ProjectDocument::Group& group)
        {
            model::TrackId track;
            if (const auto* drums = group.project().findInstrumentTrack (model::InstrumentKind::drums))
                track = drums->id;
            else if (const auto* added = group.perform (model::AddTrack {model::InstrumentKind::drums}))
                track = std::get<model::AddTrack> (*added).created;
            (void)group.perform (model::AddClip {track, pattern});
        },
        gesture);
}

bool IntentApplier::handle (const bridge::DrumsSetStep& intent)
{
    const auto pad = static_cast<std::size_t> (intent.pad);
    const auto step = static_cast<std::size_t> (intent.step);
    editDrumPattern (static_cast<std::uint64_t> (intent.clip), toGesture (intent.gesture), intent.on,
                     [&] (const model::Clip& clip)
                     { return model::withDrumStep (clip, pad, step, intent.on); });
    return true;
}

bool IntentApplier::handle (const bridge::DrumsSetPattern& intent)
{
    std::array<std::uint16_t, model::DrumKit::numPads> pads {};
    for (std::size_t i = 0; i < pads.size() && i < intent.pads.size(); ++i)
        pads[i] = static_cast<std::uint16_t> (std::lround (intent.pads[i]));
    editDrumPattern (static_cast<std::uint64_t> (intent.clip), 0, true,
                     [&] (const model::Clip& clip) { return model::withDrumPattern (clip, pads); });
    return true;
}

bool IntentApplier::handle (const bridge::DrumsClear& intent)
{
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes, 0,
              [] (const model::Clip& clip)
              {
                  auto cleared = clip;
                  cleared.notes.clear();
                  return cleared;
              });
    return true;
}

// --- Audio files ------------------------------------------------------------------------------

bool IntentApplier::handle (const bridge::AssetRemove& intent)
{
    const model::AssetId id {static_cast<std::uint64_t> (intent.asset)};
    if (!editor.perform (model::RemoveAssets {{id}}) && resync.notice)
        resync.notice (1, "That audio is still in use.");
    return true;
}

bool IntentApplier::handle (const bridge::AssetRemoveUnused&)
{
    std::vector<model::AssetId> unused;
    for (const auto& asset : editor.project().assets)
        if (!editor.project().assetUse (asset.id).any())
            unused.push_back (asset.id);
    if (!unused.empty())
        editor.perform (model::RemoveAssets {std::move (unused)});
    return true;
}

// --- Tracks and buses ------------------------------------------------------------------------

bool IntentApplier::handle (const bridge::TrackAdd& intent)
{
    if (intent.kind == 0)
        editor.perform (model::AddTrack {model::TrackKind::audio});
    else
        editor.perform (model::AddTrack {static_cast<model::InstrumentKind> (intent.kind - 1)});
    return true;
}

bool IntentApplier::handle (const bridge::TrackRemove& intent)
{
    editor.perform (model::RemoveTrack {toTrack (intent.track)});
    return true;
}

bool IntentApplier::handle (const bridge::TrackRename& intent)
{
    if (!editor.perform (model::RenameTrack {toTrack (intent.track), intent.name}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::TrackSetVolume& intent)
{
    if (!editor.perform (model::SetTrackVolume {toTrack (intent.track), static_cast<float> (intent.volumeDb)},
                         toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::TrackSetPan& intent)
{
    if (!editor.perform (model::SetTrackPan {toTrack (intent.track), static_cast<float> (intent.pan)},
                         toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::TrackSetMute& intent)
{
    if (!editor.perform (model::SetTrackMute {toTrack (intent.track), intent.muted}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::TrackSetSolo& intent)
{
    if (!editor.perform (model::SetTrackSolo {toTrack (intent.track), intent.soloed}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::TrackSetEffect& intent)
{
    const auto effect = static_cast<params::EffectKind> (intent.effect);
    const auto& descriptor = params::effectDescriptors[static_cast<std::size_t> (intent.effect)];
    model::EffectState state;
    state.enabled = intent.enabled;
    if (intent.values.size() != descriptor.numParameters)
    {
        resyncTimeline();
        return true;
    }
    std::copy (intent.values.begin(), intent.values.end(), state.values.begin());
    // The model clamps and snaps the values (ADR-003); a rejected edit re-sends the real state.
    if (!editor.perform (model::SetTrackEffect {toTrack (intent.track), effect, state},
                         toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::TrackSetSend& intent)
{
    if (!editor.perform (model::SetTrackSend {toTrack (intent.track), toBus (intent.bus),
                                              static_cast<float> (intent.levelDb)},
                         toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::BusAdd&)
{
    if (!editor.perform (model::AddBus {}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::BusRemove& intent)
{
    if (!editor.perform (model::RemoveBus {toBus (intent.bus)}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::BusRename& intent)
{
    if (!editor.perform (model::RenameBus {toBus (intent.bus), intent.name}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::BusSetVolume& intent)
{
    if (!editor.perform (model::SetBusVolume {toBus (intent.bus), static_cast<float> (intent.volumeDb)},
                         toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::BusSetPan& intent)
{
    if (!editor.perform (model::SetBusPan {toBus (intent.bus), static_cast<float> (intent.pan)},
                         toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::BusSetMute& intent)
{
    if (!editor.perform (model::SetBusMute {toBus (intent.bus), intent.muted}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::BusSetEffect& intent)
{
    const auto effect = static_cast<params::EffectKind> (intent.effect);
    const auto& descriptor = params::effectDescriptors[static_cast<std::size_t> (intent.effect)];
    model::EffectState state;
    state.enabled = intent.enabled;
    if (intent.values.size() != descriptor.numParameters)
    {
        resyncTimeline();
        return true;
    }
    std::copy (intent.values.begin(), intent.values.end(), state.values.begin());
    if (!editor.perform (model::SetBusEffect {toBus (intent.bus), effect, state}, toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

// --- Clips -----------------------------------------------------------------------------------

bool IntentApplier::editClip (std::uint64_t clip, model::ClipEdit edit, std::uint64_t gesture,
                              const std::function<model::Clip (const model::Clip&)>& change)
{
    const auto& project = editor.project();
    const auto location = project.locate (model::ClipId {clip});
    if (location)
    {
        const auto& track = project.tracks[location->track];
        if (editor.perform (
                model::SetClip {model::ClipId {clip}, track.id, change (track.clips[location->clip]), edit},
                gesture))
            return true;
    }
    resyncTimeline();
    return false;
}

bool IntentApplier::handle (const bridge::ClipCreate& intent)
{
    const auto* track = editor.project().findTrack (toTrack (intent.track));
    if (track == nullptr || track->kind != model::TrackKind::instrument)
        return true;

    model::Clip clip;
    clip.start = intent.start;
    clip.length = intent.length;
    if (track->instrument == model::InstrumentKind::drums)
        clip.loopLength = model::DrumKit::patternLength;
    editor.perform (model::AddClip {track->id, std::move (clip)});
    return true;
}

bool IntentApplier::handle (const bridge::ClipMove& intent)
{
    const auto& project = editor.project();
    const auto id = toClip (intent.clip);
    const auto* clip = project.findClip (id);
    if (clip == nullptr
        || !editor.perform (model::SetClip {id, toTrack (intent.track), model::moveClip (*clip, intent.start),
                                            model::ClipEdit::move},
                            toGesture (intent.gesture)))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::ClipResize& intent)
{
    const auto tempo = editor.project().tempoBpm;
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::resize, toGesture (intent.gesture),
              [&] (const model::Clip& clip)
              {
                  if (intent.edge == 0)
                      return model::trimClipStart (clip, intent.ticks, tempo);

                  // An audio clip cannot outlast its file.
                  core::Ticks end = intent.ticks;
                  if (clip.asset.isValid() && loadedAudioSeconds)
                      if (const auto seconds = loadedAudioSeconds (clip.asset))
                      {
                          const double remaining = *seconds
                                                 - static_cast<double> (clip.sourceOffset)
                                                       / static_cast<double> (core::flicksPerSecond);
                          end = std::min (end, clip.start
                                                   + std::max (model::Clip::minLength,
                                                               secondsToTicks (remaining, tempo)));
                      }
                  return model::resizeClip (clip, end);
              });
    return true;
}

bool IntentApplier::handle (const bridge::ClipSplit& intent)
{
    if (!editor.perform (model::SplitClip {toClip (intent.clip), intent.ticks}))
        resyncTimeline();
    return true;
}

bool IntentApplier::handle (const bridge::ClipRemove& intent)
{
    editor.perform (model::RemoveClip {toClip (intent.clip)});
    return true;
}

bool IntentApplier::handle (const bridge::ClipDuplicate& intent)
{
    const auto& project = editor.project();
    const auto location = project.locate (toClip (intent.clip));
    if (!location)
        return true;
    const auto& track = project.tracks[location->track];
    auto copy = track.clips[location->clip];
    copy.start = copy.end();
    editor.perform (model::AddClip {track.id, std::move (copy)});
    return true;
}

bool IntentApplier::handle (const bridge::ClipSetLoop& intent)
{
    const auto* drums = editor.project().findInstrumentTrack (model::InstrumentKind::drums);
    const auto id = static_cast<std::uint64_t> (intent.clip);
    const bool onDrums = drums != nullptr
                      && std::any_of (drums->clips.begin(), drums->clips.end(),
                                      [id] (const model::Clip& c) { return c.id.value == id; });
    editClip (id, model::ClipEdit::loop, 0,
              [&] (const model::Clip& clip)
              {
                  auto looped = clip;
                  if (clip.asset.isValid())
                      return looped; // audio clips do not loop (rejected as a no-op)
                  looped.loopLength
                      = intent.enabled ? (onDrums ? model::DrumKit::patternLength : clip.length) : 0;
                  return looped;
              });
    return true;
}

bool IntentApplier::handle (const bridge::ClipAddNote& intent)
{
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes, 0,
              [&] (const model::Clip& clip)
              {
                  return model::withNote (clip, {intent.start, intent.length,
                                                 static_cast<std::uint8_t> (intent.pitch),
                                                 static_cast<float> (intent.velocity)});
              });
    return true;
}

bool IntentApplier::handle (const bridge::ClipRemoveNote& intent)
{
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes, 0,
              [&] (const model::Clip& clip)
              { return model::withoutNote (clip, intent.start, static_cast<std::uint8_t> (intent.pitch)); });
    return true;
}

bool IntentApplier::handle (const bridge::ClipEditNote& intent)
{
    editClip (
        static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes, toGesture (intent.gesture),
        [&] (const model::Clip& clip)
        {
            const auto* note
                = model::findNote (clip, intent.fromStart, static_cast<std::uint8_t> (intent.fromPitch));
            if (note == nullptr)
                return clip;
            const auto velocity = note->velocity;
            return model::withNote (
                model::withoutNote (clip, intent.fromStart, static_cast<std::uint8_t> (intent.fromPitch)),
                {intent.start, intent.length, static_cast<std::uint8_t> (intent.pitch), velocity});
        });
    return true;
}

} // namespace ap::host
