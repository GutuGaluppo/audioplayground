#include "SampleLoader.h"

#include "AppPaths.h"
#include "ap/dsp/Peaks.h"
#include "ap/dsp/Resampler.h"

#include <cmath>
#include <set>

namespace ap::desktop
{
namespace
{
constexpr auto supportedPattern = "*.wav;*.wave;*.aif;*.aiff;*.flac";

bool hasSupportedExtension (const juce::File& file)
{
    return file.hasFileExtension ("wav;wave;aif;aiff;flac");
}

// "My Kick (final).WAV" -> "My-Kick-final.wav": the stored name must pass model::isSafeAssetPath.
std::string safeFileStem (const juce::File& file)
{
    std::string stem;
    for (const auto c : file.getFileNameWithoutExtension().toStdString())
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            stem.push_back (c);
        else if (!stem.empty() && stem.back() != '-')
            stem.push_back ('-');
        if (stem.size() >= 60)
            break;
    }
    while (!stem.empty() && stem.back() == '-')
        stem.pop_back();
    return stem.empty() ? "audio" : stem;
}

void registerFormats (juce::AudioFormatManager& formats)
{
    // Explicit list (not registerBasicFormats) so every platform accepts the same files.
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::AiffAudioFormat(), false);
    formats.registerFormat (new juce::FlacAudioFormat(), false);
}

std::vector<float> computeOverview (const std::vector<std::vector<float>>& channels, int points)
{
    std::vector<float> overview (static_cast<std::size_t> (points), 0.0f);
    if (channels.empty() || channels.front().empty())
        return overview;

    const auto frames = channels.front().size();
    for (std::size_t p = 0; p < overview.size(); ++p)
    {
        const auto from = frames * p / overview.size();
        const auto to = std::max (from + 1, frames * (p + 1) / overview.size());
        float peak = 0.0f;
        for (const auto& channel : channels)
            for (auto i = from; i < std::min (to, frames); ++i)
                peak = std::max (peak, std::abs (channel[i]));
        overview[p] = std::min (peak, 1.0f);
    }
    return overview;
}
} // namespace

struct SampleLoader::Decoded
{
    std::string error; // user-facing; empty on success
    model::AssetId asset;
    std::string name;
    double durationSeconds = 0.0;
    std::unique_ptr<instruments::SampleBuffer> buffer;
    std::vector<float> overview;
    std::vector<std::uint8_t> peaks;
    double rate = 0.0;
};

namespace
{
// Background thread. Never throws; failures become a user-facing message.
std::shared_ptr<SampleLoader::Decoded> decode (const juce::File& file, const std::string& name,
                                               model::AssetId asset, double rate, bool wantOverview)
{
    auto result = std::make_shared<SampleLoader::Decoded>();
    result->asset = asset;
    result->name = name;
    result->rate = rate;

    juce::AudioFormatManager formats;
    registerFormats (formats);
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        result->error = "\"" + name + "\" could not be read. It may be damaged or in an unsupported format.";
    else if (reader->numChannels < 1 || reader->numChannels > 32 || reader->sampleRate < 8000.0
             || reader->sampleRate > 384000.0)
        result->error = "\"" + name + "\" has an unsupported format.";
    else if (reader->lengthInSamples <= 0
             || static_cast<double> (reader->lengthInSamples) / reader->sampleRate
                    > SampleLoader::maxDurationSeconds)
        result->error = "\"" + name + "\" is empty or longer than 10 minutes.";
    else
    {
        const int channels = static_cast<int> (std::min<unsigned int> (reader->numChannels, 2));
        const auto length = static_cast<int> (reader->lengthInSamples);
        juce::AudioBuffer<float> decoded (channels, length);
        if (!reader->read (&decoded, 0, length, 0, true, channels > 1))
            result->error = "\"" + name + "\" could not be read completely.";
        else
        {
            auto buffer = std::make_unique<instruments::SampleBuffer>();
            buffer->assetId = asset.value;
            buffer->sampleRate = rate;
            for (int ch = 0; ch < channels; ++ch)
            {
                std::vector<float> samples (decoded.getReadPointer (ch),
                                            decoded.getReadPointer (ch) + length);
                for (auto& value : samples)
                    if (!std::isfinite (value))
                        value = 0.0f;
                buffer->channels.push_back (dsp::resample (samples, reader->sampleRate, rate));
            }
            if (wantOverview)
            {
                result->overview = computeOverview (buffer->channels, SampleLoader::overviewPoints);
                result->peaks = dsp::computePeaks (buffer->channels, rate);
            }
            result->durationSeconds = static_cast<double> (length) / reader->sampleRate;
            result->buffer = std::move (buffer);
        }
    }
    return result;
}

core::Ticks secondsToTicks (double seconds, double tempoBpm)
{
    return static_cast<core::Ticks> (
        std::ceil (seconds * tempoBpm * static_cast<double> (core::ticksPerQuarterNote) / 60.0));
}
} // namespace

