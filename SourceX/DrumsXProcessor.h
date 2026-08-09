#pragma once

#include "GrooveCorpus.h"
#include "KitEngine.h"
#include "PerformanceEngine.h"

#include <JuceHeader.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace hhx
{
    namespace pid
    {
        inline constexpr const char* complexity     = "complexity";
        inline constexpr const char* intensity      = "intensity";
        inline constexpr const char* fillAmount     = "fillAmount";
        inline constexpr const char* fillComplexity = "fillComplexity";
        inline constexpr const char* fillBars       = "fillBars";
        inline constexpr const char* fillStyle      = "fillStyle";
        inline constexpr const char* fillVelVar     = "fillVelVar";
        inline constexpr const char* kickVariation  = "kickVar";
        inline constexpr const char* followSections = "followSect";
        inline constexpr const char* swing          = "swing";
        inline constexpr const char* swingGrid      = "swingGrid";
        inline constexpr const char* humanize       = "humanize";
        inline constexpr const char* feel           = "feel";
        inline constexpr const char* ghost          = "ghost";
        inline constexpr const char* hatOpenness    = "hatOpenness";
        inline constexpr const char* rideMode       = "rideMode";
        inline constexpr const char* halfTime       = "halfTime";
        inline constexpr const char* phraseBars     = "phraseBars";
        inline constexpr const char* timeSigNum     = "timeSigNum";
        inline constexpr const char* timeSigDen     = "timeSigDen";
        inline constexpr const char* tempoMode      = "tempoMode";
        inline constexpr const char* manualBpm      = "manualBpm";
        inline constexpr const char* preset         = "preset";
        inline constexpr const char* variationRhythm = "varRhythm";
        inline constexpr const char* variationCymbal = "varCymbal";
        inline constexpr const char* manualMode     = "manualMode";
        inline constexpr const char* outputLevel    = "outputLevel";

        juce::String laneEnable (int lane);
        juce::String laneGhost (int lane);
        juce::String laneSwitch (int lane);
        juce::String laneGain (int lane);
        juce::String lanePan (int lane);
        juce::String laneTune (int lane);
        juce::String laneDamp (int lane);
    }

    /** The rock characters the plugin ships. Each is a starting point on the
        complexity / intensity plane plus a feel bias, and points at a cluster
        of the human corpus - the actual notes always come from real takes.
    */
    struct Character
    {
        const char* name;
        int   corpusCharacter;
        float complexity;
        float intensity;
        float swing;
        float ghost;
        float hatOpenness;
        bool  ride;
    };

    const std::vector<Character>& characters();

    const char* drumsXLaneName (int lane);
    int         drumsXLaneMidiNote (int lane);

    /** An immutable, fully rendered stretch of performance. The audio thread
        only ever reads one of these; every corpus lookup happens on the
        message thread when a control actually changes.
    */
    struct Timeline
    {
        std::vector<Hit> hits;
        float beatsPerBar = 4.0f;
        int   numBars     = 64;
        std::uint64_t hash = 0;
    };

    class DrumsXProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
    {
    public:
        DrumsXProcessor();
        ~DrumsXProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool   acceptsMidi()  const override { return false; }
        bool   producesMidi() const override { return true; }
        bool   isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 2.0; }

        int  getNumPrograms() override { return 1; }
        int  getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        // --- plugin API -------------------------------------------------
        juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
        KitEngine&                          getKit()   { return kit; }

        PerformanceSettings buildSettings() const;

        /** New seed → a different but equally human performance. Logic's
            "Regenerate". */
        void regenerate();
        std::uint64_t getSeed() const { return seed.load(); }

        void applyCharacter (int index);

        /** Hits for a bar range, honouring manual mode. Message thread only. */
        std::vector<Hit> renderBars (int startBar, int numBars) const;

        /** The nearest real takes to the current XY position, for the
            landing-zone display. */
        std::vector<int> getLandingZone (int maxResults) const;

        const GrooveCorpus& getCorpus() const { return corpus; }

        /** The arrangement the performance follows when "Follow Arrangement" is
            on. Hosts that expose markers can drive this; otherwise it holds the
            default song form. */
        void setSections (std::vector<SectionSpan> spans);
        std::vector<SectionSpan> getSections() const;

        std::shared_ptr<const Timeline> getTimeline() const;

        // --- transport (standalone) --------------------------------------
        void play();
        void stop();
        bool isPlaying() const { return playing.load(); }
        double getPlayheadBeats() const { return playheadBeats.load(); }

        // --- manual page --------------------------------------------------
        static constexpr int kManualBars  = 2;
        static constexpr int kManualSteps = 16 * kManualBars;

        void  setManualStep (int lane, int step, float velocity01);
        float getManualStep (int lane, int step) const;
        void  clearManual();
        bool  isManualMode() const;

        // --- export ---------------------------------------------------------
        bool exportArrangementMidi (const juce::File& dest, int numBars) const;
        int  exportPerInstrumentMidi (const juce::File& folder, int numBars) const;

        // --- UI state ---------------------------------------------------------
        float getUiScale() const { return uiScale.load(); }
        void  setUiScale (float s) { uiScale.store (juce::jlimit (0.7f, 1.6f, s)); }

    private:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
        void parameterChanged (const juce::String& id, float value) override;
        void handleAsyncUpdate() override;
        void rebuildTimeline();
        std::uint64_t settingsHash() const;

        juce::MidiMessageSequence buildSequence (int numBars, int laneFilter) const;

        juce::AudioProcessorValueTreeState apvts;
        GrooveCorpus       corpus;
        PerformanceEngine  engine;
        KitEngine          kit;

        std::atomic<std::uint64_t> seed { 20260809 };
        std::atomic<float>         uiScale { 1.0f };

        std::atomic<double> playheadBeats { 0.0 };
        std::atomic<bool>   playing { false };
        std::atomic<double> lastBpm { 120.0 };

        std::shared_ptr<const Timeline> timeline;
        juce::SpinLock                  timelineLock;

        std::vector<SectionSpan>       hostSections;
        mutable juce::SpinLock         sectionLock;

        mutable std::mutex  manualMutex;
        std::array<std::array<float, kManualSteps>, NumLanes> manualGrid {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsXProcessor)
    };
}
