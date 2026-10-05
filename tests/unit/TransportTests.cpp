#include "ap/engine/Transport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <vector>

using namespace ap::core;
using ap::engine::Transport;

namespace
{
struct Segment
{
    int offset;
    int length;
    Samples start;
};

std::vector<Segment> advance (Transport& transport, int numSamples)
{
    // The callback runs in a nonblocking context, so it may not allocate: collect into
    // preallocated storage and trim afterwards.
    std::vector<Segment> segments (16);
    std::size_t count = 0;
    transport.advance (numSamples,
                       [&segments, &count] (int offset, int length, Samples start) noexcept
                       {
                           if (count < segments.size())
                               segments[count] = {offset, length, start};
                           ++count;
                       });
    REQUIRE (count <= segments.size());
    segments.resize (count);
    return segments;
}
} // namespace

TEST_CASE ("Transport does not advance while stopped", "[transport]")
{
    Transport transport;
    transport.prepare (48000.0);

    CHECK (advance (transport, 512).empty());
    CHECK_FALSE (transport.getState().playing);
    CHECK (transport.getState().positionSamples == 0);
}

TEST_CASE ("Transport advances by exactly the number of rendered samples", "[transport]")
{
    const int blockSize = GENERATE (1, 17, 128, 333, 2048);
    CAPTURE (blockSize);

    Transport transport;
    transport.prepare (44100.0);
    transport.requestPlay();

    Samples expected = 0;
    for (int i = 0; i < 200; ++i)
    {
        const auto segments = advance (transport, blockSize);
        REQUIRE (segments.size() == 1);
        CHECK (segments[0].start == expected);
        expected += blockSize;
    }

    const auto state = transport.getState();
    CHECK (state.playing);
    CHECK (state.positionSamples == expected);
}

TEST_CASE ("Transport stop pauses and play resumes from the same position", "[transport]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.requestPlay();
    (void)advance (transport, 1000);

    transport.requestStop();
    CHECK (advance (transport, 1000).empty());
    CHECK (transport.getState().positionSamples == 1000);

    transport.requestPlay();
    const auto segments = advance (transport, 10);
    REQUIRE_FALSE (segments.empty());
    // 1000 samples at 48 kHz/120 BPM is 40 ticks exactly: resumes at the same place.
    CHECK (segments[0].start == 1000);
}

TEST_CASE ("Transport wraps sample-accurately at the loop end", "[transport][loop]")
{
    const int blockSize = GENERATE (1, 7, 64, 500, 4096);
    CAPTURE (blockSize);

    Transport transport;
    transport.prepare (48000.0);
    transport.setLoop (true, 0, ticksPerQuarterNote * 4); // one bar = 96000 samples
    transport.requestPlay();

    // Render 2.5 loop lengths and reconstruct the musical position of every sample.
    const Samples loopLength = 96000;
    const Samples total = loopLength * 5 / 2;
    std::vector<Samples> positions;
    positions.reserve (static_cast<std::size_t> (total));

    for (Samples rendered = 0; rendered < total;)
    {
        const auto length = static_cast<int> (std::min<Samples> (blockSize, total - rendered));
        for (const auto& segment : advance (transport, length))
            for (int i = 0; i < segment.length; ++i)
                positions.push_back (segment.start + i);
        rendered += length;
    }

    REQUIRE (positions.size() == static_cast<std::size_t> (total));
    for (std::size_t i = 0; i < positions.size(); ++i)
        REQUIRE (positions[i] == static_cast<Samples> (i) % loopLength);
}

TEST_CASE ("Transport count-in starts one bar early and is reported", "[transport]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setCountInBars (1);
    transport.requestPlay();

    const auto segments = advance (transport, 256);
    REQUIRE (segments.size() == 1);
    CHECK (segments[0].start == -96000);
    CHECK (transport.getState().countingIn);

    // Stopping during the count-in returns to the intended start.
    transport.requestStop();
    (void)advance (transport, 256);
    CHECK (transport.getState().positionSamples == 0);
    CHECK_FALSE (transport.getState().countingIn);
}

TEST_CASE ("Transport keeps the musical position when the tempo changes", "[transport]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.requestPlay();
    (void)advance (transport, 48000); // 1 s = 2 beats at 120 BPM

    transport.setTempo (60.0);
    const auto segments = advance (transport, 1);
    REQUIRE (segments.size() == 1);
    // Still 2 beats in, which is now 2 s.
    CHECK (segments[0].start == 96000);
    CHECK (transport.getState().positionTicks == 2 * ticksPerQuarterNote + 0);
}

TEST_CASE ("Transport seeks and ignores invalid parameters", "[transport]")
{
    Transport transport;
    transport.prepare (48000.0);

    transport.requestSeek (ticksPerQuarterNote);
    transport.setTempo (-5.0);
    transport.setTimeSignature ({4, 3});
    transport.setCountInBars (99);
    transport.requestPlay();

    const auto segments = advance (transport, 1);
    REQUIRE (segments.size() == 1);
    CHECK (segments[0].start == -4 * 96000 + 24000); // count-in clamped to 4 bars, from beat 2
    CHECK (transport.getTempo() == 120.0);
    CHECK (transport.getTimeSignature() == TimeSignature{4, 4});
}
