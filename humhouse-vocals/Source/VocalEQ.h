#pragma once

#include <JuceHeader.h>
#include <array>

namespace humvocal
{

// 4-band parametric EQ with high-pass and low-pass filters.
// Analog-modeled curves via JUCE's IIR second-order sections.
class VocalEQ
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        for (auto& f : bands) f.prepare(spec);
        hpFilter.prepare(spec);
        lpFilter.prepare(spec);
    }

    void setHighPass (float freqHz)
    {
        if (freqHz > 10.0f)
            *hpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, freqHz, 0.707f);
    }

    void setLowPass (float freqHz)
    {
        if (freqHz > 10.0f)
            *lpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, freqHz, 0.707f);
    }

    void setBand (int index, float freqHz, float gainDb, float q)
    {
        if (index < 0 || index >= kNumBands) return;
        *bands[static_cast<size_t>(index)].state =
            *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, freqHz, q, juce::Decibels::decibelsToGain(gainDb));
    }

    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        hpFilter.process(ctx);
        for (auto& f : bands) f.process(ctx);
        lpFilter.process(ctx);
    }

private:
    static constexpr int kNumBands = 4;
    double sr = 44100.0;
    bool active = true;

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    std::array<IIRFilter, kNumBands> bands;
    IIRFilter hpFilter;
    IIRFilter lpFilter;
};

} // namespace humvocal
