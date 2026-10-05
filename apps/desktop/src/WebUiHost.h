#pragma once

#include "AudioDeviceHost.h"
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
    WebUiHost (AudioDeviceHost& host, engine::Engine& engine);
    ~WebUiHost() override;

    void resized() override;

private:
    class LockedDownWebView;

    void handleIntent (const juce::var& message);
    void handle (const ap::bridge::AppReady&);
    void handle (const ap::bridge::AudioOpenSettings&);
    void handle (const ap::bridge::ToneSetEnabled&);
    void handle (const ap::bridge::ToneSetLevel&);

    void emit (const ap::bridge::Event& event);
    void sendStatus();
    void timerCallback() override;
    void showAudioSettings();

    AudioDeviceHost& host;
    engine::Engine& engine;

    std::unique_ptr<LockedDownWebView> webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUiHost)
};

} // namespace ap::desktop
