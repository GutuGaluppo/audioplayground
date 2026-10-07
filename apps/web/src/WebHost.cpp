#include "WebHost.h"

#include "BridgeCodec.h"
#include "ap/host/Snapshots.h"
#include "ap/model/ClipEditing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <variant>

namespace ap::web
{
namespace
{
constexpr std::size_t maxPendingEvents = 4096;

template <typename T> bool isAnyOf (const bridge::Intent& intent)
{
    return std::holds_alternative<T> (intent);
}

// Intents that need something the browser host does not have yet.
bool notInBrowser (const bridge::Intent& intent)
{
    return isAnyOf<bridge::AudioOpenSettings> (intent) || isAnyOf<bridge::AudioSetOutput> (intent)
        || isAnyOf<bridge::AudioSetInput> (intent) || isAnyOf<bridge::AudioSetSampleRate> (intent)
        || isAnyOf<bridge::AudioSetBufferSize> (intent) || isAnyOf<bridge::ProjectOpen> (intent)
        || isAnyOf<bridge::ProjectSave> (intent) || isAnyOf<bridge::ProjectSaveAs> (intent)
        || isAnyOf<bridge::ProjectExport> (intent) || isAnyOf<bridge::ProjectCancelExport> (intent)
        || isAnyOf<bridge::SamplerLoad> (intent) || isAnyOf<bridge::DrumsLoadPad> (intent)
        || isAnyOf<bridge::TransportRecord> (intent) || isAnyOf<bridge::TransportCapture> (intent)
        || isAnyOf<bridge::TrackSetArmed> (intent) || isAnyOf<bridge::TrackImportAudio> (intent)
        || isAnyOf<bridge::AssetLocate> (intent) || isAnyOf<bridge::AssetRemove> (intent)
        || isAnyOf<bridge::AssetRemoveUnused> (intent) || isAnyOf<bridge::AccompanimentSuggest> (intent)
        || isAnyOf<bridge::AccompanimentPreview> (intent)
        || isAnyOf<bridge::AccompanimentStopPreview> (intent) || isAnyOf<bridge::AccompanimentAdd> (intent)
        || isAnyOf<bridge::AccompanimentNext> (intent) || isAnyOf<bridge::AccompanimentDismiss> (intent);
}
} // namespace

WebHost::WebHost (double rate, int blockSize)
    : sampleRate (rate)
    , maxBlock (std::max (1, blockSize))
    , editor (engine, model::starterProject())
    , applier (editor, engine,
               {[this] { sendTransportState(); }, [this] { emit (host::timelineState (editor.project())); },
                [this] { sendProjectState(); }, [this] { emit (host::drumsKit (editor.project())); },
                [this] { emit (host::instrumentState (engine)); }, [this] { sendStatus(); },
                [this] (params::ParamId id) { emit (host::paramValue (editor.project(), id)); },
                [this] (std::size_t pad) { emit (host::drumsPad (editor.project(), pad, {})); },
                [this] (int level, std::string_view message) { notice (level, message); }})
{
    sentParameters.fill (std::numeric_limits<float>::quiet_NaN());
    engine.prepare (sampleRate, maxBlock);
    editor.onChanged = [this] { onProjectChanged(); };
    // The engine was given the project by the editor before it was prepared: give it again.
    resetProject (model::starterProject(), false, false);
}

void WebHost::setOutputLatency (double milliseconds)
{
    outputLatencyMs = std::clamp (milliseconds, 0.0, bridge::EngineStatus::outputLatencyMsMax);
    sendStatus();
}

void WebHost::emit (const bridge::Event& event)
{
    if (pending.size() >= maxPendingEvents)
        pending.erase (pending.begin(), pending.begin() + static_cast<std::ptrdiff_t> (maxPendingEvents / 2));
    pending.push_back (juce::JSON::toString (desktop::bridge::toVar (event)));
}

void WebHost::notice (int level, std::string_view message)
{
    emit (bridge::AppNotice {level, std::string (message)});
}

const std::string& WebHost::takeEvents()
{
    out.assign (1, '[');
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        if (i > 0)
            out.push_back (',');
        out += pending[i];
    }
    out.push_back (']');
    pending.clear();
    return out;
}

bool WebHost::handleIntent (std::string_view json)
{
    if (json.size() > maxIntentBytes)
        return false;
    const auto intent = desktop::bridge::parseIntent (juce::JSON::parse (json));
    if (!intent)
        return false;

    if (std::holds_alternative<bridge::AppReady> (*intent))
    {
        sendAll();
        return true;
    }
    if (std::holds_alternative<bridge::ProjectNew> (*intent))
    {
        metadata = {};
        resetProject (model::Project {}, false, false);
        return true;
    }
    if (notInBrowser (*intent))
    {
        notice (0, "That is not available in the browser yet. The desktop app has it.");
        return true;
    }
    applier.apply (*intent);
    return true;
}

