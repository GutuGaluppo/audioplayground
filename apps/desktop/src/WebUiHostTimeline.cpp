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

void WebUiHost::handle (const ap::bridge::AssetRemove& intent)
{
    const model::AssetId id {static_cast<std::uint64_t> (intent.asset)};
    if (!session.perform (model::RemoveAssets {{id}}))
        showNotice (ProjectActions::NoticeLevel::warning, "That audio is still in use.");
}

void WebUiHost::handle (const ap::bridge::AssetRemoveUnused&)
{
    std::vector<model::AssetId> unused;
    for (const auto& asset : session.project().assets)
        if (!session.project().assetUse (asset.id).any())
            unused.push_back (asset.id);
    if (!unused.empty())
        session.perform (model::RemoveAssets {std::move (unused)});
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
    using ap::bridge::ProjectAsset;

    const auto& project = session.project();
    if (recheckFiles)
        assetMissing.clear();

    ap::bridge::ProjectAssets event;
    std::map<std::uint64_t, bool> missing;
    for (const auto& asset : project.assets)
    {
        const auto known = assetMissing.find (asset.id.value);
        const bool isMissing
            = known != assetMissing.end() ? known->second : !samples.assetFileExists (asset.id);
        missing[asset.id.value] = isMissing;

        const auto use = project.assetUse (asset.id);
        ProjectAsset a;
        a.id = toInt (asset.id.value);
        a.name = model::sanitiseName (asset.name, ProjectAsset::nameMaxLength).value_or ("Audio");
        a.clips = static_cast<int> (std::min<std::size_t> (use.clips, ProjectAsset::clipsMax));
        a.pads = static_cast<int> (std::min<std::size_t> (use.pads, ProjectAsset::padsMax));
        a.sampler = use.sampler;
        a.missing = isMissing;
        event.assets.push_back (std::move (a));
    }
    assetMissing = std::move (missing);

    if (sentProjectAssets && *sentProjectAssets == event)
        return;
    sentProjectAssets = event;
    emit (event);
}

// --- Clips -----------------------------------------------------------------------------------

// --- Drum patterns ---------------------------------------------------------------------------

// --- Events ----------------------------------------------------------------------------------

void WebUiHost::sendTimeline()
{
    emit (host::timelineState (session.project()));
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
        const auto& peaks = clipAudio.state.peaks;
        if (!clipAudio.state.loaded || peaks.empty())
            continue;
        if (const auto it = sentPeaks.find (id); it != sentPeaks.end() && it->second == clipAudio.generation)
            continue;
        sentPeaks[id] = clipAudio.generation;

        ap::bridge::TimelinePeaks event;
        event.asset = toInt (id);
        event.peaksPerSecond = dsp::peaksPerSecond;
        event.data = juce::Base64::toBase64 (peaks.data(), peaks.size()).toStdString();
        if (event.data.size() <= ap::bridge::TimelinePeaks::dataMaxLength)
            emit (event);
    }
}

} // namespace ap::desktop
