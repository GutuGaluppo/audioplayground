#pragma once

#include "SampleLoader.h"
#include "Session.h"
#include "ap/dsp/Loudness.h"
#include "ap/io/WavExport.h"

#include <atomic>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>
#include <thread>

namespace ap::desktop
{

// Exports the song as a WAV file (Task 025) without touching playback: a second engine, set up
// exactly like the live one (same project, decoded audio, samples and effects), renders the song
// and its effect tails on a background thread at the live engine's rate. The result is resampled
// to the chosen rate, measured (integrated loudness, peaks) and written atomically.
// Message thread API.
class Exporter
{
public:
    static constexpr double maxSongSeconds = 30.0 * 60.0;

    struct Options
    {
        io::WavFormat format = io::WavFormat::pcm24;
        std::uint32_t sampleRate = 48000;
    };

    struct Result
    {
        std::string error; // empty on success
        std::string fileName;
        double seconds = 0.0;
        dsp::LoudnessReport loudness;
    };

    Exporter (Session& session, SampleLoader& samples);
    ~Exporter(); // cancels a running export and waits for it

    // Asks where to save, then exports. Ignored while an export runs.
    void chooseAndExport (Options options);

    // Exports to the given file (also used by tests). Returns an error message if it cannot start.
    [[nodiscard]] std::optional<std::string> start (const juce::File& target, Options options);

    void cancel() noexcept { cancelled.store (true); }
    [[nodiscard]] bool isRunning() const noexcept { return running.load(); }
    [[nodiscard]] double getProgress() const noexcept { return progress.load(); }

    // Message thread, when an export finishes, fails or is cancelled (error "cancelled").
    std::function<void (const Result&)> onFinished;

private:
    void join();

    Session& session;
    SampleLoader& samples;
    std::unique_ptr<juce::FileChooser> chooser;
    std::thread worker;
    std::atomic<bool> running {false};
    std::atomic<bool> cancelled {false};
    std::atomic<double> progress {0.0};
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace ap::desktop
