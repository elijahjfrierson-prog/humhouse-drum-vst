#pragma once

#include "AIBackend.h"
#include "MidiPattern.h"

#include <JuceHeader.h>

#include <atomic>
#include <mutex>
#include <vector>

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

    // v0.6.0 — Logic-Drummer-style arrangement API.
    // Generates a new region with current params and appends it to the arrangement.
    void appendRegion (aidrum::GenerationMode mode);

    // Removes the most-recently-appended region (keeps at least one region).
    void undoLastRegion();

    // Wipes the arrangement back to a single freshly-generated region.
    void clearArrangement();

    // Returns a copy of the full arrangement for UI rendering (thread-safe).
    std::vector<aidrum::MidiPattern> getArrangement() const;

    // Sum of all region lengths in beats.
    double getArrangementTotalBeats() const;

    // Returns the current audio-thread playhead position in beats (wraps 0..totalBeats).
    double getPlayheadBeats() const;

    // Legacy single-pattern accessor — returns the latest (last) region in the arrangement.
    aidrum::MidiPattern getCurrentPattern() const;

    // Writes the ENTIRE arrangement (every region concatenated) to `dest`
    // as a Type-1 MIDI file. Used by both "Save MIDI" and "Drag to DAW".
    bool writeArrangementAsMidiFile (const juce::File& dest) const;

    // Backwards-compat alias — also dumps the full arrangement.
    bool writeCurrentPatternAsMidiFile (const juce::File& dest) const
    {
        return writeArrangementAsMidiFile (dest);
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Builds a GenerationRequest from current APVTS state.
    aidrum::GenerationRequest buildRequestForMode (aidrum::GenerationMode mode) const;

    void renderArrangementToMidiBuffer (juce::MidiBuffer& midiOut,
                                        int               numSamples,
                                        double            sampleRate,
                                        double            bpm);

    // Maps the Pattern Length choice index to a length in beats.
    static double patternLengthBeatsFromChoice (int choiceIndex);

    juce::AudioProcessorValueTreeState apvts;
    aidrum::AIBackend                  backend;

    mutable std::mutex                  arrangementMutex;
    std::vector<aidrum::MidiPattern>    arrangement; // concatenated regions

    // Playback position (in beats) across blocks when the host isn't providing ppq.
    // Read/written from the audio thread — must be atomic.
    std::atomic<double>      playheadBeats { 0.0 };
    std::atomic<double>      lastBpm       { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessor)
};
