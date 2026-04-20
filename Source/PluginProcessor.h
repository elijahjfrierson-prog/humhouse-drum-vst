#pragma once

#include "AIBackend.h"
#include "MidiPattern.h"

#include <JuceHeader.h>

#include <atomic>
#include <mutex>

class AIDrumAudioProcessor : public juce::AudioProcessor
{
public:
    AIDrumAudioProcessor();
    ~AIDrumAudioProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi()  const override { return true; }
    bool   producesMidi() const override { return true; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Plugin-specific API --------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Trigger a new generation from the editor. Thread-safe.
    void requestGeneration (aidrum::GenerationMode mode);

    // Returns a copy of the most-recently-generated pattern (thread-safe).
    aidrum::MidiPattern getCurrentPattern() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void renderPatternToMidiBuffer (juce::MidiBuffer& midiOut,
                                    int               numSamples,
                                    double            sampleRate,
                                    double            bpm);

    juce::AudioProcessorValueTreeState apvts;
    aidrum::AIBackend                  backend;

    mutable std::mutex       patternMutex;
    aidrum::MidiPattern      currentPattern;

    // Playback position (in beats) across blocks when host isn't providing ppq.
    double   playheadBeats = 0.0;
    double   lastBpm       = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessor)
};
