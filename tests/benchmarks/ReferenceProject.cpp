// CPU budget of the reference project (plan §3.2): 8 tracks, 16 synth voices and every effect
// switched on, rendered in 128-sample blocks at 48 kHz. The worst blocks matter, not the average:
// a single late block is an audible dropout.
//
//   cmake --preset release && cmake --build --preset release --target ap_benchmarks
//   ./build/release/tests/ap_benchmarks
//
// Exits with failure when the 99.9th percentile block takes more than half of its real-time
// budget. Times are the thread's CPU time (POSIX), so other processes do not count; the maximum
// is reported too.

#include "TimelineHelpers.h"
#include "ap/core/ScopedNoDenormals.h"
#include "ap/engine/Engine.h"
#include "ap/model/ClipEditing.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#if !defined(_WIN32)
#include <time.h>
#endif

using namespace ap;

namespace
{
// CPU time of this thread: what the engine itself costs, without the time the OS gave the core to
// other processes (wall time on a busy laptop measures the laptop, not the engine).
double now()
{
#if !defined(_WIN32)
    timespec t {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<double> (t.tv_sec) + static_cast<double> (t.tv_nsec) * 1.0e-9;
#else
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

constexpr double rate = 48000.0;
constexpr int block = 128;
constexpr double seconds = 30.0;
constexpr double budget = 0.5; // of the block's duration

model::Project referenceProject()
{
    model::ProjectDocument doc;
    doc.perform (model::AddTrack {model::InstrumentKind::drums});
    doc.perform (model::AddTrack {model::InstrumentKind::synth});
    doc.perform (model::AddTrack {model::InstrumentKind::sampler});
    for (int i = 0; i < 5; ++i)
        doc.perform (model::AddTrack {model::TrackKind::audio});
    doc.perform (model::AddAsset {"audio/1-loop.wav", "loop.wav"});

    const auto bars = static_cast<core::Ticks> (seconds / 2.0) + 1; // 2 s per bar at 120 BPM
    auto beat = model::makePatternClip (0, bars * model::DrumKit::patternLength);
    for (std::size_t step = 0; step < model::DrumKit::numSteps; ++step)
        for (std::size_t pad = 0; pad < 4; ++pad)
            beat = model::withDrumStep (beat, pad, step, true);
    const auto& tracks = doc.project().tracks;
    doc.perform (model::AddClip {tracks[0].id, beat});

    model::Clip sampler;
    sampler.length = bars * 4 * core::ticksPerQuarterNote;
    sampler.loopLength = core::ticksPerQuarterNote;
    sampler.notes = {{0, 240, 60, 0.8f}, {480, 240, 67, 0.8f}};
    doc.perform (model::AddClip {tracks[2].id, sampler});

    for (std::size_t t = 3; t < tracks.size(); ++t)
    {
        model::Clip audio;
        audio.length = bars * 4 * core::ticksPerQuarterNote;
        audio.asset = doc.project().assets[0].id;
        doc.perform (model::AddClip {tracks[t].id, audio});
    }

    for (const auto& track : doc.project().tracks)
        for (std::size_t e = 0; e < params::numEffects; ++e)
        {
            auto state = model::defaultEffectState (static_cast<params::EffectKind> (e));
            state.enabled = true;
            doc.perform (model::SetTrackEffect {track.id, static_cast<params::EffectKind> (e), state});
        }
    return doc.project();
}
} // namespace

int main()
{
    const core::ScopedNoDenormals noDenormals;
    auto engine = std::make_unique<engine::Engine>();

    auto source = std::make_shared<instruments::SampleBuffer>();
    source->sampleRate = rate;
    source->channels.assign (2, std::vector<float> (static_cast<std::size_t> ((seconds + 5.0) * rate)));
    for (std::size_t i = 0; i < source->channels[0].size(); ++i)
        source->channels[0][i] = source->channels[1][i]
            = 0.2f * std::sin (static_cast<float> (i) * 0.01f) * std::sin (static_cast<float> (i) * 0.0003f);

    test::publish (*engine, referenceProject(), [&] (model::AssetId) { return source; });
    engine->prepare (rate, block);
    for (const int note : {36, 40, 43, 47, 48, 52, 55, 59, 60, 64, 67, 71, 72, 76, 79, 83})
        engine->sendNoteFromUi (
            {instruments::NoteEvent::Type::noteOn, static_cast<std::uint8_t> (note), 0.7f}); // 16 voices
    engine->setLiveInstrument (engine::Engine::LiveInstrument::synth);
    engine->getTransport().requestPlay();

    std::array<std::vector<float>, 2> output {std::vector<float> (block), std::vector<float> (block)};
    std::array<float*, 2> channels {output[0].data(), output[1].data()};
    const auto blocks = static_cast<std::size_t> (seconds * rate / block);
    std::vector<double> times;
    times.reserve (blocks);
    std::size_t worstBlock = 0;
    double worstTime = 0.0;

    for (std::size_t b = 0; b < blocks; ++b)
    {
        const double start = now();
        engine->process ({channels.data(), 2, block});
        const double end = now();
        if (b < 100) // let caches and the effect chains warm up
            continue;
        const double t = end - start;
        times.push_back (t);
        if (t > worstTime)
        {
            worstTime = t;
            worstBlock = b;
        }
    }

    const double blockSeconds = block / rate;
    std::sort (times.begin(), times.end());
    const auto percentile = [&times] (double p)
    {
        return times[std::min (times.size() - 1,
                               static_cast<std::size_t> (p * static_cast<double> (times.size())))];
    };
    double total = 0.0;
    for (const double t : times)
        total += t;

    const double mean = total / static_cast<double> (times.size()) / blockSeconds;
    const double p99 = percentile (0.99) / blockSeconds;
    const double p999 = percentile (0.999) / blockSeconds;
    const double worst = times.back() / blockSeconds;
    std::printf ("Reference project, %d samples at %.0f Hz (%zu blocks)\n", block, rate, times.size());
    std::printf ("  mean   %5.1f %% of the block\n", 100.0 * mean);
    std::printf ("  p99    %5.1f %%\n", 100.0 * p99);
    std::printf ("  p99.9  %5.1f %%  (budget %.0f %%)\n", 100.0 * p999, 100.0 * budget);
    std::printf ("  worst  %5.1f %%  (block %zu)\n", 100.0 * worst, worstBlock);
    if (worst > budget)
        std::printf ("  note: the worst block exceeded the budget\n");
    return p999 <= budget ? 0 : 1;
}
