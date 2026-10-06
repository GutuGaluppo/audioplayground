#include "Exporter.h"

#include "AppPaths.h"
#include "ap/dsp/Resampler.h"
#include "ap/engine/Export.h"

#include <cmath>
#include <map>

namespace ap::desktop
{

Exporter::Exporter (Session& sessionToUse, SampleLoader& samplesToUse)
    : session (sessionToUse)
    , samples (samplesToUse)
{
}

Exporter::~Exporter()
{
    *alive = false;
    cancel();
    join();
}

void Exporter::join()
{
    if (worker.joinable())
        worker.join();
}

void Exporter::chooseAndExport (Options options)
{
    if (isRunning())
        return;
    const auto name = juce::File::createLegalFileName (juce::String (session.project().name)) + ".wav";
    chooser = std::make_unique<juce::FileChooser> (
        "Export as WAV", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (name),
        "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, options] (const juce::FileChooser& fc)
                          {
                              auto target = fc.getResult();
                              if (target == juce::File())
                                  return; // cancelled
                              if (!target.hasFileExtension ("wav"))
                                  target = target.withFileExtension ("wav");
                              if (const auto error = start (target, options); error && onFinished)
                                  onFinished ({*error, {}, 0.0, {}});
                          });
}

std::optional<std::string> Exporter::start (const juce::File& target, Options options)
{
    if (isRunning())
        return "An export is already running.";
    join();

    // Everything the export engine needs, copied now: the user keeps editing meanwhile.
    const auto project = session.project();
    const double rate = samples.getEngineRate();
    std::map<std::uint64_t, std::shared_ptr<const instruments::SampleBuffer>> clipAudio;
    for (const auto& [id, audio] : samples.getClipAudio())
    {
        if (audio.state.loading)
            return std::string ("Audio is still loading. Try again in a moment.");
        clipAudio.emplace (id, audio.buffer);
    }
    std::array<std::shared_ptr<const instruments::SampleBuffer>, SampleLoader::numSlots> slotBuffers;
    for (std::size_t slot = 0; slot < SampleLoader::numSlots; ++slot)
        slotBuffers[slot] = samples.getSlotBuffer (slot);

    const core::TempoMap map (project.tempoBpm, project.timeSignature, rate);
    const auto length = map.ticksToSamples (engine::songEnd (project));
    if (length <= 0)
        return std::string ("There is nothing to export yet: add a clip to the timeline.");
    if (static_cast<double> (length) / rate > maxSongSeconds)
        return std::string ("The song is longer than 30 minutes, the longest export.");

    cancelled.store (false);
    progress.store (0.0);
    running.store (true);
    const auto path = toPath (target);
    const auto fileName = target.getFileName().toStdString();

    worker = std::thread (
        [this, project, rate, clipAudio = std::move (clipAudio), slotBuffers, length, options, path, fileName,
         stillAlive = alive]
        {
            Result result;
            result.fileName = fileName;

            auto engine = std::make_unique<engine::Engine>();
            engine::configureEngine (
                *engine, project, 1,
                [&clipAudio] (model::AssetId asset) -> std::shared_ptr<const instruments::SampleBuffer>
                {
                    const auto it = clipAudio.find (asset.value);
                    return it != clipAudio.end() ? it->second : nullptr;
                });
            if (slotBuffers[SampleLoader::samplerSlot] != nullptr)
                engine->loadSamplerSample (
                    std::make_unique<instruments::SampleBuffer> (*slotBuffers[SampleLoader::samplerSlot]));
            for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
                if (const auto& buffer = slotBuffers[SampleLoader::padSlot (pad)])
                    engine->getDrums().loadPadSample (static_cast<int> (pad),
                                                      std::make_unique<instruments::SampleBuffer> (*buffer));

            auto audio = engine::renderSong (*engine, {rate, length},
                                             [this] (double done)
                                             {
                                                 progress.store (0.85 * done);
                                                 return !cancelled.load();
                                             });
            engine.reset();

            if (!audio)
                result.error = "cancelled";
            else
            {
                if (static_cast<double> (options.sampleRate) != rate)
                    for (auto& channel : *audio)
                        channel = dsp::resample (channel, rate, static_cast<double> (options.sampleRate));
                progress.store (0.9);
                result.seconds
                    = static_cast<double> ((*audio)[0].size()) / static_cast<double> (options.sampleRate);
                result.loudness = dsp::measureLoudness (*audio, static_cast<double> (options.sampleRate));
                progress.store (0.95);
                if (cancelled.load())
                    result.error = "cancelled";
                else if (const auto error = io::writeWav (path, *audio, options.sampleRate, options.format))
                    result.error = error->message;
            }
            progress.store (1.0);

            juce::MessageManager::callAsync (
                [this, stillAlive, result]
                {
                    if (!*stillAlive)
                        return;
                    running.store (false);
                    join();
                    if (onFinished)
                        onFinished (result);
                });
        });
    return std::nullopt;
}

} // namespace ap::desktop
