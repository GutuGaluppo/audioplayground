#include "ap/engine/Engine.h"

#include "ap/core/ScopedNoDenormals.h"

#include <algorithm>
#include <cmath>

namespace ap::engine
{
namespace
{
constexpr double gainRampSeconds = 0.02;
constexpr double frequencyRampSeconds = 0.05;

// Safety ceiling until the master limiter exists: never send more than full scale to the device.
constexpr float outputCeiling = 1.0f;

float decibelsToGain (float db) noexcept AP_NONBLOCKING
{
    return std::pow (10.0f, db / 20.0f);
}
} // namespace

void Engine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlock = std::clamp (maxBlockSize, 16, 8192);

    transport.prepare (sampleRate);
    metronome.prepare (sampleRate);
    synth.prepare (sampleRate);
    sampler.prepare (sampleRate);
    drums.prepare (sampleRate);
    timeline.prepare (sampleRate);
    busStorage.assign (model::numInstrumentKinds * 2 * static_cast<std::size_t> (maxBlock), 0.0f);
    trackBusStorage.assign (model::Project::maxTracks * 2 * static_cast<std::size_t> (maxBlock), 0.0f);
    directBus.assign (2 * static_cast<std::size_t> (maxBlock), 0.0f);
    for (auto& line : directDelay)
        line.fill (0.0f);
    directDelayPosition = 0;
    limiter.prepare (sampleRate);
    outputLatency.store (TrackChain::latency() + limiter.latency(), std::memory_order_relaxed);
    {
        // The audio callback is stopped: the chains the current graph points at can be rebuilt.
        const std::scoped_lock lock (chainLock);
        chainRate = sampleRate;
        chainBlock = maxBlock;
        for (auto& [id, chain] : chains)
            chain->prepare (chainRate, chainBlock);
    }

    inputCapture.deviceStopped(); // a take never continues across a device restart

    toneOscillator.prepare (sampleRate);
    toneGain.reset (sampleRate, gainRampSeconds);
    toneFrequency.reset (sampleRate, frequencyRampSeconds);

    // Start silent: a device restart must never begin with a jump to full level.
    toneGain.setCurrentAndTarget (0.0f);
    toneFrequency.setCurrentAndTarget (toneFrequencyHz.load (std::memory_order_relaxed));

    prepared = true;
}

void Engine::publishRenderGraph (std::unique_ptr<RenderGraph> next)
{
    if (next != nullptr)
    {
        const std::scoped_lock lock (chainLock);
        std::map<std::uint64_t, std::shared_ptr<TrackChain>> used;
        for (auto& track : next->tracks)
        {
            auto& chain = chains[track.id.value];
            if (chain == nullptr)
            {
                chain = std::make_shared<TrackChain>();
                chain->prepare (chainRate, chainBlock);
            }
            track.chain = chain;
            used.emplace (track.id.value, chain);
        }
        // Chains of deleted tracks go with the last graph that uses them (freed off the audio thread).
        chains = std::move (used);
    }
    graphs.publish (std::move (next));
}

void Engine::releaseResources() noexcept
{
    prepared = false;
    inputCapture.deviceStopped();
}

void Engine::process (core::AudioBlock output, core::InputBlock input) noexcept AP_NONBLOCKING
{
    if (output.isEmpty())
        return;
    if (input.numSamples < output.numSamples)
        input = {}; // a mismatched input is never read past its end

    meterInput (input);

    const core::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < output.numChannels; ++ch)
        std::fill_n (output.channels[ch], output.numSamples, 0.0f);

    if (!prepared)
        return;

    // Devices may deliver more than they announced: render in pieces the buffers can hold.
    const int channels = std::min (output.numChannels, maxOutputChannels);
    const int inputChannels = std::min (input.numChannels, core::AudioRing::maxChannels);
    std::array<float*, maxOutputChannels> pointers {};
    std::array<const float*, core::AudioRing::maxChannels> inputPointers {};
    for (int done = 0; done < output.numSamples;)
    {
        const int length = std::min (maxBlock, output.numSamples - done);
        for (int ch = 0; ch < channels; ++ch)
            pointers[static_cast<std::size_t> (ch)] = output.channels[ch] + done;
        for (int ch = 0; ch < inputChannels; ++ch)
            inputPointers[static_cast<std::size_t> (ch)] = input.channels[ch] + done;
        processBlock ({pointers.data(), channels, length},
                      input.isEmpty() ? core::InputBlock {}
                                      : core::InputBlock {inputPointers.data(), inputChannels, length});
        done += length;
    }

    finaliseOutput (output);
}

