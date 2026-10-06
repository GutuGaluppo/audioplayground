#include "TempDirectory.h"
#include "TestAudio.h"
#include "ap/core/AudioRing.h"
#include "ap/engine/Engine.h"
#include "ap/engine/InputCapture.h"
#include "ap/io/Recordings.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

using namespace ap;
using engine::InputCapture;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

// A device input whose samples count up, so any captured frame tells where it came from.
struct CountingInput
{
    explicit CountingInput (int channels = 2)
        : data (static_cast<std::size_t> (channels), std::vector<float> (block))
    {
        for (auto& channel : data)
            pointers.push_back (channel.data());
    }

    core::InputBlock next()
    {
        for (std::size_t ch = 0; ch < data.size(); ++ch)
            for (int i = 0; i < block; ++i)
                data[ch][static_cast<std::size_t> (i)]
                    = static_cast<float> (counter + i) + (ch == 0 ? 0.0f : 0.5f);
        counter += block;
        return {pointers.data(), static_cast<int> (pointers.size()), block};
    }

    std::vector<std::vector<float>> data;
    std::vector<const float*> pointers;
    int counter = 0;
};

struct Rig
{
    Rig() { engine.prepare (rate, block); }

    void run (int blocks, CountingInput& input)
    {
        for (int b = 0; b < blocks; ++b)
            engine.process (output.block(), input.next());
    }

    std::vector<float> drainLeft (std::vector<float>* right = nullptr)
    {
        std::vector<float> left;
        std::array<float, 64> l {};
        std::array<float, 64> r {};
        while (const auto n = engine.getInputCapture().ring().read ({l.data(), r.data()}, l.size()))
        {
            left.insert (left.end(), l.begin(), l.begin() + static_cast<std::ptrdiff_t> (n));
            if (right != nullptr)
                right->insert (right->end(), r.begin(), r.begin() + static_cast<std::ptrdiff_t> (n));
        }
        return left;
    }

    engine::Engine engine;
    test::TestBuffer output {2, block};
};
} // namespace

// --- AudioRing -------------------------------------------------------------------------------

TEST_CASE ("AudioRing keeps frames in order across the wrap and refuses what does not fit", "[recording]")
{
    core::AudioRing ring;
    ring.allocate (100); // rounded up to 128
    REQUIRE (ring.capacity() == 128);

    std::vector<float> left (96);
    std::vector<float> right (96);
    std::array<float*, 2> out {left.data(), right.data()};

    float next = 0.0f;
    float expected = 0.0f;
    for (int round = 0; round < 20; ++round)
    {
        std::array<float, 96> l {};
        std::array<float, 96> r {};
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            l[i] = next;
            r[i] = -next;
            next += 1.0f;
        }
        const std::array<const float*, 2> in {l.data(), r.data()};
        REQUIRE (ring.write (in.data(), 2, 0, 96));
        CHECK_FALSE (ring.write (in.data(), 2, 0, 33)); // 96 + 33 > 128: nothing written
        CHECK (ring.available() == 96);

        REQUIRE (ring.read (out, 96) == 96);
        for (std::size_t i = 0; i < 96; ++i)
        {
            REQUIRE (left[i] == expected);
            REQUIRE (right[i] == -expected);
            expected += 1.0f;
        }
    }
    CHECK (ring.totalWritten() == 20 * 96);
}

TEST_CASE ("AudioRing writes a mono source to both channels", "[recording]")
{
    core::AudioRing ring;
    ring.allocate (8);
    const std::array<float, 3> mono {1.0f, 2.0f, 3.0f};
    const std::array<const float*, 1> in {mono.data()};
    REQUIRE (ring.write (in.data(), 1, 1, 2));

    std::array<float, 2> l {};
    std::array<float, 2> r {};
    REQUIRE (ring.read ({l.data(), r.data()}, 8) == 2);
    CHECK (l == std::array<float, 2> {2.0f, 3.0f});
    CHECK (r == l);
}

TEST_CASE ("AudioRing delivers every frame in order across threads", "[recording][concurrency][stress]")
{
    core::AudioRing ring;
    ring.allocate (1024);
    constexpr int total = 500'000;

    std::thread producer (
        [&ring]
        {
            std::array<float, 37> chunk {};
            const std::array<const float*, 1> in {chunk.data()};
            for (int written = 0; written < total;)
            {
                const auto n = std::min<int> (static_cast<int> (chunk.size()), total - written);
                for (int i = 0; i < n; ++i)
                    chunk[static_cast<std::size_t> (i)] = static_cast<float> (written + i);
                if (ring.write (in.data(), 1, 0, static_cast<std::size_t> (n)))
                    written += n;
                else
                    std::this_thread::yield();
            }
        });

    std::array<float, 100> l {};
    std::array<float, 100> r {};
    int expected = 0;
    bool inOrder = true;
    while (expected < total)
    {
        const auto n = ring.read ({l.data(), r.data()}, l.size());
        if (n == 0)
            std::this_thread::yield();
        for (std::size_t i = 0; i < n; ++i)
            inOrder = inOrder && l[i] == static_cast<float> (expected++);
    }
    producer.join();
    CHECK (inOrder);
}

