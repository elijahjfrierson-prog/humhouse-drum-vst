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

    // Writes the current pattern to `dest` as a standard Type-0 MIDI file.
    // Returns true on success. Used by "Save MIDI" and "Drag to DAW" in the
    // editor — works identically in standalone and plugin mode.
    bool writeCurrentPatternAsMidiFile (const juce::File& dest) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void renderPatternToMidiBuffer (juce::MidiBuffer& midiOut,
                                    int               numSamples,
                                    double            sampleRate,
                                    double            bpm);

    // Maps the Pattern Length choice index to a length in beats.
    static double patternLengthBeatsFromChoice (int choiceIndex);

    juce::AudioProcessorValueTreeState apvts;
    aidrum::AIBackend                  backend;

    mutable std::mutex       patternMutex;
    aidrum::MidiPattern      currentPattern;

    // Playback position (in beats) across blocks when the host isn't providing ppq.
    // Read/written from the audio thread and reset from the UI thread — must be atomic.
    std::atomic<double>      playheadBeats { 0.0 };
    std::atomic<double>      lastBpm       { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessor)
};
