#pragma once

#include "AccompanimentService.h"
#include "AudioDeviceHost.h"
#include "AudioRecorder.h"
#include "Exporter.h"
#include "NoteCapture.h"
#include "NoteRecorder.h"
#include "ProjectActions.h"
#include "SampleLoader.h"
#include "Session.h"
#include "ap/bridge/generated/Messages.h"
#include "ap/engine/Engine.h"
#include "ap/host/IntentApplier.h"
#include "ap/host/Snapshots.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#if !AP_HEADLESS_UI
#include <juce_gui_extra/juce_gui_extra.h>
#endif
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

#if AP_HEADLESS_UI
    // Integration tests build this class without a web view (AP_HEADLESS_UI): they feed intents
    // exactly as the page would send them and read the events the host would have emitted.
    void receiveIntent (const juce::var& message) { handleIntent (message); }
    std::function<void (const juce::var&)> eventSink;
    AccompanimentService& accompanimentForTests() noexcept { return accompaniment; }
#endif

private:
    class LockedDownWebView;

    void handleIntent (const juce::var& message);
    // Intents that only edit the project or drive the engine are handled by host::IntentApplier,
    // shared with the web host. What is left here needs this platform: devices, files, recording.
    template <typename T> void handle (const T&) {}
    void handle (const ap::bridge::AppReady&);
    void handle (const ap::bridge::AudioOpenSettings&);
    void handle (const ap::bridge::AudioSetOutput&);
    void handle (const ap::bridge::AudioSetInput&);
    void handle (const ap::bridge::AudioSetSampleRate&);
    void handle (const ap::bridge::AudioSetBufferSize&);
    void handle (const ap::bridge::ProjectNew&);
    void handle (const ap::bridge::ProjectOpen&);
    void handle (const ap::bridge::ProjectSave&);
    void handle (const ap::bridge::ProjectSaveAs&);
    void handle (const ap::bridge::ProjectExport&);
    void handle (const ap::bridge::ProjectCancelExport&);
    void sendExportState();
    void exportFinished (const Exporter::Result& result);
    void handle (const ap::bridge::SamplerLoad&);
    void handle (const ap::bridge::DrumsLoadPad&);
    void handle (const ap::bridge::TransportRecord&);
    void handle (const ap::bridge::TransportCapture&);
    void handle (const ap::bridge::TrackSetArmed&);
    void handle (const ap::bridge::TrackImportAudio&);
    void handle (const ap::bridge::AssetLocate&);
    void handle (const ap::bridge::AssetRemove&);
    void handle (const ap::bridge::AssetRemoveUnused&);
    void handle (const ap::bridge::AccompanimentSuggest&);
    void handle (const ap::bridge::AccompanimentPreview&);
    void handle (const ap::bridge::AccompanimentStopPreview&);
    void handle (const ap::bridge::AccompanimentAdd&);
    void handle (const ap::bridge::AccompanimentNext&);
    void handle (const ap::bridge::AccompanimentDismiss&);

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
    // Every audio file of the project and what uses it. recheckFiles stats each file again.
    void sendProjectAssets (bool recheckFiles = false);
    void sendTimelinePeaks (bool all);
    void sendAccompaniment();

    void emit (const ap::bridge::Event& event);
    void sendStatus();
    void sendAudioDevices();
    // Runs a device change unless a take is being recorded; shows the error, if any.
    void changeDevice (const std::function<juce::String()>& change);
    void sendTransportState();
    void sendParameter (params::ParamId id);
    void sendHistory();
    void sendProjectState();
    void sendInstrumentState();
    void sendSamplerState();
    void sendDrumPad (std::size_t pad);
    void sendDrumKit();
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
    host::IntentApplier applier;
    Exporter exporter;
    AccompanimentService accompaniment;
    bool lastExporting = false;
    std::unique_ptr<LockedDownWebView> webView;
    ap::bridge::TransportPosition lastPosition;
    bool lastPlaying = false;
    bool lastRecording = false;
    bool lastCaptureAvailable = false;
    std::map<std::uint64_t, bool> assetMissing; // asset -> file not found, as last checked
    std::optional<ap::bridge::ProjectAssets> sentProjectAssets;
    std::map<std::uint64_t, std::uint64_t> sentPeaks; // asset -> load generation already sent

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUiHost)
};

} // namespace ap::desktop
