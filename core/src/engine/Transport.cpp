#include "ap/engine/Transport.h"

#include <cmath>

namespace ap::engine
{

void Transport::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    publishedSampleRate.store (sampleRate, std::memory_order_relaxed);
    tempoMap = core::TempoMap (tempoBpm.load(), getTimeSignature(), sampleRate);
    // A device restart keeps the musical position.
    position = tempoMap.ticksToSamples (playStartTicks);
    playing = false;
    publish();
}

void Transport::requestSeek (core::Ticks ticks) noexcept
{
    seekTicks.store (std::max<core::Ticks> (0, ticks), std::memory_order_relaxed);
    seekPending.store (true, std::memory_order_release);
}

void Transport::setTempo (double bpm) noexcept
{
    if (core::isValidTempo (bpm))
        tempoBpm.store (bpm, std::memory_order_relaxed);
}

void Transport::setTimeSignature (core::TimeSignature signature) noexcept
{
    if (!signature.isValid())
        return;
    numerator.store (signature.numerator, std::memory_order_relaxed);
    denominator.store (signature.denominator, std::memory_order_relaxed);
}

void Transport::setCountInBars (int bars) noexcept
{
    countInBars.store (std::clamp (bars, 0, maxCountInBars), std::memory_order_relaxed);
}

void Transport::setLoop (bool enabled, core::Ticks start, core::Ticks end) noexcept
{
    loopStartTicks.store (std::max<core::Ticks> (0, start), std::memory_order_relaxed);
    loopEndTicks.store (std::max<core::Ticks> (0, end), std::memory_order_relaxed);
    loopEnabled.store (enabled, std::memory_order_relaxed);
}

core::TimeSignature Transport::getTimeSignature() const noexcept
{
    return {numerator.load (std::memory_order_relaxed), denominator.load (std::memory_order_relaxed)};
}

TransportState Transport::getState() const noexcept
{
    TransportState state;
    state.playing = publishedPlaying.load (std::memory_order_acquire);
    state.positionSamples = publishedPosition.load (std::memory_order_relaxed);

    // Converted on the reading thread with the current parameters; UI display only.
    const core::TempoMap map (tempoBpm.load (std::memory_order_relaxed), getTimeSignature(),
                              publishedSampleRate.load (std::memory_order_relaxed));
    state.positionTicks = map.tickAtOrBefore (state.positionSamples);
    state.countingIn = state.positionSamples < 0;
    return state;
}

void Transport::syncParameters() noexcept AP_NONBLOCKING
{
    const double bpm = tempoBpm.load (std::memory_order_relaxed);
    const core::TimeSignature meter{numerator.load (std::memory_order_relaxed),
                                    denominator.load (std::memory_order_relaxed)};

    if (bpm != tempoMap.getTempo() || !(meter == tempoMap.getTimeSignature()))
    {
        // Keep the musical position when the tempo changes mid-playback.
        const auto ticks = tempoMap.samplesToTicks (position);
        tempoMap = core::TempoMap (bpm, meter, sampleRate);
        position = tempoMap.ticksToSamples (ticks);
    }

    if (seekPending.exchange (false, std::memory_order_acquire))
    {
        playStartTicks = seekTicks.load (std::memory_order_relaxed);
        position = tempoMap.ticksToSamples (playStartTicks);
    }

    const bool want = wantPlaying.load (std::memory_order_acquire);
    if (want && !playing)
    {
        const auto countIn = static_cast<core::Ticks> (countInBars.load (std::memory_order_relaxed))
                           * tempoMap.getTimeSignature().ticksPerBar();
        playStartTicks = tempoMap.samplesToTicks (std::max<core::Samples> (0, position));
        position = tempoMap.ticksToSamples (playStartTicks - countIn);
        playing = true;
    }
    else if (!want && playing)
    {
        playing = false;
        // Stopping during the count-in returns to where playback was going to start.
        if (position < 0)
            position = tempoMap.ticksToSamples (playStartTicks);
    }
}

void Transport::publish() noexcept AP_NONBLOCKING
{
    publishedPosition.store (position, std::memory_order_relaxed);
    publishedPlaying.store (playing, std::memory_order_release);
}

} // namespace ap::engine
