#include "DrumsXProcessor.h"
#include "DrumsXEditor.h"

#if __has_include(<CorpusData.h>)
 #include <CorpusData.h>
 #define HHX_HAS_CORPUS 1
#else
 #define HHX_HAS_CORPUS 0
#endif

#include <cmath>

namespace hhx
{
    juce::String pid::laneEnable (int lane) { return "lane" + juce::String (lane) + "On"; }
    juce::String pid::laneGhost (int lane)  { return "lane" + juce::String (lane) + "Gh"; }
    juce::String pid::laneSwitch (int lane) { return "lane" + juce::String (lane) + "Smp"; }
    juce::String pid::laneGain (int lane)   { return "lane" + juce::String (lane) + "Gain"; }
    juce::String pid::lanePan (int lane)    { return "lane" + juce::String (lane) + "Pan"; }
    juce::String pid::laneTune (int lane)   { return "lane" + juce::String (lane) + "Tune"; }
    juce::String pid::laneDamp (int lane)   { return "lane" + juce::String (lane) + "Damp"; }

    const std::vector<Character>& characters()
    {
        // Each character is a landing spot on one human corpus - a cluster plus
        // a feel bias - not a different note generator. That is exactly how
        // Logic's drummers differ from one another.
        static const std::vector<Character> c {
            { "Ethan  -  Pop Rock",    0, 0.35f, 0.45f, 0.06f, 0.55f, 0.10f, false },
            { "Nikki  -  Retro Rock",  1, 0.45f, 0.50f, 0.18f, 0.70f, 0.05f, false },
            { "Jesse  -  Hard Rock",   2, 0.55f, 0.78f, 0.00f, 0.35f, 0.20f, false },
            { "Max    -  Punk Rock",   3, 0.72f, 0.90f, 0.00f, 0.20f, 0.35f, false },
            { "Kane   -  Metal",       4, 0.80f, 0.95f, 0.00f, 0.15f, 0.20f, false },
            { "Ruby   -  Shuffle",     5, 0.50f, 0.55f, 0.55f, 0.75f, 0.05f, false },
            { "Cole   -  Half Time",   6, 0.40f, 0.70f, 0.08f, 0.45f, 0.10f, false },
            { "Logan  -  Roots Rock",  7, 0.45f, 0.58f, 0.14f, 0.62f, 0.10f, false },
            { "Darcy  -  Prog",        8, 0.72f, 0.66f, 0.05f, 0.55f, 0.15f, true  },
        };
        return c;
    }

    namespace
    {
        constexpr int kTimelineBars = 64;

        int laneToMidiNote (int lane) { return laneToNote (lane); }

        const char* prettyLaneName (int lane) { return laneName (lane); }
    }

    const char* drumsXLaneName (int lane) { return prettyLaneName (lane); }
    int drumsXLaneMidiNote (int lane)     { return laneToMidiNote (lane); }

    //==============================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout DrumsXProcessor::createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        const auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };

