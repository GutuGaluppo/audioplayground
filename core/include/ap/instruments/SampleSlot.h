#pragma once

#include "ap/core/RealtimeSafety.h"
#include "ap/core/SnapshotExchange.h"
#include "ap/instruments/SampleBuffer.h"

#include <memory>

namespace ap::instruments
{

// One replaceable sample shared with the audio thread. A replaced buffer stays alive until no
// voice reads it any more, then goes back to the message thread to be freed.
class SampleSlot
{
public:
    SampleSlot() = default;
    SampleSlot (const SampleSlot&) = delete;
    SampleSlot& operator= (const SampleSlot&) = delete;

    ~SampleSlot()
    {
        delete current;
        delete fading;
    }

    // Message thread. An empty (invalid) buffer clears the slot.
    void publish (std::unique_ptr<SampleBuffer> buffer) noexcept { exchange.publish (std::move (buffer)); }
    std::size_t collectGarbage() noexcept { return exchange.collectGarbage(); }

    // Audio thread, once per block. inUse (const SampleBuffer*) -> bool says whether any voice
    // still reads a buffer. Returns the buffer that was just replaced (its voices should fade
    // out), or nullptr if nothing changed.
    template <typename InUse> const SampleBuffer* update (InUse&& inUse) noexcept AP_NONBLOCKING
    {
        if (fading != nullptr)
        {
            if (inUse (fading) || !exchange.retire (fading))
                return nullptr;
            fading = nullptr;
        }

        if (!exchange.canRetire())
            return nullptr;

        SampleBuffer* next = exchange.takePending();
        if (next == nullptr)
            return nullptr;

        const SampleBuffer* replaced = current;
        fading = current;
        if (next->isValid())
            current = next;
        else
        {
            current = nullptr;
            if (!exchange.retire (next))
            {
                // Queue full right after canRetire(): keep it and free it with the slot instead.
                if (fading == nullptr)
                    fading = next;
                else
                    current = next; // invalid buffers are never played (get() checks isValid)
            }
        }
        return replaced;
    }

    [[nodiscard]] const SampleBuffer* get() const noexcept AP_NONBLOCKING
    {
        return current != nullptr && current->isValid() ? current : nullptr;
    }

private:
    core::SnapshotExchange<SampleBuffer> exchange;
    SampleBuffer* current = nullptr;
    SampleBuffer* fading = nullptr;
};

} // namespace ap::instruments
