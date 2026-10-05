#pragma once

#include "AudioDeviceHost.h"
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
class WebUiHost final : public juce::Component, private juce::Timer
{
public:
    WebUiHost (AudioDeviceHost& host, engine::Engine& engine, Session& session);
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
    void onProjectChanged();
    void sendTransportPosition (bool force);
    void timerCallback() override;
    void showAudioSettings();

    AudioDeviceHost& host;
    engine::Engine& engine;
    Session& session;

    std::unique_ptr<LockedDownWebView> webView;
    ap::bridge::TransportPosition lastPosition;
    bool lastPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUiHost)
};

} // namespace ap::desktop