SampleLoader::SampleLoader (Session& sessionToUse, engine::Engine& engineToUse)
    : session (sessionToUse)
    , engine (engineToUse)
{
    session.setAudioLookup (
        [this] (model::AssetId asset) -> std::shared_ptr<const instruments::SampleBuffer>
        {
            const auto it = clipAudio.find (asset.value);
            return it != clipAudio.end() ? it->second.buffer : nullptr;
        });
}

SampleLoader::~SampleLoader()
{
    *alive = false;
    pool.removeAllJobs (true, 10000);
    session.setAudioLookup ({});
}

template <typename Done>
void SampleLoader::decodeInBackground (juce::File file, std::string name, model::AssetId asset, bool overview,
                                       Done done)
{
    pool.addJob (
        [stillAlive = alive, file, name = std::move (name), asset, overview, rate = engineRate,
         done = std::move (done)]() mutable
        {
            auto result = decode (file, name, asset, rate, overview);
            juce::MessageManager::callAsync (
                [stillAlive, result = std::move (result), done = std::move (done)]() mutable
                {
                    if (*stillAlive)
                        done (std::move (result));
                });
        });
}

void SampleLoader::error (const std::string& message)
{
    if (onError)
        onError (message);
}

void SampleLoader::setState (std::size_t slot, State next)
{
    slots[slot].state = std::move (next);
    if (onStateChanged)
        onStateChanged (slot);
}

model::AssetId SampleLoader::wantedAsset (std::size_t slot) const
{
    const auto& project = session.project();
    return slot == samplerSlot ? project.samplerAsset : project.drums.pads[slot - 1].sample;
}

void SampleLoader::assign (std::size_t slot, model::AssetId asset)
{
    if (slot == samplerSlot)
    {
        session.perform (model::SetSamplerAsset {asset});
        return;
    }
    auto pad = session.project().drums.pads[slot - 1];
    pad.sample = asset;
    session.perform (model::SetDrumPad {slot - 1, pad});
}

void SampleLoader::publish (std::size_t slot, std::unique_ptr<instruments::SampleBuffer> buffer)
{
    if (slot == samplerSlot)
        engine.loadSamplerSample (buffer != nullptr ? std::move (buffer)
                                                    : std::make_unique<instruments::SampleBuffer>());
    else
        engine.getDrums().loadPadSample (static_cast<int> (slot - 1),
                                         std::move (buffer)); // nullptr: factory sound
}

void SampleLoader::chooseFile (std::function<void (Copied)> onCopied)
{
    chooser = std::make_unique<juce::FileChooser> (
        "Choose an audio file", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        supportedPattern);

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, onCopied = std::move (onCopied)] (const juce::FileChooser& fc)
        {
            const auto source = fc.getResult();
            if (source == juce::File())
                return; // cancelled

            if (!hasSupportedExtension (source))
                return error ("That file type is not supported. Use WAV, AIFF or FLAC.");
            if (source.getSize() > maxFileBytes)
                return error ("That file is too large to import.");

            // Copy into the project's audio folder under a safe, unique name.
            const auto audioDir = juce::File (toJuceString (session.assetRoot().audioDirectory()));
            audioDir.createDirectory();
            const auto stem = std::to_string (session.project().nextAssetId) + "-" + safeFileStem (source);
            const auto extension = source.getFileExtension().toLowerCase().toStdString();
            auto target = audioDir.getChildFile (stem + extension);
            for (int n = 2; target.exists(); ++n)
                target = audioDir.getChildFile (stem + "-" + std::to_string (n) + extension);

            const auto temporary = target.getSiblingFile (target.getFileName() + ".importing");
            if (!source.copyFileTo (temporary) || !temporary.moveFileTo (target))
            {
                temporary.deleteFile();
                return error ("Could not copy the file into the project.");
            }

            onCopied (
                {target, "audio/" + target.getFileName().toStdString(), source.getFileName().toStdString()});
        });
}

