#include "AudioRecorder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <system_error>
#include <vector>

namespace ap::desktop
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto commitInterval = std::chrono::milliseconds (500);
constexpr auto drainInterval = std::chrono::milliseconds (10);

void removeQuietly (const io::fs::path& path)
{
    std::error_code ec;
    io::fs::remove (path, ec);
}

std::string nextTakeName (const model::Project& project)
{
    int takes = 0;
    for (const auto& asset : project.assets)
        if (asset.name.rfind ("Take ", 0) == 0)
            ++takes;
    return "Take " + std::to_string (takes + 1);
}

std::string endMessage (engine::InputCapture::EndReason reason)
{
    using Reason = engine::InputCapture::EndReason;
    switch (reason)
    {
    case Reason::jumped:
        return "Audio recording stopped where playback jumped (loop or seek). The take was kept.";
    case Reason::overflow:
        return "Audio recording stopped: the disk could not keep up. The take was kept.";
    case Reason::noInput:
        return "Audio recording stopped: no audio input is open.";
    case Reason::deviceRestarted:
        return "Audio recording stopped because the audio device changed. The take was kept.";
    case Reason::none:
    case Reason::stopped:
        break;
    }
    return {};
}
} // namespace

AudioRecorder::AudioRecorder (Session& sessionToUse, engine::Engine& engineToUse)
    : session (sessionToUse)
    , engine (engineToUse)
{
}

AudioRecorder::~AudioRecorder()
{
    if (!recording)
        return;
    engine.getInputCapture().stop();
    endWriter();
    engine.getInputCapture().finish();
    if (!take.journalWritten)
        removeQuietly (take.file); // nowhere to put it back: it never began
}

model::TrackId AudioRecorder::targetTrack (model::ProjectDocument::Group& group, model::TrackId wanted) const
{
    if (const auto* track = group.project().findTrack (wanted);
        track != nullptr && track->kind == model::TrackKind::audio)
        return track->id;
    // The track was deleted while recording (or before a crash): the take gets a new one.
    if (const auto* added = group.perform (model::AddTrack {model::TrackKind::audio}))
        return std::get<model::AddTrack> (*added).created;
    return {};
}

std::optional<std::string> AudioRecorder::start (core::Samples latency)
{
    if (recording)
        return std::nullopt;
    const auto* track = session.project().findTrack (armed);
    if (track == nullptr || track->kind != model::TrackKind::audio)
        return "Arm an audio track to record audio.";

    // A unique file in the project's audio folder, named like imported files ("<id>-take.wav").
    const auto audioDir = session.assetRoot().audioDirectory();
    std::error_code ec;
    io::fs::create_directories (audioDir, ec);
    const auto stem = std::to_string (session.project().nextAssetId) + "-take";
    auto fileName = stem + ".wav";
    for (int n = 2; io::fs::exists (audioDir / fileName, ec); ++n)
        fileName = stem + "-" + std::to_string (n) + ".wav";

    if (!engine.getInputCapture().start (engine.getSampleRate()))
        return "Recording could not start. Try again.";

    take = Take {audioDir / fileName,
                 "audio/" + fileName,
                 nextTakeName (session.project()),
                 track->id,
                 std::max<core::Samples> (0, latency),
                 false};
    stopWriter.store (false);
    writerFailed.store (false);
    framesOnDisk.store (0);
    recording = true;
    writerThread = std::thread ([this] { writerLoop(); });
    return std::nullopt;
}

void AudioRecorder::writerLoop()
{
    auto& capture = engine.getInputCapture();
    std::vector<float> left (8192);
    std::vector<float> right (8192);
    auto lastCommit = Clock::now();

    for (;;)
    {
        // Read the flag before draining: once set, the capture has ended, so this last drain
        // empties the ring for good.
        const bool last = stopWriter.load (std::memory_order_acquire);

        const auto channels = capture.getNumChannels();
        if (!writer.isOpen() && !writerFailed.load() && channels > 0)
            if (writer.open (take.file, channels, capture.getSampleRate()))
                writerFailed.store (true);

        if (writer.isOpen())
        {
            while (const auto n = capture.ring().read ({left.data(), right.data()}, left.size()))
            {
                const std::array<const float*, 2> planar {left.data(), right.data()};
                if (!writer.write (planar.data(), n))
                {
                    writerFailed.store (true);
                    break;
                }
            }
            framesOnDisk.store (writer.framesWritten());
            if (Clock::now() - lastCommit >= commitInterval)
            {
                if (!writer.commit())
                    writerFailed.store (true);
                lastCommit = Clock::now();
            }
        }

        if (last)
            break;
        std::this_thread::sleep_for (drainInterval);
    }

    if (writer.isOpen() && !writer.close())
        writerFailed.store (true);
}

void AudioRecorder::endWriter()
{
    stopWriter.store (true, std::memory_order_release);
    if (writerThread.joinable())
        writerThread.join();
    recording = false;
}

