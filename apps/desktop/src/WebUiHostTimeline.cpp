// Timeline, track and clip intents (ADR-007). Every edit goes through the Session (ADR-003); a
// rejected or no-op edit re-sends the timeline so the UI never keeps an optimistic state.

#include "WebUiHost.h"
#include "ap/model/ClipEditing.h"

#include <cmath>

namespace ap::desktop
{
namespace
{
model::TrackId toTrack (int id)
{
    return model::TrackId {static_cast<std::uint64_t> (id)};
}

int toInt (std::uint64_t id)
{
    return static_cast<int> (
        std::min<std::uint64_t> (id, static_cast<std::uint64_t> (std::numeric_limits<int>::max())));
}

int toInt (core::Ticks ticks)
{
    return static_cast<int> (std::clamp<core::Ticks> (ticks, 0, model::maxTimelineTicks));
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

bool WebUiHost::editClip (std::uint64_t clip, model::ClipEdit edit, std::uint64_t gesture,
                          const std::function<model::Clip (const model::Clip&)>& change)
{
    const auto& project = session.project();
    const auto location = project.locate (model::ClipId {clip});
    if (location)
    {
        const auto& track = project.tracks[location->track];
        if (session.perform (
                model::SetClip {model::ClipId {clip}, track.id, change (track.clips[location->clip]), edit},
                gesture))
            return true;
    }
    sendTimeline();
    return false;
}

void WebUiHost::stopTransport()
{
    const auto position = engine.getTransport().getState().positionTicks;
    engine.getTransport().requestStop();
    if (recorder.isRecording())
        recorder.stop (position);
    audioRecorder.stop();
    sendTransportState();
}

void WebUiHost::disarm()
{
    audioRecorder.stop();
    audioRecorder.setArmedTrack ({});
    if (const auto error = host.setInputEnabled (false); error.isNotEmpty())
        showNotice (ProjectActions::NoticeLevel::warning, error.toStdString());
    sendTransportState();
}

// --- Transport -------------------------------------------------------------------------------

void WebUiHost::handle (const ap::bridge::TransportRecord&)
{
    if (isRecording())
    {
        stopTransport();
        return;
    }

    // With an armed audio track, the input is recorded too (notes are always recorded).
    if (audioRecorder.getArmedTrack().isValid())
        if (const auto error = audioRecorder.start (host.getStatus().roundTripLatencySamples))
        {
            showNotice (ProjectActions::NoticeLevel::warning, *error);
            sendTransportState();
            return;
        }
    recorder.start();
    engine.getTransport().requestPlay();
    sendTransportState();
}

void WebUiHost::handle (const ap::bridge::TransportSeek& intent)
{
    engine.getTransport().requestSeek (intent.ticks);
}

void WebUiHost::handle (const ap::bridge::TransportSetLoop& intent)
{
    const bool valid = intent.end > intent.start;
    engine.getTransport().setLoop (intent.enabled && valid, intent.start, valid ? intent.end : intent.start);
    sendTransportState();
}

// --- Tracks ----------------------------------------------------------------------------------

void WebUiHost::handle (const ap::bridge::TrackAdd& intent)
{
    if (intent.kind == 0)
        session.perform (model::AddTrack {model::TrackKind::audio});
    else
        session.perform (model::AddTrack {static_cast<model::InstrumentKind> (intent.kind - 1)});
}

void WebUiHost::handle (const ap::bridge::TrackRemove& intent)
{
    session.perform (model::RemoveTrack {toTrack (intent.track)});
}

void WebUiHost::handle (const ap::bridge::TrackRename& intent)
{
    if (!session.perform (model::RenameTrack {toTrack (intent.track), intent.name}))
        sendTimeline();
}

void WebUiHost::handle (const ap::bridge::TrackSetVolume& intent)
{
    if (!session.perform (
            model::SetTrackVolume {toTrack (intent.track), static_cast<float> (intent.volumeDb)},
            toGesture (intent.gesture)))
        sendTimeline();
}

void WebUiHost::handle (const ap::bridge::TrackSetPan& intent)
{
    if (!session.perform (model::SetTrackPan {toTrack (intent.track), static_cast<float> (intent.pan)},
                          toGesture (intent.gesture)))
        sendTimeline();
}

void WebUiHost::handle (const ap::bridge::TrackSetMute& intent)
{
    if (!session.perform (model::SetTrackMute {toTrack (intent.track), intent.muted}))
        sendTimeline();
}

void WebUiHost::handle (const ap::bridge::TrackSetSolo& intent)
{
    if (!session.perform (model::SetTrackSolo {toTrack (intent.track), intent.soloed}))
        sendTimeline();
}

void WebUiHost::handle (const ap::bridge::TrackSetArmed& intent)
{
    const auto* track = session.project().findTrack (toTrack (intent.track));
    if (!intent.armed)
    {
        if (audioRecorder.getArmedTrack() == toTrack (intent.track))
            disarm();
        else
            sendTransportState();
        return;
    }
    if (track == nullptr || track->kind != model::TrackKind::audio || isRecording())
    {
        sendTransportState();
        return;
    }

    // One armed track at a time. Opening the input restarts the device (and asks for the
    // microphone permission the first time).
    if (const auto error = host.setInputEnabled (true); error.isNotEmpty())
    {
        showNotice (ProjectActions::NoticeLevel::error, error.toStdString());
        sendTransportState();
        return;
    }
    audioRecorder.setArmedTrack (track->id);
    sendTransportState();
}

void WebUiHost::handle (const ap::bridge::TrackImportAudio& intent)
{
    const auto* track = session.project().findTrack (toTrack (intent.track));
    if (track != nullptr && track->kind == model::TrackKind::audio)
        samples.chooseAndImportClip (track->id, intent.ticks);
}

// --- Clips -----------------------------------------------------------------------------------

void WebUiHost::handle (const ap::bridge::ClipCreate& intent)
{
    const auto* track = session.project().findTrack (toTrack (intent.track));
    if (track == nullptr || track->kind != model::TrackKind::instrument)
        return;

    model::Clip clip;
    clip.start = intent.start;
    clip.length = intent.length;
    if (track->instrument == model::InstrumentKind::drums)
        clip.loopLength = model::DrumKit::patternLength;
    session.perform (model::AddClip {track->id, std::move (clip)});
}

void WebUiHost::handle (const ap::bridge::ClipMove& intent)
{
    const auto& project = session.project();
    const auto id = model::ClipId {static_cast<std::uint64_t> (intent.clip)};
    const auto* clip = project.findClip (id);
    if (clip == nullptr
        || !session.perform (model::SetClip {id, toTrack (intent.track),
                                             model::moveClip (*clip, intent.start), model::ClipEdit::move},
                             toGesture (intent.gesture)))
        sendTimeline();
}

void WebUiHost::handle (const ap::bridge::ClipResize& intent)
{
    const auto tempo = session.project().tempoBpm;
    const auto& audio = samples.getClipAudio();
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::resize, toGesture (intent.gesture),
              [&] (const model::Clip& clip)
              {
                  if (intent.edge == 0)
                      return model::trimClipStart (clip, intent.ticks, tempo);

                  // An audio clip cannot outlast its file.
                  core::Ticks end = intent.ticks;
                  if (const auto it = audio.find (clip.asset.value);
                      clip.asset.isValid() && it != audio.end() && it->second.state.loaded)
                  {
                      const double remaining = it->second.state.durationSeconds
                                             - static_cast<double> (clip.sourceOffset)
                                                   / static_cast<double> (core::flicksPerSecond);
                      end = std::min (
                          end,
                          clip.start + std::max (model::Clip::minLength, secondsToTicks (remaining, tempo)));
                  }
                  return model::resizeClip (clip, end);
              });
}

void WebUiHost::handle (const ap::bridge::ClipSplit& intent)
{
    if (!session.perform (
            model::SplitClip {model::ClipId {static_cast<std::uint64_t> (intent.clip)}, intent.ticks}))
        sendTimeline();
}

void WebUiHost::handle (const ap::bridge::ClipRemove& intent)
{
    session.perform (model::RemoveClip {model::ClipId {static_cast<std::uint64_t> (intent.clip)}});
}

void WebUiHost::handle (const ap::bridge::ClipDuplicate& intent)
{
    const auto& project = session.project();
    const auto location = project.locate (model::ClipId {static_cast<std::uint64_t> (intent.clip)});
    if (!location)
        return;
    const auto& track = project.tracks[location->track];
    auto copy = track.clips[location->clip];
    copy.start = copy.end();
    session.perform (model::AddClip {track.id, std::move (copy)});
}

void WebUiHost::handle (const ap::bridge::ClipSetLoop& intent)
{
    const auto* drums = session.project().findInstrumentTrack (model::InstrumentKind::drums);
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
}

void WebUiHost::handle (const ap::bridge::ClipAddNote& intent)
{
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes, 0,
              [&] (const model::Clip& clip)
              {
                  return model::withNote (clip, {intent.start, intent.length,
                                                 static_cast<std::uint8_t> (intent.pitch),
                                                 static_cast<float> (intent.velocity)});
              });
}

void WebUiHost::handle (const ap::bridge::ClipRemoveNote& intent)
{
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes, 0,
              [&] (const model::Clip& clip)
              { return model::withoutNote (clip, intent.start, static_cast<std::uint8_t> (intent.pitch)); });
}

