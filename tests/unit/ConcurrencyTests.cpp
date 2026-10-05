#include "ap/core/SnapshotExchange.h"
#include "ap/core/SpscQueue.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <thread>

using ap::core::SnapshotExchange;
using ap::core::SpscQueue;

TEST_CASE ("SpscQueue preserves order and reports full and empty", "[concurrency]")
{
    SpscQueue<int, 4> queue;
    CHECK_FALSE (queue.pop().has_value());

    for (int i = 0; i < 4; ++i)
        CHECK (queue.push (i));
    CHECK (queue.isFull());
    CHECK_FALSE (queue.push (99)); // full: rejected, nothing overwritten

    for (int i = 0; i < 4; ++i)
        CHECK (queue.pop() == i);
    CHECK_FALSE (queue.pop().has_value());

    // Indices wrap around correctly.
    for (int round = 0; round < 1000; ++round)
    {
        CHECK (queue.push (round));
        CHECK (queue.pop() == round);
    }
}

TEST_CASE ("SpscQueue delivers every item in order across threads", "[concurrency][stress]")
{
    constexpr std::uint32_t count = 200'000;
    SpscQueue<std::uint32_t, 256> queue;

    std::thread producer (
        [&queue]
        {
            for (std::uint32_t i = 0; i < count;)
                if (queue.push (i))
                    ++i;
                else
                    std::this_thread::yield();
        });

    std::uint32_t expected = 0;
    bool inOrder = true;
    while (expected < count)
    {
        if (const auto value = queue.pop())
        {
            inOrder = inOrder && *value == expected;
            ++expected;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    producer.join();
    CHECK (inOrder);
    CHECK (queue.size() == 0);
}

namespace
{
std::atomic<int> liveSnapshots {0};

struct Counted
{
    explicit Counted (int v)
        : value (v)
        , check (v * 7)
    {
        liveSnapshots.fetch_add (1);
    }
    ~Counted() { liveSnapshots.fetch_sub (1); }
    Counted (const Counted&) = delete;
    Counted& operator= (const Counted&) = delete;

    int value;
    int check; // invariant check == value * 7 detects torn or freed reads
};
} // namespace

TEST_CASE ("SnapshotExchange hands over the newest snapshot and frees the rest", "[concurrency]")
{
    liveSnapshots = 0;
    {
        SnapshotExchange<Counted> exchange;
        CHECK (exchange.acquire() == nullptr);

        exchange.publish (std::make_unique<Counted> (1));
        CHECK (exchange.acquire()->value == 1);

        // Two publishes before the next block: the first is never seen and freed immediately.
        exchange.publish (std::make_unique<Counted> (2));
        exchange.publish (std::make_unique<Counted> (3));
        CHECK (liveSnapshots == 2); // current (1) + pending (3)

        CHECK (exchange.acquire()->value == 3);
        CHECK (exchange.acquire()->value == 3); // stable until the next publish

        CHECK (exchange.collectGarbage() == 1); // snapshot 1 retired by the audio thread
        CHECK (liveSnapshots == 1);
    }
    CHECK (liveSnapshots == 0);
}

TEST_CASE ("SnapshotExchange keeps the current snapshot when the retire queue is full", "[concurrency]")
{
    liveSnapshots = 0;
    {
        SnapshotExchange<Counted, 2> exchange;
        for (int i = 1; i <= 3; ++i)
        {
            exchange.publish (std::make_unique<Counted> (i));
            CHECK (exchange.acquire()->value == i);
        }

        // Two retired snapshots fill the queue; the audio thread must not swap again yet.
        exchange.publish (std::make_unique<Counted> (4));
        CHECK (exchange.acquire()->value == 3);

        exchange.collectGarbage();
        CHECK (exchange.acquire()->value == 4);
    }
    CHECK (liveSnapshots == 0);
}

TEST_CASE ("SnapshotExchange is safe under concurrent publish and acquire", "[concurrency][stress]")
{
    liveSnapshots = 0;
    {
        SnapshotExchange<Counted> exchange;
        std::atomic<bool> done {false};
        std::atomic<bool> consistent {true};

        std::thread audio (
            [&]
            {
                int last = 0;
                while (!done.load())
                {
                    if (const auto* snapshot = exchange.acquire())
                    {
                        // Values only move forward, and the object is intact (not freed or torn).
                        if (snapshot->check != snapshot->value * 7 || snapshot->value < last)
                            consistent = false;
                        last = snapshot->value;
                    }
                }
            });

        for (int i = 1; i <= 20'000; ++i)
        {
            exchange.publish (std::make_unique<Counted> (i));
            if (i % 16 == 0)
                exchange.collectGarbage();
        }

        done = true;
        audio.join();
        CHECK (consistent);
    }
    CHECK (liveSnapshots == 0);
}
