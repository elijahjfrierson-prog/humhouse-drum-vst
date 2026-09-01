#include "DrumsXProcessor.h"
#include "DrumsXEditor.h"

#if __has_include(<CorpusData.h>)
 #include <CorpusData.h>
 #define HHX_HAS_CORPUS 1
#else
 #define HHX_HAS_CORPUS 0
#endif

#include <cmath>
#include <map>
#include <utility>

namespace hhx
{
    juce::String pid::laneEnable (int lane) { return "lane" + juce::String (lane) + "On"; }
    juce::String pid::laneGhost (int lane)  { return "lane" + juce::String (lane) + "Gh"; }
    juce::String pid::laneSwitch (int lane) { return "lane" + juce::String (lane) + "Smp"; }
    juce::String pid::laneGain (int lane)   { return "lane" + juce::String (lane) + "Gain"; }
    juce::String pid::lanePan (int lane)    { return "lane" + juce::String (lane) + "Pan"; }
    juce::String pid::laneTune (int lane)   { return "lane" + juce::String (lane) + "Tune"; }
    juce::String pid::laneDamp (int lane)   { return "lane" + juce::String (lane) + "Damp"; }
    juce::String pid::laneComp (int lane)   { return "lane" + juce::String (lane) + "Comp"; }
    juce::String pid::laneEqLow (int lane)  { return "lane" + juce::String (lane) + "EqLo"; }
    juce::String pid::laneEqMid (int lane)  { return "lane" + juce::String (lane) + "EqMd"; }
    juce::String pid::laneEqHigh (int lane) { return "lane" + juce::String (lane) + "EqHi"; }
    juce::String pid::laneSend (int lane)   { return "lane" + juce::String (lane) + "Send"; }

    /** How much of each piece the room hears by default. A kit in a room is not
        a kit with reverb on it: the kick is mostly floor, the snare and toms
        throw a lot into the space, and the cymbals live in it. Setting these to
        zero, as they were, is what made every stroke stop dead where its
        recording ended. */
    static float defaultRoomSend (int lane)
    {
        if (lane == LaneKick)       return 0.10f;
        if (lane == LaneHatPedal)   return 0.08f;
        if (isHatLane (lane))       return 0.16f;
        if (lane == LaneSideStick || lane == LaneSnareRim) return 0.26f;
        if (isSnareLane (lane))     return 0.34f;
        if (isTomLane (lane))       return 0.40f;
        if (isRideLane (lane))      return 0.28f;
        if (isCymbalLane (lane))    return 0.44f;
        if (lane == LanePerc)       return 0.30f;
        return 0.20f;
    }

    const std::vector<Character>& characters()
    {
        // Each character is a landing spot on one human corpus - a cluster plus
        // a feel bias - not a different note generator. That is exactly how
        // Logic's drummers differ from one another.
        static const std::vector<Character> c {
            { "Ethan  -  Pop Rock",    0, 0.35f, 0.45f, 0.06f, 0.55f, 0.10f, false, false },
            { "Nikki  -  Retro Rock",  1, 0.45f, 0.50f, 0.18f, 0.70f, 0.05f, false, false },
            { "Jesse  -  Hard Rock",   2, 0.55f, 0.78f, 0.00f, 0.35f, 0.20f, false, false },
            { "Max    -  Punk Rock",   3, 0.72f, 0.90f, 0.00f, 0.20f, 0.35f, false, false },
            { "Kane   -  Metal",       4, 0.80f, 0.95f, 0.00f, 0.15f, 0.20f, false, false },
            { "Ruby   -  Shuffle",     5, 0.50f, 0.55f, 0.55f, 0.75f, 0.05f, false, false },
            // Cole is the half-time drummer: picking him plays half time, it is
            // not a bias the groove search can talk him out of.
            { "Cole   -  Half Time",   6, 0.40f, 0.70f, 0.08f, 0.45f, 0.10f, false, true  },
            { "Logan  -  Roots Rock",  7, 0.45f, 0.58f, 0.14f, 0.62f, 0.10f, false, false },
            { "Darcy  -  Prog",        8, 0.72f, 0.66f, 0.05f, 0.55f, 0.15f, true,  false },
        };
        return c;
    }

    namespace
    {
        /** Shortest window the timeline is rendered over, so short songs still
            play through several times before anything repeats. */
        constexpr int kMinRenderBars = 64;

        /** The knobs that belong to an arrangement block rather than the song. */
        bool isSectionParameter (const juce::String& id)
        {
            return id == pid::complexity || id == pid::intensity
                || id == pid::sectionLevel
                || id == pid::fillAmount || id == pid::swing
                || id == pid::halfTime
                || id == pid::variationRhythm || id == pid::variationCymbal;
        }

        /** The kit-piece switches belong to a block too: a toms-only intro is a
            property of the intro, not of the song. */
        bool isSectionLaneParameter (const juce::String& id)
        {
            for (int lane = 0; lane < NumLanes; ++lane)
                if (id == pid::laneEnable (lane) || id == pid::laneGhost (lane))
                    return true;
            return false;
        }

        int laneToMidiNote (int lane) { return laneToNote (lane); }

        const char* prettyLaneName (int lane) { return laneName (lane); }

