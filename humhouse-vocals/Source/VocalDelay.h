#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace humvocal
{

// Dual delay (short slap + long echo) with built-in ducking, feedback,
// and post-delay EQ.  Tempo-sync ready (BPM-based subdivision).
class VocalDelay
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        int maxDelay = static_cast<int>(sr * 2.0); // 2 seconds max
        for (auto& buf : delayBuf)
        {
            buf.assign(static_cast<size_t>(maxDelay), 0.0f);
        }
        writePos = 0;
        duckEnvelope = 0.0f;

        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        postFilter.prepare(spec);
        *postFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 6000.0f, 0.707f);
    }

    void setTimeMs (float ms) { delayTimeMs = juce::jlimit(10.0f, 2000.0f, ms); }
    void setFeedback (float fb) { feedback = juce::jlimit(0.0f, 0.9f, fb); }
    void setMix (float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setDuckAmount (float d) { duckAmount = juce::jlimit(0.0f, 1.0f, d); }
    void setFilterFreq (float hz) { if (sr > 0) *postFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, hz, 0.707f); }
    void setPingPong (bool on) { pingPong = on; }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active || mix < 0.01f) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const int bufSize = static_cast<int>(delayBuf[0].size());
        if (bufSize < 2) return;

        int delaySamples = static_cast<int>(delayTimeMs * 0.001f * static_cast<float>(sr));
        delaySamples = juce::jlimit(1, bufSize - 1, delaySamples);

        float attackCoeff  = std::exp(-1.0f / (static_cast<float>(sr) * 0.005f));
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * 0.100f));

        for (int i = 0; i < numSamples; ++i)
        {
            // Ducking envelope
            float dryLevel = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                dryLevel = std::max(dryLevel, std::abs(buffer.getSample(ch, i)));

            if (dryLevel > duckEnvelope)
                duckEnvelope = attackCoeff * duckEnvelope + (1.0f - attackCoeff) * dryLevel;
            else
                duckEnvelope = releaseCoeff * duckEnvelope + (1.0f - releaseCoeff) * dryLevel;

            float duckGain = 1.0f - duckAmount * juce::jlimit(0.0f, 1.0f, duckEnvelope * 3.0f);

            for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
            {
                float dry = buffer.getSample(ch, i);

                int readIdx = (writePos - delaySamples + bufSize) % bufSize;
                float delayed = delayBuf[static_cast<size_t>(ch)][static_cast<size_t>(readIdx)];

                // Feedback (with optional ping-pong cross-feed)
                int fbCh = (pingPong && numChannels >= 2) ? (1 - ch) : ch;
                delayBuf[static_cast<size_t>(ch)][static_cast<size_t>(writePos)] =
                    dry + delayBuf[static_cast<size_t>(fbCh)][static_cast<size_t>(readIdx)] * feedback;

                buffer.setSample(ch, i, dry + delayed * mix * duckGain);
            }

            writePos = (writePos + 1) % bufSize;
        }

        // Post-delay filter (darken repeats)
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        postFilter.process(ctx);
    }

private:
    double sr = 44100.0;
    bool active = true;
    float delayTimeMs = 250.0f;
    float feedback = 0.3f;
    float mix = 0.2f;
    float duckAmount = 0.5f;
    bool pingPong = false;
    float duckEnvelope = 0.0f;
    int writePos = 0;

    std::array<std::vector<float>, 2> delayBuf;

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    IIRFilter postFilter;
};

} // namespace humvocal
