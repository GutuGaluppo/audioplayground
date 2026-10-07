// Audio files in the browser host: the page decodes them, this keeps them for the clips, the
// sampler and the drum pads, and tells the UI what is loaded.

#include "WebHost.h"
#include "ap/dsp/Peaks.h"
#include "ap/model/ClipEditing.h"

#include <algorithm>
#include <cmath>

namespace ap::web
{
namespace
{
core::Ticks secondsToTicks (double seconds, double tempoBpm)
{
    return static_cast<core::Ticks> (
        std::ceil (seconds * tempoBpm * static_cast<double> (core::ticksPerQuarterNote) / 60.0));
}
} // namespace

std::size_t WebHost::audioBytes() const noexcept
{
    std::size_t total = 0;
    for (const auto& [path, loaded] : audio)
        total += loaded.bytes;
    if (incoming)
        for (const auto& channel : incoming->channels)
            total += channel.size() * sizeof (float);
    return total;
}

std::shared_ptr<const instruments::SampleBuffer> WebHost::audioFor (model::AssetId id) const
{
    const auto* asset = editor.project().findAsset (id);
    if (asset == nullptr)
        return nullptr;
    const auto it = audio.find (asset->relativePath);
    return it != audio.end() ? it->second.buffer : nullptr;
}

host::AssetAudio WebHost::assetAudio (model::AssetId id) const
{
    host::AssetAudio state;
    const auto* asset = editor.project().findAsset (id);
    if (asset == nullptr)
        return state;
    state.name = asset->name;
    if (const auto it = audio.find (asset->relativePath); it != audio.end())
    {
        state.loaded = true;
        state.durationSeconds = it->second.durationSeconds;
        state.overview = it->second.overview;
    }
    else if (missingAudio.count (asset->relativePath) > 0)
        state.missing = true;
    else
        state.loading = true; // the page is fetching it
    return state;
}

// --- Receiving a file ------------------------------------------------------------------------

std::string WebHost::beginAudio (std::string_view path, double rate, int channels, int frames)
{
    incoming.reset();
    if (!model::isSafeAssetPath (path))
        return "That file name cannot be used.";
    // The page decodes at the output rate, so the samples are ready to play as they are.
    if (std::abs (rate - sampleRate) > 1.0)
        return "That audio is not at the engine's sample rate.";
    if (channels < 1 || channels > 2 || frames < 1)
        return "That audio has an unsupported format.";
    if (static_cast<double> (frames) / rate > maxAudioSeconds)
        return "That audio is longer than 10 minutes.";

    const std::size_t bytes
        = static_cast<std::size_t> (channels) * static_cast<std::size_t> (frames) * sizeof (float);
    std::size_t existing = 0;
    if (const auto it = audio.find (std::string (path)); it != audio.end())
        existing = it->second.bytes; // replaced, not added to
    if (audioBytes() - existing + bytes > maxAudioBytes)
        return "There is not enough memory for more audio. Remove audio you do not use.";

    PendingAudio pendingAudio;
    pendingAudio.path = std::string (path);
    pendingAudio.rate = rate;
    pendingAudio.channels.assign (static_cast<std::size_t> (channels),
                                  std::vector<float> (static_cast<std::size_t> (frames), 0.0f));
    incoming = std::move (pendingAudio);
    return {};
}

float* WebHost::audioChannel (int index) noexcept
{
    if (!incoming || index < 0 || static_cast<std::size_t> (index) >= incoming->channels.size())
        return nullptr;
    return incoming->channels[static_cast<std::size_t> (index)].data();
}

void WebHost::unregisterAudio (const std::string& path)
{
    audio.erase (path);
}

std::string WebHost::endAudio (AudioUse use, int a, int b, std::string_view rawName)
{
    if (!incoming)
        return "No audio was being received.";
    auto received = std::move (*incoming);
    incoming.reset();

    // An existing path must belong to the song: nothing else may pile up in memory.
    const auto& project = editor.project();
    const bool listed
        = std::any_of (project.assets.begin(), project.assets.end(),
                       [&] (const model::Asset& asset) { return asset.relativePath == received.path; });
    if (use == AudioUse::existing && !listed)
        return "That audio is not part of the open song.";

    auto buffer = std::make_shared<instruments::SampleBuffer>();
    buffer->sampleRate = received.rate;
    for (auto& channel : received.channels)
    {
        for (auto& sample : channel)
            if (!std::isfinite (sample))
                sample = 0.0f;
        buffer->channels.push_back (std::move (channel));
    }

    LoadedAudio loaded;
    loaded.durationSeconds = static_cast<double> (buffer->frames()) / buffer->sampleRate;
    loaded.overview = dsp::computeOverview (buffer->channels);
    loaded.peaks = dsp::computePeaks (buffer->channels, buffer->sampleRate);
    loaded.bytes = buffer->channels.size() * static_cast<std::size_t> (buffer->frames()) * sizeof (float);
    loaded.generation = ++audioGeneration;
    loaded.buffer = std::move (buffer);
    const auto seconds = loaded.durationSeconds;
    audio[received.path] = std::move (loaded);
    missingAudio.erase (received.path);

    const std::string name = std::string (rawName);
    bool ok = true;
    switch (use)
    {
    case AudioUse::existing:
        break;
    case AudioUse::clip:
        ok = editor.performGroup (
            "Import audio",
            [&] (model::ProjectDocument::Group& group)
            {
                const auto* added = group.perform (model::AddAsset {received.path, name});
                if (added == nullptr)
                    return;
                const auto start = static_cast<core::Ticks> (std::max (b, 0));
                model::Clip clip;
                clip.start = start;
                clip.length = std::clamp (secondsToTicks (seconds, group.project().tempoBpm),
                                          model::Clip::minLength, model::maxTimelineTicks - start);
                clip.asset = std::get<model::AddAsset> (*added).created;
                (void)group.perform (
                    model::AddClip {model::TrackId {static_cast<std::uint64_t> (std::max (a, 0))}, clip});
            });
        break;
    case AudioUse::relink:
        ok = editor.perform (model::RelinkAsset {
            model::AssetId {static_cast<std::uint64_t> (std::max (a, 0))}, received.path, name});
        break;
    case AudioUse::sampler:
        ok = editor.performGroup (
            "Import sample",
            [&] (model::ProjectDocument::Group& group)
            {
                const auto* added = group.perform (model::AddAsset {received.path, name});
                if (added != nullptr)
                    (void)group.perform (model::SetSamplerAsset {std::get<model::AddAsset> (*added).created});
            });
        break;
    case AudioUse::pad:
        ok = a >= 0 && a < static_cast<int> (model::DrumKit::numPads)
          && editor.performGroup (
              "Import sample",
              [&] (model::ProjectDocument::Group& group)
              {
                  const auto* added = group.perform (model::AddAsset {received.path, name});
                  if (added == nullptr)
                      return;
                  auto settings = group.project().drums.pads[static_cast<std::size_t> (a)];
                  settings.sample = std::get<model::AddAsset> (*added).created;
                  (void)group.perform (model::SetDrumPad {static_cast<std::size_t> (a), settings});
              });
        break;
    }

    if (!ok)
    {
        unregisterAudio (received.path);
        return use == AudioUse::relink ? "Could not use that file for the missing audio."
                                       : "Could not add the audio to the song.";
    }

    editor.refresh();
    syncSlots();
    sendAssetEvents (false);
    return {};
}

void WebHost::audioMissing (std::string_view path)
{
    const std::string key (path);
    audio.erase (key);
    missingAudio.insert (key);
    editor.refresh();
    syncSlots();
    sendAssetEvents (false);
}

void WebHost::forgetAudio (std::string_view path)
{
    const std::string key (path);
    audio.erase (key);
    missingAudio.erase (key);
    editor.refresh();
    syncSlots();
    sendAssetEvents (false);
}

// --- What the UI is told ---------------------------------------------------------------------

void WebHost::sendAssetEvents (bool everything)
{
    const auto& project = editor.project();

    // Audio the clips use, with what is known of each: loaded, still arriving, or missing.
    bridge::TimelineAssets timeline;
    std::set<std::uint64_t> used;
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
            if (clip.asset.isValid() && used.insert (clip.asset.value).second)
                timeline.assets.push_back (
                    host::timelineAsset (project, clip.asset, assetAudio (clip.asset)));
    emit (timeline);

    if (everything)
        sentPeaks.clear();
    std::erase_if (sentPeaks, [&] (const auto& sent) { return used.count (sent.first) == 0; });
    for (const auto id : used)
    {
        const auto* asset = project.findAsset (model::AssetId {id});
        if (asset == nullptr)
            continue;
        const auto it = audio.find (asset->relativePath);
        if (it == audio.end())
            continue;
        if (const auto sent = sentPeaks.find (id);
            sent != sentPeaks.end() && sent->second == it->second.generation)
            continue;
        if (const auto peaks = host::timelinePeaks (model::AssetId {id}, it->second.peaks))
        {
            sentPeaks[id] = it->second.generation;
            emit (*peaks);
        }
    }

    const auto assets
        = host::projectAssets (project,
                               [this] (model::AssetId id)
                               {
                                   const auto* asset = editor.project().findAsset (id);
                                   return asset != nullptr && missingAudio.count (asset->relativePath) > 0;
                               });
    if (everything || !sentProjectAssets || !(*sentProjectAssets == assets))
    {
        sentProjectAssets = assets;
        emit (assets);
    }
    sendSamplerState();
    sendDrumPads();
}

void WebHost::sendSamplerState()
{
    bridge::SamplerState state;
    const auto id = editor.project().samplerAsset;
    if (id.isValid())
    {
        const auto audioState = assetAudio (id);
        state.name = model::sanitiseName (audioState.name, bridge::SamplerState::nameMaxLength).value_or ("");
        state.loaded = audioState.loaded;
        state.missing = audioState.missing;
        state.loading = audioState.loading;
        state.durationSeconds
            = std::clamp (audioState.durationSeconds, 0.0, bridge::SamplerState::durationSecondsMax);
        state.overview = audioState.overview;
    }
    if (!sentSampler || !(*sentSampler == state))
    {
        sentSampler = state;
        emit (state);
    }
}

// Puts the audio the sampler and the pads point at into the engine (and takes it out again).
void WebHost::syncSlots()
{
    const auto& project = editor.project();

    const auto wantedSampler
        = project.samplerAsset.isValid() ? project.findAsset (project.samplerAsset) : nullptr;
    const auto samplerEntry
        = wantedSampler != nullptr ? audio.find (wantedSampler->relativePath) : audio.end();
    if (samplerEntry != audio.end())
    {
        if (samplerLoaded != wantedSampler->relativePath
            || samplerGeneration != samplerEntry->second.generation)
        {
            auto copy = std::make_unique<instruments::SampleBuffer> (*samplerEntry->second.buffer);
            copy->assetId = project.samplerAsset.value; // the engine tells which asset the sampler holds
            engine.loadSamplerSample (std::move (copy));
            samplerLoaded = wantedSampler->relativePath;
            samplerGeneration = samplerEntry->second.generation;
        }
    }
    else if (!samplerLoaded.empty())
    {
        engine.loadSamplerSample (std::make_unique<instruments::SampleBuffer>());
        samplerLoaded.clear();
    }

    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
    {
        const auto sample = project.drums.pads[pad].sample;
        const auto* asset = sample.isValid() ? project.findAsset (sample) : nullptr;
        const auto entry = asset != nullptr ? audio.find (asset->relativePath) : audio.end();
        if (entry != audio.end())
        {
            if (padLoaded[pad] != asset->relativePath || padGeneration[pad] != entry->second.generation)
            {
                auto copy = std::make_unique<instruments::SampleBuffer> (*entry->second.buffer);
                copy->assetId = sample.value;
                engine.getDrums().loadPadSample (static_cast<int> (pad), std::move (copy));
                padLoaded[pad] = asset->relativePath;
                padGeneration[pad] = entry->second.generation;
            }
        }
        else if (!padLoaded[pad].empty())
        {
            engine.getDrums().loadPadSample (static_cast<int> (pad), nullptr); // back to the factory sound
            padLoaded[pad].clear();
        }
    }
}

} // namespace ap::web
