#include "WebUiHost.h"

#include "AppPaths.h"
#include "EmbeddedResources.h"
#include "generated/BridgeCodec.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace ap::desktop
{
namespace
{
const juce::Colour background {0xff111214};
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
    return appDataDirectory().getChildFile ("WebView");
}
} // namespace

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

WebUiHost::WebUiHost (AudioDeviceHost& hostToUse, engine::Engine& engineToUse, Session& sessionToUse,
                      ProjectActions& actionsToUse, SampleLoader& samplesToUse,
                      AudioRecorder& audioRecorderToUse)
    : host (hostToUse)
    , engine (engineToUse)
    , session (sessionToUse)
    , actions (actionsToUse)
    , samples (samplesToUse)
    , audioRecorder (audioRecorderToUse)
    , recorder (sessionToUse, engineToUse)
{
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

    host.onStatusChanged = [this]
    {
        const auto status = host.getStatus();
        samples.sync (status.sampleRate);
        engine.setRecordingLatency (
            static_cast<core::Samples> (std::llround (status.outputLatencyMs * status.sampleRate / 1000.0)));
        sendStatus();
    };
    session.onChanged = [this] { onProjectChanged(); };
    samples.onStateChanged = [this] (std::size_t slot)
    {
        if (slot == SampleLoader::samplerSlot)
            sendSamplerState();
        else
            sendDrumPad (slot - 1);
    };
    samples.onClipAudioChanged = [this] { sendTimelineAssets(); };
    samples.onError
        = [this] (const std::string& message) { showNotice (ProjectActions::NoticeLevel::error, message); };
    audioRecorder.onNotice
        = [this] (const std::string& message) { showNotice (ProjectActions::NoticeLevel::warning, message); };
    setSize (1100, 720);
    startTimerHz (meterRefreshHz);
}