        StringArray characterNames;
        for (const auto& c : characters())
            characterNames.add (c.name);

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::preset, 1 },
                                                            "Character", characterNames, 2));

        const auto dbStr  = [] (float v, int) { return String (v, 1) + " dB"; };
        const auto semiStr = [] (float v, int) { return String (v, 2) + " st"; };

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::complexity, 1 },
                                                           "Complexity", NormalisableRange<float> (0.0f, 1.0f), 0.45f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::intensity, 1 },
                                                           "Intensity", NormalisableRange<float> (0.0f, 1.0f), 0.55f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fillAmount, 1 },
                                                           "Fills", NormalisableRange<float> (0.0f, 1.0f), 0.35f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fillComplexity, 1 },
                                                           "Fill Complexity", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::fillBars, 1 },
                                                            "Fill Length",
                                                            StringArray { "1/2 Bar", "1 Bar", "2 Bars" }, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::fillStyle, 1 },
                                                            "Fill Style",
                                                            StringArray { "Any", "Straight", "Triplet", "Roll",
                                                                          "Syncopated", "Tom Led", "Cymbal Led" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fillVelVar, 1 },
                                                           "Fill Vel Variation", NormalisableRange<float> (0.0f, 1.0f), 0.3f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::kickVariation, 1 },
                                                           "Kick Variation", NormalisableRange<float> (0.0f, 1.0f), 0.3f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::followSections, 1 },
                                                         "Follow Arrangement", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::swing, 1 },
                                                           "Swing", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::swingGrid, 1 },
                                                            "Swing Grid", StringArray { "1/8", "1/16" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::humanize, 1 },
                                                           "Humanize", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::feel, 1 },
                                                           "Feel", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (
                                                               [] (float v, int)
                                                               {
                                                                   if (v < 0.47f) return String ("Push ") + String (roundToInt ((0.5f - v) * 200.0f));
                                                                   if (v > 0.53f) return String ("Pull ") + String (roundToInt ((v - 0.5f) * 200.0f));
                                                                   return String ("Even");
                                                               })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::ghost, 1 },
                                                           "Ghost Notes", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::hatOpenness, 1 },
                                                           "Hat Openness", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::rideMode, 1 }, "Ride", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::halfTime, 1 }, "Half Time", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::phraseBars, 1 },
                                                            "Phrase", StringArray { "1 Bar", "2 Bars", "4 Bars" }, 1));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { pid::timeSigNum, 1 },
                                                         "Beats / Bar", 2, 12, 4));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::timeSigDen, 1 },
                                                            "Beat Unit", StringArray { "2", "4", "8", "16" }, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::tempoMode, 1 },
                                                            "Tempo", StringArray { "Follow Host", "Manual" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::manualBpm, 1 },
                                                           "BPM", NormalisableRange<float> (40.0f, 260.0f, 0.1f), 120.0f));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { pid::variationRhythm, 1 },
                                                         "Kick / Snare Variation", 0, 7, 0));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { pid::variationCymbal, 1 },
                                                         "Hat / Ride Variation", 0, 7, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::manualMode, 1 }, "Manual Pattern", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::outputLevel, 1 },
                                                           "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));

        for (int lane = 0; lane < NumLanes; ++lane)
        {
            const juce::String n (prettyLaneName (lane));
            layout.add (std::make_unique<AudioParameterBool> (
                ParameterID { pid::laneEnable (lane), 1 }, n + " On", true));
            layout.add (std::make_unique<AudioParameterBool> (
                ParameterID { pid::laneGhost (lane), 1 }, n + " Ghost", false));
            layout.add (std::make_unique<AudioParameterInt> (
                ParameterID { pid::laneSwitch (lane), 1 }, n + " Sample", -4, 4, 0));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneGain (lane), 1 }, n + " Gain",
                NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (dbStr)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::lanePan (lane), 1 }, n + " Pan",
                NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneTune (lane), 1 }, n + " Tune",
                NormalisableRange<float> (-6.0f, 6.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (semiStr)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneDamp (lane), 1 }, n + " Damp",
                NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        }

        return layout;
    }

    //==============================================================================
    DrumsXProcessor::DrumsXProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "HHDX", createLayout())
    {
       #if HHX_HAS_CORPUS
        int size = 0;
        if (const auto* data = CorpusData::getNamedResource ("rock_corpus_hhc", size))
            corpus.loadFromMemory (data, (std::size_t) size);
       #endif
        engine.setCorpus (&corpus);

        kit.loadBundledKit ("SoCalRock");

        // A conventional rock song form. Hosts that expose markers can replace
        // it through setSections(); the performance only follows it when
        // "Follow Arrangement" is on.
        hostSections = { { 0,  4, SectionIntro },  { 4,  8, SectionVerse },
                         { 12, 8, SectionChorus }, { 20, 8, SectionVerse },
                         { 28, 8, SectionChorus }, { 36, 4, SectionBridge },
                         { 40, 8, SectionChorus }, { 48, 8, SectionVerse },
                         { 56, 8, SectionOutro } };

        for (auto* p : getParameters())
            if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                apvts.addParameterListener (withID->paramID, this);

        rebuildTimeline();
    }

    DrumsXProcessor::~DrumsXProcessor()
    {
        for (auto* p : getParameters())
            if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                apvts.removeParameterListener (withID->paramID, this);
    }

    void DrumsXProcessor::parameterChanged (const juce::String& id, float value)
    {
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            if (id == pid::laneSwitch (lane)) kit.setLaneSampleSwitch (lane, (int) value);
            else if (id == pid::laneGain (lane)) kit.setLaneGainDb (lane, value);
            else if (id == pid::lanePan (lane))  kit.setLanePan (lane, value);
            else if (id == pid::laneTune (lane)) kit.setLaneTune (lane, value);
            else if (id == pid::laneDamp (lane)) kit.setLaneDamp (lane, value);
        }

        // Re-render off the audio thread. Nothing the user typed into the
        // manual grid is touched by this.
        triggerAsyncUpdate();
    }

    void DrumsXProcessor::handleAsyncUpdate()
    {
        rebuildTimeline();
    }

    std::uint64_t DrumsXProcessor::settingsHash() const
    {
        std::uint64_t h = 1469598103934665603ull;
        const auto feed = [&h] (double v)
        {
            const auto bits = (std::uint64_t) std::llround (v * 100000.0);
            h = (h ^ bits) * 1099511628211ull;
        };

        for (auto* p : getParameters())
            feed (p->getValue());

        feed ((double) seed.load());
        if (isManualMode())
        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            for (const auto& lane : manualGrid)
                for (float v : lane)
                    feed (v);
        }
        return h;
    }

    void DrumsXProcessor::rebuildTimeline()
    {
        auto next = std::make_shared<Timeline>();
        const auto s = buildSettings();
        next->beatsPerBar = s.beatsPerBar;
        next->numBars     = kTimelineBars;
        next->hash        = settingsHash();
        next->hits        = renderBars (0, kTimelineBars);

        const juce::SpinLock::ScopedLockType sl (timelineLock);
        timeline = std::move (next);
    }

    std::shared_ptr<const Timeline> DrumsXProcessor::getTimeline() const
    {
        const juce::SpinLock::ScopedLockType sl (timelineLock);
        return timeline;
    }

    void DrumsXProcessor::applyCharacter (int index)
    {
        const auto& list = characters();
        if (index < 0 || index >= (int) list.size())
            return;

        const auto& c = list[(std::size_t) index];
        const auto set = [this] (const char* id, float v)
        {
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };

        if (auto* p = apvts.getParameter (pid::preset))
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
                if (choice->getIndex() != index)
                    choice->setValueNotifyingHost (choice->convertTo0to1 ((float) index));

        set (pid::complexity,  c.complexity);
        set (pid::intensity,   c.intensity);
        set (pid::swing,       c.swing);
        set (pid::ghost,       c.ghost);
        set (pid::hatOpenness, c.hatOpenness);
        set (pid::rideMode,    c.ride ? 1.0f : 0.0f);
    }

    PerformanceSettings DrumsXProcessor::buildSettings() const
    {
        const auto get = [this] (const juce::String& id)
        {
            const auto* p = apvts.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        PerformanceSettings s;
        s.complexity     = get (pid::complexity);
        s.intensity      = get (pid::intensity);
        s.fillAmount     = get (pid::fillAmount);
        s.fillComplexity = get (pid::fillComplexity);
        const int fillLenIdx = (int) get (pid::fillBars);
        s.fillLengthBars = fillLenIdx == 0 ? 0.5f : (fillLenIdx == 1 ? 1.0f : 2.0f);
        s.fillVelVar     = get (pid::fillVelVar);
        s.kickVariation  = get (pid::kickVariation);
        s.followSections = get (pid::followSections) > 0.5f;

        const int styleIdx = (int) get (pid::fillStyle);
        static const std::uint8_t styles[] = { 0, FillStraight, FillTriplet, FillRoll,
                                               FillSyncopated, FillTomLed, FillCymbalLed };
        s.fillStyleMask  = styleIdx > 0 && styleIdx < (int) std::size (styles)
                         ? styles[styleIdx] : (std::uint8_t) 0;

        const int charIdx = juce::jlimit (0, (int) characters().size() - 1, (int) get (pid::preset));
        s.character      = characters()[(std::size_t) charIdx].corpusCharacter;
        s.swing          = get (pid::swing);
        s.swingSixteenth = get (pid::swingGrid) > 0.5f;
        s.humanize       = get (pid::humanize);
        s.feel           = get (pid::feel);
        s.ghostAmount    = get (pid::ghost);
        s.hatOpenness    = get (pid::hatOpenness);
        s.rideInsteadOfHat = get (pid::rideMode) > 0.5f;
        s.halfTime       = get (pid::halfTime) > 0.5f;

        const int phraseChoice = (int) get (pid::phraseBars);
        s.phraseBars     = phraseChoice == 0 ? 1 : (phraseChoice == 1 ? 2 : 4);

        s.timeSigNum     = juce::jlimit (2, 12, (int) get (pid::timeSigNum));
        const int denIdx = (int) get (pid::timeSigDen);
        s.timeSigDen     = denIdx == 0 ? 2 : (denIdx == 1 ? 4 : (denIdx == 2 ? 8 : 16));
        s.beatsPerBar    = (float) s.timeSigNum * 4.0f / (float) s.timeSigDen;

        s.variationRhythm = (int) get (pid::variationRhythm);
        s.variationCymbal = (int) get (pid::variationCymbal);

        std::uint32_t mask = 0;
        std::uint32_t ghost = 0;
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            if (get (pid::laneEnable (lane)) > 0.5f)
                mask |= (1u << lane);
            if (get (pid::laneGhost (lane)) > 0.5f)
                ghost |= (1u << lane);
        }
        s.laneMask     = mask;
        s.fillLaneMask = mask;
        s.ghostMask    = ghost;

        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            s.sections = hostSections;
        }

        s.seed = seed.load();
        return s;
    }

    void DrumsXProcessor::setSections (std::vector<SectionSpan> spans)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            hostSections = std::move (spans);
        }
        triggerAsyncUpdate();
    }

    std::vector<SectionSpan> DrumsXProcessor::getSections() const
    {
        const juce::SpinLock::ScopedLockType sl (sectionLock);
        return hostSections;
    }

    std::vector<int> DrumsXProcessor::getLandingZone (int maxResults) const
    {
        return engine.landingZone (buildSettings(), maxResults);
    }

    void DrumsXProcessor::regenerate()
    {
        seed.store (seed.load() * 6364136223846793005ull + 1442695040888963407ull);
        rebuildTimeline();
        updateHostDisplay();
    }

    //==============================================================================
    bool DrumsXProcessor::isManualMode() const
    {
        const auto* p = apvts.getRawParameterValue (pid::manualMode);
        return p != nullptr && p->load() > 0.5f;
    }

    void DrumsXProcessor::setManualStep (int lane, int step, float velocity01)
    {
        if (lane < 0 || lane >= NumLanes || step < 0 || step >= kManualSteps)
            return;
        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            manualGrid[(std::size_t) lane][(std::size_t) step] = juce::jlimit (0.0f, 1.0f, velocity01);
        }
        triggerAsyncUpdate();
    }

    float DrumsXProcessor::getManualStep (int lane, int step) const
    {
        if (lane < 0 || lane >= NumLanes || step < 0 || step >= kManualSteps)
            return 0.0f;
        const std::lock_guard<std::mutex> lock (manualMutex);
        return manualGrid[(std::size_t) lane][(std::size_t) step];
    }

    void DrumsXProcessor::clearManual()
    {
        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            for (auto& lane : manualGrid)
                lane.fill (0.0f);
        }
        triggerAsyncUpdate();
    }

    //==============================================================================
    std::vector<Hit> DrumsXProcessor::renderBars (int startBar, int numBars) const
    {
        const auto s = buildSettings();

        if (! isManualMode())
            return engine.renderBars (s, startBar, numBars);

        // Manual mode plays the user's grid verbatim, looping every two bars.
        // Nothing the generator does can touch it.
        std::vector<Hit> out;
        const std::lock_guard<std::mutex> lock (manualMutex);
        for (int bar = 0; bar < numBars; ++bar)
        {
            const int  sourceBar = (startBar + bar) % kManualBars;
            const float barStart = (float) (startBar + bar) * s.beatsPerBar;
            for (int lane = 0; lane < NumLanes; ++lane)
            {
                if ((s.laneMask & (1u << lane)) == 0)
                    continue;
                for (int step = 0; step < 16; ++step)
                {
                    const float v = manualGrid[(std::size_t) lane][(std::size_t) (sourceBar * 16 + step)];
                    if (v <= 0.0f)
                        continue;
                    const float beat = barStart + s.beatsPerBar * ((float) step / 16.0f);
                    out.push_back ({ beat, (std::uint8_t) lane,
                                     (std::uint8_t) juce::jlimit (1, 127, juce::roundToInt (v * 127.0f)) });
                }
            }
        }
        std::sort (out.begin(), out.end(), [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
        return out;
    }

    //==============================================================================
    void DrumsXProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        kit.prepare (sampleRate, samplesPerBlock);
        playheadBeats.store (0.0);
    }

    void DrumsXProcessor::releaseResources()
    {
        kit.allNotesOff();
    }

    bool DrumsXProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    }

    void DrumsXProcessor::play()
    {
        playheadBeats.store (0.0);
        playing.store (true);
    }

    void DrumsXProcessor::stop()
    {
        playing.store (false);
        kit.allNotesOff();
    }

    void DrumsXProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        midi.clear();

        const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const int    numSamples = buffer.getNumSamples();

        const auto* tempoModeParam = apvts.getRawParameterValue (pid::tempoMode);
        const bool  manualTempo = tempoModeParam != nullptr && tempoModeParam->load() > 0.5f;

        double bpm = manualTempo ? (double) apvts.getRawParameterValue (pid::manualBpm)->load() : 120.0;
        bool   transportRunning = playing.load();
        double blockStartBeat   = playheadBeats.load();

        if (auto* ph = getPlayHead())
        {
            if (const auto pos = ph->getPosition())
            {
                if (! manualTempo)
                    if (const auto hostBpm = pos->getBpm())
                        bpm = *hostBpm;

                if (pos->getIsPlaying())
                {
                    transportRunning = true;
                    if (const auto ppq = pos->getPpqPosition())
                        blockStartBeat = *ppq;
                }
                else if (! playing.load())
                {
                    transportRunning = false;
                }
            }
        }

        lastBpm.store (bpm);

        if (! transportRunning)
        {
            kit.renderNextBlock (buffer, 0, numSamples);
            return;
        }

        const double beatsPerSample = bpm / (60.0 * sampleRate);
        const double blockEndBeat   = blockStartBeat + beatsPerSample * numSamples;

        // The audio thread never touches the corpus; it reads a snapshot that
        // the message thread rendered when a control last changed.
        std::shared_ptr<const Timeline> tl;
        {
            const juce::SpinLock::ScopedTryLockType sl (timelineLock);
            if (sl.isLocked())
                tl = timeline;
        }

        if (tl == nullptr || tl->hits.empty())
        {
            kit.renderNextBlock (buffer, 0, numSamples);
            playheadBeats.store (blockEndBeat);
            return;
        }

        const double loopBeats = (double) tl->beatsPerBar * tl->numBars;
        const double loopStart = std::fmod (blockStartBeat, loopBeats);
        const double loopEnd   = loopStart + (blockEndBeat - blockStartBeat);

        const auto emit = [&] (double from, double to, double timeOrigin)
        {
            for (const auto& h : tl->hits)
            {
                if ((double) h.beat < from)
                    continue;
                if ((double) h.beat >= to)
                    break;

                const int offset = juce::jlimit (0, numSamples - 1,
                                                 (int) std::llround (((double) h.beat - timeOrigin) / beatsPerSample));
                const int note = laneToMidiNote (h.lane);
                midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) h.velocity), offset);
                midi.addEvent (juce::MidiMessage::noteOff (10, note), juce::jmin (numSamples - 1, offset + 8));
                kit.noteOn (h.lane, (float) h.velocity / 127.0f, h.variant);
            }
        };

        if (loopEnd <= loopBeats)
        {
            emit (loopStart, loopEnd, loopStart);
        }
        else
        {
            emit (loopStart, loopBeats, loopStart);
            emit (0.0, loopEnd - loopBeats, loopStart - loopBeats);
        }

        kit.renderNextBlock (buffer, 0, numSamples);

        const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (pid::outputLevel)->load());
        buffer.applyGain (gain);

        playheadBeats.store (playing.load() ? std::fmod (blockEndBeat, loopBeats) : blockEndBeat);
    }

    //==============================================================================
    juce::MidiMessageSequence DrumsXProcessor::buildSequence (int numBars, int laneFilter) const
    {
        juce::MidiMessageSequence seq;
        const auto s = buildSettings();
        const auto hits = renderBars (0, numBars);

        for (const auto& h : hits)
        {
            if (laneFilter >= 0 && h.lane != laneFilter)
                continue;
            const int note = laneToMidiNote (h.lane);
            seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) h.velocity), (double) h.beat);
            seq.addEvent (juce::MidiMessage::noteOff (10, note), (double) h.beat + 0.05);
        }
        seq.updateMatchedPairs();
        juce::ignoreUnused (s);
        return seq;
    }

    bool DrumsXProcessor::exportArrangementMidi (const juce::File& dest, int numBars) const
    {
        const auto s = buildSettings();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (960);

        juce::MidiMessageSequence meta;
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (s.timeSigNum, s.timeSigDen), 0.0);
        meta.addEvent (juce::MidiMessage::tempoMetaEvent (
            (int) std::llround (60'000'000.0 / lastBpm.load())), 0.0);
        meta.addEvent (juce::MidiMessage::textMetaEvent (3, "HumHouse Drums X"), 0.0);
        file.addTrack (meta);

        auto seq = buildSequence (numBars, -1);
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Drums"), 0.0);
        file.addTrack (seq);

        dest.getParentDirectory().createDirectory();
        if (auto stream = dest.createOutputStream())
        {
            stream->setPosition (0);
            stream->truncate();
            return file.writeTo (*stream);
        }
        return false;
    }

    int DrumsXProcessor::exportPerInstrumentMidi (const juce::File& folder, int numBars) const
    {
        if (! folder.createDirectory())
            return 0;

        const auto s = buildSettings();
        int written = 0;

        for (int lane = 0; lane < NumLanes; ++lane)
        {
            auto seq = buildSequence (numBars, lane);
            if (seq.getNumEvents() == 0)
                continue;

            juce::MidiFile file;
            file.setTicksPerQuarterNote (960);

            juce::MidiMessageSequence meta;
            meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (s.timeSigNum, s.timeSigDen), 0.0);
            meta.addEvent (juce::MidiMessage::tempoMetaEvent (
                (int) std::llround (60'000'000.0 / lastBpm.load())), 0.0);
            meta.addEvent (juce::MidiMessage::textMetaEvent (3, prettyLaneName (lane)), 0.0);
            file.addTrack (meta);
            file.addTrack (seq);

            const auto dest = folder.getChildFile (juce::String (lane + 1).paddedLeft ('0', 2)
                                                   + " " + juce::String (prettyLaneName (lane)).replace (" ", "") + ".mid");
            if (auto stream = dest.createOutputStream())
            {
                stream->setPosition (0);
                stream->truncate();
                if (file.writeTo (*stream))
                    ++written;
            }
        }
        return written;
    }

    //==============================================================================
    void DrumsXProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = apvts.copyState();
        state.setProperty ("seed", (juce::int64) seed.load(), nullptr);
        state.setProperty ("uiScale", uiScale.load(), nullptr);

        juce::MemoryOutputStream grid;
        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            for (const auto& lane : manualGrid)
                for (float v : lane)
                    grid.writeFloat (v);
        }
        state.setProperty ("manualGrid", grid.getMemoryBlock().toBase64Encoding(), nullptr);

        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void DrumsXProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr)
            return;

        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid())
            return;

        if (state.hasProperty ("seed"))
            seed.store ((std::uint64_t) (juce::int64) state.getProperty ("seed"));
        if (state.hasProperty ("uiScale"))
            setUiScale ((float) state.getProperty ("uiScale"));

        if (state.hasProperty ("manualGrid"))
        {
            juce::MemoryBlock block;
            if (block.fromBase64Encoding (state.getProperty ("manualGrid").toString()))
            {
                juce::MemoryInputStream grid (block, false);
                const std::lock_guard<std::mutex> lock (manualMutex);
                for (auto& lane : manualGrid)
                    for (float& v : lane)
                        v = grid.getNumBytesRemaining() >= 4 ? grid.readFloat() : 0.0f;
            }
        }

        apvts.replaceState (state);

        for (int lane = 0; lane < NumLanes; ++lane)
        {
            const auto load = [this] (const juce::String& id)
            {
                const auto* p = apvts.getRawParameterValue (id);
                return p != nullptr ? p->load() : 0.0f;
            };
            kit.setLaneSampleSwitch (lane, (int) load (pid::laneSwitch (lane)));
            kit.setLaneGainDb (lane, load (pid::laneGain (lane)));
            kit.setLanePan (lane, load (pid::lanePan (lane)));
            kit.setLaneTune (lane, load (pid::laneTune (lane)));
            kit.setLaneDamp (lane, load (pid::laneDamp (lane)));
        }
    }

    juce::AudioProcessorEditor* DrumsXProcessor::createEditor()
    {
        return new DrumsXEditor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hhx::DrumsXProcessor();
}
