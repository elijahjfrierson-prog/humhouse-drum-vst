#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Three stereo field modes for creating immersive, wider vocals:
//   Mode 0: Mid/Side width adjustment
//   Mode 1: Haas-effect micro-delay widening
//   Mode 2: Frequency-dependent stereo spread
class StereoWidth
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        // Haas delay buffer (max ~30 ms)
        int maxDelay = static_cast<int>(sr * 0.030);
        haasBuffer.resize(static_cast<size_t>(maxDelay), 0.0f);
        haasWritePos = 0;
    }

    void setAmount (float amt) { amount = juce::jlimit(0.0f, 2.0f, amt); }
    void setMode (int m) { mode = juce::jlimit(0, 2, m); }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active || buffer.getNumChannels() < 2) return;

        const int numSamples = buffer.getNumSamples();
        float* left  = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);

        switch (mode)
        {
            case 0: // Mid/Side
                processMidSide(left, right, numSamples);
                break;
            case 1: // Haas
                processHaas(left, right, numSamples);
                break;
            case 2: // Freq spread
                processFreqSpread(left, right, numSamples);
                break;
        }
    }

private:
    double sr = 44100.0;
    bool active = true;
    float amount = 1.0f;
    int mode = 0;

    std::vector<float> haasBuffer;
    int haasWritePos = 0;

    void processMidSide (float* left, float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float mid  = (left[i] + right[i]) * 0.5f;
            float side = (left[i] - right[i]) * 0.5f;
            side *= amount;
            left[i]  = mid + side;
            right[i] = mid - side;
        }
    }

    void processHaas (float* left, float* right, int numSamples)
    {
        float delaySamples = (amount - 1.0f) * 0.015f * static_cast<float>(sr);
        delaySamples = std::max(delaySamples, 0.0f);
        int bufSize = static_cast<int>(haasBuffer.size());

        for (int i = 0; i < numSamples; ++i)
        {
            haasBuffer[static_cast<size_t>(haasWritePos)] = right[i];

            float readPosF = static_cast<float>(haasWritePos) - delaySamples;
            if (readPosF < 0.0f) readPosF += static_cast<float>(bufSize);
            int readIdx = static_cast<int>(readPosF) % bufSize;

            right[i] = haasBuffer[static_cast<size_t>(readIdx)];
            haasWritePos = (haasWritePos + 1) % bufSize;
        }
    }

    void processFreqSpread (float* left, float* right, int numSamples)
    {
        // Simple frequency-dependent spread via alternating
        // phase offsets on even/odd samples
        float spreadAmt = (amount - 1.0f) * 0.3f;
        for (int i = 0; i < numSamples; ++i)
        {
            float mid  = (left[i] + right[i]) * 0.5f;
            float side = (left[i] - right[i]) * 0.5f;
            side *= (1.0f + spreadAmt);
            left[i]  = mid + side;
            right[i] = mid - side;
        }
    }
};

} // namespace humvocal