void Engine::processBlock (core::AudioBlock output, core::InputBlock input) noexcept AP_NONBLOCKING
{
    const int n = output.numSamples;
    currentGraph = graphs.acquire();
    if (currentGraph != nullptr)
        renderedGraphVersion.store (currentGraph->projectVersion, std::memory_order_release);

    timeline.beginBlock (currentGraph, n);

    // Every track renders into its own bus; sources without a track into the direct bus.
    const TrackBuses buses {
        trackBusStorage.data(), maxBlock,
        currentGraph != nullptr ? std::min (currentGraph->tracks.size(), model::Project::maxTracks) : 0};
    for (std::size_t t = 0; t < buses.count; ++t)
    {
        std::fill_n (buses.left (t), n, 0.0f);
        std::fill_n (buses.right (t), n, 0.0f);
    }
    std::fill_n (directBus.data(), n, 0.0f);
    std::fill_n (directBus.data() + maxBlock, n, 0.0f);
    std::array<float*, 2> directChannels {directBus.data(), directBus.data() + maxBlock};
    const core::AudioBlock direct {directChannels.data(), 2, n};

    const auto target = liveInstrument.load (std::memory_order_relaxed);
    if (target != routedInstrument)
    {
        routeLiveNote ({instruments::NoteEvent::Type::allNotesOff, 0, 0.0f});
        routedInstrument = target;
    }

    while (const auto note = uiNotes.pop())
        routeLiveNote (*note);
    while (const auto note = midiNotes.pop())
        routeLiveNote (*note);

    const float metronomeGain = decibelsToGain (parameters.get (params::ParamId::metronomeLevel));
    transport.advance (n,
                       [this, &buses, direct, input,
                        metronomeGain] (int offset, int length, core::Samples start) noexcept AP_NONBLOCKING
                       {
                           inputCapture.capture (input, offset, length, start);
                           timeline.segment (buses, offset, length, start, transport.getTempoMap());
                           metronome.render (direct, offset, length, start, transport.getTempoMap(),
                                             metronomeGain);
                       });
    const bool playing = transport.isPlayingOnAudioThread();
    timeline.endBlock (buses, playing, transport.getTempoMap());

    if (!playing)
        metronome.renderTail (direct);

    // All instruments always render so released notes ring out after switching. Each one joins its
    // track's bus, or the direct bus at unity if it has no track.
    for (std::size_t k = 0; k < model::numInstrumentKinds; ++k)
    {
        const auto instrument = static_cast<model::InstrumentKind> (k);
        renderInstrument (instrument, n);
        const float* left = busStorage.data() + k * 2 * static_cast<std::size_t> (maxBlock);
        const float* right = left + maxBlock;
        const int track = currentGraph != nullptr ? currentGraph->instrumentTrack[k] : -1;
        float* toLeft = directBus.data();
        float* toRight = directBus.data() + maxBlock;
        if (track >= 0 && static_cast<std::size_t> (track) < buses.count)
        {
            toLeft = buses.left (static_cast<std::size_t> (track));
            toRight = buses.right (static_cast<std::size_t> (track));
        }
        for (int i = 0; i < n; ++i)
        {
            toLeft[i] += left[i];
            toRight[i] += right[i];
        }
    }

    renderTestTone (direct);
    mixTracks (output, buses);
    mixDirect (output);
    limiter.process (output);

    clock.store (clock.load (std::memory_order_relaxed) + n, std::memory_order_relaxed);
}

