#pragma once

#include "ap/core/RealtimeSafety.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace ap::core
{

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // padding from alignas below is intentional (avoids false sharing)
#endif

// Bounded, wait-free single-producer / single-consumer queue.
//
// Exactly one thread may push and exactly one (other) thread may pop. Storage is fixed at
// compile time: no allocation ever, so either side may be the audio thread.
template <typename T, std::size_t Capacity> class SpscQueue
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert (std::is_nothrow_copy_assignable_v<T> && std::is_nothrow_default_constructible_v<T>);

public:
    // Producer thread. Returns false (and drops nothing) if the queue is full.
    bool push (const T& value) noexcept AP_NONBLOCKING
    {
        const auto tail = writeIndex.load (std::memory_order_relaxed);
        if (tail - readIndex.load (std::memory_order_acquire) == Capacity)
            return false;
        slots[tail & mask] = value;
        writeIndex.store (tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread.
    std::optional<T> pop() noexcept AP_NONBLOCKING
    {
        const auto head = readIndex.load (std::memory_order_relaxed);
        if (head == writeIndex.load (std::memory_order_acquire))
            return std::nullopt;
        T value = slots[head & mask];
        readIndex.store (head + 1, std::memory_order_release);
        return value;
    }

    // Approximate from any thread; exact from the producer (free space) or consumer (size).
    [[nodiscard]] std::size_t size() const noexcept AP_NONBLOCKING
    {
        return writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_acquire);
    }

    [[nodiscard]] bool isFull() const noexcept AP_NONBLOCKING { return size() >= Capacity; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t mask = Capacity - 1;

    std::array<T, Capacity> slots {};
    // Separate cache lines so producer and consumer do not false-share.
    alignas (64) std::atomic<std::size_t> writeIndex {0};
    alignas (64) std::atomic<std::size_t> readIndex {0};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace ap::core