void AudioRecorder::poll()
{
    if (!recording)
        return;
    auto& capture = engine.getInputCapture();
    const auto state = capture.getState();

    if (!take.journalWritten && state != engine::InputCapture::State::waiting && capture.getNumChannels() > 0)
    {
        // Where the take will start, whatever its length (see placeTake).
        const auto& project = session.project();
        const core::TempoMap map (project.tempoBpm, project.timeSignature, capture.getSampleRate());
        const auto placement = engine::placeTake (
            capture.getStartPosition(), static_cast<std::int64_t> (maxTakeSeconds * capture.getSampleRate()),
            take.latency, map);
        take.journalWritten = !io::writeJournal (io::journalPathFor (take.file),
                                                 {take.relativePath, take.track.value, placement.start,
                                                  placement.sourceOffset, take.name})
                                   .has_value();
    }

    if (writerFailed.load())
    {
        stop();
        notice ("Audio recording stopped: the take could not be written to disk.");
    }
    else if (state == engine::InputCapture::State::ended)
    {
        const auto reason = capture.getEndReason();
        stop();
        if (const auto message = endMessage (reason); !message.empty())
            notice (message);
    }
    else if (static_cast<double> (framesOnDisk.load()) >= maxTakeSeconds * capture.getSampleRate())
    {
        stop();
        notice ("Audio recording stopped at 10 minutes, the longest take. Press record again to continue.");
    }
}

bool AudioRecorder::stop()
{
    if (!recording)
        return false;

    auto& capture = engine.getInputCapture();
    capture.stop();
    endWriter();

    const auto frames = static_cast<std::int64_t> (writer.framesWritten());
    const auto startPosition = capture.getStartPosition();
    const auto rate = capture.getSampleRate();
    const bool began = capture.getNumChannels() > 0;
    capture.finish();

    const auto journal = io::journalPathFor (take.file);
    const auto discard = [&]
    {
        removeQuietly (take.file);
        removeQuietly (journal);
        return false;
    };
    if (!began || frames == 0)
        return discard();

    const auto& project = session.project();
    const auto placement = engine::placeTake (startPosition, frames, take.latency,
                                              core::TempoMap (project.tempoBpm, project.timeSignature, rate));
    if (placement.length == 0)
        return discard(); // everything was played before the song start (e.g. during the count-in)

    const bool added
        = session.performGroup ("Record audio",
                                [&] (model::ProjectDocument::Group& group)
                                {
                                    const auto track = targetTrack (group, take.track);
                                    const auto* asset
                                        = group.perform (model::AddAsset {take.relativePath, take.name});
                                    if (!track.isValid() || asset == nullptr)
                                        return;
                                    model::Clip clip;
                                    clip.start = placement.start;
                                    clip.length = placement.length;
                                    clip.asset = std::get<model::AddAsset> (*asset).created;
                                    clip.sourceOffset = placement.sourceOffset;
                                    (void)group.perform (model::AddClip {track, std::move (clip)});
                                });

    if (!added)
    {
        notice ("Could not add the recording to the timeline.");
        return discard();
    }
    removeQuietly (journal);
    return true;
}

std::optional<std::string> AudioRecorder::recoverInterrupted()
{
    const auto root = session.assetRoot();
    int recovered = 0;

    for (const auto& path : io::listJournals (root))
    {
        const auto journal = io::readJournal (path);
        const auto file = journal ? io::resolveAssetPath (root, journal->file) : std::nullopt;
        const auto& project = session.project();
        const bool alreadyInProject
            = journal
           && std::any_of (project.assets.begin(), project.assets.end(),
                           [&] (const model::Asset& asset) { return asset.relativePath == journal->file; });
        const auto info = file && !alreadyInProject ? io::repairRecording (*file) : std::nullopt;
        removeQuietly (path); // handled now, whatever the outcome: never ask twice
        if (!info)
            continue;

        const double seconds
            = static_cast<double> (info->frames) / info->sampleRate
            - static_cast<double> (journal->sourceOffset) / static_cast<double> (core::flicksPerSecond);
        const auto length = std::min (
            static_cast<core::Ticks> (std::floor (seconds * project.tempoBpm
                                                  * static_cast<double> (core::ticksPerQuarterNote) / 60.0)),
            model::maxTimelineTicks - journal->start);
        if (length < model::Clip::minLength)
        {
            removeQuietly (*file);
            continue;
        }

        const bool added = session.performGroup (
            "Recover recording",
            [&] (model::ProjectDocument::Group& group)
            {
                const auto track = targetTrack (group, model::TrackId {journal->track});
                const auto* asset = group.perform (model::AddAsset {journal->file, journal->name});
                if (!track.isValid() || asset == nullptr)
                    return;
                model::Clip clip;
                clip.start = journal->start;
                clip.length = length;
                clip.asset = std::get<model::AddAsset> (*asset).created;
                clip.sourceOffset = journal->sourceOffset;
                (void)group.perform (model::AddClip {track, std::move (clip)});
            });
        if (added)
            ++recovered;
    }

    if (recovered == 0)
        return std::nullopt;
    return recovered == 1 ? std::string ("Recovered a recording that was interrupted when the app closed.")
                          : "Recovered " + std::to_string (recovered)
                                + " recordings that were interrupted when the app closed.";
}

void AudioRecorder::notice (const std::string& message)
{
    if (onNotice)
        onNotice (message);
}

} // namespace ap::desktop