void Engine::mixTracks (core::AudioBlock output, const TrackBuses& buses) noexcept AP_NONBLOCKING
{
    const int n = output.numSamples;
    for (std::size_t t = 0; t < buses.count; ++t)
    {
        const auto& track = currentGraph->tracks[t];
        std::array<float*, 2> channels {buses.left (t), buses.right (t)};
        if (auto* chain = track.chain.get(); chain != nullptr && chain->getSampleRate() == sampleRate)
        {
            chain->set (track.effects);
            chain->process ({channels.data(), 2, n});
        }

        const auto gain = timeline.trackGain (t);
        if (gain.isSilent())
            continue;
        if (output.numChannels >= 2)
            for (int i = 0; i < n; ++i)
            {
                output.channels[0][i] += channels[0][i] * gain.left (i, n);
                output.channels[1][i] += channels[1][i] * gain.right (i, n);
            }
        else
            for (int i = 0; i < n; ++i)
                output.channels[0][i]
                    += 0.5f * (channels[0][i] * gain.left (i, n) + channels[1][i] * gain.right (i, n));
    }
}

void Engine::mixDirect (core::AudioBlock output) noexcept AP_NONBLOCKING
{
    // Delay by the chains' latency, so the metronome and track-less sources line up with tracks.
    const int n = output.numSamples;
    const float* left = directBus.data();
    const float* right = directBus.data() + maxBlock;
    auto position = directDelayPosition;
    for (int i = 0; i < n; ++i)
    {
        const float delayedLeft = directDelay[0][position];
        const float delayedRight = directDelay[1][position];
        directDelay[0][position] = left[i];
        directDelay[1][position] = right[i];
        position = (position + 1) % directDelay[0].size();
        if (output.numChannels >= 2)
        {
            output.channels[0][i] += delayedLeft;
            output.channels[1][i] += delayedRight;
        }
        else
            output.channels[0][i] += 0.5f * (delayedLeft + delayedRight);
    }
    directDelayPosition = position;
}

void Engine::routeLiveNote (const instruments::NoteEvent& note) noexcept AP_NONBLOCKING
{
    const auto instrument = static_cast<model::InstrumentKind> (routedInstrument);
    timeline.events (instrument).add (0, note);

    const auto latency
        = recordingLatency.load (std::memory_order_relaxed) + outputLatency.load (std::memory_order_relaxed);
    const bool playing = transport.isPlayingOnAudioThread();
    const auto heardTicks
        = playing ? transport.getTempoMap().samplesToTicks (transport.getPositionOnAudioThread() - latency)
                  : 0;
    (void)playedNotes.push (
        {clock.load (std::memory_order_relaxed) - latency, playing, heardTicks, note, instrument});
}

void Engine::handleNote (model::InstrumentKind instrument,
                         const instruments::NoteEvent& note) noexcept AP_NONBLOCKING
{
    switch (instrument)
    {
    case model::InstrumentKind::synth:
        synth.handle (note);
        break;
    case model::InstrumentKind::sampler:
        sampler.handle (note);
        break;
    case model::InstrumentKind::drums:
        drums.handle (note);
        break;
    }
}

void Engine::renderInstrument (model::InstrumentKind instrument, int n) noexcept AP_NONBLOCKING
{
    const auto k = static_cast<std::size_t> (instrument);
    float* const left = busStorage.data() + k * 2 * static_cast<std::size_t> (maxBlock);
    float* const right = left + maxBlock;
    std::fill_n (left, n, 0.0f);
    std::fill_n (right, n, 0.0f);

    // Render in chunks between events so every note starts on its exact sample.
    const auto& events = timeline.events (instrument);
    int index = 0;
    for (int position = 0; position < n;)
    {
        while (index < events.size() && events[index].offset <= position)
            handleNote (instrument, events[index++].event);

        const int next = index < events.size() ? std::min (events[index].offset, n) : n;
        std::array<float*, 2> chunk {left + position, right + position};
        const core::AudioBlock block {chunk.data(), 2, next - position};
        switch (instrument)
        {
        case model::InstrumentKind::synth:
            synth.render (block, parameters);
            break;
        case model::InstrumentKind::sampler:
            sampler.render (block, parameters);
            break;
        case model::InstrumentKind::drums:
            drums.render (block);
            break;
        }
        position = next;
    }
    while (index < events.size())
        handleNote (instrument, events[index++].event);
}

