#pragma once

#include <JuceHeader.h>

namespace humvocal
{

// "Lo-Fi Signal Cutoff" — the "Gamma Button".
// Band-pass filter combo (HP + LP) that simulates a telephone / radio
// vocal effect.  Engages a 400 Hz high-pass and 3.5 kHz low-pass to
// strip low rumble and high sparkle, leaving a gritty mid-range vocal.
// Optional bit-crush and sample-rate reduction for extra lo-fi character.
class LoFiFilter
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        hpFilter.prepare(spec);
        lpFilter.prepare(spec);
        updateFilters();
        sampleHold.fill(0.0f);
        holdCounter.fill(0);
    }

    void setHighCut (float hz) { hpFreq = hz; updateFilters(); }
    void setLowCut (float hz) { lpFreq = hz; updateFilters(); }
    void setBitDepth (float bits) { bitDepth = juce::jlimit(4.0f, 32.0f, bits); }
    void setDownsample (float factor) { downsampleFactor = juce::jlimit(1.0f, 16.0f, factor); }
    void setMix (float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Band-pass filtering
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        hpFilter.process(ctx);
        lpFilter.process(ctx);

        // Bit-crush + downsample
        if (bitDepth < 31.0f || downsampleFactor > 1.5f)
        {
            float quantLevels = std::pow(2.0f, bitDepth);
            int dsInt = static_cast<int>(downsampleFactor);

            for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
            {
                float* data = buffer.getWritePointer(ch);
                auto chIdx = static_cast<size_t>(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    // Downsample (sample-and-hold)
                    if (dsInt > 1)
                    {
                        if (holdCounter[chIdx] % dsInt == 0)
                            sampleHold[chIdx] = data[i];
                        else
                            data[i] = sampleHold[chIdx];
                    }

                    // Bit-crush
                    if (bitDepth < 31.0f)
                    {
                        data[i] = std::round(data[i] * quantLevels) / quantLevels;
                    }

                    if (++holdCounter[chIdx] >= dsInt) holdCounter[chIdx] = 0;
                }
            }
        }
    }

private:
    double sr = 44100.0;
    bool active = false; // Off by default (engaged via button)
    float hpFreq = 400.0f;
    float lpFreq = 3500.0f;
    float bitDepth = 32.0f;
    float downsampleFactor = 1.0f;
    float mix = 1.0f;
    std::array<float, 2> sampleHold {{ 0.0f, 0.0f }};
    std::array<int, 2> holdCounter {{ 0, 0 }};

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    IIRFilter hpFilter;
    IIRFilter lpFilter;

    void updateFilters()
    {
        if (sr <= 0.0) return;
        *hpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, hpFreq, 0.707f);
        *lpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, lpFreq, 0.707f);
    }
};

} // namespace humvocal
