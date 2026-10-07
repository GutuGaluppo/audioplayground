#include "WebUiHost.h"

#include "generated/BridgeCodec.h"
#if !AP_HEADLESS_UI
#include "AppPaths.h"
#include "EmbeddedResources.h"
#endif

#include <array>
#include <cstdio>
#include <juce_audio_utils/juce_audio_utils.h>

namespace ap::desktop
{
namespace
{
const juce::Colour background {0xff111214};
constexpr int meterRefreshHz = 30;
#if !AP_HEADLESS_UI
constexpr auto intentEventId = "ap.intent";
constexpr auto nativeEventId = "ap.event";
#endif

#if !AP_HEADLESS_UI
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
    return appDataDirectory().getChildFile ("WebView");
}
#endif
} // namespace

#if AP_HEADLESS_UI
class WebUiHost::LockedDownWebView
{
};
#else
class WebUiHost::LockedDownWebView final : public juce::WebBrowserComponent
{
public:
    LockedDownWebView (const Options& options, juce::String devUrl)
        : WebBrowserComponent (options)
        , developmentUrl (std::move (devUrl))
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
#endif

WebUiHost::WebUiHost (AudioDeviceHost& hostToUse, engine::Engine& engineToUse, Session& sessionToUse,
                      ProjectActions& actionsToUse, SampleLoader& samplesToUse,
                      AudioRecorder& audioRecorderToUse)
    : host (hostToUse)
    , engine (engineToUse)
    , session (sessionToUse)
    , actions (actionsToUse)
    , samples (samplesToUse)
    , audioRecorder (audioRecorderToUse)
    , recorder (sessionToUse)
    , capture (sessionToUse)
    , applier (sessionToUse, engineToUse,
               {[this] { sendTransportState(); }, [this] { sendTimeline(); }, [this] { sendProjectState(); },
                [this] { sendDrumKit(); }, [this] { sendInstrumentState(); }, [this] { sendStatus(); },
                [this] (params::ParamId id) { sendParameter (id); },
                [this] (std::size_t pad) { sendDrumPad (pad); }})
    , exporter (sessionToUse, samplesToUse)
    , accompaniment (sessionToUse, samplesToUse, engineToUse)
{
#if !AP_HEADLESS_UI
    const auto devUrl = developmentServerUrl();

    auto options = juce::WebBrowserComponent::Options {}
                       .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                       .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
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
#endif

    applier.loadedAudioSeconds = [this] (model::AssetId asset) -> std::optional<double>
    {
        const auto& audio = samples.getClipAudio();
        if (const auto it = audio.find (asset.value); it != audio.end() && it->second.state.loaded)
            return it->second.state.durationSeconds;
        return std::nullopt;
    };
    host.onStatusChanged = [this]
    {
        const auto status = host.getStatus();
        samples.sync (status.sampleRate);
        engine.setRecordingLatency (
            static_cast<core::Samples> (std::llround (status.outputLatencyMs * status.sampleRate / 1000.0)));
        sendStatus();
        sendAudioDevices();
    };
    host.onDeviceRecovered = [this] (const juce::String& message)
    { showNotice (ProjectActions::NoticeLevel::warning, message.toStdString()); };
    session.onChanged = [this] { onProjectChanged(); };
    samples.onStateChanged = [this] (std::size_t slot)
    {
        if (slot == SampleLoader::samplerSlot)
            sendSamplerState();
        else
            sendDrumPad (slot - 1);
    };
    samples.onClipAudioChanged = [this]
    {
        accompaniment.audioChanged();
        sendProjectAssets (true);
        sendTimelineAssets();
        sendTimelinePeaks (false);
    };
    samples.onError
        = [this] (const std::string& message) { showNotice (ProjectActions::NoticeLevel::error, message); };
    audioRecorder.onNotice
        = [this] (const std::string& message) { showNotice (ProjectActions::NoticeLevel::warning, message); };
    accompaniment.onChanged = [this] { sendAccompaniment(); };
    audioRecorder.onTakeAdded = [this] (model::ClipId clip) { accompaniment.suggestFor (clip); };
    exporter.onFinished = [this] (const Exporter::Result& result) { exportFinished (result); };
    setSize (1100, 720);
    startTimerHz (meterRefreshHz);
}

WebUiHost::~WebUiHost()
{
    stopTimer();
    host.onStatusChanged = nullptr;
    host.onDeviceRecovered = nullptr;
    session.onChanged = nullptr;
    samples.onStateChanged = nullptr;
    samples.onClipAudioChanged = nullptr;
    samples.onError = nullptr;
    audioRecorder.onNotice = nullptr;
    audioRecorder.onTakeAdded = nullptr;
    accompaniment.onChanged = nullptr;
}

void WebUiHost::resized()
{
#if !AP_HEADLESS_UI
    if (webView)
        webView->setBounds (getLocalBounds());
#endif
}

void WebUiHost::handleIntent (const juce::var& message)
{
    const auto intent = bridge::parseIntent (message);
    if (!intent)
    {
        DBG ("Rejected malformed intent from UI");
        return;
    }

    // Stopping also ends recording, previews and note capture, which only this host knows about.
    if (std::holds_alternative<ap::bridge::TransportStop> (*intent))
    {
        stopTransport();
        return;
    }
    if (applier.apply (*intent))
        return;
    std::visit ([this] (const auto& typed) { handle (typed); }, *intent);
}

void WebUiHost::handle (const ap::bridge::AppReady&)
{
    sendStatus();
    sendAudioDevices();
    sendTransportState();
    sendTransportPosition (true);

    for (std::size_t i = 0; i < params::numParameters; ++i)
        sendParameter (static_cast<params::ParamId> (i));
    sendHistory();
    sendProjectState();
    sendInstrumentState();
    sendSamplerState();
    sendDrumKit();
    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
        sendDrumPad (pad);
    sendTimeline();
    sendProjectAssets (true);
    sendTimelineAssets();
    sendTimelinePeaks (true);
    sendExportState();
    sendAccompaniment();
}

void WebUiHost::handle (const ap::bridge::AudioOpenSettings&)
{
    showAudioSettings();
}

void WebUiHost::changeDevice (const std::function<juce::String()>& change)
{
    if (isRecording())
    {
        showNotice (ProjectActions::NoticeLevel::warning, "Stop recording before changing the audio device.");
        sendAudioDevices(); // put the menu back on what is really in use
        return;
    }
    if (const auto error = change(); error.isNotEmpty())
        showNotice (ProjectActions::NoticeLevel::error, error.toStdString());
    sendStatus();
    sendAudioDevices();
}

void WebUiHost::handle (const ap::bridge::AudioSetOutput& intent)
{
    changeDevice ([&] { return host.setOutputDevice (juce::String::fromUTF8 (intent.name.c_str())); });
}

void WebUiHost::handle (const ap::bridge::AudioSetInput& intent)
{
    changeDevice ([&] { return host.setPreferredInput (juce::String::fromUTF8 (intent.name.c_str())); });
}

void WebUiHost::handle (const ap::bridge::AudioSetSampleRate& intent)
{
    changeDevice ([&] { return host.setSampleRate (intent.rate); });
}

void WebUiHost::handle (const ap::bridge::AudioSetBufferSize& intent)
{
    changeDevice ([&] { return host.setBufferSize (intent.size); });
}

void WebUiHost::handle (const ap::bridge::ProjectNew&)
{
    if (isRecording())
        stopTransport(); // the take joins the project before it is saved or replaced
    actions.newProject();
}

void WebUiHost::handle (const ap::bridge::ProjectOpen&)
{
    if (isRecording())
        stopTransport(); // the take joins the project before it is saved or replaced
    actions.openProject();
}

void WebUiHost::handle (const ap::bridge::ProjectSave&)
{
    if (isRecording())
        stopTransport(); // the take joins the project before it is saved or replaced
    actions.save();
}

void WebUiHost::handle (const ap::bridge::ProjectSaveAs&)
{
    if (isRecording())
        stopTransport(); // the take joins the project before it is saved or replaced
    actions.saveAs();
}

void WebUiHost::handle (const ap::bridge::ProjectExport& intent)
{
    if (isRecording())
        stopTransport(); // the take joins the song first
    const std::array<io::WavFormat, 3> formats {io::WavFormat::pcm16, io::WavFormat::pcm24,
                                                io::WavFormat::float32};
    exporter.chooseAndExport (
        {formats[static_cast<std::size_t> (intent.format)], static_cast<std::uint32_t> (intent.sampleRate)});
}

void WebUiHost::handle (const ap::bridge::ProjectCancelExport&)
{
    exporter.cancel();
}

void WebUiHost::sendExportState()
{
    lastExporting = exporter.isRunning();
    emit (ap::bridge::ExportState {lastExporting, juce::jlimit (0.0, 1.0, exporter.getProgress())});
}

void WebUiHost::exportFinished (const Exporter::Result& result)
{
    sendExportState();
    if (result.error == "cancelled")
        return showNotice (ProjectActions::NoticeLevel::info, "Export cancelled.");
    if (!result.error.empty())
        return showNotice (ProjectActions::NoticeLevel::error, "Export failed. " + result.error);

    const auto minutes = static_cast<int> (result.seconds) / 60;
    const auto seconds = static_cast<int> (result.seconds) % 60;
    std::array<char, 96> figures {};
    std::snprintf (figures.data(), figures.size(), "(%d:%02d) · %.1f LUFS · peak %.1f dBTP", minutes, seconds,
                   result.loudness.integratedLufs, result.loudness.truePeakDb);
    showNotice (ProjectActions::NoticeLevel::info, "Exported \"" + result.fileName + "\" " + figures.data());
}

void WebUiHost::sendProjectState()
{
    ap::bridge::ProjectState event;
    event.name = session.project().name;
    event.dirty = session.isDirty();
    event.hasLocation = session.location().has_value();
    emit (event);
}

void WebUiHost::showNotice (ProjectActions::NoticeLevel level, const std::string& message)
{
    ap::bridge::AppNotice event;
    event.level = static_cast<int> (level);
    event.message = model::sanitiseName (message, ap::bridge::AppNotice::messageMaxLength).value_or ("");
    emit (event);
}

void WebUiHost::handle (const ap::bridge::SamplerLoad&)
{
    samples.chooseAndImport (SampleLoader::samplerSlot);
}

void WebUiHost::handle (const ap::bridge::DrumsLoadPad& intent)
{
    samples.chooseAndImport (SampleLoader::padSlot (static_cast<std::size_t> (intent.pad)));
}

void WebUiHost::sendDrumKit()
{
    emit (host::drumsKit (session.project()));
}

void WebUiHost::sendDrumPad (std::size_t pad)
{
    const auto& state = samples.getState (SampleLoader::padSlot (pad));
    emit (host::drumsPad (session.project(), pad, {state.name, state.missing}));
}

void WebUiHost::sendInstrumentState()
{
    emit (host::instrumentState (engine));
}

void WebUiHost::sendHistory()
{
    emit (host::historyState (session.document()));
}

void WebUiHost::sendParameter (params::ParamId id)
{
    emit (host::paramValue (session.project(), id));
}

void WebUiHost::sendTransportState()
{
    auto event = host::transportState (session.project(), engine,
                                       {isRecording(), audioRecorder.getArmedTrack(), capture.hasNotes()});
    lastRecording = event.recording;
    lastCaptureAvailable = event.captureAvailable;
    lastPlaying = event.playing;
    emit (event);
}

void WebUiHost::sendTransportPosition (bool force)
{
    const auto event = host::transportPosition (engine);
    if (force || !(event == lastPosition))
    {
        lastPosition = event;
        emit (event);
    }
}

void WebUiHost::sendSamplerState()
{
    const auto& state = samples.getState (SampleLoader::samplerSlot);
    ap::bridge::SamplerState event;
    event.name = model::sanitiseName (state.name, ap::bridge::SamplerState::nameMaxLength).value_or ("");
    event.loaded = state.loaded;
    event.missing = state.missing;
    event.loading = state.loading;
    event.durationSeconds
        = std::clamp (state.durationSeconds, 0.0, ap::bridge::SamplerState::durationSecondsMax);
    event.overview = state.overview;
    emit (event);
}

void WebUiHost::onProjectChanged()
{
    // An armed track that no longer exists (deleted, undone, another project) disarms.
    if (const auto armed = audioRecorder.getArmedTrack(); armed.isValid() && !audioRecorder.isRecording())
        if (const auto* track = session.project().findTrack (armed);
            track == nullptr || track->kind != model::TrackKind::audio)
            disarm();

    samples.sync (host.getStatus().sampleRate);
    accompaniment.projectChanged();
    sendProjectState();
    sendTimeline();
    sendProjectAssets();
    sendDrumKit();
    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
        sendDrumPad (pad);
    sendTransportState();
    for (std::size_t i = 0; i < params::numParameters; ++i)
        sendParameter (static_cast<params::ParamId> (i));
    sendHistory();
}

void WebUiHost::emit (const ap::bridge::Event& event)
{
#if AP_HEADLESS_UI
    if (eventSink)
        eventSink (bridge::toVar (event));
#else
    webView->emitEventIfBrowserIsVisible (nativeEventId, bridge::toVar (event));
#endif
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
    event.inputName = status.inputName.substring (0, 200).toStdString();
    event.inputChannels = juce::jlimit (0, 2, status.numInputChannels);
    event.roundTripLatencyMs
        = status.sampleRate > 0.0
            ? juce::jlimit (0.0, ap::bridge::EngineStatus::roundTripLatencyMsMax,
                            1000.0 * static_cast<double> (status.roundTripLatencySamples) / status.sampleRate)
            : 0.0;
    event.error = status.deviceName.isEmpty() && status.error.isEmpty()
                    ? std::string ("Audio device unavailable. Choose another output device.")
                    : status.error.substring (0, 900).toStdString();
    event.toneEnabled = engine.isTestToneEnabled();

    emit (event);
}

void WebUiHost::sendAudioDevices()
{
    using ap::bridge::AudioDevices;
    const auto list = host.getDevices();

    AudioDevices event;
    for (const auto& name : list.outputs)
        if (event.outputs.size() < AudioDevices::outputsMaxItems)
            event.outputs.push_back ({name.substring (0, 200).toStdString()});
    for (const auto& name : list.inputs)
        if (event.inputs.size() < AudioDevices::inputsMaxItems)
            event.inputs.push_back ({name.substring (0, 200).toStdString()});
    event.output = list.output.substring (0, 200).toStdString();
    event.input = list.preferredInput.substring (0, 200).toStdString();
    for (const auto rate : list.sampleRates)
        if (event.sampleRates.size() < AudioDevices::sampleRatesMaxItems)
            event.sampleRates.push_back (static_cast<float> (rate));
    for (const auto size : list.bufferSizes)
        if (event.bufferSizes.size() < AudioDevices::bufferSizesMaxItems)
            event.bufferSizes.push_back (static_cast<float> (size));
    event.sampleRate = juce::jlimit (0.0, AudioDevices::sampleRateMax, list.sampleRate);
    event.bufferSize = juce::jlimit (0, AudioDevices::bufferSizeMax, list.bufferSize);
    emit (event);
}

void WebUiHost::timerCallback()
{
    emit (host::engineMeters (engine));

    pumpPlayedNotes();
    audioRecorder.poll();
    if (exporter.isRunning() || lastExporting)
        sendExportState(); // may end the take by itself (loop wrap, device change, length limit)

    // The audio thread may change the playing state (e.g. a device restart); keep the UI in sync.
    if (engine.getTransport().getState().playing != lastPlaying)
    {
        if (lastPlaying)
            stopNoteRecording (engine.getTransport().getState().positionTicks);
        if (lastPlaying)
            audioRecorder.stop();
        sendTransportState();
    }
    else if (isRecording() != lastRecording || capture.hasNotes() != lastCaptureAvailable)
        sendTransportState();

    sendTransportPosition (false);
}

void WebUiHost::showAudioSettings()
{
    // Temporary native dialog until device selection moves into the web UI.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (host.getDeviceManager(), 0,
                                                                          2,     // inputs (for recording)
                                                                          1, 2,  // outputs
                                                                          false, // MIDI in
                                                                          false, // MIDI out
                                                                          true,  // stereo pairs
                                                                          false);
    selector->setSize (520, 440);

    juce::DialogWindow::LaunchOptions dialog;
    dialog.content.setOwned (selector.release());
    dialog.dialogTitle = "Audio settings";
    dialog.dialogBackgroundColour = background;
    dialog.useNativeTitleBar = true;
    dialog.resizable = false;
    dialog.launchAsync();
}

} // namespace ap::desktop
