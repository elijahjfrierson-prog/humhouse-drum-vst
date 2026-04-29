#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Analog saturation with three distinct flavour modes:
//   Tube  — even-harmonic soft clipping (warm, forgiving)
//   Tape  — asymmetric clipping with slight compression
//   Transformer — odd-harmonic hard clipping (gritty, punchy)
class SaturationEngine
{
public:
    enum Mode { Tube = 0, Tape, Transformer, NumModes };

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        oversampling.initProcessing(static_cast<size_t>(blockSize));
    }

    void setDrive (float d) { drive = juce::jlimit(0.0f, 1.0f, d); }
    void setMode (int m) { mode = static_cast<Mode>(juce::jlimit(0, static_cast<int>(NumModes) - 1, m)); }
    void setMix (float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active || drive < 0.01f) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float dry = data[i];
                float wet = saturate(dry);
                data[i] = dry * (1.0f - mix) + wet * mix;
            }
        }
    }

private:
    double sr = 44100.0;
    bool active = true;
    float drive = 0.3f;
    Mode mode = Tube;
    float mix = 1.0f;

    // 2x oversampling to reduce aliasing from the nonlinearity
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    float saturate (float x) const
    {
        float gained = x * (1.0f + drive * 10.0f);

        switch (mode)
        {
            case Tube:
                // Even-harmonic soft clip (warm, tube-like)
                return std::tanh(gained) * 0.9f;

            case Tape:
            {
                // Asymmetric tape saturation
                float pos = std::tanh(gained * 0.8f);
                float neg = gained / (1.0f + std::abs(gained));
                return (gained >= 0.0f ? pos : neg) * 0.85f;
            }

            case Transformer:
                // Odd-harmonic hard clip
                return juce::jlimit(-0.95f, 0.95f, gained * 1.2f) *
                       (1.0f / (1.0f + std::abs(gained * 0.3f)));

            default:
                return std::tanh(gained);
        }
    }
};

} // namespace humvocal