// --- InputCapture ----------------------------------------------------------------------------

TEST_CASE ("Capture waits for playback, then records the input from the first played block", "[recording]")
{
    Rig rig;
    CountingInput input;
    auto& capture = rig.engine.getInputCapture();
    REQUIRE (capture.start (rate));
    CHECK_FALSE (capture.start (rate)); // one take at a time

    rig.run (3, input); // stopped: nothing captured
    CHECK (capture.getState() == InputCapture::State::waiting);

    rig.engine.getTransport().requestPlay();
    rig.run (10, input);
    CHECK (capture.getState() == InputCapture::State::capturing);
    CHECK (capture.getStartPosition() == 0);
    CHECK (capture.getNumChannels() == 2);

    capture.stop();
    rig.run (2, input); // after stop() nothing more is written
    CHECK (capture.getState() == InputCapture::State::ended);
    CHECK (capture.getEndReason() == InputCapture::EndReason::stopped);

    std::vector<float> right;
    const auto left = rig.drainLeft (&right);
    REQUIRE (left.size() == 10 * block);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        REQUIRE (left[i] == static_cast<float> (3 * block + static_cast<int> (i)));
        REQUIRE (right[i] == left[i] + 0.5f);
    }

    capture.finish();
    CHECK (capture.getState() == InputCapture::State::idle);
    CHECK (capture.start (rate));
}

TEST_CASE ("Capture includes the count-in, starting at a negative position", "[recording]")
{
    Rig rig;
    CountingInput input (1);
    rig.engine.getTransport().setCountInBars (1);
    REQUIRE (rig.engine.getInputCapture().start (rate));
    rig.engine.getTransport().requestPlay();
    rig.run (4, input);

    const core::TempoMap map (120.0, {}, rate);
    CHECK (rig.engine.getInputCapture().getStartPosition() == -map.ticksToSamples (4 * 960));
    CHECK (rig.engine.getInputCapture().getNumChannels() == 1);
}

TEST_CASE ("Capture ends at a loop wrap, keeping exactly the frames before it", "[recording]")
{
    Rig rig;
    CountingInput input;
    auto& transport = rig.engine.getTransport();
    transport.setLoop (true, 0, 960); // one beat: 24 000 samples at 120 BPM
    REQUIRE (rig.engine.getInputCapture().start (rate));
    transport.requestPlay();
    rig.run (100, input); // 25 600 samples: wraps once

    CHECK (rig.engine.getInputCapture().getEndReason() == InputCapture::EndReason::jumped);
    CHECK (rig.drainLeft().size() == 24000);
}

TEST_CASE ("Capture ends when the reader falls behind instead of dropping frames silently", "[recording]")
{
    Rig rig;
    CountingInput input;
    REQUIRE (rig.engine.getInputCapture().start (rate));
    rig.engine.getTransport().requestPlay();
    const auto capacity = static_cast<int> (rig.engine.getInputCapture().ring().capacity());
    rig.run (capacity / block + 2, input);

    CHECK (rig.engine.getInputCapture().getEndReason() == InputCapture::EndReason::overflow);
    CHECK (rig.drainLeft().size() == static_cast<std::size_t> (capacity / block * block));
}

TEST_CASE ("Capture ends without an input or when the device restarts", "[recording]")
{
    SECTION ("no input")
    {
        Rig rig;
        REQUIRE (rig.engine.getInputCapture().start (rate));
        rig.engine.getTransport().requestPlay();
        rig.engine.process (rig.output.block());
        CHECK (rig.engine.getInputCapture().getEndReason() == InputCapture::EndReason::noInput);
    }
    SECTION ("device restart")
    {
        Rig rig;
        CountingInput input;
        REQUIRE (rig.engine.getInputCapture().start (rate));
        rig.engine.getTransport().requestPlay();
        rig.run (2, input);
        rig.engine.releaseResources();
        CHECK (rig.engine.getInputCapture().getEndReason() == InputCapture::EndReason::deviceRestarted);
    }
}