WebUiHost::~WebUiHost()
{
    stopTimer();
    host.onStatusChanged = nullptr;
    session.onChanged = nullptr;
    samples.onStateChanged = nullptr;
    samples.onClipAudioChanged = nullptr;
    samples.onError = nullptr;
    audioRecorder.onNotice = nullptr;
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
    sendTransportState();
    sendTransportPosition (true);

    for (std::size_t i = 0; i < params::numParameters; ++i)
        sendParameter (static_cast<params::ParamId> (i));
    sendHistory();
    sendProjectState();
    sendInstrumentState();
    sendSamplerState();
    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
        sendDrumPad (pad);
    sendTimeline();
    sendTimelineAssets();
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

void WebUiHost::handle (const ap::bridge::ParamSet& intent)
{
    // The codec checked the envelope; the parameter system checks the ID and its own range.
    const auto id = params::findParamId (intent.id);
    if (!id)
    {
        DBG ("Rejected param.set for an unknown parameter");
        return;
    }

    const auto value = static_cast<float> (intent.value);
    if (!params::isInRange (params::descriptor (*id), value))
    {
        DBG ("Rejected out-of-range param.set");
        return;
    }

    // Value changes are sent back through onProjectChanged; a rejected or no-op edit still
    // echoes the current value so a control never stays out of sync.
    if (!session.perform (model::SetParameter {*id, value},
                          static_cast<model::ProjectDocument::GestureId> (intent.gesture)))
        sendParameter (*id);
}

void WebUiHost::handle (const ap::bridge::EditUndo&)
{
    session.undo();
}

void WebUiHost::handle (const ap::bridge::EditRedo&)
{
    session.redo();
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

void WebUiHost::handle (const ap::bridge::ProjectRename& intent)
{
    if (!session.perform (model::RenameProject {intent.name}))
        sendProjectState(); // rejected (e.g. empty): restore the shown name
}

void WebUiHost::handle (const ap::bridge::NoteOn& intent)
{
    engine.sendNoteFromUi ({instruments::NoteEvent::Type::noteOn, static_cast<std::uint8_t> (intent.note),
                            static_cast<float> (intent.velocity)});
}

void WebUiHost::handle (const ap::bridge::NoteOff& intent)
{
    engine.sendNoteFromUi (
        {instruments::NoteEvent::Type::noteOff, static_cast<std::uint8_t> (intent.note), 0.0f});
}

void WebUiHost::handle (const ap::bridge::NoteAllOff&)
{
    engine.sendNoteFromUi ({instruments::NoteEvent::Type::allNotesOff, 0, 0.0f});
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

void WebUiHost::handle (const ap::bridge::InstrumentSelect& intent)
{
    engine.setLiveInstrument (static_cast<engine::Engine::LiveInstrument> (intent.instrument));
    sendInstrumentState();
}

void WebUiHost::handle (const ap::bridge::SamplerLoad&)
{
    samples.chooseAndImport (SampleLoader::samplerSlot);
}

void WebUiHost::handle (const ap::bridge::DrumsTrigger& intent)
{
    // Pads play through the same lock-free note queue as the keyboard (GM drum notes).
    engine.sendNoteFromUi ({instruments::NoteEvent::Type::noteOn,
                            static_cast<std::uint8_t> (instruments::DrumMachine::firstMidiNote + intent.pad),
                            static_cast<float> (intent.velocity)});
}

void WebUiHost::handle (const ap::bridge::DrumsSetPad& intent)
{
    const auto pad = static_cast<std::size_t> (intent.pad);
    auto settings = session.project().drums.pads[pad];
    settings.volumeDb = static_cast<float> (intent.volumeDb);
    settings.pitch = static_cast<float> (intent.pitch);
    settings.muted = intent.muted;
    if (!session.perform (model::SetDrumPad {pad, settings},
                          static_cast<model::ProjectDocument::GestureId> (intent.gesture)))
        sendDrumPad (pad);
}

void WebUiHost::handle (const ap::bridge::DrumsLoadPad& intent)
{
    samples.chooseAndImport (SampleLoader::padSlot (static_cast<std::size_t> (intent.pad)));
}

void WebUiHost::handle (const ap::bridge::DrumsResetPad& intent)
{
    const auto pad = static_cast<std::size_t> (intent.pad);
    auto settings = session.project().drums.pads[pad];
    settings.sample = {};
    session.perform (model::SetDrumPad {pad, settings});
}

void WebUiHost::sendDrumPad (std::size_t pad)
{
    const auto& settings = session.project().drums.pads[pad];
    const auto& state = samples.getState (SampleLoader::padSlot (pad));

    ap::bridge::DrumsPad event;
    event.pad = static_cast<int> (pad);
    event.custom = settings.sample.isValid();
    event.name = event.custom
                   ? model::sanitiseName (state.name, ap::bridge::DrumsPad::nameMaxLength).value_or ("Sample")
                   : std::string (instruments::factoryKitNames[pad]);
    event.volumeDb = static_cast<double> (settings.volumeDb);
    event.pitch = static_cast<double> (settings.pitch);
    event.muted = settings.muted;
    event.missing = state.missing;
    emit (event);
}

void WebUiHost::sendInstrumentState()
{
    emit (ap::bridge::InstrumentState {static_cast<int> (engine.getLiveInstrument())});
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
    sendProjectState();
    sendTimeline();
    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
        sendDrumPad (pad);
    sendTransportState();
    for (std::size_t i = 0; i < params::numParameters; ++i)
        sendParameter (static_cast<params::ParamId> (i));
    sendHistory();
}

void WebUiHost::sendHistory()
{
    const auto& doc = session.document();
    ap::bridge::HistoryState event;
    event.canUndo = doc.canUndo();
    event.canRedo = doc.canRedo();
    event.undoLabel
        = std::string (doc.undoDescription().substr (0, ap::bridge::HistoryState::undoLabelMaxLength));
    event.redoLabel
        = std::string (doc.redoDescription().substr (0, ap::bridge::HistoryState::redoLabelMaxLength));
    emit (event);
}

void WebUiHost::sendParameter (params::ParamId id)
{
    emit (ap::bridge::ParamValue {std::string (params::descriptor (id).id),
                                  static_cast<double> (session.project().parameter (id))});
}

void WebUiHost::handle (const ap::bridge::TransportPlay&)
{
    engine.getTransport().requestPlay();
}

void WebUiHost::handle (const ap::bridge::TransportStop&)
{
    stopTransport();
}

void WebUiHost::handle (const ap::bridge::TransportReturnToStart&)
{
    engine.getTransport().requestSeek (0);
}

void WebUiHost::handle (const ap::bridge::TransportSetTempo& intent)
{
    if (!session.perform (model::SetTempo {intent.bpm}))
        sendTransportState(); // rejected or unchanged: resync the field
}

void WebUiHost::handle (const ap::bridge::TransportSetCountIn& intent)
{
    engine.getTransport().setCountInBars (intent.bars);
    sendTransportState();
}

void WebUiHost::handle (const ap::bridge::MetronomeSetEnabled& intent)
{
    engine.getMetronome().setEnabled (intent.enabled);
    sendTransportState();
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

void WebUiHost::sendTransportState()
{
    const auto& transport = engine.getTransport();

    ap::bridge::TransportState event;
    event.playing = transport.getState().playing;
    event.bpm = session.project().tempoBpm;
    event.numerator = session.project().timeSignature.numerator;
    event.denominator = session.project().timeSignature.denominator;
    event.countInBars = transport.getCountInBars();
    event.metronomeEnabled = engine.getMetronome().isEnabled();
    event.recording = isRecording();
    event.armedTrack = static_cast<int> (
        std::min<std::uint64_t> (audioRecorder.getArmedTrack().value, std::numeric_limits<int>::max()));
    lastRecording = event.recording;
    const auto loop = transport.getLoop();
    event.loopEnabled = loop.enabled;
    event.loopStart = static_cast<int> (std::clamp<core::Ticks> (loop.start, 0, model::maxTimelineTicks));
    event.loopEnd = static_cast<int> (std::clamp<core::Ticks> (loop.end, 0, model::maxTimelineTicks));

    lastPlaying = event.playing;
    emit (event);
}

void WebUiHost::sendTransportPosition (bool force)
{
    const auto& transport = engine.getTransport();
    const auto state = transport.getState();

    // Bars and beats depend only on the meter; the sample rate is irrelevant for this conversion.
    const core::TempoMap map (transport.getTempo(), transport.getTimeSignature(), 48000.0);
    const auto position = map.toBarBeatTick (state.positionTicks);

    ap::bridge::TransportPosition event;
    event.bar = static_cast<int> (std::clamp<std::int64_t> (
        position.bar, ap::bridge::TransportPosition::barMin, ap::bridge::TransportPosition::barMax));
    event.beat = position.beat;
    event.ticks = static_cast<int> (std::clamp<core::Ticks> (state.positionTicks,
                                                             ap::bridge::TransportPosition::ticksMin,
                                                             ap::bridge::TransportPosition::ticksMax));
    event.countingIn = state.countingIn;

    if (force || !(event == lastPosition))
    {
        lastPosition = event;
        emit (event);
    }
}

void WebUiHost::timerCallback()
{
    emit (
        ap::bridge::EngineMeters {juce::jlimit (0.0, 1.0, static_cast<double> (engine.consumeOutputPeak())),
                                  juce::jlimit (0.0, 1.0, static_cast<double> (engine.consumeInputPeak()))});

    recorder.poll();
    audioRecorder.poll(); // may end the take by itself (loop wrap, device change, length limit)

    // The audio thread may change the playing state (e.g. a device restart); keep the UI in sync.
    if (engine.getTransport().getState().playing != lastPlaying)
    {
        if (lastPlaying && recorder.isRecording())
            recorder.stop (engine.getTransport().getState().positionTicks);
        if (lastPlaying)
            audioRecorder.stop();
        sendTransportState();
    }
    else if (isRecording() != lastRecording)
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
