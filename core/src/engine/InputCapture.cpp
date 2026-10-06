#include "ap/engine/InputCapture.h"

#include "ap/model/Project.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace ap::engine
{

bool InputCapture::start (double sampleRate)
{
    if (getState() != State::idle || !(sampleRate > 0.0))
        return false;

    const auto needed = static_cast<std::size_t> (std::ceil (ringSeconds * sampleRate));
    if (frames.capacity() < needed)
        frames.allocate (needed);
    frames.reset();
    rate = sampleRate;
    numChannels.store (0, std::memory_order_relaxed);
    startPosition.store (0, std::memory_order_relaxed);
    status.store (pack (State::waiting), std::memory_order_seq_cst);
    return true;
}

void InputCapture::end (EndReason reason) noexcept AP_NONBLOCKING
{
    // Whoever ends the take first records the reason.
    auto current = status.load (std::memory_order_seq_cst);
    while (stateOf (current) == State::waiting || stateOf (current) == State::capturing)
        if (status.compare_exchange_weak (current, pack (State::ended, reason), std::memory_order_seq_cst))
            return;
}

void InputCapture::stop() noexcept
{
    end (EndReason::stopped);

    // The audio thread may be inside capture() having seen the old state; wait until it leaves.
    // It announces itself before reading the state (both sequentially consistent), so once this
    // loop exits it can no longer write a frame. The wait is bounded by one segment copy.
    while (audioThreadInside.load (std::memory_order_seq_cst))
        std::this_thread::yield();
}

void InputCapture::finish() noexcept
{
    if (getState() != State::ended)
        stop();
    status.store (pack (State::idle), std::memory_order_seq_cst);
}

void InputCapture::deviceStopped() noexcept
{
    end (EndReason::deviceRestarted);
}

void InputCapture::capture (core::InputBlock input, int offset, int length,
                            core::Samples position) noexcept AP_NONBLOCKING
{
    audioThreadInside.store (true, std::memory_order_seq_cst);
    auto current = getState();

    if (current == State::waiting)
    {
        if (input.isEmpty())
            end (EndReason::noInput);
        else
        {
            numChannels.store (std::min (input.numChannels, core::AudioRing::maxChannels),
                               std::memory_order_relaxed);
            startPosition.store (position, std::memory_order_relaxed);
            nextPosition = position;
            auto expected = pack (State::waiting);
            if (status.compare_exchange_strong (expected, pack (State::capturing), std::memory_order_seq_cst))
                current = State::capturing;
            else
                current = stateOf (expected); // stopped meanwhile
        }
    }

    if (current == State::capturing && length > 0)
    {
        if (position != nextPosition)
            end (EndReason::jumped);
        else if (input.isEmpty() || offset + length > input.numSamples)
            end (EndReason::noInput);
        else if (!frames.write (input.channels, std::min (input.numChannels, core::AudioRing::maxChannels),
                                static_cast<std::size_t> (offset), static_cast<std::size_t> (length)))
            end (EndReason::overflow);
        else
            nextPosition += length;
    }

    audioThreadInside.store (false, std::memory_order_seq_cst);
}

TakePlacement placeTake (core::Samples capturePosition, std::int64_t capturedFrames, core::Samples latency,
                         const core::TempoMap& tempoMap) noexcept
{
    // Timeline sample at which the first captured frame was played.
    const auto heard = capturePosition - std::max<core::Samples> (0, latency);
    const auto dataEnd = heard + std::max<std::int64_t> (0, capturedFrames);
    const auto from = std::max<core::Samples> (0, heard);
    if (dataEnd <= from)
        return {};

    // First tick at or after `from`.
    auto start = tempoMap.tickAtOrBefore (from);
    if (tempoMap.ticksToSamples (start) < from)
        ++start;

    const auto end = std::min (tempoMap.tickAtOrBefore (dataEnd), model::maxTimelineTicks);
    if (start >= model::maxTimelineTicks || end - start < model::Clip::minLength)
        return {};

    const auto firstFrame = tempoMap.ticksToSamples (start) - heard;
    const double rate = tempoMap.getSampleRate();
    const auto integralRate = static_cast<std::int64_t> (rate);
    const auto offset
        = static_cast<double> (integralRate) == rate && integralRate > 0
            ? firstFrame * core::flicksPerSecond / integralRate
            : static_cast<core::Flicks> (std::llround (static_cast<double> (firstFrame)
                                                       * static_cast<double> (core::flicksPerSecond) / rate));

    return {start, end - start, offset};
}

} // namespace ap::engine
