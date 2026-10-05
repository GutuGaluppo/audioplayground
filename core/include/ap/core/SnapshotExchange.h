#pragma once

#include "ap/core/RealtimeSafety.h"
#include "ap/core/SpscQueue.h"

#include <atomic>
#include <memory>

namespace ap::core
{

// Hands immutable snapshots from the message thread to the audio thread without locks, and
// returns retired snapshots so they are destroyed off the audio thread (ADR-005).
//
//   message thread: publish (std::make_unique<T> (...));  collectGarbage() periodically
//   audio thread:   const T* current = acquire();          once per block
//
// Ownership moves through atomic exchanges, so every snapshot has exactly one owner at any
// time. If the audio thread falls behind, intermediate snapshots it never saw are destroyed
// by the publisher directly.
template <typename T, std::size_t RetiredCapacity = 64> class SnapshotExchange
{
public:
    SnapshotExchange() = default;
    SnapshotExchange (const SnapshotExchange&) = delete;
    SnapshotExchange& operator= (const SnapshotExchange&) = delete;

    // Destroy only while no thread is calling acquire().
    ~SnapshotExchange()
    {
        collectGarbage();
        delete pending.exchange (nullptr, std::memory_order_acq_rel);
        delete current;
    }

    // --- Message thread -------------------------------------------------------------------

    void publish (std::unique_ptr<T> snapshot) noexcept
    {
        // If the audio thread has not picked up the previous pending snapshot, we own it again.
        delete pending.exchange (snapshot.release(), std::memory_order_acq_rel);
    }

    // Destroys snapshots the audio thread has finished with. Returns how many were freed.
    std::size_t collectGarbage() noexcept
    {
        std::size_t freed = 0;
        while (const auto retiredSnapshot = retired.pop())
        {
            delete *retiredSnapshot;
            ++freed;
        }
        return freed;
    }

    // --- Audio thread ---------------------------------------------------------------------

    // Low-level API for owners that must keep an old snapshot alive while it is still in use
    // (e.g. a sampler whose voices still read the previous sample). The caller then owns the
    // returned snapshot and must hand it back with retire() when done.
    [[nodiscard]] T* takePending() noexcept AP_NONBLOCKING
    {
        return pending.exchange (nullptr, std::memory_order_acq_rel);
    }

    // Hands a snapshot back for destruction on the message thread. Returns false if the queue is
    // full; the caller keeps ownership and retries on a later block.
    [[nodiscard]] bool retire (T* snapshot) noexcept AP_NONBLOCKING { return retired.push (snapshot); }

    [[nodiscard]] bool canRetire() const noexcept AP_NONBLOCKING { return !retired.isFull(); }

    // Returns the newest published snapshot, or nullptr if none has been published.
    [[nodiscard]] const T* acquire() noexcept AP_NONBLOCKING
    {
        // Only swap when the old snapshot can be handed back; otherwise keep using it for
        // another block (the message thread will drain the queue).
        if (!retired.isFull())
        {
            if (T* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current != nullptr)
                    retired.push (current);
                current = next;
            }
        }
        return current;
    }

private:
    std::atomic<T*> pending {nullptr};
    T* current = nullptr; // audio thread only (and the destructor)
    SpscQueue<T*, RetiredCapacity> retired;
};

} // namespace ap::core
