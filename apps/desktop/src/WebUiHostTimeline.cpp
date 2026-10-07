// What the timeline needs from this platform: the recording and accompaniment intents, the asset
// events. The track, bus and clip edits themselves are host::IntentApplier (shared with the web).

#include "WebUiHost.h"
#include "ap/dsp/Peaks.h"
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
} // namespace

void WebUiHost::stopTransport()
{
    accompaniment.stopPreview(); // a beat being previewed ends with the music
    const auto position = engine.getTransport().getState().positionTicks;
    engine.getTransport().requestStop();
    stopNoteRecording (position);
    audioRecorder.stop();
    sendTransportState();
}

void WebUiHost::pumpPlayedNotes()
{
    while (const auto note = engine.popPlayedNote())
    {
        recorder.handle (*note);
        capture.handle (*note);
    }
}

void WebUiHost::stopNoteRecording (core::Ticks position)
{
    if (!recorder.isRecording())
        return;
    pumpPlayedNotes();
    if (recorder.stop (position))
        capture.clear(); // already on the timeline: capturing them again would duplicate them
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

void WebUiHost::handle (const ap::bridge::TransportCapture&)
{
    pumpPlayedNotes();
    if (!capture.capture (engine.getHeardClock(), engine.getTransport().getState().positionTicks,
                          engine.getSampleRate()))
        showNotice (ProjectActions::NoticeLevel::info, "Play something first, then capture it.");
    sendTransportState();
}

// --- Tracks ----------------------------------------------------------------------------------

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

void WebUiHost::handle (const ap::bridge::AssetLocate& intent)
{
    samples.chooseAndRelink (model::AssetId {static_cast<std::uint64_t> (intent.asset)});
}

void WebUiHost::handle (const ap::bridge::AccompanimentSuggest& intent)
{
    accompaniment.suggestFor (model::ClipId {static_cast<std::uint64_t> (intent.clip)});
}

void WebUiHost::handle (const ap::bridge::AccompanimentPreview&)
{
    accompaniment.preview();
    sendTransportState();
}

void WebUiHost::handle (const ap::bridge::AccompanimentStopPreview&)
{
    accompaniment.stopPreview();
    sendTransportState();
}

void WebUiHost::handle (const ap::bridge::AccompanimentAdd&)
{
    accompaniment.add();
}

void WebUiHost::handle (const ap::bridge::AccompanimentNext&)
{
    accompaniment.next();
}

void WebUiHost::handle (const ap::bridge::AccompanimentDismiss&)
{
    accompaniment.dismiss();
    sendTransportState();
}

void WebUiHost::sendAccompaniment()
{
    const auto& s = accompaniment.snapshot();
    ap::bridge::AccompanimentState event;
    event.state = static_cast<int> (s.state);
    event.reason = static_cast<int> (s.reason);
    event.message = s.message.substr (0, 280);
    event.grooveName = s.grooveName.substr (0, 90);
    event.detail = s.detail.substr (0, 280);
    event.clip = toInt (s.clip.value);
    event.canTryAnother = s.canTryAnother;
    emit (event);
}

void WebUiHost::sendProjectAssets (bool recheckFiles)
{
    if (recheckFiles)
        assetMissing.clear();

    std::map<std::uint64_t, bool> missing;
    const auto event = host::projectAssets (session.project(),
                                            [&] (model::AssetId id)
                                            {
                                                const auto known = assetMissing.find (id.value);
                                                const bool isMissing = known != assetMissing.end()
                                                                         ? known->second
                                                                         : !samples.assetFileExists (id);
                                                missing[id.value] = isMissing;
                                                return isMissing;
                                            });
    assetMissing = std::move (missing);

    if (sentProjectAssets && *sentProjectAssets == event)
        return;
    sentProjectAssets = event;
    emit (event);
}

// --- Events ----------------------------------------------------------------------------------

void WebUiHost::sendTimeline()
{
    emit (host::timelineState (session.project()));
}

void WebUiHost::sendTimelineAssets()
{
    ap::bridge::TimelineAssets event;
    for (const auto& [id, audio] : samples.getClipAudio())
        event.assets.push_back (
            host::timelineAsset (session.project(), model::AssetId {id},
                                 {audio.state.name, audio.state.loaded, audio.state.missing,
                                  audio.state.loading, audio.state.durationSeconds, audio.state.overview}));
    emit (event);
}

void WebUiHost::sendTimelinePeaks (bool all)
{
    // Detailed peaks are large (up to ~170 kB per asset), so each load is sent once, separately
    // from the frequent asset updates.
    if (all)
        sentPeaks.clear();
    const auto& audio = samples.getClipAudio();
    std::erase_if (sentPeaks, [&audio] (const auto& sent) { return audio.count (sent.first) == 0; });

    for (const auto& [id, clipAudio] : audio)
    {
        if (!clipAudio.state.loaded)
            continue;
        if (const auto it = sentPeaks.find (id); it != sentPeaks.end() && it->second == clipAudio.generation)
            continue;
        if (const auto event = host::timelinePeaks (model::AssetId {id}, clipAudio.state.peaks))
        {
            sentPeaks[id] = clipAudio.generation;
            emit (*event);
        }
    }
}

} // namespace ap::desktop
