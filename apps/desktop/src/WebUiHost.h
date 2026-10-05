#pragma once

#include "AudioDeviceHost.h"
#include "ProjectActions.h"
#include "SampleLoader.h"
#include "Session.h"
#include "ap/bridge/generated/Messages.h"
#include "ap/engine/Engine.h"

#include <juce_gui_extra/juce_gui_extra.h>
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
               SampleLoader& samples);

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
    void handle (const ap::bridge::TransportPlay&);
    void handle (const ap::bridge::TransportStop&);
    void handle (const ap::bridge::TransportReturnToStart&);
    void handle (const ap::bridge::TransportSetTempo&);
    void handle (const ap::bridge::TransportSetCountIn&);
    void handle (const ap::bridge::MetronomeSetEnabled&);

    void emit (const ap::bridge::Event& event);
    void sendStatus();
    void sendTransportState();
    void sendParameter (params::ParamId id);
    void sendHistory();
    void sendProjectState();
    void sendInstrumentState();
    void sendSamplerState();
    void onProjectChanged();
    void sendTransportPosition (bool force);
    void timerCallback() override;
    void showAudioSettings();

    AudioDeviceHost& host;
    engine::Engine& engine;
    Session& session;
    ProjectActions& actions;
    SampleLoader& samples;

    std::unique_ptr<LockedDownWebView> webView;
    ap::bridge::TransportPosition lastPosition;
    bool lastPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUiHost)
};

} // namespace ap::desktop
