#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace humvocal
{

// Vocal doubler — creates a slightly detuned, micro-delayed copy
// of the input and blends it in to thicken the vocal.  Two voices
// (left-biased and right-biased) with independent detune & delay
// for a natural, wide double-track effect.
class VocalDoubler
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        int maxDelay = static_cast<int>(sr * 0.050); // 50 ms max
        for (auto& buf : delayBuf)
            buf.assign(static_cast<size_t>(maxDelay), 0.0f);
        writePos = 0;
        lfoPhase[0] = 0.0;
        lfoPhase[1] = 0.33; // offset for voice 2
    }

    void setMix (float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setDetune (float cents) { detuneCents = juce::jlimit(0.0f, 50.0f, cents); }
    void setDelay (float ms) { delayMs = juce::jlimit(5.0f, 50.0f, ms); }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active || mix < 0.01f) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const int bufSize = static_cast<int>(delayBuf[0].size());
        if (bufSize < 2) return;

        float baseDelay = delayMs * 0.001f * static_cast<float>(sr);

        for (int i = 0; i < numSamples; ++i)
        {
            // Use channel 0 as source for both voices
            float src = buffer.getSample(0, i);

            for (int v = 0; v < 2; ++v)
            {
                delayBuf[static_cast<size_t>(v)][static_cast<size_t>(writePos)] = src;

                // LFO modulates delay for pitch wobble (detune effect)
                float lfo = std::sin(2.0f * juce::MathConstants<float>::pi * static_cast<float>(lfoPhase[v]));
                float detuneDelaySamples = detuneCents * 0.01f * static_cast<float>(sr) / 1000.0f;
                float totalDelay = baseDelay + lfo * detuneDelaySamples * (v == 0 ? 1.0f : -1.0f);
                totalDelay = juce::jlimit(1.0f, static_cast<float>(bufSize - 1), totalDelay);

                float readPosF = static_cast<float>(writePos) - totalDelay;
                if (readPosF < 0.0f) readPosF += static_cast<float>(bufSize);
                int readIdx = static_cast<int>(readPosF);
                float frac = readPosF - static_cast<float>(readIdx);
                int nextIdx = (readIdx + 1) % bufSize;

                float doubled = delayBuf[static_cast<size_t>(v)][static_cast<size_t>(readIdx % bufSize)] * (1.0f - frac) +
                                delayBuf[static_cast<size_t>(v)][static_cast<size_t>(nextIdx)] * frac;

                // Pan: voice 0 slightly left, voice 1 slightly right
                if (numChannels >= 2)
                {
                    float panL = (v == 0) ? 0.7f : 0.3f;
                    float panR = (v == 0) ? 0.3f : 0.7f;
                    buffer.addSample(0, i, doubled * mix * 0.5f * panL);
                    buffer.addSample(1, i, doubled * mix * 0.5f * panR);
                }
                else
                {
                    buffer.addSample(0, i, doubled * mix * 0.5f);
                }

                lfoPhase[v] += 0.5 / sr; // ~0.5 Hz detune LFO
                if (lfoPhase[v] >= 1.0) lfoPhase[v] -= 1.0;
            }

            writePos = (writePos + 1) % bufSize;
        }
    }

private:
    double sr = 44100.0;
    bool active = true;
    float mix = 0.5f;
    float detuneCents = 10.0f;
    float delayMs = 20.0f;

    std::array<std::vector<float>, 2> delayBuf;
    int writePos = 0;
    std::array<double, 2> lfoPhase {};
};

} // namespace humvocal
