#pragma once

#include "ap/core/RealtimeSafety.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ap::core
{

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // padding from alignas below is intentional (avoids false sharing)
#endif

// Bounded, wait-free single-producer / single-consumer ring of planar audio frames (1 or 2
// channels). The producer is typically the audio thread, the consumer a disk-writing thread.
//
// allocate() and reset() are not real-time safe and must only be called while neither side is
// using the ring. write() and read() never allocate, lock or block.
class AudioRing
{
public:
    static constexpr int maxChannels = 2;

    // capacityFrames is rounded up to a power of two.
    void allocate (std::size_t capacityFrames)
    {
        std::size_t capacity = 2;
        while (capacity < capacityFrames)
            capacity *= 2;
        for (auto& channel : storage)
            channel.assign (capacity, 0.0f);
        mask = capacity - 1;
        reset();
    }

    void reset() noexcept
    {
        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
    }

    [[nodiscard]] bool isAllocated() const noexcept { return mask != 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return isAllocated() ? mask + 1 : 0; }

    // Producer. Writes all frames or none: returns false when they do not fit. A mono source is
    // written to both channels.
    bool write (const float* const* channels, int numChannels, std::size_t offset,
                std::size_t numFrames) noexcept AP_NONBLOCKING
    {
        if (!isAllocated() || numChannels <= 0)
            return false;
        const auto tail = writeIndex.load (std::memory_order_relaxed);
        if (capacity() - (tail - readIndex.load (std::memory_order_acquire)) < numFrames)
            return false;

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            const float* source = channels[std::min (ch, numChannels - 1)] + offset;
            auto& target = storage[static_cast<std::size_t> (ch)];
            const auto first = std::min (numFrames, capacity() - (tail & mask));
            std::copy_n (source, first, target.data() + (tail & mask));
            std::copy_n (source + first, numFrames - first, target.data());
        }
        writeIndex.store (tail + numFrames, std::memory_order_release);
        return true;
    }

    // Consumer. Reads up to maxFrames into two channel buffers; returns the number read.
    std::size_t read (std::array<float*, maxChannels> channels, std::size_t maxFrames) noexcept AP_NONBLOCKING
    {
        const auto head = readIndex.load (std::memory_order_relaxed);
        const auto available = writeIndex.load (std::memory_order_acquire) - head;
        const auto count = std::min (available, maxFrames);
        if (count == 0)
            return 0;

        for (std::size_t ch = 0; ch < maxChannels; ++ch)
        {
            const auto& source = storage[ch];
            const auto first = std::min (count, capacity() - (head & mask));
            std::copy_n (source.data() + (head & mask), first, channels[ch]);
            std::copy_n (source.data(), count - first, channels[ch] + first);
        }
        readIndex.store (head + count, std::memory_order_release);
        return count;
    }

    // Frames written since the last reset (exact on the producer, a lower bound elsewhere).
    [[nodiscard]] std::uint64_t totalWritten() const noexcept AP_NONBLOCKING
    {
        return writeIndex.load (std::memory_order_acquire);
    }

    // Frames waiting to be read (exact on the consumer).
    [[nodiscard]] std::size_t available() const noexcept AP_NONBLOCKING
    {
        return writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_acquire);
    }

private:
    std::array<std::vector<float>, maxChannels> storage;
    std::size_t mask = 0;
    alignas (64) std::atomic<std::size_t> writeIndex {0};
    alignas (64) std::atomic<std::size_t> readIndex {0};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace ap::core