TEST_CASE ("Capture is the same whatever the device block size", "[recording]")
{
    const int size = GENERATE (1, 64, 300, 1024);
    CAPTURE (size);

    engine::Engine engine;
    engine.prepare (rate, 256); // larger device blocks are split internally
    std::vector<float> in (static_cast<std::size_t> (size));
    const float* pointer = in.data();
    test::TestBuffer output (2, size);
    REQUIRE (engine.getInputCapture().start (rate));
    engine.getTransport().requestPlay();

    int counter = 0;
    for (int b = 0; b < 4096 / size + 1; ++b)
    {
        for (auto& sample : in)
            sample = static_cast<float> (counter++);
        engine.process (output.block(), {&pointer, 1, size});
    }
    engine.getInputCapture().stop();

    std::array<float, 128> l {};
    std::array<float, 128> r {};
    int expected = 0;
    bool inOrder = true;
    while (const auto n = engine.getInputCapture().ring().read ({l.data(), r.data()}, l.size()))
        for (std::size_t i = 0; i < n; ++i)
            inOrder = inOrder && l[i] == static_cast<float> (expected++);
    CHECK (inOrder);
    CHECK (expected == counter);
}

TEST_CASE ("The input meter reports the input peak and resets", "[recording]")
{
    Rig rig;
    std::vector<float> in (block, 0.25f);
    in[10] = -0.75f;
    const float* pointer = in.data();
    rig.engine.process (rig.output.block(), {&pointer, 1, block});
    CHECK (rig.engine.consumeInputPeak() == 0.75f);
    CHECK (rig.engine.consumeInputPeak() == 0.0f);

    // The input is never monitored.
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < block; ++i)
            REQUIRE (rig.output.block().channels[ch][i] == 0.0f);
}

// --- Placement -------------------------------------------------------------------------------

TEST_CASE ("A placed take plays every frame where the musician heard it", "[recording]")
{
    const double tempo = GENERATE (60.0, 97.0, 120.0, 173.0);
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    const core::Samples latency = GENERATE (0, 1, 333, 8820);
    const core::Samples position = GENERATE (-96000, -5, 0, 7, 123457);
    CAPTURE (tempo, sampleRate, latency, position);

    const core::TempoMap map (tempo, {}, sampleRate);
    const std::int64_t frames = 480000;
    const auto take = engine::placeTake (position, frames, latency, map);
    REQUIRE (take.length >= model::Clip::minLength);
    REQUIRE (take.start >= 0);
    REQUIRE (take.sourceOffset >= 0);

    // The timeline player plays frame (sample - clipStart + firstFrame) at a timeline sample.
    const auto clipStart = map.ticksToSamples (take.start);
    const auto firstFrame = core::flicksToFrames (take.sourceOffset, sampleRate);
    const auto heard = position - latency; // where frame 0 belongs
    CHECK (clipStart - firstFrame == heard);
    CHECK (clipStart >= std::max<core::Samples> (0, heard));
    CHECK (clipStart - std::max<core::Samples> (0, heard)
           <= static_cast<core::Samples> (map.getSamplesPerTick()) + 1);
    CHECK (map.ticksToSamples (take.start + take.length) <= heard + frames); // never past the audio
}

TEST_CASE ("A take recorded entirely before time zero is empty", "[recording]")
{
    const core::TempoMap map (120.0, {}, rate);
    CHECK (engine::placeTake (-100000, 50000, 0, map).length == 0);
    CHECK (engine::placeTake (1000, 2000, 5000, map).length == 0);
    CHECK (engine::placeTake (0, 10, 0, map).length == 0); // shorter than a clip can be
}

// --- Files -----------------------------------------------------------------------------------

namespace
{
std::vector<char> readAll (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (in), {}};
}
} // namespace

TEST_CASE ("RecordingWriter writes a float WAV whose header matches the data", "[recording][io]")
{
    test::TempDirectory dir;
    const auto path = dir.path() / "take.wav";
    io::RecordingWriter writer;
    REQUIRE_FALSE (writer.open (path, 2, rate).has_value());

    std::vector<float> l (1000, 0.5f);
    std::vector<float> r (1000, -0.5f);
    const std::array<const float*, 2> in {l.data(), r.data()};
    REQUIRE (writer.write (in.data(), 1000));
    REQUIRE (writer.write (in.data(), 24));
    REQUIRE (writer.close());

    const auto bytes = readAll (path);
    REQUIRE (bytes.size() == 44 + 1024 * 8);
    std::uint32_t dataSize = 0;
    std::memcpy (&dataSize, bytes.data() + 40, 4);
    CHECK (dataSize == 1024 * 8);
    float first = 0.0f;
    float second = 0.0f;
    std::memcpy (&first, bytes.data() + 44, 4);
    std::memcpy (&second, bytes.data() + 48, 4);
    CHECK (first == 0.5f);
    CHECK (second == -0.5f);
}

