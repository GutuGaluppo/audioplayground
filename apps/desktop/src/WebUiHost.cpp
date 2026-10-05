#include "WebUiHost.h"

#include "EmbeddedResources.h"
#include "generated/BridgeCodec.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace ap::desktop
{
namespace
{
const juce::Colour background{0xff111214};
constexpr int meterRefreshHz = 30;
constexpr auto intentEventId = "ap.intent";
constexpr auto nativeEventId = "ap.event";

juce::String mimeTypeFor (const juce::String& path)
{
    const auto extension = path.fromLastOccurrenceOf (".", false, false).toLowerCase();

    if (extension == "html")
        return "text/html";
    if (extension == "js" || extension == "mjs")
        return "text/javascript";
    if (extension == "css")
        return "text/css";
    if (extension == "json")
        return "application/json";
    if (extension == "svg")
        return "image/svg+xml";
    if (extension == "png")
        return "image/png";
    if (extension == "woff2")
        return "font/woff2";
    if (extension == "wasm")
        return "application/wasm";

    return "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource> serveEmbedded (const juce::String& requestPath)
{
    // Requests arrive as "/path?query". Only exact matches in the embedded table are served,
    // so traversal sequences simply never match.
    auto path
        = requestPath.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    path = path.trimCharactersAtStart ("/");
    if (path.isEmpty())
        path = "index.html";

    for (const auto& file : ui::embeddedFiles())
    {
        if (path == file.path)
        {
            juce::WebBrowserComponent::Resource resource;
            const auto* bytes = reinterpret_cast<const std::byte*> (file.data);
            resource.data.assign (bytes, bytes + file.size);
            resource.mimeType = mimeTypeFor (path);
            return resource;
        }
    }

    return std::nullopt;
}

// Debug builds only: AP_UI_DEV_URL=http://localhost:5173 loads the Vite dev server (hot reload).
juce::String developmentServerUrl()
{
#if JUCE_DEBUG
    const auto url = juce::SystemStats::getEnvironmentVariable ("AP_UI_DEV_URL", {});
    if (url.startsWith ("http://localhost:") || url.startsWith ("http://127.0.0.1:"))
        return url;
#endif
    return {};
}

juce::File webViewDataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Audio Playground")
        .getChildFile ("WebView");
}
} // namespace

class WebUiHost::LockedDownWebView final : public juce::WebBrowserComponent
{
public:
    LockedDownWebView (const Options& options, juce::String devUrl)
        : WebBrowserComponent (options), developmentUrl (std::move (devUrl))
    {
    }

    bool pageAboutToLoad (const juce::String& url) override
    {
        if (url.startsWith (getResourceProviderRoot()))
            return true;

        if (developmentUrl.isNotEmpty() && url.startsWith (developmentUrl))
            return true;

        DBG ("Blocked navigation outside the app UI");
        return false;
    }

    void newWindowAttemptingToLoad (const juce::String&) override
    {
        DBG ("Blocked new window from the app UI");
    }

private:
    juce::String developmentUrl;
};

WebUiHost::WebUiHost (AudioDeviceHost& hostToUse, engine::Engine& engineToUse)
    : host (hostToUse), engine (engineToUse)
{
    const auto devUrl = developmentServerUrl();

    auto options = juce::WebBrowserComponent::Options{}
                       .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                       .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                                    .withUserDataFolder (webViewDataFolder())
                                                    .withStatusBarDisabled()
                                                    .withBuiltInErrorPageDisabled()
                                                    .withBackgroundColour (background))
                       .withNativeIntegrationEnabled()
                       .withKeepPageLoadedWhenBrowserIsHidden()
                       .withInitialisationData ("apProtocolVersion", ap::bridge::protocolVersion)
                       .withEventListener (intentEventId,
                                           [this] (const juce::var& message) { handleIntent (message); })
                       .withResourceProvider ([] (const juce::String& path) { return serveEmbedded (path); },
                                              devUrl.isNotEmpty() ? std::optional<juce::String> (devUrl)
                                                                  : std::nullopt);

    webView = std::make_unique<LockedDownWebView> (options, devUrl);
    addAndMakeVisible (*webView);
    webView->goToURL (devUrl.isNotEmpty() ? devUrl : juce::WebBrowserComponent::getResourceProviderRoot());

    host.onStatusChanged = [this] { sendStatus(); };
    setSize (1100, 720);
    startTimerHz (meterRefreshHz);
}

WebUiHost::~WebUiHost()
{
    stopTimer();
    host.onStatusChanged = nullptr;
}

void WebUiHost::resized()
{
    webView->setBounds (getLocalBounds());
}

void WebUiHost::handleIntent (const juce::var& message)
{
    const auto intent = bridge::parseIntent (message);
    if (!intent)
    {
        DBG ("Rejected malformed intent from UI");
        return;
    }

    std::visit ([this] (const auto& typed) { handle (typed); }, *intent);
}

void WebUiHost::handle (const ap::bridge::AppReady&)
{
    sendStatus();
}

void WebUiHost::handle (const ap::bridge::AudioOpenSettings&)
{
    showAudioSettings();
}

void WebUiHost::handle (const ap::bridge::ToneSetEnabled& intent)
{
    engine.setTestToneEnabled (intent.enabled);
    sendStatus();
}

void WebUiHost::handle (const ap::bridge::ToneSetLevel& intent)
{
    engine.setTestToneLevelDb (static_cast<float> (intent.db));
    sendStatus();
}

void WebUiHost::emit (const ap::bridge::Event& event)
{
    webView->emitEventIfBrowserIsVisible (nativeEventId, bridge::toVar (event));
}

void WebUiHost::sendStatus()
{
    const auto status = host.getStatus();

    ap::bridge::EngineStatus event;
    event.deviceName = status.deviceName.substring (0, 200).toStdString();
    event.sampleRate = juce::jlimit (0.0, ap::bridge::EngineStatus::sampleRateMax, status.sampleRate);
    event.bufferSize = juce::jlimit (0, ap::bridge::EngineStatus::bufferSizeMax, status.bufferSize);
    event.outputLatencyMs
        = juce::jlimit (0.0, ap::bridge::EngineStatus::outputLatencyMsMax, status.outputLatencyMs);
    event.error = status.deviceName.isEmpty() && status.error.isEmpty()
                    ? std::string ("Audio device unavailable. Choose another output device.")
                    : status.error.substring (0, 900).toStdString();
    event.toneEnabled = engine.isTestToneEnabled();
    event.toneLevelDb = static_cast<double> (engine.getTestToneLevelDb());

    emit (event);
}

void WebUiHost::timerCallback()
{
    emit (
        ap::bridge::EngineMeters{juce::jlimit (0.0, 1.0, static_cast<double> (engine.consumeOutputPeak()))});
}

void WebUiHost::showAudioSettings()
{
    // Temporary native dialog until device selection moves into the web UI.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (host.getDeviceManager(), 0,
                                                                          0, // inputs: none until recording
                                                                          1, 2,  // outputs
                                                                          false, // MIDI in
                                                                          false, // MIDI out
                                                                          true,  // stereo pairs
                                                                          false);
    selector->setSize (520, 360);

    juce::DialogWindow::LaunchOptions dialog;
    dialog.content.setOwned (selector.release());
    dialog.dialogTitle = "Audio settings";
    dialog.dialogBackgroundColour = background;
    dialog.useNativeTitleBar = true;
    dialog.resizable = false;
    dialog.launchAsync();
}

} // namespace ap::desktop