void SampleLoader::chooseAndImport (std::size_t slot)
{
    if (slot >= numSlots)
        return;

    chooseFile (
        [this, slot] (Copied copied)
        {
            if (!session.perform (model::AddAsset {copied.relativePath, copied.displayName}))
            {
                copied.file.deleteFile();
                return error ("Could not add the file to the project.");
            }
            // sync() (via the session's onChanged) loads it once the slot points at it.
            assign (slot, session.project().assets.back().id);
        });
}

void SampleLoader::chooseAndImportClip (model::TrackId track, core::Ticks position)
{
    chooseFile (
        [this, track, position] (Copied copied)
        {
            // Decode first: the clip's length is the file's duration.
            const auto file = copied.file;
            const auto name = copied.displayName;
            decodeInBackground (
                file, name, {}, true,
                [this, track, position, copied = std::move (copied)] (std::shared_ptr<Decoded> result) mutable
                { finishClipImport (track, position, std::move (copied), std::move (result)); });
        });
}

void SampleLoader::finishClipImport (model::TrackId track, core::Ticks position, Copied copied,
                                     std::shared_ptr<Decoded> result)
{
    if (!result->error.empty())
    {
        copied.file.deleteFile();
        return error (result->error);
    }
    if (result->rate != engineRate)
        result->buffer.reset(); // the device changed meanwhile: sync() decodes it again

    model::AssetId asset;
    const bool added = session.performGroup (
        "Import audio",
        [&] (model::ProjectDocument::Group& group)
        {
            const auto* addedAsset
                = group.perform (model::AddAsset {copied.relativePath, copied.displayName});
            if (addedAsset == nullptr)
                return;
            asset = std::get<model::AddAsset> (*addedAsset).created;

            model::Clip clip;
            clip.start = position;
            clip.length = std::clamp (secondsToTicks (result->durationSeconds, group.project().tempoBpm),
                                      model::Clip::minLength, model::maxTimelineTicks - position);
            clip.asset = asset;
            (void)group.perform (model::AddClip {track, clip});
        });

    if (!added || !asset.isValid())
    {
        copied.file.deleteFile();
        return error ("Could not add the audio to the timeline.");
    }

    // Keep the decoded audio: no second decode for the clip that was just added.
    if (result->buffer != nullptr)
    {
        auto& entry = clipAudio[asset.value];
        entry.buffer = std::shared_ptr<const instruments::SampleBuffer> (std::move (result->buffer));
        entry.loadedRate = result->rate;
        entry.generation = ++clipGeneration;
        entry.state.name = result->name;
        entry.state.loaded = true;
        entry.state.durationSeconds = result->durationSeconds;
        entry.state.overview = std::move (result->overview);
        entry.state.peaks = std::move (result->peaks);
        session.refreshEngine();
        if (onClipAudioChanged)
            onClipAudioChanged();
    }
}

void SampleLoader::sync (double engineSampleRate)
{
    if (engineSampleRate > 0.0)
        engineRate = engineSampleRate;

    for (std::size_t slot = 0; slot < numSlots; ++slot)
        syncSlot (slot);
    syncClipAudio();
}

std::optional<juce::File> SampleLoader::resolve (model::AssetId asset, std::string& name) const
{
    const auto* entry = session.project().findAsset (asset);
    name = entry != nullptr ? entry->name : std::string ("Sample");
    const auto path
        = entry != nullptr ? io::resolveAssetPath (session.assetRoot(), entry->relativePath) : std::nullopt;
    if (!path)
        return std::nullopt;
    const juce::File file (toJuceString (*path));
    return file.existsAsFile() ? std::optional<juce::File> (file) : std::nullopt;
}

