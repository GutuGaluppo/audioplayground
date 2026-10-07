// Exporting the song as a WAV file in the browser host. The same render as the desktop's (the core's
// SongRender, then resampling, loudness and the WAV encoder), taken a slice at a time.

#include "WebHost.h"
#include "ap/dsp/Loudness.h"
#include "ap/dsp/Resampler.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ap::web
{
namespace
{
constexpr std::size_t maxExportBytes
    = 640u * 1024u * 1024u;            // what the render, the copy and the file may take together
constexpr double secondsPerStep = 1.0; // how much music one step renders
} // namespace

std::string WebHost::exportBegin (int format, int rate)
{
    exportCancel();
    exported = {};
    if (format < 0 || format > 2 || rate < 8000 || rate > 192000)
        return "The export format is not supported.";

    const auto project = editor.project(); // a copy: the song may change while it renders
    const core::TempoMap map (project.tempoBpm, project.timeSignature, sampleRate);
    const auto length = map.ticksToSamples (engine::songEnd (project));
    if (length <= 0)
        return "There is nothing to export yet: add a clip to the timeline.";
    const double seconds = static_cast<double> (length) / sampleRate;
    if (seconds > maxExportSeconds)
        return "The song is longer than 30 minutes, the longest export.";

    // The render (two channels of floats), its resampled copy and the file all exist at once.
    const double bytesPerSample = format == 0 ? 2.0 : format == 1 ? 3.0 : 4.0;
    const double estimate = seconds * 2.0 * (4.0 * sampleRate + 4.0 * rate + bytesPerSample * rate);
    if (estimate > static_cast<double> (maxExportBytes))
        return "The song is too long to export in the browser at this sample rate. Try a lower one.";

    // Audio the song lists but the page could not provide: silent in the export, and said so.
    std::set<std::string> lost;
    for (const auto& asset : project.assets)
        if (audio.count (asset.relativePath) == 0 && project.assetUse (asset.id).any())
            lost.insert (asset.relativePath);
    exported.missingAudio = static_cast<int> (lost.size());

    auto next = std::make_unique<ExportJob>();
    next->format = format == 0 ? io::WavFormat::pcm16
                 : format == 1 ? io::WavFormat::pcm24
                               : io::WavFormat::float32;
    next->rate = static_cast<std::uint32_t> (rate);
    next->engine = std::make_unique<engine::Engine>();
    auto& renderEngine = *next->engine;
    engine::configureEngine (
        renderEngine, project, 1,
        [this, &project] (model::AssetId id) -> std::shared_ptr<const instruments::SampleBuffer>
        {
            const auto* asset = project.findAsset (id);
            const auto it = asset != nullptr ? audio.find (asset->relativePath) : audio.end();
            return it != audio.end() ? it->second.buffer : nullptr;
        });
    if (const auto* asset
        = project.samplerAsset.isValid() ? project.findAsset (project.samplerAsset) : nullptr)
        if (const auto it = audio.find (asset->relativePath); it != audio.end())
            renderEngine.loadSamplerSample (std::make_unique<instruments::SampleBuffer> (*it->second.buffer));
    for (std::size_t pad = 0; pad < model::DrumKit::numPads; ++pad)
        if (const auto* asset = project.drums.pads[pad].sample.isValid()
                                  ? project.findAsset (project.drums.pads[pad].sample)
                                  : nullptr)
            if (const auto it = audio.find (asset->relativePath); it != audio.end())
                renderEngine.getDrums().loadPadSample (
                    static_cast<int> (pad), std::make_unique<instruments::SampleBuffer> (*it->second.buffer));

    next->render.emplace (renderEngine, engine::SongRenderSettings {sampleRate, length});
    job = std::move (next);
    return {};
}

int WebHost::exportStep()
{
    if (!job)
        return -1;
    const int blocks = std::max (1, static_cast<int> (std::ceil (secondsPerStep * sampleRate / 512.0)));
    if (job->render->step (blocks))
        return static_cast<int> (850.0 * job->render->progress());

    // Rendered: now what the desktop does with it (resample, measure, write).
    auto rendered = job->render->finish();
    const auto rate = job->rate;
    const auto format = job->format;
    job.reset();

    if (std::abs (static_cast<double> (rate) - sampleRate) > 0.5)
        for (auto& channel : rendered)
            channel = dsp::resample (channel, sampleRate, static_cast<double> (rate));
    exported.seconds = static_cast<double> (rendered[0].size()) / static_cast<double> (rate);
    const auto loudness = dsp::measureLoudness (rendered, static_cast<double> (rate));
    exported.lufs = loudness.integratedLufs;
    exported.truePeak = loudness.truePeakDb;

    auto encoded = io::encodeWav (rendered, rate, format);
    if (const auto* error = std::get_if<io::IoError> (&encoded))
    {
        exported.error = error->message;
        return -1;
    }
    exported.bytes = std::move (std::get<std::vector<std::uint8_t>> (encoded));
    return 1001;
}

void WebHost::exportCancel()
{
    if (job && job->render)
        job->render->abort();
    job.reset();
}

void WebHost::exportRelease()
{
    exportCancel();
    exported = {};
}

} // namespace ap::web