void WebUiHost::handle (const ap::bridge::ClipEditNote& intent)
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
}

// --- Drum patterns ---------------------------------------------------------------------------

void WebUiHost::handle (const ap::bridge::DrumsSetStep& intent)
{
    const auto pad = static_cast<std::size_t> (intent.pad);
    const auto step = static_cast<std::size_t> (intent.step);

    if (intent.clip != 0)
    {
        editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes,
                  toGesture (intent.gesture),
                  [&] (const model::Clip& clip) { return model::withDrumStep (clip, pad, step, intent.on); });
        return;
    }

    // No clip given: the pattern at the playhead's bar, created (with the drum track if needed)
    // when there is none. A stroke that creates it stays one undo step.
    const auto& project = session.project();
    const auto bar = project.timeSignature.ticksPerBar();
    const auto barStart
        = std::max<core::Ticks> (0, engine.getTransport().getState().positionTicks) / bar * bar;
    if (const auto* drums = project.findInstrumentTrack (model::InstrumentKind::drums))
        for (const auto& clip : drums->clips)
            if (clip.start <= barStart && barStart < clip.end())
            {
                editClip (clip.id.value, model::ClipEdit::notes, toGesture (intent.gesture),
                          [&] (const model::Clip& c)
                          { return model::withDrumStep (c, pad, step, intent.on); });
                return;
            }

    if (!intent.on)
        return;

    auto pattern = model::withDrumStep (model::makePatternClip (barStart, 4 * bar), pad, step, true);
    session.performGroup (
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
        toGesture (intent.gesture));
}

