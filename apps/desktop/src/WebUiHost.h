#pragma once

#include "AudioDeviceHost.h"
#include "AudioRecorder.h"
#include "NoteCapture.h"
#include "NoteRecorder.h"
#include "ProjectActions.h"
#include "SampleLoader.h"
#include "Session.h"
#include "ap/bridge/generated/Messages.h"
#include "ap/engine/Engine.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include <memory>

namespace ap::desktop
{

// Hosts the React UI (ADR-002) and translates bridge messages (ADR-003).
//
// Security model:
// - The UI is served only from the embedded bundle through the resource provider.
// - Navigation anywhere else is refused; new windows are refused.
// - The only channel from the page is the "ap.intent" event, strictly validated by the
//   generated codec. Malformed messages are dropped.
class WebUiHost final
    : public juce::Component
    , private juce::Timer
{
public:
    WebUiHost (AudioDeviceHost& host, engine::Engine& engine, Session& session, ProjectActions& actions,
               SampleLoader& samples, AudioRecorder& audioRecorder);

    // Shows a transient message in the UI.
    void showNotice (ProjectActions::NoticeLevel level, const std::string& message);
    ~WebUiHost() override;

    void resized() override;

private:
    class LockedDownWebView;

    void handleIntent (const juce::var& message);
    void handle (const ap::bridge::AppReady&);
    void handle (const ap::bridge::AudioOpenSettings&);
    void handle (const ap::bridge::ToneSetEnabled&);
    void handle (const ap::bridge::ParamSet&);
    void handle (const ap::bridge::EditUndo&);
    void handle (const ap::bridge::EditRedo&);
    void handle (const ap::bridge::ProjectNew&);
    void handle (const ap::bridge::ProjectOpen&);
    void handle (const ap::bridge::ProjectSave&);
    void handle (const ap::bridge::ProjectSaveAs&);
    void handle (const ap::bridge::ProjectRename&);
    void handle (const ap::bridge::NoteOn&);
    void handle (const ap::bridge::NoteOff&);
    void handle (const ap::bridge::NoteAllOff&);
    void handle (const ap::bridge::InstrumentSelect&);
    void handle (const ap::bridge::SamplerLoad&);
    void handle (const ap::bridge::DrumsSetStep&);
    void handle (const ap::bridge::DrumsClear&);
    void handle (const ap::bridge::DrumsTrigger&);
    void handle (const ap::bridge::DrumsSetPad&);
    void handle (const ap::bridge::DrumsLoadPad&);
    void handle (const ap::bridge::DrumsResetPad&);
    void handle (const ap::bridge::TransportPlay&);
    void handle (const ap::bridge::TransportStop&);
    void handle (const ap::bridge::TransportReturnToStart&);
    void handle (const ap::bridge::TransportSetTempo&);
    void handle (const ap::bridge::TransportSetCountIn&);
    void handle (const ap::bridge::MetronomeSetEnabled&);
    void handle (const ap::bridge::TransportRecord&);
    void handle (const ap::bridge::TransportCapture&);
    void handle (const ap::bridge::TransportSeek&);
    void handle (const ap::bridge::TransportSetLoop&);
    void handle (const ap::bridge::TrackAdd&);
    void handle (const ap::bridge::TrackRemove&);
    void handle (const ap::bridge::TrackRename&);
    void handle (const ap::bridge::TrackSetVolume&);
    void handle (const ap::bridge::TrackSetPan&);
    void handle (const ap::bridge::TrackSetMute&);
    void handle (const ap::bridge::TrackSetSolo&);
    void handle (const ap::bridge::TrackSetArmed&);
    void handle (const ap::bridge::TrackImportAudio&);
    void handle (const ap::bridge::ClipCreate&);
    void handle (const ap::bridge::ClipMove&);
    void handle (const ap::bridge::ClipResize&);
    void handle (const ap::bridge::ClipSplit&);
    void handle (const ap::bridge::ClipRemove&);
    void handle (const ap::bridge::ClipDuplicate&);
    void handle (const ap::bridge::ClipSetLoop&);
    void handle (const ap::bridge::ClipAddNote&);
    void handle (const ap::bridge::ClipRemoveNote&);
    void handle (const ap::bridge::ClipEditNote&);

    // Timeline helpers (WebUiHostTimeline.cpp).
    bool editClip (std::uint64_t clip, model::ClipEdit edit, std::uint64_t gesture,
                   const std::function<model::Clip (const model::Clip&)>& change);
    void stopTransport();
    void stopNoteRecording (core::Ticks position);
    void pumpPlayedNotes();
    void disarm();
    [[nodiscard]] bool isRecording() const noexcept
    {
        return recorder.isRecording() || audioRecorder.isRecording();
    }
    void sendTimeline();
    void sendTimelineAssets();
    void sendTimelinePeaks (bool all);

    void emit (const ap::bridge::Event& event);
    void sendStatus();
    void sendTransportState();
    void sendParameter (params::ParamId id);
    void sendHistory();
    void sendProjectState();
    void sendInstrumentState();
    void sendSamplerState();
    void sendDrumPad (std::size_t pad);
    void onProjectChanged();
    void sendTransportPosition (bool force);
    void timerCallback() override;
    void showAudioSettings();

    AudioDeviceHost& host;
    engine::Engine& engine;
    Session& session;
    ProjectActions& actions;
    SampleLoader& samples;
    AudioRecorder& audioRecorder;

    NoteRecorder recorder;
    NoteCapture capture;
    std::unique_ptr<LockedDownWebView> webView;
    ap::bridge::TransportPosition lastPosition;
    bool lastPlaying = false;
    bool lastRecording = false;
    bool lastCaptureAvailable = false;
    std::map<std::uint64_t, std::uint64_t> sentPeaks; // asset -> load generation already sent

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUiHost)
};

} // namespace ap::desktop