TEST_CASE ("RecordingWriter refuses formats it cannot write", "[recording][io]")
{
    test::TempDirectory dir;
    io::RecordingWriter writer;
    CHECK (writer.open (dir.path() / "a.wav", 3, rate).has_value());
    CHECK (writer.open (dir.path() / "b.wav", 1, 44100.5).has_value());
    CHECK (writer.open (dir.path() / "missing" / "c.wav", 1, rate).has_value());
}

TEST_CASE ("repairRecording recovers everything written before a crash", "[recording][io]")
{
    test::TempDirectory dir;
    const auto path = dir.path() / "take.wav";
    {
        io::RecordingWriter writer;
        REQUIRE_FALSE (writer.open (path, 1, rate).has_value());
        std::vector<float> samples (500, 0.25f);
        const float* pointer = samples.data();
        REQUIRE (writer.write (&pointer, 500));
        REQUIRE (writer.commit());
        REQUIRE (writer.write (&pointer, 300));
    }
    // Simulate a crash after the last commit: the header still says 500 frames, and the process
    // died in the middle of a frame.
    {
        std::fstream file (path, std::ios::in | std::ios::out | std::ios::binary);
        const std::uint32_t stale = 500 * 4;
        file.seekp (40);
        file.write (reinterpret_cast<const char*> (&stale), 4);
        file.seekp (0, std::ios::end);
        file.write ("xy", 2);
    }

    const auto info = io::repairRecording (path);
    REQUIRE (info.has_value());
    CHECK (info->frames == 800);
    CHECK (info->numChannels == 1);
    CHECK (info->sampleRate == rate);
    CHECK (std::filesystem::file_size (path) == 44 + 800 * 4);
    std::uint32_t dataSize = 0;
    std::memcpy (&dataSize, readAll (path).data() + 40, 4);
    CHECK (dataSize == 800 * 4);
}

TEST_CASE ("repairRecording leaves files it did not write alone", "[recording][io]")
{
    test::TempDirectory dir;
    const auto path = dir.path() / "other.wav";
    {
        std::ofstream out (path, std::ios::binary);
        out << std::string (100, 'x');
    }
    CHECK_FALSE (io::repairRecording (path).has_value());
    CHECK (std::filesystem::file_size (path) == 100);
    CHECK_FALSE (io::repairRecording (dir.path() / "missing.wav").has_value());
}

TEST_CASE ("Recording journals round-trip and reject anything unexpected", "[recording][io]")
{
    test::TempDirectory dir;
    const io::ProjectFolder folder {dir.path()};
    std::filesystem::create_directories (folder.audioDirectory());
    const auto path = io::journalPathFor (folder.audioDirectory() / "4-take.wav");
    CHECK (path.filename() == "4-take.wav.journal");

    const io::RecordingJournal journal {"audio/4-take.wav", 3, 1920, 12345, "Take 2"};
    REQUIRE_FALSE (io::writeJournal (path, journal).has_value());
    CHECK (io::readJournal (path) == journal);
    CHECK (io::listJournals (folder) == std::vector<std::filesystem::path> {path});

    const auto rejects = [&path] (const std::string& text)
    {
        std::ofstream (path, std::ios::binary | std::ios::trunc) << text;
        return !io::readJournal (path).has_value();
    };
    const std::string good
        = R"({"format":"audio-playground.take","version":1,"file":"audio/4-take.wav","track":3,"start":0,"sourceOffset":0,"name":"T"})";
    CHECK_FALSE (rejects (good));
    CHECK (rejects ("not json"));
    CHECK (rejects (
        R"({"format":"audio-playground.take","version":2,"file":"audio/4-take.wav","track":3,"start":0,"sourceOffset":0,"name":"T"})"));
    CHECK (rejects (
        R"({"format":"audio-playground.take","version":1,"file":"../escape.wav","track":3,"start":0,"sourceOffset":0,"name":"T"})"));
    CHECK (rejects (
        R"({"format":"audio-playground.take","version":1,"file":"audio/4-take.wav","track":0,"start":0,"sourceOffset":0,"name":"T"})"));
    CHECK (rejects (
        R"({"format":"audio-playground.take","version":1,"file":"audio/4-take.wav","track":3,"start":-1,"sourceOffset":0,"name":"T"})"));
    CHECK (rejects (
        R"({"format":"audio-playground.take","version":1,"file":"audio/4-take.wav","track":3,"start":0,"sourceOffset":"0","name":"T"})"));
    CHECK (rejects (
        R"({"format":"audio-playground.take","version":1,"file":"audio/4-take.wav","track":3,"start":0,"sourceOffset":0,"name":"T","extra":1})"));
}
