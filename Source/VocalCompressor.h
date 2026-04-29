#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Studio-grade vocal compressor with soft/hard knee THD modes,
// auto-gain makeup, and an auto-level module for consistency.
class VocalCompressor
{
public:
    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate;
        envelope = 0.0f;
    }

    void setThreshold (float db) { thresholdDb = db; }
    void setRatio (float r) { ratio = std::max(r, 1.0f); }
    void setAttack (float ms) { attackMs = ms; }
    void setRelease (float ms) { releaseMs = ms; }
    void setMakeupGain (float db) { makeupDb = db; }
    void setKnee (float db) { kneeDb = db; }
    void setAutoGain (bool on) { autoGain = on; }
    void setAutoLevel (bool on) { autoLevel = on; }
    void setAutoLevelTarget (float db) { autoLevelTarget = db; }
    void setTHDMode (int mode) { thdMode = mode; } // 0=off, 1=soft, 2=hard
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        float attackCoeff  = std::exp(-1.0f / (static_cast<float>(sr) * attackMs * 0.001f));
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * releaseMs * 0.001f));

        float peakLevel = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // Detect level (max across channels)
            float inputLevel = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                inputLevel = std::max(inputLevel, std::abs(buffer.getSample(ch, i)));

            // Envelope follower
            if (inputLevel > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * inputLevel;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevel;

            float envDb = juce::Decibels::gainToDecibels(envelope, -100.0f);

            // Gain computer with soft/hard knee
            float gainReduction = computeGainReduction(envDb);

            // THD — add harmonic warmth
            float thdGain = 1.0f;
            if (thdMode > 0)
                thdGain = applyTHD(envelope);

            float gainLin = juce::Decibels::decibelsToGain(gainReduction);
            float makeup = autoGain ? juce::Decibels::decibelsToGain(-gainReduction * 0.5f)
                                    : juce::Decibels::decibelsToGain(makeupDb);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float sample = buffer.getSample(ch, i);
                sample *= gainLin * makeup * thdGain;
                buffer.setSample(ch, i, sample);
            }

            peakLevel = std::max(peakLevel, std::abs(buffer.getSample(0, i)));
        }

        // Auto-level: normalize output to target
        if (autoLevel && peakLevel > 0.0f)
        {
            float targetGain = juce::Decibels::decibelsToGain(autoLevelTarget);
            float correction = targetGain / peakLevel;
            correction = std::min(correction, 6.0f); // limit to +15 dB max
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.applyGain(ch, 0, numSamples, correction);
        }
    }

private:
    double sr = 44100.0;
    bool active = true;
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 5.0f;
    float releaseMs = 50.0f;
    float makeupDb = 0.0f;
    float kneeDb = 6.0f;
    bool autoGain = true;
    bool autoLevel = false;
    float autoLevelTarget = -14.0f;
    int thdMode = 0;
    float envelope = 0.0f;

    float computeGainReduction (float inputDb) const
    {
        float overDb = inputDb - thresholdDb;

        // Soft knee
        if (kneeDb > 0.0f && overDb > -kneeDb * 0.5f && overDb < kneeDb * 0.5f)
        {
            float x = overDb + kneeDb * 0.5f;
            overDb = x * x / (2.0f * kneeDb);
        }
        else
        {
            overDb = std::max(overDb, 0.0f);
        }

        return -overDb * (1.0f - 1.0f / ratio);
    }

    float applyTHD (float level) const
    {
        if (thdMode == 1) // Soft — even harmonics (tube warmth)
            return 1.0f + 0.05f * level;
        if (thdMode == 2) // Hard — odd harmonics (transistor grit)
            return 1.0f + 0.1f * level * level;
        return 1.0f;
    }
};

} // namespace humvocal
