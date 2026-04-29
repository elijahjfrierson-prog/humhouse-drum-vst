#pragma once

#include <JuceHeader.h>

namespace humvocal
{

// Dual reverb (short plate + long hall) with built-in ducking and
// post-reverb EQ so the tail never clouds the dry vocal.
class VocalReverb
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        reverbShort.prepare(spec);
        reverbLong.prepare(spec);
        duckEnvelope = 0.0f;

        postEQ.prepare(spec);
        *postEQ.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 8000.0f, 0.707f);
    }

    void setShortSize (float s) { shortParams.roomSize = juce::jlimit(0.0f, 1.0f, s); reverbShort.setParameters(shortParams); }
    void setShortDamping (float d) { shortParams.damping = d; reverbShort.setParameters(shortParams); }
    void setShortMix (float m) { shortMix = m; }

    void setLongSize (float s) { longParams.roomSize = juce::jlimit(0.0f, 1.0f, s); reverbLong.setParameters(longParams); }
    void setLongDamping (float d) { longParams.damping = d; reverbLong.setParameters(longParams); }
    void setLongMix (float m) { longMix = m; }

    void setDuckAmount (float d) { duckAmount = juce::jlimit(0.0f, 1.0f, d); }
    void setPostEQFreq (float hz) { postEQFreq = hz; if (sr > 0) *postEQ.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, hz, 0.707f); }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Create wet copies
        juce::AudioBuffer<float> wetShort (numChannels, numSamples);
        juce::AudioBuffer<float> wetLong  (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            wetShort.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            wetLong.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        }

        // Process reverbs
        juce::dsp::AudioBlock<float> blockShort (wetShort);
        juce::dsp::AudioBlock<float> blockLong  (wetLong);
        juce::dsp::ProcessContextReplacing<float> ctxShort (blockShort);
        juce::dsp::ProcessContextReplacing<float> ctxLong  (blockLong);
        reverbShort.process(ctxShort);
        reverbLong.process(ctxLong);

        // Post-EQ on wet signals
        postEQ.process(ctxShort);

        // Ducking — reduce reverb when dry signal is loud
        float attackCoeff  = std::exp(-1.0f / (static_cast<float>(sr) * 0.005f));
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * 0.100f));

        for (int i = 0; i < numSamples; ++i)
        {
            float dryLevel = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                dryLevel = std::max(dryLevel, std::abs(buffer.getSample(ch, i)));

            if (dryLevel > duckEnvelope)
                duckEnvelope = attackCoeff * duckEnvelope + (1.0f - attackCoeff) * dryLevel;
            else
                duckEnvelope = releaseCoeff * duckEnvelope + (1.0f - releaseCoeff) * dryLevel;

            float duckGain = 1.0f - duckAmount * juce::jlimit(0.0f, 1.0f, duckEnvelope * 3.0f);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float dry = buffer.getSample(ch, i);
                float ws  = wetShort.getSample(ch, i) * shortMix * duckGain;
                float wl  = wetLong.getSample(ch, i)  * longMix  * duckGain;
                buffer.setSample(ch, i, dry + ws + wl);
            }
        }
    }

private:
    double sr = 44100.0;
    bool active = true;
    float shortMix = 0.2f;
    float longMix  = 0.15f;
    float duckAmount = 0.5f;
    float postEQFreq = 8000.0f;
    float duckEnvelope = 0.0f;

    juce::dsp::Reverb reverbShort;
    juce::dsp::Reverb reverbLong;
    juce::dsp::Reverb::Parameters shortParams { 0.3f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f };
    juce::dsp::Reverb::Parameters longParams  { 0.7f, 0.4f, 1.0f, 0.0f, 1.0f, 0.0f };

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    IIRFilter postEQ;
};

} // namespace humvocal