void Engine::renderTestTone (core::AudioBlock output) noexcept AP_NONBLOCKING
{
    const bool enabled = toneEnabled.load (std::memory_order_relaxed);
    const float levelGain = decibelsToGain (parameters.get (params::ParamId::toneLevel));
    toneGain.setTarget (enabled ? levelGain : 0.0f);
    toneFrequency.setTarget (toneFrequencyHz.load (std::memory_order_relaxed));

    if (!toneGain.isSmoothing() && toneGain.getCurrent() == 0.0f)
    {
        toneOscillator.reset();
        return;
    }

    for (int i = 0; i < output.numSamples; ++i)
    {
        toneOscillator.setFrequency (static_cast<double> (toneFrequency.next()));
        const float sample = toneOscillator.next() * toneGain.next();
        for (int ch = 0; ch < output.numChannels; ++ch)
            output.channels[ch][i] += sample;
    }
}

void Engine::finaliseOutput (core::AudioBlock output) noexcept AP_NONBLOCKING
{
    float blockPeak = 0.0f;
    int nonFinite = 0;

    for (int ch = 0; ch < output.numChannels; ++ch)
    {
        float* const samples = output.channels[ch];
        for (int i = 0; i < output.numSamples; ++i)
        {
            float sample = samples[i];
            if (!std::isfinite (sample))
            {
                sample = 0.0f;
                ++nonFinite;
            }
            sample = std::clamp (sample, -outputCeiling, outputCeiling);
            samples[i] = sample;
            blockPeak = std::max (blockPeak, std::abs (sample));
        }
    }

    if (nonFinite > 0)
        nonFiniteSamples.fetch_add (nonFinite, std::memory_order_relaxed);

    updatePeak (outputPeak, blockPeak);
}

void Engine::meterInput (core::InputBlock input) noexcept AP_NONBLOCKING
{
    float blockPeak = 0.0f;
    for (int ch = 0; ch < input.numChannels && input.channels != nullptr; ++ch)
        for (int i = 0; i < input.numSamples; ++i)
        {
            const float sample = std::abs (input.channels[ch][i]);
            if (sample > blockPeak) // also false for NaN
                blockPeak = sample;
        }
    updatePeak (inputPeak, std::min (blockPeak, 1.0f));
}

void Engine::updatePeak (std::atomic<float>& peak, float blockPeak) noexcept AP_NONBLOCKING
{
    float previous = peak.load (std::memory_order_relaxed);
    while (blockPeak > previous
           && !peak.compare_exchange_weak (previous, blockPeak, std::memory_order_relaxed))
    {
    }
}

void Engine::setTestToneEnabled (bool enabled) noexcept
{
    toneEnabled.store (enabled, std::memory_order_relaxed);
}

void Engine::setTestToneFrequency (float hz) noexcept
{
    if (!std::isfinite (hz))
        return;
    toneFrequencyHz.store (std::clamp (hz, minToneFrequencyHz, maxToneFrequencyHz),
                           std::memory_order_relaxed);
}

bool Engine::isTestToneEnabled() const noexcept
{
    return toneEnabled.load (std::memory_order_relaxed);
}

float Engine::consumeOutputPeak() noexcept
{
    return outputPeak.exchange (0.0f, std::memory_order_relaxed);
}

int Engine::getNonFiniteSampleCount() const noexcept
{
    return nonFiniteSamples.load (std::memory_order_relaxed);
}

} // namespace ap::engine
