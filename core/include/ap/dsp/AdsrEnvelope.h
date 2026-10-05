#pragma once

#include "ap/core/RealtimeSafety.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ap::dsp
{

// ADSR envelope generator.
// - Attack is linear (snappy and predictable); decay and release are exponential (natural).
// - Every segment starts from the current level, so retriggering or releasing mid-attack never
//   jumps (no clicks).
// - Times have a 1 ms floor: an instant change would click.
class AdsrEnvelope
{
public:
    enum class Stage : std::uint8_t
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    struct Settings
    {
        float attackSeconds = 0.005f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.7f; // 0..1
        float releaseSeconds = 0.3f;
    };

    static constexpr float minSegmentSeconds = 0.001f;
    static constexpr float silence = 1.0e-4f; // -80 dB: treated as finished

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        reset();
        setSettings (settings);
    }

    void reset() noexcept AP_NONBLOCKING
    {
        stage = Stage::idle;
        level = 0.0f;
    }

    void setSettings (const Settings& newSettings) noexcept AP_NONBLOCKING
    {
        const auto seconds = [] (float s)
        { return std::isfinite (s) ? std::max (s, minSegmentSeconds) : minSegmentSeconds; };
        settings.attackSeconds = seconds (newSettings.attackSeconds);
        settings.decaySeconds = seconds (newSettings.decaySeconds);
        settings.releaseSeconds = seconds (newSettings.releaseSeconds);
        settings.sustainLevel = std::isfinite (newSettings.sustainLevel)
                                  ? std::clamp (newSettings.sustainLevel, 0.0f, 1.0f)
                                  : 0.0f;

        attackStep = static_cast<float> (1.0 / (static_cast<double> (settings.attackSeconds) * sampleRate));
        decayCoefficient = exponentialCoefficient (settings.decaySeconds);
        releaseCoefficient = exponentialCoefficient (settings.releaseSeconds);
    }

    void noteOn() noexcept AP_NONBLOCKING { stage = Stage::attack; }

    void noteOff() noexcept AP_NONBLOCKING
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }

    [[nodiscard]] float next() noexcept AP_NONBLOCKING
    {
        switch (stage)
        {
        case Stage::idle:
            return 0.0f;

        case Stage::attack:
            level += attackStep;
            if (level >= 1.0f)
            {
                level = 1.0f;
                stage = Stage::decay;
            }
            break;

        case Stage::decay:
            // Exponential approach to the sustain level.
            level = settings.sustainLevel + (level - settings.sustainLevel) * decayCoefficient;
            if (level - settings.sustainLevel < silence)
            {
                level = settings.sustainLevel;
                stage = Stage::sustain;
            }
            break;

        case Stage::sustain:
            level = settings.sustainLevel;
            if (level <= 0.0f)
                stage = Stage::idle;
            break;

        case Stage::release:
            level *= releaseCoefficient;
            if (level < silence)
            {
                level = 0.0f;
                stage = Stage::idle;
            }
            break;
        }
        return level;
    }

    [[nodiscard]] bool isActive() const noexcept AP_NONBLOCKING { return stage != Stage::idle; }
    [[nodiscard]] Stage getStage() const noexcept AP_NONBLOCKING { return stage; }
    [[nodiscard]] float getLevel() const noexcept AP_NONBLOCKING { return level; }

private:
    // Decays to -80 dB (silence) in the given time.
    float exponentialCoefficient (float seconds) const noexcept AP_NONBLOCKING
    {
        const double samples = static_cast<double> (seconds) * sampleRate;
        return static_cast<float> (std::pow (static_cast<double> (silence), 1.0 / std::max (1.0, samples)));
    }

    double sampleRate = 48000.0;
    Settings settings {};
    Stage stage = Stage::idle;
    float level = 0.0f;
    float attackStep = 0.0f;
    float decayCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
};

} // namespace ap::dsp
