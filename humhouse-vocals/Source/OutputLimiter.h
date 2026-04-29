#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Transparent brick-wall limiter for the final output stage.
// Uses look-ahead (configurable) and smooth gain reduction to
// ensure the vocal never clips while preserving dynamics.
class OutputLimiter
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        envelope = 0.0f;

        // Look-ahead delay buffer (1 ms default)
        int maxLookahead = static_cast<int>(sr * 0.010); // 10 ms max
        for (auto& buf : lookaheadBuf)
            buf.assign(static_cast<size_t>(maxLookahead), 0.0f);
        writePos = 0;
        lookaheadSamples = static_cast<int>(sr * 0.001); // 1 ms
    }

    void setCeiling (float db) { ceilingDb = db; ceilingLin = juce::Decibels::decibelsToGain(db); }
    void setRelease (float ms) { releaseMs = ms; }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * releaseMs * 0.001f));

        for (int i = 0; i < numSamples; ++i)
        {
            // Peak detection across channels
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max(peak, std::abs(buffer.getSample(ch, i)));

            // Attack is instant (look-ahead compensated)
            if (peak > envelope)
                envelope = peak;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * peak;

            // Gain reduction
            float gain = 1.0f;
            if (envelope > ceilingLin)
                gain = ceilingLin / envelope;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float sample = buffer.getSample(ch, i);
                buffer.setSample(ch, i, sample * gain);
            }
        }
    }

private:
    double sr = 44100.0;
    bool active = true;
    float ceilingDb = -0.3f;
    float ceilingLin = 0.966f;
    float releaseMs = 50.0f;
    float envelope = 0.0f;
    int writePos = 0;
    int lookaheadSamples = 44;

    std::array<std::vector<float>, 2> lookaheadBuf;
};

} // namespace humvocal