void WebUiHost::handle (const ap::bridge::DrumsClear& intent)
{
    editClip (static_cast<std::uint64_t> (intent.clip), model::ClipEdit::notes, 0,
              [] (const model::Clip& clip)
              {
                  auto cleared = clip;
                  cleared.notes.clear();
                  return cleared;
              });
}

// --- Events ----------------------------------------------------------------------------------

void WebUiHost::sendTimeline()
{
    using ap::bridge::TimelineClip;
    using ap::bridge::TimelineTrack;

    ap::bridge::TimelineState event;
    for (const auto& track : session.project().tracks)
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
        event.tracks.push_back (std::move (t));
    }
    emit (event);
}

void WebUiHost::sendTimelineAssets()
{
    using ap::bridge::TimelineAsset;

    ap::bridge::TimelineAssets event;
    for (const auto& [id, audio] : samples.getClipAudio())
    {
        const auto* asset = session.project().findAsset (model::AssetId {id});
        TimelineAsset a;
        a.id = toInt (id);
        a.name = model::sanitiseName (asset != nullptr ? asset->name : audio.state.name,
                                      TimelineAsset::nameMaxLength)
                     .value_or ("Audio");
        a.loaded = audio.state.loaded;
        a.missing = audio.state.missing;
        a.loading = audio.state.loading;
        a.durationSeconds = std::clamp (audio.state.durationSeconds, 0.0, TimelineAsset::durationSecondsMax);
        a.overview = audio.state.overview;
        event.assets.push_back (std::move (a));
    }
    emit (event);
}

} // namespace ap::desktop
