#pragma once

#include "AIBackend.h"
#include "DrumSynth.h"
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

    // --- v0.8.0 Manual Mode API -----------------------------------------
    // 16-bar (default) click-to-edit step grid. When manual mode is on,
    // playback/export use the manual pattern instead of the AI arrangement.
    // The manual pattern stores GM drum notes; the active DrumKit remaps
    // them at render/export time so the KIT combo still alters timbre.
    bool isManualMode() const;
    void setManualMode (bool shouldBeOn);

    // Clickable grid toggles — velocity is 0..1 (default 0.85, ghost ~0.35).
    void setManualCell   (int midiNote, int stepIndex, float velocity);
    void clearManualCell (int midiNote, int stepIndex);
    void clearManualPattern();

    // Copy of the manual pattern for UI rendering (thread-safe).
    // Notes are returned with their GM note numbers (pre-kit remap)
    // so the grid can hit-test against the displayed rows directly.
    aidrum::MidiPattern getManualPattern() const;

    // Commits the current manual pattern as a new region at the end of
    // the arrangement (with kit remapping applied). Lets the user mix
    // hand-built bars in with AI-generated ones.
    void commitManualPatternAsRegion();

    int  getManualNumBars() const;
    void setManualNumBars (int bars);

    // --- v1.0.0 Transport API -------------------------------------------
    // Audio-generating transport for the Standalone app (and anywhere a
    // host doesn't drive the playhead). Play/Pause/Stop/Loop mirror the
    // buttons in the UI.
    enum class TransportState : int { Stopped = 0, Playing, Paused };

    void play();
    void pause();
    void stop();
    void setLooping (bool shouldLoop);
    bool isLooping() const;
    TransportState getTransportState() const;

    // Audio level, 0..1. UI-facing master drum-synth gain so users can
    // tame or push the built-in synthesized kit.
    void  setOutputLevel (float level01);
    float getOutputLevel() const;

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

    // v0.8.0 — manual pattern (16-bar step grid, user-editable).
    mutable std::mutex                  manualMutex;
    aidrum::MidiPattern                 manualPattern;   // GM notes, length = numBars * 4 beats
    int                                 manualNumBars = 16;
    std::atomic<bool>                   manualModeActive { false };

    // Applies the active DrumKit remap to a pattern copy (used when rendering
    // the manual pattern to MIDI — AI patterns already go through AIBackend).
    aidrum::MidiPattern withActiveKitApplied (aidrum::MidiPattern p) const;

    // Playback position (in beats) across blocks when the host isn't providing ppq.
    // Read/written from the audio thread — must be atomic.
    std::atomic<double>      playheadBeats { 0.0 };
    std::atomic<double>      lastBpm       { 120.0 };

    // v1.0.0 — transport state + internal clock (used when no host drives us).
    std::atomic<int>         transportState    { (int) TransportState::Stopped };
    std::atomic<bool>        loopingEnabled    { true };
    std::atomic<bool>        hostTransportSeen { false };
    std::atomic<float>       outputLevel       { 0.85f };

    // v1.0.0 — synthesized drum voice renderer so the Standalone app
    // makes sound out of the box. Plugin hosts still receive MIDI too.
    aidrum::DrumSynth        drumSynth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessor)
};