        /** Where a piece sits when you are sat behind the kit: kick and snare
            under you, hats out to the left hand, ride and china over the right,
            and the toms travelling across the front from rack to floor.
        */
        float seatPan (int lane)
        {
            switch (lane)
            {
                case LaneKick:                                          return  0.00f;
                case LaneSnare: case LaneSnareRim: case LaneSideStick:
                case LaneSnareGhost: case LaneSnareFlam:
                case LaneSnareRoll:                                     return -0.08f;

                case LaneHatClosed: case LaneHatTight:  case LaneHatOpen1:
                case LaneHatOpen2:  case LaneHatOpen3:  case LaneHatOpen4:
                case LaneHatPedal:  case LaneHatSplash: case LaneHatBell:
                                                                        return -0.38f;

                case LaneRideBow: case LaneRideBell:
                case LaneRideEdge: case LaneRideCrash:                  return  0.42f;

                case LaneCrashL:                                        return -0.62f;
                case LaneCrashR:                                        return  0.62f;
                case LaneCrash3:                                        return  0.30f;
                case LaneChina:                                         return  0.70f;
                case LaneSplash:                                        return -0.30f;

                case LaneTom1:                                          return -0.22f;
                case LaneTom2:                                          return  0.02f;
                case LaneTom3:                                          return  0.30f;
                case LaneTom4:                                          return  0.52f;

                case LanePerc:                                          return  0.45f;
                default:                                                return  0.0f;
            }
        }
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
                                                           // Opens on the simple side, a little above the middle for
                                                           // loudness: a plain groove played with weight, which is
                                                           // where a session starts rather than in the busy corner.
                                                           "Complexity", NormalisableRange<float> (0.0f, 1.0f), 0.28f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::intensity, 1 },
                                                           // Which take gets played, not a level: it opens low, on
                                                           // the plain readings of a groove, so a song starts on
                                                           // the part a drummer would play it with.
                                                           "Loud", NormalisableRange<float> (0.0f, 1.0f), 0.25f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::sectionLevel, 1 },
                                                           "Section Intensity", NormalisableRange<float> (0.0f, 1.0f), 0.55f,
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
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::manualBars, 1 },
                                                           "Pattern Bars",
                                                           juce::StringArray { "1 Bar", "2 Bars", "4 Bars" }, 1));
        // How the drummer is started. "Always Play" is the old behaviour; the
        // other two put the host's MIDI in charge, so the kit can enter at bar
        // 8 of a song, or play notes drawn in the DAW's piano roll verbatim.
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::triggerMode, 1 }, "Trigger",
                                                            StringArray { "Always Play", "When MIDI Held", "Play My Notes" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::outputLevel, 1 },
                                                           "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::micBlend, 1 }, "Mic Blend",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::bleed, 1 }, "Bleed",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.15f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::crush, 1 }, "Mono Crush",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::roomSize, 1 }, "Room Size",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::roomDamping, 1 }, "Room Damping",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::roomMix, 1 }, "Room Return",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.22f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::roomSpace, 1 }, "Space",
                                                           StringArray { "Dry", "Studio", "Room", "Hall", "Plate" },
                                                           KitEngine::SpaceRoom));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::roomDuck, 1 }, "Room Duck",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::mixVoicing, 1 }, "Mix",
                                                           StringArray { "Raw", "Modern", "Punch", "Room", "Vintage" },
                                                           KitEngine::MixModern));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::punch, 1 }, "Punch",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::glue, 1 }, "Glue",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::drive, 1 }, "Drive",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.2f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::squeeze, 1 }, "Squeeze",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::master, 1 }, "Master",
                                                           // On by default, and far enough up to be
                                                           // heard: the kit should arrive glued and
                                                           // loud rather than needing a master chain
                                                           // built around it before it sounds like a
                                                           // record.
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::squeezeGlow, 1 }, "Glow",
                                                           StringArray { "Off", "Clean", "Tube", "Tape", "Transformer" },
                                                           KitEngine::GlowClean));

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
                NormalisableRange<float> (-1.0f, 1.0f, 0.01f), seatPan (lane)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneTune (lane), 1 }, n + " Tune",
                NormalisableRange<float> (-6.0f, 6.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (semiStr)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneDamp (lane), 1 }, n + " Damp",
                NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneComp (lane), 1 }, n + " Comp",
                NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneEqLow (lane), 1 }, n + " Low",
                NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (dbStr)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneEqMid (lane), 1 }, n + " Mid",
                NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (dbStr)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneEqHigh (lane), 1 }, n + " High",
                NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (dbStr)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid::laneSend (lane), 1 }, n + " Room Send",
                NormalisableRange<float> (0.0f, 1.0f, 0.01f), defaultRoomSend (lane),
                AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        }

        return layout;
    }

    //==============================================================================
    DrumsXProcessor::DrumsXProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "HHDX", createLayout())
    {
        loadContent();
        engine.setCorpus (&corpus);

        kit.setMicBlend (apvts.getRawParameterValue (pid::micBlend)->load());
        kit.setBleed (apvts.getRawParameterValue (pid::bleed)->load());
        kit.setCrush (apvts.getRawParameterValue (pid::crush)->load());
        pushMixParameters();

        // A conventional rock song form. Hosts that expose markers can replace
        // it through setSections(); the performance only follows it when
        // "Follow Arrangement" is on.
        // One eight-bar block to start with; "+" appends as many more as the
        // song needs, each with its own settings.
        arrangement.push_back ({ nextSectionId++, 8, SectionVerse });
        captureParamsIntoSelectedSection();

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
            else if (id == pid::laneComp (lane)) kit.setLaneCompression (lane, value);
            else if (id == pid::laneEqLow (lane)) kit.setLaneEqLowDb (lane, value);
            else if (id == pid::laneEqMid (lane)) kit.setLaneEqMidDb (lane, value);
            else if (id == pid::laneEqHigh (lane)) kit.setLaneEqHighDb (lane, value);
            else if (id == pid::laneSend (lane)) kit.setLaneReverbSend (lane, value);
        }

        if (id == pid::micBlend)   kit.setMicBlend (value);
        else if (id == pid::bleed) kit.setBleed (value);
        else if (id == pid::crush) kit.setCrush (value);
        else if (id == pid::roomSize || id == pid::roomDamping || id == pid::roomMix
                 || id == pid::roomSpace || id == pid::roomDuck)
            pushRoomParameters();
        else if (id == pid::mixVoicing || id == pid::punch || id == pid::glue
                 || id == pid::drive || id == pid::squeeze || id == pid::squeezeGlow
                 || id == pid::master)
            pushMixParameters();

        // A performance knob edits the block that is selected, and only that
        // block; the rest of the arrangement re-renders to exactly what it was.
        if (! syncingSection.load()
            && (isSectionParameter (id) || isSectionLaneParameter (id)))
            captureParamsIntoSelectedSection();

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
        feed (std::round (lastBpm.load()));

        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            for (const auto& sec : arrangement)
            {
                feed ((double) sec.id);
                feed ((double) sec.numBars);
                feed ((double) sec.section);
                feed ((double) sec.complexity);
                feed ((double) sec.intensity);
                feed ((double) sec.velocity);
                feed ((double) sec.fillAmount);
                feed ((double) sec.swing);
                feed (sec.halfTime ? 1.0 : 0.0);
                feed ((double) sec.variationRhythm);
                feed ((double) sec.variationCymbal);
                feed ((double) sec.laneMask);
                feed ((double) sec.ghostMask);
            }
        }

        if (isManualMode())
        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            for (const auto& lane : manualGrid)
                for (float v : lane)
                    feed (v);
        }
        return h;
    }

    juce::File DrumsXProcessor::contentFolderUnder (const juce::File& root)
    {
        // On macOS JUCE's "application data" locations are /Library and
        // ~/Library, while an installer package writes to their Application
        // Support subfolder; on Windows and Linux the root is already the data
        // folder. Both spellings are searched so the installed tree is found
        // wherever the platform's installer put it.
        for (const auto& base : { root, root.getChildFile ("Application Support") })
        {
            const auto folder = base.getChildFile ("HumHouse/Drums X/Content");
            if (folder.getChildFile ("content_manifest.json").existsAsFile())
                return folder;
        }

        return {};
    }

    juce::File DrumsXProcessor::findSharedContentFolder()
    {
        using SL = juce::File;

        // Development and CI runs point at a content tree that was never
        // installed, so an override wins over the installed locations.
        const auto override_ = juce::SystemStats::getEnvironmentVariable ("HHX_CONTENT_DIR", {});
        if (override_.isNotEmpty())
        {
            const juce::File folder (override_);
            if (folder.getChildFile ("content_manifest.json").existsAsFile())
                return folder;
        }

        for (const auto& root : { SL::getSpecialLocation (SL::commonApplicationDataDirectory),
                                  SL::getSpecialLocation (SL::userApplicationDataDirectory) })
            if (const auto folder = contentFolderUnder (root); folder != juce::File())
                return folder;

        return {};
    }

    void DrumsXProcessor::loadContent()
    {
        // Installed content wins over the bundled fallback, but only when its
        // manifest declares the binary layout this build can actually parse.
        const auto shared = findSharedContentFolder();

        if (shared != juce::File())
        {
            const auto manifest = juce::JSON::parse (shared.getChildFile ("content_manifest.json"));
            const auto version = (int) manifest.getProperty ("format_version", -1);
            const auto corpusFile = shared.getChildFile (manifest.getProperty ("corpus", "rock_corpus.hhc")
                                                                 .toString());

            if (version == (int) GrooveCorpus::kFormatVersion && corpusFile.existsAsFile())
            {
                juce::MemoryBlock block;
                if (corpusFile.loadFileAsData (block)
                    && corpus.loadFromMemory (block.getData(), block.getSize()))
                {
                    contentDescription = "installed content " + juce::String (version);
                }
            }
            else
            {
                contentDescription = "bundled content (installed content is version "
                                   + juce::String (version) + ")";
            }

            // Installed kits are multi-mic and multi-layer; the compiled-in kit
            // is only the fallback for a plug-in running without content.
            if (const auto* kits = manifest["kits"].getArray())
            {
                int preferredKit = -1;

                for (const auto& entry : *kits)
                {
                    // A manifest only names kits inside its own content tree, so
                    // a "../.." entry is rejected rather than followed.
                    const auto folder = shared.getChildFile (entry["folder"].toString());
                    if (! folder.isAChildOf (shared) || ! folder.isDirectory()
                        || ! folder.getChildFile ("kit.json").existsAsFile())
                        continue;

                    auto name = entry["name"].toString();
                    kitNames.add (name.isNotEmpty() ? name : folder.getFileName());
                    kitFolders.push_back (folder);

                    // The kit the manifest calls the default is the one a new
                    // session opens on; the rest are tried in order behind it.
                    if ((bool) entry.getProperty ("default", false)
                        && preferredKit < 0)
                        preferredKit = (int) kitFolders.size() - 1;
                }

                std::vector<int> order;
                if (preferredKit >= 0)
                    order.push_back (preferredKit);
                for (int i = 0; i < (int) kitFolders.size(); ++i)
                    if (i != preferredKit)
                        order.push_back (i);

                for (const int i : order)
                {
                    if (kit.loadKitFolder (kitFolders[(std::size_t) i]) <= 0)
                        continue;
                    selectedKit.store (i);
                    contentDescription += " + kit " + kit.getKitName();
                    break;
                }
            }
        }
        else
        {
            contentDescription = "no installed content found";
        }

       #if HHX_HAS_CORPUS
        if (! corpus.isLoaded())
        {
            int size = 0;
            if (const auto* data = CorpusData::getNamedResource ("rock_corpus_hhc", size))
                corpus.loadFromMemory (data, (std::size_t) size);
        }
       #endif

        if (kit.numLoadedSamples() == 0)
        {
            kit.loadBundledKit ("SoCalRock");
            contentDescription += " + bundled kit";
        }

        kit.setMicBlend (apvts.getRawParameterValue (pid::micBlend)->load());
        kit.setBleed (apvts.getRawParameterValue (pid::bleed)->load());
        kit.setCrush (apvts.getRawParameterValue (pid::crush)->load());
        pushRoomParameters();
        pushMixParameters();
    }

    void DrumsXProcessor::selectKit (int index)
    {
        if (index < 0 || index >= (int) kitFolders.size() || index == selectedKit.load())
            return;

        if (kit.loadKitFolder (kitFolders[(std::size_t) index]) <= 0)
            return;

        selectedKit.store (index);

        // The strip is per lane, not per kit, so the new kit opens with the
        // gains, tuning and sends the session was already using.
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
            kit.setLaneCompression (lane, load (pid::laneComp (lane)));
            kit.setLaneEqLowDb (lane, load (pid::laneEqLow (lane)));
            kit.setLaneEqMidDb (lane, load (pid::laneEqMid (lane)));
            kit.setLaneEqHighDb (lane, load (pid::laneEqHigh (lane)));
            kit.setLaneReverbSend (lane, load (pid::laneSend (lane)));
        }
    }

    void DrumsXProcessor::pushRoomParameters()
    {
        const auto load = [this] (const juce::String& id)
        {
            const auto* p = apvts.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        kit.setRoom (load (pid::roomSize), load (pid::roomDamping), load (pid::roomMix));
        kit.setRoomSpace ((int) std::lround (load (pid::roomSpace)));
        kit.setRoomDuck (load (pid::roomDuck));
    }

    void DrumsXProcessor::pushMixParameters()
    {
        const auto load = [this] (const juce::String& id)
        {
            const auto* p = apvts.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        kit.setMixVoicing ((int) std::lround (load (pid::mixVoicing)));
        kit.setPunch (load (pid::punch));
        kit.setGlue (load (pid::glue));
        kit.setDrive (load (pid::drive));
        kit.setSqueeze (load (pid::squeeze));
        kit.setSqueezeGlow ((int) std::lround (load (pid::squeezeGlow)));
        kit.setMaster (load (pid::master));
    }

    void DrumsXProcessor::rebuildTimeline()
    {
        auto next = std::make_shared<Timeline>();
        const auto s = buildSettings();
        next->beatsPerBar = s.beatsPerBar;
        // The loop follows the arrangement, but a short song is played through
        // several times before it repeats: the blocks come back in order while
        // the takes inside them keep changing, so one eight-bar block still
        // gives 64 bars of non-repeating playing.
        const int songBars = std::max (1, arrangementBars (s.arrangement));
        next->numBars     = songBars * ((kMinRenderBars + songBars - 1) / songBars);
        next->hash        = settingsHash();
        next->hits        = renderBars (0, next->numBars);

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
        set (pid::halfTime,    c.halfTime ? 1.0f : 0.0f);
    }

    PerformanceSettings DrumsXProcessor::buildSettings() const
    {
        const auto get = [this] (const juce::String& id)
        {
            const auto* p = apvts.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        PerformanceSettings s;
        s.complexity      = get (pid::complexity);
        s.intensity       = get (pid::intensity);
        s.sectionVelocity = get (pid::sectionLevel);
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
        s.halfTime       = get (pid::halfTime) > 0.5f
                        || characters()[(std::size_t) charIdx].halfTime;

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
            s.sections    = hostSections;
            s.arrangement = arrangement;
        }

        // With blocks on the strip the piece switches belong to the selected
        // block, so the song-wide mask has to stay open: an intro of nothing
        // but toms must not take the hats out of the chorus behind it.
        if (! s.arrangement.empty())
        {
            s.laneMask     = 0xFFFFFFFFu;
            s.fillLaneMask = 0xFFFFFFFFu;
            s.ghostMask    = 0;
        }

        // Rounded to whole beats per minute: the fill rules only care which
        // figures are playable at this speed, and a host nudging the tempo by a
        // hundredth should not re-render the song.
        s.tempoBpm = (float) std::round (lastBpm.load());

        s.seed = seed.load();
        return s;
    }

    //==============================================================================
    void DrumsXProcessor::captureParamsIntoSelectedSection()
    {
        const auto get = [this] (const juce::String& id)
        {
            const auto* p = apvts.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        const juce::SpinLock::ScopedLockType sl (sectionLock);
        const int index = selectedSection.load();
        if (index < 0 || index >= (int) arrangement.size())
            return;

        auto& sec = arrangement[(std::size_t) index];
        sec.complexity      = get (pid::complexity);
        sec.intensity       = get (pid::intensity);
        sec.velocity        = get (pid::sectionLevel);
        sec.fillAmount      = get (pid::fillAmount);
        sec.swing           = get (pid::swing);
        sec.halfTime        = get (pid::halfTime) > 0.5f;
        sec.variationRhythm = (int) get (pid::variationRhythm);
        sec.variationCymbal = (int) get (pid::variationCymbal);

        std::uint32_t blockMask = 0, blockGhost = 0;
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            if (get (pid::laneEnable (lane)) > 0.5f)
                blockMask |= (1u << lane);
            if (get (pid::laneGhost (lane)) > 0.5f)
                blockGhost |= (1u << lane);
        }
        sec.laneMask  = blockMask;
        sec.ghostMask = blockGhost;
    }

    void DrumsXProcessor::pushSectionToParams (int index)
    {
        ArrangementSection sec;
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            if (index < 0 || index >= (int) arrangement.size())
                return;
            sec = arrangement[(std::size_t) index];
        }

        syncingSection.store (true);
        const auto set = [this] (const char* id, float v)
        {
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };

        set (pid::complexity,   sec.complexity);
        set (pid::intensity,    sec.intensity);
        set (pid::sectionLevel, sec.velocity);
        set (pid::fillAmount,   sec.fillAmount);
        set (pid::swing,      sec.swing);
        set (pid::halfTime,   sec.halfTime ? 1.0f : 0.0f);
        set (pid::variationRhythm, (float) sec.variationRhythm);
        set (pid::variationCymbal, (float) sec.variationCymbal);

        for (int lane = 0; lane < NumLanes; ++lane)
        {
            set (pid::laneEnable (lane).toRawUTF8(),
                 (sec.laneMask  & (1u << lane)) != 0 ? 1.0f : 0.0f);
            set (pid::laneGhost (lane).toRawUTF8(),
                 (sec.ghostMask & (1u << lane)) != 0 ? 1.0f : 0.0f);
        }
        syncingSection.store (false);
    }

    std::vector<ArrangementSection> DrumsXProcessor::getArrangement() const
    {
        const juce::SpinLock::ScopedLockType sl (sectionLock);
        return arrangement;
    }

    int DrumsXProcessor::numSections() const
    {
        const juce::SpinLock::ScopedLockType sl (sectionLock);
        return (int) arrangement.size();
    }

    int DrumsXProcessor::totalArrangementBars() const
    {
        const juce::SpinLock::ScopedLockType sl (sectionLock);
        return std::max (1, arrangementBars (arrangement));
    }

    int DrumsXProcessor::sectionStartBar (int index) const
    {
        const juce::SpinLock::ScopedLockType sl (sectionLock);
        int bar = 0;
        for (int i = 0; i < index && i < (int) arrangement.size(); ++i)
            bar += std::max (1, arrangement[(std::size_t) i].numBars);
        return bar;
    }

    void DrumsXProcessor::setSelectedSection (int index)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            if (index < 0 || index >= (int) arrangement.size() || index == selectedSection.load())
                return;
        }
        selectedSection.store (index);
        pushSectionToParams (index);
        triggerAsyncUpdate();
    }

    void DrumsXProcessor::addSection()
    {
        // "+" writes song form, not another copy of the same block: a drummer
        // thinks in verses and choruses and plays each of them differently.
        // Which form a song takes is drawn from the seed, so two songs do not
        // both march through the same A/B/C/A. The type still gets shaped
        // further by the engine's section colouring, so the block knobs only
        // lean in the right direction rather than doing the whole job.
        static const std::vector<std::vector<int>> forms {
            { SectionVerse, SectionChorus, SectionVerse, SectionChorus },
            { SectionVerse, SectionChorus, SectionVerse, SectionBridge, SectionChorus },
            { SectionVerse, SectionChorus, SectionChorus, SectionVerse, SectionChorus },
            { SectionVerse, SectionVerse, SectionChorus, SectionBridge, SectionChorus, SectionChorus },
            { SectionVerse, SectionChorus, SectionVerse, SectionChorus, SectionBridge, SectionChorus },
        };
        const auto& form = forms[(std::size_t) (seed.load() % forms.size())];

        int added = 0;
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);

            // The song's first block is home: every later verse returns to it
            // instead of inheriting whatever the last chorus was set to.
            ArrangementSection sec;
            if (! arrangement.empty())
                sec = arrangement.front();

            sec.id      = nextSectionId++;
            sec.section = form[arrangement.size() % form.size()];

            switch (sec.section)
            {
                case SectionChorus:
                    sec.complexity = juce::jmin (1.0f, sec.complexity + 0.12f);
                    sec.velocity   = juce::jmin (1.0f, sec.velocity + 0.12f);
                    sec.fillAmount = juce::jmin (1.0f, sec.fillAmount + 0.10f);
                    sec.halfTime   = false;
                    break;
                case SectionBridge:
                    sec.complexity = juce::jmax (0.0f, sec.complexity - 0.15f);
                    sec.velocity   = juce::jmax (0.0f, sec.velocity - 0.08f);
                    sec.halfTime   = true;
                    break;
                default:
                    sec.halfTime = false;
                    break;
            }

            arrangement.push_back (sec);
            added = (int) arrangement.size() - 1;
        }

        selectedSection.store (added);
        pushSectionToParams (added);
        rebuildTimeline();
        updateHostDisplay();
    }

    void DrumsXProcessor::duplicateSection (int index)
    {
        int added = 0;
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            ArrangementSection sec;
            if (index >= 0 && index < (int) arrangement.size())
                sec = arrangement[(std::size_t) index];
            sec.id = nextSectionId++;
            arrangement.push_back (sec);
            added = (int) arrangement.size() - 1;
        }
        selectedSection.store (added);
        pushSectionToParams (added);
        rebuildTimeline();
        updateHostDisplay();
    }

    void DrumsXProcessor::removeSection (int index)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            if (index < 0 || index >= (int) arrangement.size() || arrangement.size() <= 1)
                return;
            arrangement.erase (arrangement.begin() + index);
        }
        selectedSection.store (juce::jlimit (0, numSections() - 1, index));
        pushSectionToParams (selectedSection.load());
        rebuildTimeline();
        updateHostDisplay();
    }

    void DrumsXProcessor::setSectionBars (int index, int bars)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            if (index < 0 || index >= (int) arrangement.size())
                return;
            arrangement[(std::size_t) index].numBars = juce::jlimit (1, 64, bars);
        }
        rebuildTimeline();
        updateHostDisplay();
    }

    void DrumsXProcessor::setSectionType (int index, int section)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            if (index < 0 || index >= (int) arrangement.size())
                return;
            arrangement[(std::size_t) index].section = juce::jlimit (0, (int) NumSections - 1, section);
        }
        rebuildTimeline();
        updateHostDisplay();
    }

    namespace
    {
        std::uint32_t lanesMask (std::initializer_list<int> lanes)
        {
            std::uint32_t m = 0;
            for (const int lane : lanes)
                m |= (1u << lane);
            return m;
        }

        std::uint32_t rangeMask (int first, int last)
        {
            std::uint32_t m = 0;
            for (int lane = first; lane <= last; ++lane)
                m |= (1u << lane);
            return m;
        }

        std::uint32_t kickMask()  { return lanesMask ({ LaneKick }); }
        std::uint32_t snareMask() { return rangeMask (LaneSnare, LaneSnareRoll); }
        std::uint32_t hatMask()   { return rangeMask (LaneHatClosed, LaneHatBell); }
        std::uint32_t rideMask()  { return rangeMask (LaneRideBow, LaneRideCrash); }
        std::uint32_t crashMask() { return rangeMask (LaneCrashL, LaneSplash); }
        std::uint32_t tomMask()   { return rangeMask (LaneTom1, LaneTom4); }
    }

    const char* DrumsXProcessor::blockPresetName (BlockPreset preset)
    {
        switch (preset)
        {
            case BlockPreset::kickHatsIntro:  return "Intro: kick and hats";
            case BlockPreset::kickSnareIntro: return "Intro: kick and snare";
            case BlockPreset::tomsIntro:      return "Intro: toms";
            case BlockPreset::verse:          return "Verse: full kit, held back";
            case BlockPreset::build:          return "Build: ride, rising";
            case BlockPreset::chorus:         return "Chorus: full kit";
            case BlockPreset::crashChorus:    return "Chorus: crashes wide open";
            case BlockPreset::wholeKit:       return "Whole kit";
        }
        return "Whole kit";
    }

    void DrumsXProcessor::applyBlockPreset (int index, BlockPreset preset)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            if (index < 0 || index >= (int) arrangement.size())
                return;

            auto& sec = arrangement[(std::size_t) index];
            const auto everything = 0xFFFFFFFFu;

            switch (preset)
            {
                case BlockPreset::kickHatsIntro:
                    sec.section    = SectionIntro;
                    sec.laneMask   = kickMask() | hatMask();
                    sec.velocity   = 0.45f;
                    sec.intensity  = 0.25f;
                    sec.complexity = 0.25f;
                    sec.fillAmount = 0.15f;
                    break;
                case BlockPreset::kickSnareIntro:
                    sec.section    = SectionIntro;
                    sec.laneMask   = kickMask() | snareMask();
                    sec.velocity   = 0.5f;
                    sec.intensity  = 0.3f;
                    sec.complexity = 0.25f;
                    sec.fillAmount = 0.2f;
                    break;
                case BlockPreset::tomsIntro:
                    sec.section    = SectionIntro;
                    sec.laneMask   = kickMask() | tomMask();
                    sec.velocity   = 0.55f;
                    sec.intensity  = 0.35f;
                    sec.complexity = 0.4f;
                    sec.fillAmount = 0.25f;
                    break;
                case BlockPreset::verse:
                    sec.section    = SectionVerse;
                    sec.laneMask   = everything & ~crashMask();
                    sec.velocity   = 0.6f;
                    sec.intensity  = 0.45f;
                    sec.complexity = 0.4f;
                    sec.fillAmount = 0.3f;
                    break;
                case BlockPreset::build:
                    sec.section    = SectionBridge;
                    sec.laneMask   = kickMask() | snareMask() | rideMask() | tomMask();
                    sec.velocity   = 0.7f;
                    sec.intensity  = 0.6f;
                    sec.complexity = 0.45f;
                    sec.fillAmount = 0.35f;
                    break;
                case BlockPreset::chorus:
                    sec.section    = SectionChorus;
                    sec.laneMask   = everything;
                    // A chorus is the verse hit harder with the kit wide
                    // open, not a busier part and not a bar of fills: the
                    // density stays where the song's is and the loudness is
                    // what moves.
                    sec.velocity   = 0.85f;
                    sec.intensity  = 0.8f;
                    sec.complexity = 0.45f;
                    sec.fillAmount = 0.3f;
                    break;
                case BlockPreset::crashChorus:
                    sec.section    = SectionChorus;
                    sec.laneMask   = everything;
                    sec.velocity   = 1.0f;
                    sec.intensity  = 1.0f;
                    sec.complexity = 0.5f;
                    sec.fillAmount = 0.4f;
                    break;
                case BlockPreset::wholeKit:
                    sec.laneMask   = everything;
                    break;
            }

            // A preset says which pieces play, never which are demoted to
            // ghost strokes: that stays the player's choice.
            sec.ghostMask &= sec.laneMask;
        }

        if (index == selectedSection.load())
            pushSectionToParams (index);

        rebuildTimeline();
        updateHostDisplay();
    }

    std::uint32_t DrumsXProcessor::getSectionLanes (int index) const
    {
        const juce::SpinLock::ScopedLockType sl (sectionLock);
        if (index < 0 || index >= (int) arrangement.size())
            return 0xFFFFFFFFu;
        return arrangement[(std::size_t) index].laneMask;
    }

    void DrumsXProcessor::setSectionLanes (int index, std::uint32_t laneMask)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sectionLock);
            if (index < 0 || index >= (int) arrangement.size())
                return;
            auto& sec = arrangement[(std::size_t) index];
            sec.laneMask  = laneMask;
            sec.ghostMask &= laneMask;
        }

        if (index == selectedSection.load())
            pushSectionToParams (index);

        rebuildTimeline();
        updateHostDisplay();
    }

    void DrumsXProcessor::toggleSectionPiece (int index, const std::vector<int>& lanes)
    {
        if (lanes.empty())
            return;

        std::uint32_t group = 0;
        for (const int lane : lanes)
            if (lane >= 0 && lane < NumLanes)
                group |= (1u << lane);

        const auto current = getSectionLanes (index);
        const bool on = (current & group) != 0;
        setSectionLanes (index, on ? (current & ~group) : (current | group));
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

    int DrumsXProcessor::manualBars() const
    {
        const auto* p = apvts.getRawParameterValue (pid::manualBars);
        switch (p != nullptr ? (int) p->load() : 1)
        {
            case 0:  return 1;
            case 2:  return 4;
            default: return 2;
        }
    }

    void DrumsXProcessor::setManualStep (int lane, int step, float velocity01)
    {
        if (lane < 0 || lane >= NumLanes || step < 0 || step >= kManualSteps)
            return;
        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            manualGrid[(std::size_t) lane][(std::size_t) step] = juce::jlimit (0.0f, 1.0f, velocity01);
        }

        // Writing a note means playing it: the first stroke put in the grid
        // switches manual mode on, the same way a dropped file does, so one
        // note is enough to hear the drummer build a groove around it.
        if (velocity01 > 0.0f && ! isManualMode())
            if (auto* p = apvts.getParameter (pid::manualMode))
                p->setValueNotifyingHost (1.0f);

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

    bool DrumsXProcessor::hasManualNotes() const
    {
        const std::lock_guard<std::mutex> lock (manualMutex);
        const int steps = manualSteps();
        for (const auto& lane : manualGrid)
            for (int step = 0; step < steps; ++step)
                if (lane[(std::size_t) step] > 0.0f)
                    return true;
        return false;
    }

    bool DrumsXProcessor::importManualMidi (const juce::File& source)
    {
        auto stream = source.createInputStream();
        if (stream == nullptr)
            return false;

        juce::MidiFile file;
        if (! file.readFrom (*stream))
            return false;

        const int format = file.getTimeFormat();
        if (format <= 0)                       // SMPTE files carry no musical grid
            return false;
        const double ticksPerQuarter = (double) format;

        std::array<std::array<float, kManualSteps>, NumLanes> grid {};
        bool any = false;
        int  lastStep = 0;

        for (int t = 0; t < file.getNumTracks(); ++t)
        {
            const auto* track = file.getTrack (t);
            if (track == nullptr)
                continue;

            for (int e = 0; e < track->getNumEvents(); ++e)
            {
                const auto& msg = track->getEventPointer (e)->message;
                if (! msg.isNoteOn())
                    continue;

                const int lane = noteToLane (msg.getNoteNumber());
                if (lane < 0)
                    continue;

                // Sixteenths, and anything past the grid's four bars wraps onto
                // it, so dropping a longer loop still lands in time.
                const double quarters = msg.getTimeStamp() / ticksPerQuarter;
                int step = (int) std::llround (quarters * 4.0);
                if (step < 0)
                    continue;
                step %= kManualSteps;

                auto& cell = grid[(std::size_t) lane][(std::size_t) step];
                cell = std::max (cell, juce::jlimit (0.1f, 1.0f, msg.getFloatVelocity()));
                lastStep = std::max (lastStep, step);
                any = true;
            }
        }

        if (! any)
            return false;

        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            manualGrid = grid;
        }

        // The pattern is as long as what was dropped, rounded up to a length
        // the grid holds, so a four-bar loop is not folded back onto bar one.
        if (auto* p = apvts.getParameter (pid::manualBars))
        {
            const int index = lastStep >= 32 ? 2 : (lastStep >= 16 ? 1 : 0);
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        }

        // A dropped pattern is meant to be played, so the page switches itself
        // into manual mode rather than silently holding the notes.
        if (auto* p = apvts.getParameter (pid::manualMode))
            p->setValueNotifyingHost (1.0f);

        rebuildTimeline();
        updateHostDisplay();
        return true;
    }

    bool DrumsXProcessor::exportManualMidi (const juce::File& dest) const
    {
        juce::MidiFile file;
        file.setTicksPerQuarterNote (kTicksPerQuarter);

        const auto s = buildSettings();
        juce::MidiMessageSequence meta;
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (s.timeSigNum, s.timeSigDen), 0.0);
        meta.addEvent (juce::MidiMessage::tempoMetaEvent (
            (int) std::llround (60'000'000.0 / lastBpm.load())), 0.0);
        meta.addEvent (juce::MidiMessage::textMetaEvent (3, "HumHouse Drums X pattern"), 0.0);
        file.addTrack (meta);

        juce::MidiMessageSequence seq;
        {
            const int steps = manualSteps();
            const std::lock_guard<std::mutex> lock (manualMutex);
            for (int lane = 0; lane < NumLanes; ++lane)
            {
                for (int step = 0; step < steps; ++step)
                {
                    const float v = manualGrid[(std::size_t) lane][(std::size_t) step];
                    if (v <= 0.0f)
                        continue;

                    const int    note = laneToMidiNote (lane);
                    const double tick = (double) step * 0.25 * kTicksPerQuarter;
                    const auto   vel  = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (v * 127.0f));
                    seq.addEvent (juce::MidiMessage::noteOn (10, note, vel), tick);
                    seq.addEvent (juce::MidiMessage::noteOff (10, note), tick + kNoteTicks);
                }
            }
        }
        if (seq.getNumEvents() == 0)
            return false;

        seq.updateMatchedPairs();
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

    //==============================================================================
    std::vector<Hit> DrumsXProcessor::renderBars (int startBar, int numBars) const
    {
        const auto s = buildSettings();

        if (! isManualMode())
            return engine.renderBars (s, startBar, numBars);

        // Manual mode plays the pattern the player wrote, but plays it the way
        // a drummer would rather than looping it: the notes are theirs, while
        // the fills at the phrase ends, the piece switches of the block and its
        // section loudness come from the arrangement. Nothing is added inside
        // the bar on a piece the player has written, so what is written is
        // what is heard - while a piece they have not written at all is played
        // by the drummer, which is what makes one note read as a groove.
        std::vector<Hit> out;
        const int patternBars = manualBars();
        const std::lock_guard<std::mutex> lock (manualMutex);
        const int phraseBars = juce::jmax (1, s.phraseBars);

        // Which part of the kit the player has actually written. A drum machine
        // plays back what is on the grid and nothing else, which is why one
        // written kick is one written kick; a session drummer hearing that kick
        // plays a groove around it. So the pieces the player has said nothing
        // about are played by the drummer, and the ones they have written are
        // theirs alone.
        const auto family = [] (int lane)
        {
            if (lane == LaneKick)        return 0;
            if (isSnareLane (lane))      return 1;
            if (isHatLane (lane))        return 2;
            if (isRideLane (lane))       return 3;
            if (isTomLane (lane))        return 4;
            if (isCymbalLane (lane))     return 5;
            return 6;
        };

        unsigned written = 0;
        for (int lane = 0; lane < NumLanes; ++lane)
            for (int step = 0; step < patternBars * 16; ++step)
                if (manualGrid[(std::size_t) lane][(std::size_t) step] > 0.0f)
                {
                    written |= 1u << family (lane);
                    break;
                }

        std::vector<Hit> band;
        if (written != 0)
            for (const auto& h : engine.renderBars (s, startBar, numBars))
                if ((written & (1u << family (h.lane))) == 0)
                    band.push_back (h);

        for (int bar = 0; bar < numBars; ++bar)
        {
            const int   absBar   = startBar + bar;
            const auto  sec      = engine.settingsForBar (s, absBar);
            const int   sourceBar = absBar % patternBars;
            const float barStart = (float) absBar * s.beatsPerBar;

            // Where this bar sits in its phrase decides whether it hands over
            // with a fill, and the block's own loudness scales the strokes.
            const int   phrase   = absBar / phraseBars;
            const bool  lastBar  = ((absBar + 1) % phraseBars) == 0;
            const float fillBeats = lastBar
                                  ? std::min (s.beatsPerBar,
                                              s.beatsPerBar * juce::jmax (0.5f, s.fillLengthBars))
                                  : 0.0f;
            const auto  fill = fillBeats > 0.0f ? engine.fillForPhrase (s, phrase, fillBeats)
                                                : std::vector<Hit> {};
            const float fillStart = s.beatsPerBar - fillBeats;
            const float level = 0.72f + 0.5f * juce::jlimit (0.0f, 1.0f, sec.sectionVelocity);

            for (int lane = 0; lane < NumLanes; ++lane)
            {
                if ((sec.laneMask & (1u << lane)) == 0)
                    continue;
                for (int step = 0; step < 16; ++step)
                {
                    const float v = manualGrid[(std::size_t) lane][(std::size_t) (sourceBar * 16 + step)];
                    if (v <= 0.0f)
                        continue;
                    const float inBar = s.beatsPerBar * ((float) step / 16.0f);
                    if (! fill.empty() && inBar >= fillStart - 0.01f)
                        continue;               // the fill takes over the bar here
                    out.push_back ({ barStart + inBar, (std::uint8_t) lane,
                                     (std::uint8_t) juce::jlimit (1, 127,
                                         juce::roundToInt (v * 127.0f * level)) });
                }
            }

            for (const auto& h : fill)
                out.push_back ({ barStart + fillStart + h.beat, h.lane, h.velocity });

            // The rest of the kit is played around the written part, and it
            // stays out of the handover for the same reason the written part
            // does: the fill is the whole bar's figure, not a layer over it.
            for (const auto& h : band)
            {
                const float inBar = h.beat - barStart;
                if (inBar < -0.01f || inBar >= s.beatsPerBar)
                    continue;
                if (! fill.empty() && inBar >= fillStart - 0.01f)
                    continue;
                out.push_back (h);
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

        const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const int    numSamples = buffer.getNumSamples();

        const auto* triggerParam = apvts.getRawParameterValue (pid::triggerMode);
        const int   trigger = triggerParam != nullptr ? (int) std::lround (triggerParam->load()) : 0;

        // What the host is sending us decides whether the drummer plays at all,
        // so the incoming notes are read before the buffer is reused for our
        // own output. In "Play My Notes" they are the performance.
        struct GateEdge { int offset; bool open; };
        std::array<GateEdge, 32> edges {};
        int numEdges = 0;

        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int  at  = juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition);

            if (msg.isNoteOn())
            {
                ++heldNotes;
            }
            else if (msg.isNoteOff())
            {
                heldNotes = juce::jmax (0, heldNotes - 1);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                heldNotes = 0;
            }
            else
            {
                continue;
            }

            const bool wantOpen = heldNotes > 0;
            if (wantOpen != gateOpen && numEdges < (int) edges.size())
            {
                gateOpen = wantOpen;
                edges[(std::size_t) numEdges++] = { at, wantOpen };
            }
        }

        // The kit's own strokes are played from the host's notes in "Play My
        // Notes": the drawn part is the truth, we only voice it.
        int numHostHits = 0;
        std::array<PendingHit, 64> hostHits {};
        if (trigger == 2)
        {
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (! msg.isNoteOn())
                    continue;
                const int lane = noteToLane (msg.getNoteNumber());
                if (lane < 0 || numHostHits >= (int) hostHits.size())
                    continue;
                hostHits[(std::size_t) numHostHits++] =
                    { juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition),
                      (std::uint8_t) lane,
                      (std::uint8_t) juce::jlimit (1, 127, (int) msg.getVelocity()),
                      (std::uint8_t) 0 };
            }
        }

        const bool gateAtBlockStart = numEdges > 0 ? ! edges[0].open : gateOpen;

        /** Whether the drummer is allowed to strike at this point in the block. */
        const auto gatedAt = [&] (int offset)
        {
            if (trigger == 0)
                return true;
            if (trigger == 2)
                return false;
            bool open = gateAtBlockStart;
            for (int i = 0; i < numEdges; ++i)
                if (edges[(std::size_t) i].offset <= offset)
                    open = edges[(std::size_t) i].open;
            return open;
        };

        midi.clear();

        if (trigger == 2)
        {
            int cursor = 0;
            for (int i = 0; i < numHostHits; ++i)
            {
                const auto& h = hostHits[(std::size_t) i];
                if (h.offset > cursor)
                {
                    kit.renderNextBlock (buffer, cursor, h.offset - cursor);
                    cursor = h.offset;
                }
                kit.noteOn (h.lane, (float) h.velocity / 127.0f, h.variant);
                midi.addEvent (juce::MidiMessage::noteOn (10, laneToMidiNote (h.lane),
                                                          (juce::uint8) h.velocity), h.offset);
                midi.addEvent (juce::MidiMessage::noteOff (10, laneToMidiNote (h.lane)),
                               juce::jmin (numSamples - 1, h.offset + 8));
            }
            if (cursor < numSamples)
                kit.renderNextBlock (buffer, cursor, numSamples - cursor);

            applyOutputStage (buffer);
            return;
        }

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

        const double wasBpm = lastBpm.exchange (bpm);
        // Which fills are playable depends on the tempo, so a tempo change is a
        // reason to re-render - off this thread, once the beat has moved.
        if (std::abs (std::round (wasBpm) - std::round (bpm)) >= 1.0)
            triggerAsyncUpdate();

        if (! transportRunning)
        {
            kit.renderNextBlock (buffer, 0, numSamples);
            applyOutputStage (buffer);
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
            applyOutputStage (buffer);
            playheadBeats.store (blockEndBeat);
            return;
        }

        const double loopBeats = (double) tl->beatsPerBar * tl->numBars;
        const double loopStart = std::fmod (blockStartBeat, loopBeats);
        const double loopEnd   = loopStart + (blockEndBeat - blockStartBeat);

        // Strokes are collected first and struck at their own sample offset
        // below. Firing them all at the top of the block would quantise the
        // whole performance to the buffer size, which is the one thing that
        // makes real micro-timing inaudible.
        int numPending = 0;
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
                // Held-note trigger: strokes outside the held span are never
                // struck, so the kit can enter at bar 8 of a song. Whatever is
                // already ringing is left to ring out.
                if (! gatedAt (offset))
                    continue;

                const int note = laneToMidiNote (h.lane);
                midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) h.velocity), offset);
                midi.addEvent (juce::MidiMessage::noteOff (10, note), juce::jmin (numSamples - 1, offset + 8));

                if (numPending < (int) pending.size())
                    pending[(std::size_t) numPending++] = { offset, h.lane, h.velocity, h.variant };
                else
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

        int cursor = 0;
        for (int i = 0; i < numPending; ++i)
        {
            const auto& p = pending[(std::size_t) i];
            if (p.offset > cursor)
            {
                kit.renderNextBlock (buffer, cursor, p.offset - cursor);
                cursor = p.offset;
            }
            kit.noteOn (p.lane, (float) p.velocity / 127.0f, p.variant);
        }
        if (cursor < numSamples)
            kit.renderNextBlock (buffer, cursor, numSamples - cursor);

        applyOutputStage (buffer);

        playheadBeats.store (playing.load() ? std::fmod (blockEndBeat, loopBeats) : blockEndBeat);
    }

    void DrumsXProcessor::applyOutputStage (juce::AudioBuffer<float>& buffer)
    {
        const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (pid::outputLevel)->load());
        buffer.applyGain (gain);

        // A whole kit landing together can pass full scale even at a sane
        // output level, so the last dB is a soft knee instead of the host's
        // hard clip: the drums stay usable in any mix without a limiter.
        constexpr float knee = 0.89f;
        constexpr float room = 1.0f - knee;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float x = data[i];
                const float mag = std::abs (x);
                if (mag > knee)
                    data[i] = std::copysign (knee + room * std::tanh ((mag - knee) / room), x);
            }
        }
    }

    //==============================================================================
    juce::MidiMessageSequence DrumsXProcessor::buildSequence (int numBars, int laneFilter) const
    {
        // MidiFile::writeTo emits sequence timestamps as raw ticks, so the
        // whole sequence lives in ticks rather than beats.
        juce::MidiMessageSequence seq;
        const auto s = buildSettings();
        const auto hits = renderBars (0, numBars);

        // Two lanes can share a note number - the rim and the stick land on 40 -
        // and a second note-on at the same tick leaves the first one hanging for
        // whatever imports the file, so only the loudest of them is written.
        std::map<std::pair<long long, int>, int> struck;

        for (const auto& h : hits)
        {
            if (laneFilter >= 0 && h.lane != laneFilter)
                continue;
            const auto key = std::pair { (long long) std::llround ((double) h.beat * kTicksPerQuarter),
                                         laneToMidiNote (h.lane) };
            auto& loudest = struck[key];
            loudest = std::max (loudest, (int) h.velocity);
        }

        for (const auto& [key, velocity] : struck)
        {
            const auto tick = (double) key.first;
            seq.addEvent (juce::MidiMessage::noteOn (10, key.second, (juce::uint8) velocity), tick);
            seq.addEvent (juce::MidiMessage::noteOff (10, key.second), tick + kNoteTicks);
        }
        seq.updateMatchedPairs();
        juce::ignoreUnused (s);
        return seq;
    }

    bool DrumsXProcessor::exportArrangementMidi (const juce::File& dest, int numBars) const
    {
        const auto s = buildSettings();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (kTicksPerQuarter);

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
            file.setTicksPerQuarterNote (kTicksPerQuarter);

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
        state.setProperty ("kit", kitNames[selectedKit.load()], nullptr);

        juce::MemoryOutputStream grid;
        {
            const std::lock_guard<std::mutex> lock (manualMutex);
            for (const auto& lane : manualGrid)
                for (float v : lane)
                    grid.writeFloat (v);
        }
        state.setProperty ("manualGrid", grid.getMemoryBlock().toBase64Encoding(), nullptr);

        state.removeChild (state.getChildWithName ("arrangement"), nullptr);
        juce::ValueTree blocks ("arrangement");
        blocks.setProperty ("selected", selectedSection.load(), nullptr);
        for (const auto& sec : getArrangement())
        {
            juce::ValueTree block ("section");
            block.setProperty ("id", sec.id, nullptr);
            block.setProperty ("bars", sec.numBars, nullptr);
            block.setProperty ("type", sec.section, nullptr);
            block.setProperty ("complexity", sec.complexity, nullptr);
            block.setProperty ("intensity", sec.intensity, nullptr);
            block.setProperty ("level", sec.velocity, nullptr);
            block.setProperty ("fills", sec.fillAmount, nullptr);
            block.setProperty ("swing", sec.swing, nullptr);
            block.setProperty ("halfTime", sec.halfTime, nullptr);
            block.setProperty ("varRhythm", sec.variationRhythm, nullptr);
            block.setProperty ("varCymbal", sec.variationCymbal, nullptr);
            block.setProperty ("lanes", (juce::int64) sec.laneMask, nullptr);
            block.setProperty ("ghosts", (juce::int64) sec.ghostMask, nullptr);
            blocks.appendChild (block, nullptr);
        }
        state.appendChild (blocks, nullptr);

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

        // By name, not index: a session opened against a different content
        // version keeps the kit it was written with when that kit is installed.
        if (state.hasProperty ("kit"))
            selectKit (kitNames.indexOf (state.getProperty ("kit").toString()));

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

        if (const auto blocks = state.getChildWithName ("arrangement"); blocks.isValid())
        {
            std::vector<ArrangementSection> restored;
            for (const auto& block : blocks)
            {
                ArrangementSection sec;
                sec.id              = (int)   block.getProperty ("id", (int) restored.size() + 1);
                sec.numBars         = (int)   block.getProperty ("bars", 8);
                sec.section         = (int)   block.getProperty ("type", (int) SectionVerse);
                sec.complexity      = (float) block.getProperty ("complexity", 0.45);
                sec.intensity       = (float) block.getProperty ("intensity", 0.55);
                // Sessions saved before the block had its own Intensity knob
                // keep sounding the same: the pad's loudness becomes its level.
                sec.velocity        = (float) block.getProperty ("level", sec.intensity);
                sec.fillAmount      = (float) block.getProperty ("fills", 0.35);
                sec.swing           = (float) block.getProperty ("swing", 0.0);
                sec.halfTime        = (bool)  block.getProperty ("halfTime", false);
                sec.variationRhythm = (int)   block.getProperty ("varRhythm", 0);
                sec.variationCymbal = (int)   block.getProperty ("varCymbal", 0);
                // Sessions saved before the switches were per block played the
                // whole kit in every block, which is what the default is.
                sec.laneMask  = (std::uint32_t) (juce::int64) block.getProperty (
                                    "lanes", (juce::int64) ((1u << NumLanes) - 1u));
                sec.ghostMask = (std::uint32_t) (juce::int64) block.getProperty ("ghosts", (juce::int64) 0);
                restored.push_back (sec);
            }

            if (! restored.empty())
            {
                const juce::SpinLock::ScopedLockType sl (sectionLock);
                nextSectionId = 1;
                for (const auto& sec : restored)
                    nextSectionId = std::max (nextSectionId, sec.id + 1);
                arrangement = std::move (restored);
                selectedSection.store (juce::jlimit (0, (int) arrangement.size() - 1,
                                                     (int) blocks.getProperty ("selected", 0)));
            }
            state.removeChild (blocks, nullptr);
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
            kit.setLaneCompression (lane, load (pid::laneComp (lane)));
            kit.setLaneEqLowDb (lane, load (pid::laneEqLow (lane)));
            kit.setLaneEqMidDb (lane, load (pid::laneEqMid (lane)));
            kit.setLaneEqHighDb (lane, load (pid::laneEqHigh (lane)));
            kit.setLaneReverbSend (lane, load (pid::laneSend (lane)));
        }

        pushRoomParameters();
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
