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
        inline constexpr const char* sectionLevel   = "sectionLevel";
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
        inline constexpr const char* micBlend       = "micBlend";
        inline constexpr const char* bleed          = "bleed";
        inline constexpr const char* crush          = "crush";
        inline constexpr const char* roomSize       = "roomSize";
        inline constexpr const char* roomDamping    = "roomDamping";
        inline constexpr const char* roomMix        = "roomMix";
        inline constexpr const char* roomSpace      = "roomSpace";
        inline constexpr const char* roomDuck       = "roomDuck";
        inline constexpr const char* mixVoicing     = "mixVoicing";
        inline constexpr const char* punch          = "punch";
        inline constexpr const char* glue           = "glue";
        inline constexpr const char* drive          = "drive";

        juce::String laneEnable (int lane);
        juce::String laneGhost (int lane);
        juce::String laneSwitch (int lane);
        juce::String laneGain (int lane);
        juce::String lanePan (int lane);
        juce::String laneTune (int lane);
        juce::String laneDamp (int lane);
        juce::String laneComp (int lane);
        juce::String laneSend (int lane);
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

        // --- arrangement strip ---------------------------------------------
        /** The song, block by block. The performance knobs always edit the
            selected block, so a chorus can be pushed without the verses either
            side of it changing a note. */
        std::vector<ArrangementSection> getArrangement() const;
        int  numSections() const;
        int  totalArrangementBars() const;
        int  sectionStartBar (int index) const;

        int  getSelectedSection() const { return selectedSection.load(); }
        void setSelectedSection (int index);

        /** Logic's "+": append another block, for as long as the song needs. */
        void addSection();
        void duplicateSection (int index);
        void removeSection (int index);
        void setSectionBars (int index, int bars);
        void setSectionType (int index, int section);

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

        /** Where installed content lives, or an invalid file when only the
            bundled fallback content is present. */
        static juce::File findSharedContentFolder();

        /** The content tree under one data root, in either the plain layout
            (Windows, Linux) or the Application Support layout macOS installers
            use. Invalid when the root holds no content. */
        static juce::File contentFolderUnder (const juce::File& root);

        /** Human-readable description of what the instrument is playing from,
            e.g. "installed content 3" or "bundled content". */
        juce::String getContentDescription() const { return contentDescription; }

        // --- kit browser ------------------------------------------------------
        /** The kits the installed content offers, in manifest order. */
        juce::StringArray getAvailableKits() const { return kitNames; }
        int  getSelectedKit() const { return selectedKit.load(); }

        /** Loads another of those kits. Message thread only: strokes are dropped
            while the swap runs, ringing voices keep their own samples, and the
            choice is saved with the project. */
        void selectKit (int index);

        // --- UI state ---------------------------------------------------------
        float getUiScale() const { return uiScale.load(); }
        void  setUiScale (float s) { uiScale.store (juce::jlimit (0.7f, 1.6f, s)); }

    private:
        /** MidiFile timestamps are ticks, so exported sequences are built in
            ticks and every note gets a 32nd of sounding length. */
        static constexpr double kTicksPerQuarter = 960.0;
        static constexpr double kNoteTicks       = 120.0;

        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
        void parameterChanged (const juce::String& id, float value) override;
        void handleAsyncUpdate() override;
        void rebuildTimeline();
        void loadContent();
        void pushRoomParameters();
        void pushMixParameters();
        std::uint64_t settingsHash() const;

        juce::MidiMessageSequence buildSequence (int numBars, int laneFilter) const;

        /** Output level plus a soft ceiling, applied however the block was
            rendered. */
        void applyOutputStage (juce::AudioBuffer<float>& buffer);

        /** A stroke waiting for its own sample offset inside the block. Owned
            by the audio thread, sized so a dense bar never needs to allocate. */
        struct PendingHit
        {
            int          offset   = 0;
            std::uint8_t lane     = 0;
            std::uint8_t velocity = 0;
            std::uint8_t variant  = 0;
        };

        std::array<PendingHit, 256> pending {};

        juce::AudioProcessorValueTreeState apvts;
        GrooveCorpus       corpus;
        PerformanceEngine  engine;
        KitEngine          kit;

        juce::String contentDescription { "bundled content" };

        juce::StringArray kitNames;
        std::vector<juce::File> kitFolders;
        std::atomic<int>   selectedKit { 0 };

        std::atomic<std::uint64_t> seed { 20260809 };
        std::atomic<float>         uiScale { 1.0f };

        std::atomic<double> playheadBeats { 0.0 };
        std::atomic<bool>   playing { false };
        std::atomic<double> lastBpm { 120.0 };

        std::shared_ptr<const Timeline> timeline;
        juce::SpinLock                  timelineLock;

        std::vector<SectionSpan>       hostSections;
        mutable juce::SpinLock         sectionLock;

        std::vector<ArrangementSection> arrangement;
        std::atomic<int>                selectedSection { 0 };
        std::atomic<bool>               syncingSection { false };
        int                             nextSectionId = 1;

        /** Copies the performance parameters into the selected block, and the
            other way round when the selection changes. */
        void captureParamsIntoSelectedSection();
        void pushSectionToParams (int index);

        mutable std::mutex  manualMutex;
        std::array<std::array<float, kManualSteps>, NumLanes> manualGrid {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsXProcessor)
    };
}
