#include "SampleLoader.h"

#include "AppPaths.h"
#include "ap/dsp/Resampler.h"

#include <cmath>

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
    bool missing = false;
    model::AssetId asset;
    std::string name;
    double durationSeconds = 0.0;
    std::unique_ptr<instruments::SampleBuffer> buffer;
    std::vector<float> overview;
    double rate = 0.0;
};

SampleLoader::SampleLoader (Session& sessionToUse, engine::Engine& engineToUse)
    : session (sessionToUse)
    , engine (engineToUse)
{
}

SampleLoader::~SampleLoader()
{
    *alive = false;
    pool.removeAllJobs (true, 10000);
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

void SampleLoader::chooseAndImport (std::size_t slot)
{
    if (slot >= numSlots)
        return;

    chooser = std::make_unique<juce::FileChooser> (
        "Choose a sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory), supportedPattern);

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, slot] (const juce::FileChooser& fc)
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

            const auto relative = "audio/" + target.getFileName().toStdString();
            const auto displayName = source.getFileName().toStdString();
            if (!session.perform (model::AddAsset {relative, displayName}))
            {
                target.deleteFile();
                return error ("Could not add the file to the project.");
            }
            // sync() (via the session's onChanged) loads it once the slot points at it.
            assign (slot, session.project().assets.back().id);
        });
}

void SampleLoader::sync (double engineSampleRate)
{
    if (engineSampleRate > 0.0)
        engineRate = engineSampleRate;

    for (std::size_t slot = 0; slot < numSlots; ++slot)
        syncSlot (slot);
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

    const auto* asset = session.project().findAsset (wanted);
    const auto path
        = asset != nullptr ? io::resolveAssetPath (session.assetRoot(), asset->relativePath) : std::nullopt;
    const auto name = asset != nullptr ? asset->name : std::string ("Sample");
    const juce::File file = path ? juce::File (toJuceString (*path)) : juce::File();

    if (!path || !file.existsAsFile())
    {
        publish (slot, nullptr); // drum pads fall back to the factory sound; the sampler goes silent
        State missing;
        missing.name = name;
        missing.missing = true;
        setState (slot, std::move (missing));
        error ("The sample \"" + name + "\" is missing from the project folder.");
        return;
    }

    startLoad (slot, wanted, file, name);
}

void SampleLoader::startLoad (std::size_t slot, model::AssetId asset, juce::File file, std::string name)
{
    State loading = slots[slot].state;
    loading.name = name;
    loading.loading = true;
    loading.missing = false;
    setState (slot, std::move (loading));

    const auto jobGeneration = slots[slot].generation;
    const auto rate = engineRate;
    const bool wantOverview = slot == samplerSlot;
    pool.addJob (
        [this, stillAlive = alive, slot, jobGeneration, rate, wantOverview, asset, file, name]
        {
            auto result = std::make_shared<Decoded>();
            result->asset = asset;
            result->name = name;
            result->rate = rate;

            juce::AudioFormatManager formats;
            registerFormats (formats);
            const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            const auto fail = [&result] (std::string message) { result->error = std::move (message); };

            if (reader == nullptr)
                fail ("\"" + name + "\" could not be read. It may be damaged or in an unsupported format.");
            else if (reader->numChannels < 1 || reader->numChannels > 32 || reader->sampleRate < 8000.0
                     || reader->sampleRate > 384000.0)
                fail ("\"" + name + "\" has an unsupported format.");
            else if (reader->lengthInSamples <= 0
                     || static_cast<double> (reader->lengthInSamples) / reader->sampleRate
                            > maxDurationSeconds)
                fail ("\"" + name + "\" is empty or longer than 10 minutes.");
            else
            {
                const int channels = static_cast<int> (std::min<unsigned int> (reader->numChannels, 2));
                const auto length = static_cast<int> (reader->lengthInSamples);
                juce::AudioBuffer<float> decoded (channels, length);
                if (!reader->read (&decoded, 0, length, 0, true, channels > 1))
                    fail ("\"" + name + "\" could not be read completely.");
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
                        result->overview = computeOverview (buffer->channels, overviewPoints);
                    result->durationSeconds = static_cast<double> (length) / reader->sampleRate;
                    result->buffer = std::move (buffer);
                }
            }

            juce::MessageManager::callAsync (
                [this, stillAlive, slot, jobGeneration, result]
                {
                    if (*stillAlive)
                        apply (slot, jobGeneration, result);
                });
        });
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