void WebHost::sendStatus()
{
    bridge::EngineStatus event;
    event.deviceName = "Web Audio";
    event.sampleRate = std::clamp (sampleRate, 0.0, bridge::EngineStatus::sampleRateMax);
    event.bufferSize = maxBlock;
    event.outputLatencyMs = outputLatencyMs;
    event.toneEnabled = engine.isTestToneEnabled();
    emit (event);
}

void WebHost::sendTransportState()
{
    const auto event = host::transportState (editor.project(), engine);
    lastPlaying = event.playing;
    emit (event);
}

void WebHost::sendParameters (bool everything)
{
    for (std::size_t i = 0; i < params::numParameters; ++i)
    {
        const auto id = static_cast<params::ParamId> (i);
        const float value = editor.project().parameter (id);
        if (everything || !(sentParameters[i] == value))
        {
            sentParameters[i] = value;
            emit (host::paramValue (editor.project(), id));
        }
    }
}

void WebHost::sendDrumPads()
{
    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
        emit (host::drumsPad (editor.project(), pad, {}));
}

void WebHost::sendAll()
{
    sendStatus();
    sendTransportState();
    lastPosition = host::transportPosition (engine);
    emit (lastPosition);
    sendParameters (true);
    emit (host::historyState (editor.document()));
    sendProjectState();
    emit (host::instrumentState (engine));
    emit (bridge::SamplerState {});
    emit (host::drumsKit (editor.project()));
    sendDrumPads();
    emit (host::timelineState (editor.project()));
    emit (bridge::ProjectAssets {});
    emit (bridge::TimelineAssets {});
    emit (bridge::ExportState {false, 0.0});
    emit (bridge::AccompanimentState {});
}

void WebHost::sendProjectState()
{
    emit (bridge::ProjectState {editor.project().name, editor.document().version() != savedVersion,
                                hasLocation});
}

void WebHost::onProjectChanged()
{
    if (loading)
        return; // resetProject() reports everything once, when it is done
    sendProjectState();
    emit (host::timelineState (editor.project()));
    emit (host::drumsKit (editor.project()));
    sendDrumPads();
    sendTransportState();
    sendParameters (false);
    emit (host::historyState (editor.document()));
}

void WebHost::resetProject (model::Project project, bool dirty, bool location)
{
    loading = true;
    editor.reset (std::move (project));
    loading = false;
    // A different song starts from the top, silent.
    engine.getTransport().requestStop();
    engine.getTransport().requestSeek (0);
    savedVersion = dirty ? editor.document().version() + 1 : editor.document().version();
    hasLocation = location;
    sentParameters.fill (std::numeric_limits<float>::quiet_NaN());
    sendAll();
}

std::string WebHost::exportProject (std::string_view timestamp)
{
    // The clock comes from the page; anything that does not look like a time is not trusted.
    const bool plausible
        = !timestamp.empty() && timestamp.size() <= model::maxTimestampLength
       && std::all_of (
           timestamp.begin(), timestamp.end(), [] (char c)
           { return (c >= '0' && c <= '9') || c == '-' || c == ':' || c == 'T' || c == 'Z' || c == '.'; });
    const std::string now = plausible ? std::string (timestamp) : std::string ("1970-01-01T00:00:00Z");
    if (metadata.createdAt.empty())
        metadata.createdAt = now;
    metadata.updatedAt = now;
    return model::serialiseProject (editor.project(), metadata);
}

std::string WebHost::importProject (std::string_view json, bool dirty, bool location)
{
    auto result = model::parseProject (json);
    if (const auto* error = std::get_if<model::LoadError> (&result))
        return error->message.empty() ? std::string ("This project could not be opened.") : error->message;

    auto& loaded = std::get<model::LoadedProject> (result);
    metadata = std::move (loaded.metadata);
    resetProject (std::move (loaded.project), dirty, location);
    return {};
}

void WebHost::projectSaved (bool location)
{
    savedVersion = editor.document().version();
    hasLocation = location;
    sendProjectState();
}

void WebHost::projectUnsaved()
{
    savedVersion = editor.document().version() + 1;
    hasLocation = false;
    sendProjectState();
}

void WebHost::process (float* left, float* right, int frames)
{
    int done = 0;
    while (done < frames)
    {
        const int n = std::min (maxBlock, frames - done);
        std::array<float*, 2> channels {left + done, right + done};
        engine.process ({channels.data(), 2, n});
        done += n;
    }

    framesSincePulse += frames;
    if (static_cast<double> (framesSincePulse) >= sampleRate / eventRateHz)
    {
        framesSincePulse = 0;
        pulse();
    }
}

void WebHost::pulse()
{
    emit (host::engineMeters (engine));
    while (engine.popPlayedNote().has_value())
    {
        // Played notes feed recording and capture on the desktop; nothing records here yet.
    }

    if (engine.getTransport().getState().playing != lastPlaying)
        sendTransportState();

    const auto position = host::transportPosition (engine);
    if (!(position == lastPosition))
    {
        lastPosition = position;
        emit (position);
    }
}

} // namespace ap::web
