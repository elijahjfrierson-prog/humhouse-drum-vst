#pragma once

#include "DeEsser.h"
#include "LoFiFilter.h"
#include "OutputLimiter.h"
#include "PitchEngine.h"
#include "SaturationEngine.h"
#include "StereoWidth.h"
#include "TapeEmulation.h"
#include "VocalCompressor.h"
#include "VocalDelay.h"
#include "VocalDoubler.h"
#include "VocalEQ.h"
#include "VocalReverb.h"

#include <JuceHeader.h>
#include <atomic>

class HumHouseVocalsProcessor : public juce::AudioProcessor
{
public:
    HumHouseVocalsProcessor();
    ~HumHouseVocalsProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Pitch feedback for the heatmap visualizer
    float getDetectedPitchHz() const { return detectedPitchHz.load(); }
    float getTargetPitchHz() const { return targetPitchHz.load(); }
    float getCorrectionCents() const { return correctionCents.load(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateModuleParameters();

    // DSP modules — signal chain order
    humvocal::PitchEngine       pitchEngine;
    humvocal::VocalEQ           vocalEQ;
    humvocal::VocalCompressor   compressor;
    humvocal::DeEsser           deEsser;
    humvocal::SaturationEngine  saturation;
    humvocal::TapeEmulation     tapeEmulation;
    humvocal::StereoWidth       stereoWidth;
    humvocal::VocalDoubler      doubler;
    humvocal::VocalReverb       reverb;
    humvocal::VocalDelay        delay;
    humvocal::LoFiFilter        lofiFilter;
    humvocal::OutputLimiter     limiter;

    // Atomic pitch feedback
    std::atomic<float> detectedPitchHz { 0.0f };
    std::atomic<float> targetPitchHz   { 0.0f };
    std::atomic<float> correctionCents { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumHouseVocalsProcessor)
};