void SampleLoader::syncClipAudio()
{
    std::set<std::uint64_t> wanted;
    for (const auto& track : session.project().tracks)
        if (track.kind == model::TrackKind::audio)
            for (const auto& clip : track.clips)
                wanted.insert (clip.asset.value);

    bool changed = false;
    for (auto it = clipAudio.begin(); it != clipAudio.end();)
    {
        if (wanted.count (it->first) == 0)
        {
            it = clipAudio.erase (it); // the published graph keeps its own reference until replaced
            changed = true;
        }
        else
            ++it;
    }

    for (const auto id : wanted)
    {
        auto& entry = clipAudio[id];
        const bool upToDate = entry.state.loading || entry.state.missing || entry.loadedRate == engineRate;
        if (upToDate)
            continue;

        entry.generation = ++clipGeneration;
        std::string name;
        const auto file = resolve (model::AssetId {id}, name);
        entry.state.name = name;
        changed = true;
        if (!file)
        {
            entry.state.missing = true;
            entry.state.loaded = false;
            entry.buffer.reset();
            error ("The audio file \"" + name + "\" is missing from the project folder.");
            continue;
        }

        entry.state.loading = true;
        decodeInBackground (*file, name, model::AssetId {id}, true,
                            [this, generation = entry.generation] (std::shared_ptr<Decoded> result)
                            { applyClip (generation, std::move (result)); });
    }

    if (changed)
    {
        session.refreshEngine();
        if (onClipAudioChanged)
            onClipAudioChanged();
    }
}

void SampleLoader::applyClip (std::uint64_t generation, std::shared_ptr<Decoded> result)
{
    const auto it = clipAudio.find (result->asset.value);
    if (it == clipAudio.end() || it->second.generation != generation)
        return; // no longer used, or superseded

    auto& entry = it->second;
    entry.state.loading = false;
    if (!result->error.empty())
    {
        entry.state.missing = true;
        entry.buffer.reset();
        error (result->error);
    }
    else
    {
        entry.buffer = std::shared_ptr<const instruments::SampleBuffer> (std::move (result->buffer));
        entry.loadedRate = result->rate;
        entry.state.loaded = true;
        entry.state.missing = false;
        entry.state.durationSeconds = result->durationSeconds;
        entry.state.overview = std::move (result->overview);
        entry.state.peaks = std::move (result->peaks);
    }

    session.refreshEngine();
    if (onClipAudioChanged)
        onClipAudioChanged();
    syncClipAudio(); // the device rate may have changed while decoding
}

void SampleLoader::syncSlot (std::size_t slot)
{
    auto& s = slots[slot];
    const auto wanted = wantedAsset (slot);
    const bool upToDate
        = wanted == s.requested
       && (!wanted.isValid() || s.loadedRate == engineRate || s.state.loading || s.state.missing);
    if (upToDate)
        return;

    const bool assetChanged = wanted != s.requested;
    s.requested = wanted;
    ++s.generation;

    if (!wanted.isValid())
    {
        if (assetChanged)
            publish (slot, nullptr);
        s.loadedRate = engineRate;
        setState (slot, {});
        return;
    }

    std::string name;
    const auto file = resolve (wanted, name);
    if (!file)
    {
        publish (slot, nullptr); // drum pads fall back to the factory sound; the sampler goes silent
        State missing;
        missing.name = name;
        missing.missing = true;
        setState (slot, std::move (missing));
        error ("The sample \"" + name + "\" is missing from the project folder.");
        return;
    }

    startLoad (slot, wanted, *file, name);
}

void SampleLoader::startLoad (std::size_t slot, model::AssetId asset, juce::File file, std::string name)
{
    State loading = slots[slot].state;
    loading.name = name;
    loading.loading = true;
    loading.missing = false;
    setState (slot, std::move (loading));

    decodeInBackground (std::move (file), std::move (name), asset, slot == samplerSlot,
                        [this, slot, jobGeneration = slots[slot].generation] (std::shared_ptr<Decoded> result)
                        { apply (slot, jobGeneration, std::move (result)); });
}

void SampleLoader::apply (std::size_t slot, std::uint64_t jobGeneration, std::shared_ptr<Decoded> result)
{
    if (jobGeneration != slots[slot].generation)
        return; // superseded by a newer request

    if (!result->error.empty())
    {
        publish (slot, nullptr);
        State failed;
        failed.name = result->name;
        failed.missing = true;
        setState (slot, std::move (failed));
        error (result->error);
        return;
    }

    publish (slot, std::move (result->buffer));
    slots[slot].loadedRate = result->rate;

    State loaded;
    loaded.name = result->name;
    loaded.loaded = true;
    loaded.durationSeconds = result->durationSeconds;
    loaded.overview = std::move (result->overview);
    setState (slot, std::move (loaded));
}

} // namespace ap::desktop
