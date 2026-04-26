#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StarterGrooves.generated.h"
#include "StarterGrooveKitFilter.h"
#include "FillLibrary.generated.h"

namespace
{
    // v1.6.1-rc.3 — strict 6-instrument whitelist. User: "just kick,
    // snare, tom, crash, ride and hi-hat will be triggered … no
    // tambourines, no shakers, no cowbells". We drop any MIDI note
    // that isn't one of those six families at both render-time and
    // MIDI-export-time so tambourine/shaker/clap/etc. are silently
    // scrubbed from the 119 STARTER patterns.
    inline bool isAllowedDrumNote (int note) noexcept
    {
        switch (note)
        {
            // KICK
            case 35: case 36:
            // SNARE (incl. side-stick 37, electric 40)
            case 37: case 38: case 39: case 40:
            // TOMS (low floor 41, high floor 43, low 45, low-mid 47,
            // hi-mid 48, high 50)
            case 41: case 43: case 45: case 47: case 48: case 50:
            // HI-HAT (closed 42, pedal 44, open 46)
            case 42: case 44: case 46:
            // CRASH (crash-1 49, crash-2 57, chinese 52, splash 55)
            case 49: case 52: case 55: case 57:
            // RIDE (ride-1 51, ride-bell 53, ride-cymbal-2 59)
            case 51: case 53: case 59:
                return true;
            default:
                return false;
        }
    }
}

#include <algorithm>
#include <numeric>
#include <random>

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    constexpr const char* kParamVariation     = "variation";
    constexpr const char* kParamComplexity    = "complexity";
    constexpr const char* kParamVelocity      = "velocity";
    constexpr const char* kParamHumanize      = "humanize";
    constexpr const char* kParamPatternLength = "patternLength";
    constexpr const char* kParamGenre         = "genre";
    constexpr const char* kParamMode          = "mode"; // 0 = Groove, 1 = Fill

    // v0.6.0 Logic-Drummer controls
    constexpr const char* kParamSwing         = "swing";
    constexpr const char* kParamFillsProb     = "fillsProb";
    constexpr const char* kParamHalfTime      = "halfTime";
    constexpr const char* kParamHiHat         = "hiHat";

    // v0.7.0 DrumKit voicing
    constexpr const char* kParamDrumKit       = "drumKit";

    // v1.3.0 Ambient room
    constexpr const char* kParamRoom          = "room";
    constexpr const char* kParamRoomAmount    = "roomAmount";

    // v1.5.0 — 5 CC0 bundled kits + separate fill complexity + manual grid
    // step division (1/16, 1/32, 1/64).
    constexpr const char* kParamBundledKit      = "bundledKit";
    constexpr const char* kParamFillComplexity  = "fillComplexity";
    constexpr const char* kParamStepDiv         = "stepDiv";

    // v1.6.1-rc.4 — arrangement playback time scale (½× / 1× / 2×).
    // Affects the rate the playhead advances through the arrangement so
    // the user can audition HALF TIME / NORMAL / DOUBLE TIME without
    // changing the host BPM.
    constexpr const char* kParamTimeScale       = "timeScale";

    // v1.6.1-rc.7 — intensity knob (0..1 stored; 0..127 displayed).
    // Drives the base velocity + per-hit fluctuation curve applied at
    // MIDI emit time. See shapeVelocity() below.
    constexpr const char* kParamIntensity       = "intensity";

    // v1.6.1-rc.6 — single bundled kit. The plugin now pivots around
    // user-loaded sample packs (LOAD KIT). We only ship one crispy
    // default kit so there's no "which built-in sounds the most like my
    // track" friction; if the user wants a different character they
    // drop in their own folder. Internally the kit is still named
    // "Thrash" so all sample-loading / kitProfileFor() paths keep
    // working unchanged.
    // v1.6.1-rc.11 — two bundled kits ship now: (Nu Rock) 70's Yamaha
    // and (Bay Grunge) Yamaha Maple. The internal names below are also
    // the WAV-bundle prefixes scanned by SampleKit::loadBundled().
    // Display strings live separately (see kBundledKitDisplayNames).
    const juce::StringArray kBundledKitChoices {
        "NuRockYamaha", "BayGrungeMaple"
    };
    const juce::StringArray kBundledKitDisplayNames {
        "(Nu Rock) 70's Yamaha", "(Bay Grunge) Yamaha Maple"
    };

    const juce::StringArray kStepDivChoices {
        "1/16", "1/32", "1/64"
    };

    const juce::StringArray kTimeScaleChoices {
        "HALF", "NORMAL", "DOUBLE"
    };

    inline double timeScaleFactorForChoice (int choiceIndex) noexcept
    {
        switch (choiceIndex)
        {
            case 0:  return 0.5;   // HALF
            case 2:  return 2.0;   // DOUBLE
            default: return 1.0;   // NORMAL
        }
    }

    const juce::StringArray kPatternLengthChoices {
        "1/16 note", "1/8 note", "1/4 note", "1/2 bar", "1 bar", "2 bars"
    };

    const juce::StringArray kHiHatChoices {
        "Dynamic", "Closed", "Open", "Ride"
    };

    const juce::StringArray kRoomChoices {
        "Dry / Studio", "Small Room", "Garage", "Live Bar",
        "Hallway", "Big Hall", "Stadium"
    };

    juce::StringArray buildGenreChoices()
    {
        juce::StringArray arr;
        for (const auto& n : aidrum::genreDisplayNames())
            arr.add (juce::String (n));
        return arr;
    }
}

double AIDrumAudioProcessor::patternLengthBeatsFromChoice (int choiceIndex)
{
    switch (choiceIndex)
    {
        case 0: return 0.25;  // 1/16 note
        case 1: return 0.5;   // 1/8 note
        case 2: return 1.0;   // 1/4 note
        case 3: return 2.0;   // 1/2 bar
        case 4: return 4.0;   // 1 bar
        case 5: return 8.0;   // 2 bars
        default: return 4.0;
    }
}

AIDrumAudioProcessor::AIDrumAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    // v1.5.0 — arrangement starts EMPTY. User clicks the `+` in the
    // arrangement strip to create their first region. Every region can
    // be deleted. Matches Logic Pro X's Drummer track behavior.

    // Manual pattern starts empty; 16 bars of 4/4 = 64 beats.
    manualPattern.lengthInBeats = static_cast<double> (manualNumBars * 4);

    // v1.4.0 — auto-load the bundled default kit so the plugin makes
    // real-sample sound out of the box. v1.6.1-rc.6 collapses to a
    // single bundled kit; the user overrides with LOAD KIT to drop in
    // their own samples.
    // v1.6.1-rc.7 — defer the actual bundled-kit load to prepareToPlay()
    // so the WAVs are baked to the host's real sample rate. We just
    // record the name here; loading at a placeholder 48 kHz and never
    // re-baking would mean every drum hit plays ~8.8% slow / 1.4 semi-
    // tones flat on a 44.1 kHz Logic Pro project.
    sampleKit.prepare (48000.0, 0);
    // v1.6.1-rc.8 — bundled kit re-baked from the user's 21 light/medium/
    // heavy/heaviest one-shots (kick x3, snare x5, floor tom x2, small tom
    // x2, left crash x3, right crash x3, ride x3 + carried-over hats).
    currentBundledKitName = "NuRockYamaha";
    {
        std::lock_guard<std::mutex> lock (loadedKitPathMutex);
        loadedKitPath = "Built-in Default";
    }

    // v1.6.0 — per-bus default trims: kick and snare lead, cymbals drop to
    // accent level so they never overpower the backbeat. Toms sit a hair
    // below nominal too (they come in for fills).
    busMixer.params_ref ((int) aidrum::Bus::ClosedHat).gainDb.store (-6.0f);
    busMixer.params_ref ((int) aidrum::Bus::OpenHat)  .gainDb.store (-6.0f);
    busMixer.params_ref ((int) aidrum::Bus::Ride)     .gainDb.store (-6.0f);
    busMixer.params_ref ((int) aidrum::Bus::Crash)    .gainDb.store (-6.0f);
    busMixer.params_ref ((int) aidrum::Bus::China)    .gainDb.store (-6.0f);
    busMixer.params_ref ((int) aidrum::Bus::Toms)     .gainDb.store (-1.5f);

    // v1.5.0 — Live-knob regeneration. Every APVTS parameter (variation,
    // velocity, complexity, fill complexity, humanize, swing, etc.) hot-
    // mutates the last arrangement region the moment the knob moves,
    // instead of waiting for the user to click Generate. Kit changes hot-
    // swap the bundled sample set.
    for (auto* p : getParameters())
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rap->paramID, this);
    }
}

AIDrumAudioProcessor::~AIDrumAudioProcessor()
{
    for (auto* p : getParameters())
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rap->paramID, this);
}

void AIDrumAudioProcessor::parameterChanged (const juce::String& id, float /*newValue*/)
{
    // Hot-swap the bundled sample kit when the user picks a new one.
    if (id == kParamBundledKit)
    {
        const int idx = (int) apvts.getRawParameterValue (kParamBundledKit)->load();
        if (idx >= 0 && idx < kBundledKitChoices.size())
            loadBundledKit (kBundledKitChoices[idx]);
        return;
    }

    // Every other generator-facing parameter triggers a live regen of
    // the last region so the user hears their tweak immediately. Pattern
    // length / genre / drumKit / room changes also re-emit — all cheap
    // compared to the audio thread. Skip regen for purely cosmetic or
    // transport params. v1.6.1-rc.5: kParamTimeScale only affects the
    // playback-rate in renderArrangementToMidiBuffer and is never read
    // by buildRequestForMode/backend.generate — regenerating on it
    // would silently destroy any hand-chosen STARTER groove the user
    // appended into the last region.
    // v1.6.1-rc.7 — kParamIntensity is consumed only at MIDI emit time
    // by shapeVelocity(), never by the generation pipeline. Regenerating
    // on every intensity tweak would silently overwrite hand-picked
    // STARTER grooves with random ones for zero audible benefit.
    // v1.6.1-rc.11 — kParamFillComplexity is consumed at auto-fill splice
    // time by spliceMandatoryFillIntoRegion() to pick the starting fill
    // from the 22-fill library. Cycling the FILL selector must NEVER
    // regenerate the live region — the user reported the cycler was
    // "garbage and changing the whole arrangement". Skip regen here.
    if (id == kParamRoom || id == kParamRoomAmount
     || id == kParamStepDiv || id == kParamTimeScale
     || id == kParamIntensity
     || id == kParamFillComplexity)
        return;

    regenerateCurrentRegion();
}

void AIDrumAudioProcessor::regenerateCurrentRegion()
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (arrangement.empty())
        return;

    // v1.6.1-rc.5 — preserve the region's fill/groove slot across live
    // regen. The old code always used Groove mode, silently converting
    // any Fill region into a Groove the moment the user touched a knob.
    const bool wasFill = arrangement.back().isFill;
    auto req = buildRequestForMode (wasFill ? aidrum::GenerationMode::Fill
                                            : aidrum::GenerationMode::Groove);
    req.phraseBar = static_cast<int> (arrangement.size()) - 1;

    const auto existingLen = arrangement.back().lengthInBeats;
    if (existingLen > 0.0) req.lengthInBeats = existingLen;

    auto regenerated = backend.generate (req);
    regenerated.isFill = wasFill;
    arrangement.back() = std::move (regenerated);
}

APVTS::ParameterLayout AIDrumAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // v1.6.1-rc.7 — all continuous 0..1 knobs get a mild skew (0.55) so
    // the lower half of the rotary moves slower and the upper half
    // faster. Combined with the editor-side setMouseDragSensitivity (400
    // px/rotation) this fixes the "I barely touch it and it moves all
    // over the place" feel. 0.55 is gentle enough that users still reach
    // full range in a single rotation.
    constexpr float kKnobSkew = 0.55f;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamVariation, 1 }, "Variation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f, kKnobSkew), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamComplexity, 1 }, "Complexity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f, kKnobSkew), 0.25f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamVelocity, 1 }, "Velocity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f, kKnobSkew), 0.9f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamHumanize, 1 }, "Humanize",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f, kKnobSkew), 0.25f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamPatternLength, 1 }, "Pattern Length",
        kPatternLengthChoices, 4)); // default: 1 bar

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamGenre, 1 }, "Genre",
        buildGenreChoices(), 0)); // default: Auto

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamMode, 1 }, "Mode",
        juce::StringArray { "Groove", "Fill" }, 0));

    // v0.6.0 Logic-Drummer controls
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamSwing, 1 }, "Swing",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f, kKnobSkew), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamFillsProb, 1 }, "Fills",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f, kKnobSkew), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kParamHalfTime, 1 }, "Half-Time", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamHiHat, 1 }, "Hi-Hat",
        kHiHatChoices, 0));

    // v0.7.0 DrumKit — 20 labeled kits, each with unique GM voicing
    // + velocity / ghost / accent profile.
    {
        juce::StringArray kitNames;
        for (const auto& n : aidrum::drumKitDisplayNames())
            kitNames.add (juce::String (n));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { kParamDrumKit, 1 }, "Drum Kit",
            kitNames,
            (int) aidrum::DrumKit::LudwigSupraphonicClassicRock));
    }

    // v1.3.0 Ambient room preset + wet amount (0-100%).
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamRoom, 1 }, "Room", kRoomChoices, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamRoomAmount, 1 }, "Room Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f, kKnobSkew), 0.25f));

    // v1.6.1-rc.6 — single bundled kit (see kBundledKitChoices above).
    // The param is kept as an AudioParameterChoice (rather than deleted
    // outright) so existing save files / host state still round-trip
    // cleanly.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamBundledKit, 1 }, "Bundled Kit",
        kBundledKitChoices, 0));

    // v1.6.1-rc.7 — Fill Complexity knob was replaced with a FILL
    // SELECTOR cycler that rotates through the 21 user-supplied MIDI
    // fills. The param ID is preserved (for save-file round-trip) but
    // its range is now stepped to 21 discrete values: value * (N-1)
    // yields the fill library index. The param still stores a float
    // 0..1 so old save files where fillComplexity = 0.35 simply round
    // to fill #7 instead of being orphaned.
    {
        const int numFills = std::max (1, (int) aidrum::fillLibrary().size());
        const float step   = 1.0f / (float) std::max (1, numFills - 1);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kParamFillComplexity, 1 }, "Fill Selector",
            juce::NormalisableRange<float> (0.0f, 1.0f, step), 0.0f));
    }

    // v1.6.1-rc.7 — INTENSITY knob (0..1 stored; 0..127 displayed). See
    // shapeVelocity() for the curve: base = 35 + 92*intensity, and each
    // hit fluctuates ±(7.5..15) with instrument-specific factors so
    // kick/snare stay stable, hats breathe, and ghosts vary the most.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamIntensity, 1 }, "Intensity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f / 127.0f, kKnobSkew),
        0.70f));

    // v1.5.0 — Manual grid step division (Logic-style 1/16, 1/32, 1/64).
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamStepDiv, 1 }, "Step Div",
        kStepDivChoices, 0));

    // v1.6.1-rc.4 — arrangement playback time scale.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamTimeScale, 1 }, "Time Scale",
        kTimeScaleChoices, 1)); // default: NORMAL

    return { params.begin(), params.end() };
}

void AIDrumAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    playheadBeats.store (0.0, std::memory_order_relaxed);
    lastBpm.store (120.0, std::memory_order_relaxed);
    drumSynth.prepare (sampleRate);
    drumSynth.reset();
    sampleKit.prepare (sampleRate, 0);
    sampleKit.reset();
    // v1.6.1-rc.7 — re-bake the bundled kit at the host's actual sample
    // rate. SampleKit::loadBundled() resamples the WAV data to the rate
    // most-recently passed to prepare(), so loading once in the ctor at
    // a placeholder 48 kHz and never re-loading meant any non-48 kHz
    // host (44.1 kHz Logic Pro by default, 96 kHz mastering sessions)
    // played the kit at the wrong pitch and tempo.
    if (currentBundledKitName.isNotEmpty())
        sampleKit.loadBundled (currentBundledKitName);
    busMixer.prepare (sampleRate, 0, 2);
    busMixer.reset();
    hostTransportSeen.store (false, std::memory_order_relaxed);
}

// ============================================================================
// v1.4.0 — Sampler API
// ============================================================================
int AIDrumAudioProcessor::loadSampleKit (const juce::File& folder)
{
    const int n = sampleKit.load (folder);
    if (n > 0)
    {
        currentBundledKitName.clear();
        std::lock_guard<std::mutex> lock (loadedKitPathMutex);
        loadedKitPath = folder.getFullPathName();
    }
    return n;
}

void AIDrumAudioProcessor::unloadSampleKit()
{
    sampleKit.unload();
    currentBundledKitName.clear();
    std::lock_guard<std::mutex> lock (loadedKitPathMutex);
    loadedKitPath.clear();
}

int AIDrumAudioProcessor::loadBundledKit (const juce::String& kitName)
{
    const int n = sampleKit.loadBundled (kitName);
    if (n > 0)
    {
        currentBundledKitName = kitName;
        std::lock_guard<std::mutex> lock (loadedKitPathMutex);
        loadedKitPath = "Built-in " + kitName;
    }
    return n;
}

juce::String AIDrumAudioProcessor::getSampleKitPath() const
{
    std::lock_guard<std::mutex> lock (loadedKitPathMutex);
    return loadedKitPath;
}

bool AIDrumAudioProcessor::isSampleKitActive() const
{
    return sampleKit.isActive();
}

float AIDrumAudioProcessor::getUiScale() const
{
    return uiScale.load (std::memory_order_relaxed);
}

void AIDrumAudioProcessor::setUiScale (float s)
{
    // v1.6.1-rc.10 — clamp range tightened to match the new compressed
    // stops (0.55 / 0.70 / 0.85 / 1.00). Previous 0.75 → 1.5 range left
    // legacy projects loading at 1.5 (gigantic) which the user flagged
    // as "crazy big".
    uiScale.store (juce::jlimit (0.55f, 1.10f, s), std::memory_order_relaxed);
}

// ============================================================================
// v1.6.1-rc.7 — Fill selector API + Intensity readout
// ============================================================================

int AIDrumAudioProcessor::getFillLibrarySize() const
{
    return (int) aidrum::fillLibrary().size();
}

int AIDrumAudioProcessor::getCurrentFillIndex() const
{
    const int n = getFillLibrarySize();
    if (n <= 0) return 0;
    const float v = apvts.getRawParameterValue (kParamFillComplexity)->load();
    return juce::jlimit (0, n - 1,
                         (int) std::round (v * (float) (n - 1)));
}

juce::String AIDrumAudioProcessor::getCurrentFillName() const
{
    const auto& lib = aidrum::fillLibrary();
    if (lib.empty()) return "—";
    const int idx = getCurrentFillIndex();
    return juce::String (std::string (lib[(size_t) idx].name));
}

void AIDrumAudioProcessor::cycleFillSelector (int direction)
{
    const int n = getFillLibrarySize();
    if (n <= 1) return;
    const int dir = (direction >= 0) ? 1 : -1;
    const int next = ((getCurrentFillIndex() + dir) % n + n) % n;
    if (auto* p = apvts.getParameter (kParamFillComplexity))
    {
        const float norm = (float) next / (float) (n - 1);
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    }
}

int AIDrumAudioProcessor::getIntensity127() const
{
    return juce::jlimit (0, 127,
                         (int) std::round (
                             apvts.getRawParameterValue (kParamIntensity)->load() * 127.0f));
}

void AIDrumAudioProcessor::releaseResources() {}

bool AIDrumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

aidrum::GenerationRequest
AIDrumAudioProcessor::buildRequestForMode (aidrum::GenerationMode mode) const
{
    aidrum::GenerationRequest req;
    req.mode          = mode;
    req.variation     = apvts.getRawParameterValue (kParamVariation )->load();
    req.complexity    = apvts.getRawParameterValue (kParamComplexity)->load();
    req.velocity      = apvts.getRawParameterValue (kParamVelocity  )->load();
    req.humanize      = apvts.getRawParameterValue (kParamHumanize  )->load();
    req.lengthInBeats = patternLengthBeatsFromChoice (
                            (int) apvts.getRawParameterValue (kParamPatternLength)->load());
    req.genre         = static_cast<aidrum::Genre> (
                            (int) apvts.getRawParameterValue (kParamGenre)->load());
    req.tempoBpm      = lastBpm.load (std::memory_order_relaxed);

    req.swing         = apvts.getRawParameterValue (kParamSwing)    ->load();
    req.fillsProb     = apvts.getRawParameterValue (kParamFillsProb)->load();
    req.halfTime      = apvts.getRawParameterValue (kParamHalfTime) ->load() > 0.5f;
    req.hiHatMode     = static_cast<aidrum::HiHatMode> (
                            (int) apvts.getRawParameterValue (kParamHiHat)->load());
    req.kit           = static_cast<aidrum::DrumKit> (
                            (int) apvts.getRawParameterValue (kParamDrumKit)->load());

    // v1.5.0 — fillComplexity knob is independent of the overall complexity
    // knob; this lets users dial "simple groove + intricate fills" or vice-versa.
    // v1.6.1-rc.7: the knob now cycles the library fill selector. Map the
    // stored float (0..1) to a fill-library index so makeFill() can emit
    // that exact MIDI pattern verbatim.
    const float fcValue = apvts.getRawParameterValue (kParamFillComplexity)->load();
    req.fillComplexity = fcValue;
    const int numFills = (int) aidrum::fillLibrary().size();
    req.fillIndex      = (numFills > 0)
        ? juce::jlimit (0, numFills - 1,
                        static_cast<int> (std::round (fcValue * (float) (numFills - 1))))
        : -1;

    // v1.6.1-rc.7 — Intensity drives velocity base + fluctuation at emit
    // time. The generator doesn't consume it directly (notes are still
    // generated with their natural 0..1 velocities); shapeVelocity()
    // scales + fluctuates when we push notes into the MIDI buffer.
    req.intensity = apvts.getRawParameterValue (kParamIntensity)->load();

    // v1.6.1-rc.6 — single bundled kit. We always request the "Thrash"
    // groove profile (tight quick kick, cracking snare, bright hats;
    // the kit the user approved in rc.5 when we collapsed from 6 to 1).
    // The choice parameter is retained for save-file round-tripping
    // but its index is ignored — we never want the old PopRock /
    // NuRock / AltRock / IndieLofi / HardRock placement profiles
    // driving the single "Default" kit.
    req.bundledKit = aidrum::BundledKit::Thrash;

    // v1.6.1-rc.8 — pass the manual-grid step division through to the
    // intelligence pad so 1/32 / 1/64 unlock musically-placed
    // ostinatos and grace-note rolls (never random spam — see
    // applyDrummerPost in AIBackend.cpp).
    {
        const int stepDivIdx = (int) apvts.getRawParameterValue (kParamStepDiv)->load();
        req.stepsPerBar = (stepDivIdx == 2 ? 64 : stepDivIdx == 1 ? 32 : 16);
    }
    return req;
}

// ============================================================================
// v1.6.1-rc.7 — intensity-driven velocity shaping
// ============================================================================
namespace
{
    // v1.6.1-rc.8 — map a GM-ish note number to the arrangement strip's
    // 8-lane index (0=R CRASH, 1=L CRASH, 2=RIDE, 3=HI-HAT,
    // 4=SMALL TOM, 5=FLOOR TOM, 6=SNARE, 7=KICK). Mirrors
    // ArrangementStrip::laneFor() — keep them in sync.
    inline int noteToLane (int n) noexcept
    {
        if (n == 35 || n == 36)                                    return 7;
        if (n == 37 || n == 38 || n == 39 || n == 40)              return 6;
        if (n == 41 || n == 43 || n == 45)                         return 5; // FLOOR
        if (n == 47 || n == 48 || n == 50)                         return 4; // SMALL
        if (n == 42 || n == 44 || n == 46)                         return 3;
        if (n == 51 || n == 53 || n == 59)                         return 2;
        if (n == 49)                                               return 1; // L CRASH
        return 0; // 52 / 55 / 57 — china / right crash
    }

    // Instrument-specific fluctuation factors. Kick/snare stay stable
    // (drummers are consistent on backbeats); hats breathe more; toms
    // accent; ghost notes (velocity < 0.4) vary the most.
    inline float fluctuationFactorForNote (int noteNumber, float noteVelocity01) noexcept
    {
        // Ghost notes fluctuate most regardless of instrument
        if (noteVelocity01 < 0.4f) return 1.35f;

        switch (noteNumber)
        {
            // Kick
            case 35: case 36:                                return 0.55f;
            // Snare / side-stick
            case 37: case 38: case 39: case 40:              return 0.50f;
            // Toms
            case 41: case 43: case 45: case 47: case 48: case 50: return 0.75f;
            // Hi-hat family (breathes)
            case 42: case 44: case 46:                       return 0.95f;
            // Ride
            case 51: case 53: case 59:                       return 0.80f;
            // Crashes (hits hard, little variation)
            case 49: case 52: case 55: case 57:              return 0.45f;
            default:                                         return 0.70f;
        }
    }

    // Applies the INTENSITY curve to a raw note velocity (0..1) and
    // returns the final MIDI velocity byte (1..127).
    //
    // Model (matches the user's rc.7 brief: "60% intensity = velocity
    // floats between 56% and 61%"):
    //   centre  = intensity * 127           (the knob IS the velocity)
    //   jitter  = uniform[-4..+1] %         (asymmetric, slightly biased
    //             below centre — drummers more often *under*-strike than
    //             over-strike)
    //   factor  = fluctuationFactorForNote() (kick/snare ~0.5, hats ~0.95,
    //             ghosts 1.35) so the kit feels like real human hands
    //             rather than a uniform wash of jitter
    //   ghosts  = notes whose stored velocity is < 0.4 are rendered as
    //             ghost-strength hits (centre * 0.45 + small jitter) so
    //             the user's authored ghost notes stay quiet even at
    //             100% intensity
    //   accents = notes whose stored velocity is > 0.95 punch ~6 above
    //             centre so backbeat snares + crashes still land hard
    inline juce::uint8 shapeVelocity (int   noteNumber,
                                      float noteVelocity01,
                                      float intensity01,
                                      juce::Random& rng,
                                      int   ghostMask = 0) noexcept
    {
        const float i        = juce::jlimit (0.0f, 1.0f, intensity01);
        const float centre   = i * 127.0f;
        const float factor   = fluctuationFactorForNote (noteNumber, noteVelocity01);

        // Per-hit jitter, in MIDI velocity units, scaled by ~5% of full
        // range. Asymmetric: -4% to +1% (matches user's 56..61 example
        // from a 60% knob).
        const float lowPct   = -4.0f * factor;
        const float highPct  =  1.0f * factor;
        const float pctJit   = lowPct + (float) rng.nextDouble() * (highPct - lowPct);
        const float jitter   = pctJit * 1.27f; // 1% of 127

        // Note-baked dynamics (ghost / accent) shift the centre a bit so
        // the user's authored phrasing survives the intensity remap.
        float dynamicAdj = 0.0f;
        if (noteVelocity01 < 0.4f)        dynamicAdj = -centre * 0.55f; // ghost
        else if (noteVelocity01 > 0.95f)  dynamicAdj = +6.0f;           // accent

        // v1.6.1-rc.7 — lane-level ghost mask. If the GHOST button has
        // been armed for this note's lane, the entire hit is rendered
        // as a ghost (centre × 0.45) so the row "feathers" without the
        // user having to redraw their pattern. Mapping mirrors the
        // arrangement strip's lane table.
        const int lane = noteToLane (noteNumber);
        if (lane >= 0 && (ghostMask & (1 << lane)) != 0)
        {
            const float ghostCentre = centre * 0.45f;
            const float v = ghostCentre + jitter * 0.5f;
            return static_cast<juce::uint8> (juce::jlimit (1.0f, 127.0f, v));
        }

        const float v = centre + dynamicAdj + jitter;
        return static_cast<juce::uint8> (juce::jlimit (1.0f, 127.0f, v));
    }
}

void AIDrumAudioProcessor::appendRegion (aidrum::GenerationMode requestedMode)
{
    auto req = buildRequestForMode (requestedMode);

    // v1.3.0 phraseBar: region index in the arrangement so the backend
    // can evolve the performance (fills vary every 8 bars, dynamics
    // breathe across phrases, fills never repeat).
    aidrum::MidiPattern previous;
    bool hasPrevious = false;
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        req.phraseBar = static_cast<int> (arrangement.size());
        if (! arrangement.empty())
        {
            previous = arrangement.back();
            hasPrevious = true;
        }
    }

    // v1.6.0 — 8-bar phrase cap is the ONLY place we auto-promote a region
    // to a Fill. Between caps the fillsProb knob is still available.
    const bool phraseCap = (req.phraseBar % 8) == 7;
    if (requestedMode == aidrum::GenerationMode::Groove)
    {
        if (phraseCap)
            req.mode = aidrum::GenerationMode::Fill;
        else if (req.fillsProb > 0.0f)
        {
            std::mt19937_64 rng (static_cast<std::uint64_t> (std::random_device{}()));
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            if (unit (rng) < req.fillsProb)
                req.mode = aidrum::GenerationMode::Fill;
        }
    }

    // v1.6.0 — Groove regions appended via `+` duplicate the previous
    // region's pattern so the arrangement stays cohesive across bars
    // (the user explicitly asked for "complement, don't randomize").
    // Fills and first-region always generate fresh.
    aidrum::MidiPattern pattern;
    if (req.mode == aidrum::GenerationMode::Groove && hasPrevious)
        pattern = previous;
    else
        pattern = backend.generate (req);

    // v1.6.1-rc.5 — tag the region with its generation mode so
    // regenerateCurrentRegion can preserve Fill-vs-Groove on knob-tweak
    // regen instead of silently converting every Fill into a Groove.
    pattern.isFill = (req.mode == aidrum::GenerationMode::Fill);

    std::lock_guard<std::mutex> lock (arrangementMutex);
    arrangement.push_back (std::move (pattern));
    // Note: deliberately do NOT reset the playhead here — we want the
    // sequencer to keep rolling into the newly-appended region.
}

void AIDrumAudioProcessor::undoLastRegion()
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    // v1.5.0: allow arrangement to go empty. The UI shows the `+` button
    // centered so the user can seed a new first region.
    if (! arrangement.empty())
        arrangement.pop_back();
    if (arrangement.empty())
        playheadBeats.store (0.0, std::memory_order_release);
}

void AIDrumAudioProcessor::deleteRegion (int index)
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (index < 0 || index >= static_cast<int> (arrangement.size()))
        return;
    arrangement.erase (arrangement.begin() + index);
    if (arrangement.empty())
        playheadBeats.store (0.0, std::memory_order_release);
}

void AIDrumAudioProcessor::clearArrangement()
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    arrangement.clear();
    playheadBeats.store (0.0, std::memory_order_release);
}

// ============================================================================
// v1.6.0 STARTER GROOVES
// ============================================================================

int AIDrumAudioProcessor::starterGrooveCount() const
{
    // v1.6.1-rc.7 fix — index against the merged library (analyzer +
    // BFD palette grooves). starterIndicesForKit() returns indices into
    // allStarterGrooves(), so every lookup that consumes those indices
    // must too — otherwise the 55 BFD palette grooves are silently
    // unreachable and a third of the STARTER dropdown goes blank.
    return static_cast<int> (aidrum::allStarterGrooves().size());
}

juce::String AIDrumAudioProcessor::starterGrooveName (int index) const
{
    const auto& lib = aidrum::allStarterGrooves();
    if (index < 0 || index >= static_cast<int> (lib.size()))
        return {};
    return juce::String (std::string (lib[(size_t) index].name));
}

void AIDrumAudioProcessor::appendStarterGroove (int index)
{
    const auto& lib = aidrum::allStarterGrooves();
    if (index < 0 || index >= static_cast<int> (lib.size()))
        return;

    aidrum::MidiPattern pat = lib[(size_t) index].pattern;
    // Force tempo onto the pattern so the arrangement strip sees a length
    // that matches what the user hears.
    std::lock_guard<std::mutex> lock (arrangementMutex);
    arrangement.push_back (std::move (pat));
}

// ---------- v1.6.1-rc.3 kit-filtered STARTERs ----------

int AIDrumAudioProcessor::starterGrooveCountForKit (int kitIndex) const
{
    return static_cast<int> (aidrum::starterIndicesForKit (kitIndex).size());
}

juce::String AIDrumAudioProcessor::starterGrooveNameForKit (int kitIndex, int subIndex) const
{
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (subIndex < 0 || subIndex >= static_cast<int> (bucket.size()))
        return {};
    return starterGrooveName (bucket[(size_t) subIndex]);
}

void AIDrumAudioProcessor::appendStarterGrooveForKit (int kitIndex, int subIndex)
{
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (subIndex < 0 || subIndex >= static_cast<int> (bucket.size()))
        return;
    appendStarterGroove (bucket[(size_t) subIndex]);
}

void AIDrumAudioProcessor::appendRandomGrooveForKit (int kitIndex)
{
    // v1.6.1-rc.7 — Logic-Pro Scripter-style cycler. The user explicitly
    // asked the COMPOSE button to walk through groove options "in a
    // reasonable and predictable way" rather than dealing a random card
    // every press. We round-robin through the kit's bucket so repeated
    // taps audition every groove in order, then loop. The counter is
    // per-kit (kitIndex 0..N-1) so switching kits doesn't yank the user
    // mid-cycle.
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (bucket.empty())
        return;
    const int safeKit = juce::jlimit (0, (int) composeCycleIndex.size() - 1,
                                      std::max (0, kitIndex));
    auto& counter = composeCycleIndex[(size_t) safeKit];
    const size_t pick = counter % bucket.size();
    counter = (counter + 1) % (bucket.size() * 16); // bound so it never overflows
    appendStarterGroove (bucket[pick]);
}

// v1.6.1-rc.9 — COMPOSE: mold around the existing last region. We pick
// the next corpus groove via the same Scripter-style cycler the old
// COMPOSE used, then *overlay* notes from that groove onto the last
// region instead of replacing it. The user's manual edits are the
// authoritative voice (kick / snare / hat positions on the
// downbeats); the corpus pattern only contributes the decoration
// notes the user wouldn't have drawn by hand — ghost-snare drags,
// 1/16 hat ostinatos, kick syncopations on the "and" of beats. We
// dedupe near-coincident hits per (note, time) so we never stack two
// kicks on the same downbeat.
void AIDrumAudioProcessor::composeMoldAroundForKit (int kitIndex)
{
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (bucket.empty())
        return;

    // Scripter-style cycler — same counter as the old COMPOSE so
    // repeated clicks audition every groove in the bucket in order.
    const int safeKit = juce::jlimit (0, (int) composeCycleIndex.size() - 1,
                                      std::max (0, kitIndex));
    auto& counter = composeCycleIndex[(size_t) safeKit];
    const size_t pick = counter % bucket.size();
    counter = (counter + 1) % (bucket.size() * 16);

    const auto& lib = aidrum::allStarterGrooves();
    const int libIdx = bucket[pick];
    if (libIdx < 0 || libIdx >= static_cast<int> (lib.size()))
        return;

    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (arrangement.empty())
    {
        // First click — nothing to mold around. Drop the groove in as
        // a fresh region so the user gets sound, then subsequent COMPOSE
        // clicks will mold around it.
        arrangement.push_back (lib[(size_t) libIdx].pattern);
        // v1.6.1-rc.11 — apply intensity-driven crash/hat balance
        // BEFORE the fill splice so the fill bar gets the freshest
        // hits laid by the splicer (otherwise crash placement could
        // dog-pile inside the fill window).
        applyIntensityCrashHatBalance (arrangement.back(),
                                       static_cast<std::uint64_t> (counter) ^ 0x5151ULL);
        // v1.6.1-rc.11 — every COMPOSE result carries a fill at
        // bar-8 beat 1 (was beat 2 in rc.10), even on first click.
        spliceMandatoryFillIntoRegion (arrangement.back(), (int) counter);
        return;
    }

    auto& last = arrangement.back();
    const aidrum::MidiPattern& src = lib[(size_t) libIdx].pattern;

    // Build a fast (note, quantised-beat) set of what the user already
    // has so we never double-up a kick/snare on the same downbeat.
    auto quant = [] (double b) { return (int) std::llround (b * 8.0); }; // 1/32 grid
    std::set<std::pair<int,int>> existing;
    existing.clear();
    for (const auto& n : last.notes)
        existing.insert ({ n.noteNumber, quant (n.startBeat) });

    // Phase-align src to last by wrapping its beat positions modulo
    // last.lengthInBeats so an 8-bar src groove decorates a 4-bar
    // user region cleanly.
    const double targetLen = std::max (0.5, last.lengthInBeats);
    for (const auto& n : src.notes)
    {
        aidrum::MidiNote dec = n;
        dec.startBeat = std::fmod (n.startBeat, targetLen);
        if (dec.startBeat < 0.0) dec.startBeat += targetLen;

        const auto key = std::make_pair (dec.noteNumber, quant (dec.startBeat));
        if (existing.count (key))
            continue;

        // Mold-around damping: corpus decorations come in softer than
        // the user's primary pattern so they read as accents, not as
        // a competing groove. Ghost-velocity hits stay ghost.
        dec.velocity = juce::jlimit (0.05f, 1.0f, dec.velocity * 0.78f);
        last.notes.push_back (dec);
        existing.insert (key);
    }

    // Keep notes sorted by start time so any consumer that assumes
    // monotonic order (e.g. older arrangement-strip render code)
    // doesn't get confused.
    std::sort (last.notes.begin(), last.notes.end(),
               [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
               { return a.startBeat < b.startBeat; });

    // v1.6.1-rc.11 — apply intensity-driven crash/hat balance to the
    // molded result before the fill splice (so high INTENSITY thins
    // hats / lays alternating L↔R crashes / no hats above 0.92).
    applyIntensityCrashHatBalance (last,
                                   static_cast<std::uint64_t> (counter) ^ 0xC0DEULL);
    // v1.6.1-rc.11 — mold-around result must always end on a fill at
    // bar-8 BEAT 1 (was beat 2 in rc.10) — user changed the spec.
    spliceMandatoryFillIntoRegion (last, (int) counter);
}

// v1.6.1-rc.9 — RANDOMIZE: full pattern replace, the original COMPOSE
// behavior. Picks a fresh groove and either replaces the last region
// or appends one if the arrangement is empty.
void AIDrumAudioProcessor::randomizePatternForKit (int kitIndex)
{
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (bucket.empty())
        return;

    std::mt19937_64 rng (static_cast<std::uint64_t> (std::random_device{}()));
    std::uniform_int_distribution<size_t> pick (0, bucket.size() - 1);
    const auto& lib = aidrum::allStarterGrooves();
    const int libIdx = bucket[pick (rng)];
    if (libIdx < 0 || libIdx >= static_cast<int> (lib.size()))
        return;

    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (arrangement.empty())
        arrangement.push_back (lib[(size_t) libIdx].pattern);
    else
        arrangement.back() = lib[(size_t) libIdx].pattern;

    // v1.6.1-rc.11 — apply intensity-driven crash/hat balance before
    // the fill splice. Above 0.92 intensity, ALL generator-placed
    // hats are stripped and L+R crashes hit on every phrase top.
    applyIntensityCrashHatBalance (arrangement.back(),
                                   static_cast<std::uint64_t> (libIdx) ^ 0xBEEFULL);
    // v1.6.1-rc.11 — RANDOMIZE result must always end on a fill at
    // bar-8 BEAT 1 (was beat 2 in rc.10) — user changed the spec.
    spliceMandatoryFillIntoRegion (arrangement.back(), libIdx);
}

// v1.6.1-rc.9 — Auto-fill helper. Returns a one-bar fill that the
// caller will splice in as the 8th bar of an 8-bar block. Action
// starts on beat 2 of the bar so beat 1 still carries the previous
// groove's downbeat. The fill is a tom roll → snare/crash punctuation
// using the canonical 8-lane note numbers.
aidrum::MidiPattern AIDrumAudioProcessor::makeAutoFillForKit (int /*kitIndex*/,
                                                              int seed) const
{
    aidrum::MidiPattern p;
    p.lengthInBeats = 4.0;          // one 4/4 bar; host clock forces tempo

    const int noteSnare    = 38;
    const int noteHighTom  = 48;
    const int noteLowTom   = 43;
    const int noteCrashL   = 49;
    const int noteKick     = 36;

    auto add = [&p] (int n, double startBeat, float v)
    {
        aidrum::MidiNote x;
        x.noteNumber = n;
        x.startBeat  = startBeat;
        x.lengthBeat = 0.20;
        x.velocity   = juce::jlimit (0.0f, 1.0f, v);
        p.notes.push_back (x);
    };

    // Beat 1 — kick anchor only (so the user's groove still resolves).
    add (noteKick, 0.00, 0.95f);

    // Beat 2 → 4 = the fill action. Tom drag with snare punctuation,
    // ending on a crash + kick on beat 1 of the *next* bar (which the
    // caller appends as a separate region — we just close the bar).
    // Pattern shape (8th-note grid):
    //   2.0 snare  2.5 snare ghost  3.0 high-tom  3.25 high-tom
    //   3.5 low-tom 3.75 low-tom  3.875 snare flam (1/64 grace)  4 ends.
    add (noteSnare,   2.00, 0.85f);
    add (noteSnare,   2.50, 0.45f);   // ghost
    add (noteHighTom, 3.00, 0.80f);
    add (noteHighTom, 3.25, 0.78f);
    add (noteLowTom,  3.50, 0.85f);
    add (noteLowTom,  3.75, 0.88f);
    add (noteSnare,   3.875, 0.70f);  // 1/64 flam grace into the downbeat

    // Crash on the implicit downbeat of the next bar — we represent
    // that by laying it just inside the last 1/64 of this region so
    // the audio engine fires it before the next region starts.
    add (noteCrashL,  3.99, 0.92f);

    // Tiny seed-driven shuffle so repeated 8-bar cycles aren't
    // identical: drop one random tom hit from the middle on every
    // 4th cycle for breath.
    if ((seed & 3) == 0 && p.notes.size() > 3)
    {
        // Erase the 4th note (first high-tom) for a tasteful drag.
        for (auto it = p.notes.begin(); it != p.notes.end(); ++it)
            if (it->noteNumber == noteHighTom)
            {
                p.notes.erase (it);
                break;
            }
    }

    return p;
}

// v1.6.1-rc.11 — guarantees every COMPOSE / RANDOMIZE result carries a
// fill that starts on bar-8 BEAT 1 of every 8-bar block (was bar-8 beat 2
// in rc.10 — user changed the spec, with the annotated screenshot showing
// the fill landing on the very downbeat of bar 8).
//
// rc.11 also:
//   * Cycles through the 22-fill library by index (FILL selector +
//     per-block stride). The user's "Cycle between 2 of the 22 Fills
//     Every Bar" — we lay down the FILL-selector fill at every boundary,
//     and on multi-block regions we advance one slot per block so the
//     library walks instead of repeating.
//   * If a fill pattern is shorter than the bar (the fill's
//     lengthInBeats < 4.0), the fill is *tiled* (looped end-to-end)
//     until it reaches beat 4 of bar 8 — fulfilling "IF THE PATTERN IS
//     INCOMPLETE REPEAT THE FILL TILL THE FULL GROOVE IS DONE".
//   * NEVER stretches the region past its existing length. The rc.10
//     auto-stretch to multiples of 32 was the source of "the arrangement
//     randomly shapes out too far when clicking the FILL dial". Now if
//     the region is shorter than 8 bars we just splice into whatever
//     bar-8 window we *do* have (region.lengthInBeats - 4 .. end) and
//     skip the splice entirely if the region is < 4 beats.
void AIDrumAudioProcessor::spliceMandatoryFillIntoRegion (
    aidrum::MidiPattern& region, int seed) const
{
    constexpr double kBarsPerBlock = 8.0;
    constexpr double kBeatsPerBar  = 4.0;
    constexpr double kBlockBeats   = kBarsPerBlock * kBeatsPerBar; // 32

    if (region.lengthInBeats < kBeatsPerBar - 1e-6)
        return; // region too short to host a 1-bar fill — skip silently

    const auto& fillLib = aidrum::fillLibrary();
    if (fillLib.empty())
        return;

    // FILL-selector index (0..21). Auto-fill picks the SAME fill the
    // user's FILL cycler is currently on, then advances one library slot
    // per 8-bar block so 16-bar / 24-bar regions walk through the
    // library instead of looping the same pattern.
    // NB: kParamFillComplexity is a normalised 0..1 parameter — multiply
    // by (N-1) and round before casting, otherwise every value < 1.0
    // truncates to 0 and the cycler is silently pinned to fill #0
    // (caught by Devin Review in rc.11).
    const int numFills = static_cast<int> (fillLib.size());
    const float fcRaw  = apvts.getRawParameterValue (kParamFillComplexity)->load();
    const int selector = juce::jlimit (0, numFills - 1,
                                       static_cast<int> (std::round (fcRaw * (float) (numFills - 1))));

    // Compute how many full 8-bar blocks fit in this region. Every full
    // block gets a fill anchored at bar-8 beat 1 of that block. Anything
    // less than a full block (e.g. a 6-bar region) gets its fill
    // anchored on the LAST bar's beat 1 — so the user always hears a
    // fill on the closing bar of whatever they laid down.
    const int fullBlocks = static_cast<int> (region.lengthInBeats / kBlockBeats);
    const bool partialTail = (region.lengthInBeats - fullBlocks * kBlockBeats) > 1e-6;

    auto fillAnchorForBlock = [&] (int b) -> double
    {
        if (b < fullBlocks)
            return b * kBlockBeats + (kBarsPerBlock - 1.0) * kBeatsPerBar; // bar 8 beat 1
        // partial tail: anchor on the LAST full bar of the region
        const double lastBarStart = std::floor ((region.lengthInBeats - 1e-6) / kBeatsPerBar) * kBeatsPerBar;
        return juce::jmax (0.0, lastBarStart);
    };

    const int totalAnchors = fullBlocks + (partialTail ? 1 : 0);
    if (totalAnchors == 0)
        return;

    // Wipe existing notes inside every fill window so the fill reads
    // clean. Window = [anchor, min(anchor+4, region.lengthInBeats)).
    auto inFillWindow = [&] (double beat) noexcept
    {
        for (int b = 0; b < totalAnchors; ++b)
        {
            const double anchor = fillAnchorForBlock (b);
            const double end    = juce::jmin (anchor + kBeatsPerBar, region.lengthInBeats);
            if (beat >= anchor - 1e-6 && beat < end - 1e-6)
                return true;
        }
        return false;
    };

    region.notes.erase (
        std::remove_if (region.notes.begin(), region.notes.end(),
                        [&] (const aidrum::MidiNote& n)
                        { return inFillWindow (n.startBeat); }),
        region.notes.end());

    // Splice fills.
    for (int b = 0; b < totalAnchors; ++b)
    {
        const int           fillIdx = (selector + b) % static_cast<int> (fillLib.size());
        const auto&         fill    = fillLib[(size_t) fillIdx].pattern;
        const double        anchor  = fillAnchorForBlock (b);
        const double        windowEnd = juce::jmin (anchor + kBeatsPerBar, region.lengthInBeats);
        const double        fillLen = (fill.lengthInBeats > 1e-6 ? fill.lengthInBeats : kBeatsPerBar);

        // Tile/loop the fill until it covers the full window.
        // "IF THE PATTERN IS INCOMPLETE REPEAT THE FILL TILL THE FULL
        // GROOVE IS DONE" — repeat the fill end-to-end inside the bar.
        for (double tileOffset = 0.0;
             tileOffset < (windowEnd - anchor) - 1e-6;
             tileOffset += fillLen)
        {
            for (const auto& src : fill.notes)
            {
                const double absBeat = anchor + tileOffset + src.startBeat;
                if (absBeat < anchor - 1e-6) continue;
                if (absBeat >= windowEnd - 1e-6) continue;
                aidrum::MidiNote shifted = src;
                shifted.startBeat = absBeat;
                region.notes.push_back (shifted);
            }
        }
        juce::ignoreUnused (seed);
    }

    std::sort (region.notes.begin(), region.notes.end(),
               [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
               { return a.startBeat < b.startBeat; });
}

// v1.6.1-rc.11 — INTENSITY-driven crash / hat balance for generator
// output. The user has been emphatic: "HIGHER INTENSITY MORE CRASHES AND
// LESS HI HATS ALL THE TIME … NOT HATS ABOVE 92 INTENSITY … UNLESS
// MANUALLY PUT IN ON MANUAL MODE OR AUTO MODE." This helper is invoked
// ONLY by COMPOSE / RANDOMIZE (and other generator paths) — never on
// the manual pattern, so hand-clicked hats are never touched.
//
// Curve (intensity = APVTS reading, 0..1):
//   intensity <= 0.55                : leave hats alone, no extra crashes
//   0.55 .. 0.92                     : probabilistically thin hats; place
//                                       L↔R alternating crashes on every
//                                       4-beat phrase top + the "and" of 4
//   intensity >= 0.92                : strip ALL hat hits (42/44/46) from
//                                       the generator output; lay double
//                                       L+R crashes on every 4-beat
//                                       phrase top + every snare backbeat
void AIDrumAudioProcessor::applyIntensityCrashHatBalance (
    aidrum::MidiPattern& region, std::uint64_t seed) const
{
    constexpr int kClosedHat = 42;
    constexpr int kPedalHat  = 44;
    constexpr int kOpenHat   = 46;
    constexpr int kSnare     = 38;
    constexpr int kCrashL    = 49;
    constexpr int kCrashR    = 57;

    const float intensity = juce::jlimit (
        0.0f, 1.0f, apvts.getRawParameterValue (kParamIntensity)->load());

    // Below the floor, generator output is left alone.
    if (intensity <= 0.55f + 1e-3f)
        return;

    std::mt19937_64 rng (seed ^ 0x9E3779B97F4A7C15ULL);
    auto roll = [&] () { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); };

    // Hat thinning probability: 0% at 0.55, ~70% at 0.85, 100% at >=0.92.
    auto hatRemoveProb = [intensity] () -> float
    {
        if (intensity >= 0.92f) return 1.0f;
        if (intensity <= 0.55f) return 0.0f;
        const float t = (intensity - 0.55f) / (0.92f - 0.55f);
        return juce::jlimit (0.0f, 1.0f, std::pow (t, 0.85f));
    } ();

    region.notes.erase (
        std::remove_if (region.notes.begin(), region.notes.end(),
                        [&] (const aidrum::MidiNote& n)
                        {
                            const bool isHat = (n.noteNumber == kClosedHat
                                             || n.noteNumber == kPedalHat
                                             || n.noteNumber == kOpenHat);
                            if (! isHat) return false;
                            return roll() < hatRemoveProb;
                        }),
        region.notes.end());

    // Crash placement. Density rises with intensity. Beat positions
    // chosen from real sludge / heavy-rock placement: phrase tops,
    // snare-backbeat doubles, "and" of 4 leading into the next bar.
    const double regLen = std::max (0.0, region.lengthInBeats);
    if (regLen < 4.0)
        return;

    auto noteAt = [&] (int n, double startBeat, float vel) -> aidrum::MidiNote
    {
        aidrum::MidiNote m;
        m.noteNumber = n;
        m.startBeat  = startBeat;
        m.lengthBeat = 0.20;
        m.velocity   = juce::jlimit (0.0f, 1.0f, vel);
        return m;
    };

    auto hasNote = [&] (int n, double startBeat) -> bool
    {
        for (const auto& x : region.notes)
            if (x.noteNumber == n && std::abs (x.startBeat - startBeat) < 1e-2)
                return true;
        return false;
    };

    auto place = [&] (int n, double startBeat, float vel)
    {
        if (startBeat < 0.0 || startBeat >= regLen - 1e-3) return;
        if (hasNote (n, startBeat)) return;
        region.notes.push_back (noteAt (n, startBeat, vel));
    };

    // Phrase-top crashes on every bar 1, alternating L↔R per bar.
    // At very high intensity, double them (L AND R together) on the
    // 1 of bar 1 / bar 5 of every 8-bar phrase.
    const int totalBars = static_cast<int> (regLen / 4.0 + 1e-6);
    for (int bar = 0; bar < totalBars; ++bar)
    {
        const double anchor = bar * 4.0;
        const bool   isPhraseTop = (bar % 4 == 0); // bar 1, 5, 9, …
        const float  vel = 0.85f + 0.10f * intensity;

        if (intensity >= 0.92f)
        {
            // Sludge — both L and R crashes hit on every phrase top
            if (isPhraseTop)
            {
                place (kCrashL, anchor, vel);
                place (kCrashR, anchor, vel);
            }
            else if (intensity >= 0.95f && (bar & 1))
            {
                // and on every other bar at the top
                place ((bar % 2) ? kCrashL : kCrashR, anchor, vel * 0.92f);
            }
        }
        else
        {
            // Mid-high — alternate L/R on phrase tops
            if (isPhraseTop)
                place ((bar & 1) ? kCrashR : kCrashL, anchor, vel);
        }
    }

    // Mid-bar accents on the "and" of 4 leading into the next bar
    // (3.5 within each bar). Probabilistic, scaled by intensity.
    if (intensity >= 0.65f)
    {
        const float prob = juce::jlimit (0.0f, 1.0f,
                                          (intensity - 0.65f) / 0.35f);
        for (int bar = 0; bar < totalBars; ++bar)
        {
            if (roll() >= prob) continue;
            const double accent = bar * 4.0 + 3.5;
            const int    note   = (bar & 1) ? kCrashL : kCrashR;
            place (note, accent, 0.70f + 0.20f * intensity);
        }
    }

    // Backbeat-doubled crashes on every snare hit (beats 2/4) at very
    // high intensity. This is what sells the "more crash, less hat"
    // sludge feel — the kit reads as crash-led instead of hat-led.
    if (intensity >= 0.88f)
    {
        std::vector<aidrum::MidiNote> snareCrashes;
        snareCrashes.reserve (16);
        for (const auto& n : region.notes)
        {
            if (n.noteNumber != kSnare) continue;
            // Only double on the 2 / 4 backbeats — avoid ghost notes
            const double inBar = std::fmod (n.startBeat, 4.0);
            const bool onBackbeat = std::abs (inBar - 1.0) < 1e-2
                                  || std::abs (inBar - 3.0) < 1e-2;
            if (! onBackbeat) continue;
            const int crashNote = (((int) std::round (n.startBeat / 4.0)) & 1)
                                  ? kCrashR : kCrashL;
            snareCrashes.push_back (noteAt (crashNote, n.startBeat, 0.78f));
        }
        for (auto& sc : snareCrashes)
            if (! hasNote (sc.noteNumber, sc.startBeat))
                region.notes.push_back (std::move (sc));
    }

    std::sort (region.notes.begin(), region.notes.end(),
               [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
               { return a.startBeat < b.startBeat; });
}

void AIDrumAudioProcessor::remapLastRegionToKit (int kitIndex)
{
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (bucket.empty())
        return;

    std::mt19937_64 rng (static_cast<std::uint64_t> (std::random_device{}()));
    std::uniform_int_distribution<size_t> pick (0, bucket.size() - 1);
    const auto& lib = aidrum::allStarterGrooves();
    const int libIdx = bucket[pick (rng)];
    if (libIdx < 0 || libIdx >= static_cast<int> (lib.size()))
        return;

    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (arrangement.empty())
        return;
    arrangement.back() = lib[(size_t) libIdx].pattern;
}

// ============================================================================
// v1.6.1-rc.4 per-note editing
// ============================================================================

void AIDrumAudioProcessor::deleteNoteInRegion (int regionIndex, int noteIndex)
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (regionIndex < 0 || regionIndex >= static_cast<int> (arrangement.size()))
        return;
    auto& region = arrangement[(size_t) regionIndex];
    if (noteIndex < 0 || noteIndex >= static_cast<int> (region.notes.size()))
        return;
    region.notes.erase (region.notes.begin() + noteIndex);
}

void AIDrumAudioProcessor::duplicateNoteInRegion (int regionIndex, int noteIndex)
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (regionIndex < 0 || regionIndex >= static_cast<int> (arrangement.size()))
        return;
    auto& region = arrangement[(size_t) regionIndex];
    if (noteIndex < 0 || noteIndex >= static_cast<int> (region.notes.size()))
        return;
    aidrum::MidiNote copy = region.notes[(size_t) noteIndex];
    // nudge 1 sixteenth to the right so it's audible; clamp to region end.
    copy.startBeat = std::min (region.lengthInBeats - 0.05,
                               copy.startBeat + 0.25);
    region.notes.push_back (copy);
}

void AIDrumAudioProcessor::addNoteToRegion (int regionIndex, int noteNumber,
                                            double startBeat, double lengthBeats,
                                            float velocity)
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (regionIndex < 0 || regionIndex >= static_cast<int> (arrangement.size()))
        return;
    auto& region = arrangement[(size_t) regionIndex];
    aidrum::MidiNote n;
    n.noteNumber = juce::jlimit (0, 127, noteNumber);
    n.velocity   = juce::jlimit (0.0f, 1.0f, velocity);
    n.startBeat  = juce::jlimit (0.0, region.lengthInBeats - 0.01, startBeat);
    n.lengthBeat = std::max (0.05, lengthBeats);
    region.notes.push_back (n);
}

// ============================================================================
// v1.6.0 COPY / PASTE region
// ============================================================================

void AIDrumAudioProcessor::copyRegionToClipboard (int index)
{
    // v1.6.1-rc.5 — match pasteCopiedRegion's lock order
    // (arrangementMutex → release → clipboardMutex) so the two routines
    // can't ABBA-deadlock if they're ever invoked from different threads.
    aidrum::MidiPattern snap;
    {
        std::lock_guard<std::mutex> lockA (arrangementMutex);
        if (index < 0 || index >= static_cast<int> (arrangement.size()))
            return;
        snap = arrangement[(size_t) index];
    }
    std::lock_guard<std::mutex> lockC (clipboardMutex);
    clipboardPattern = std::move (snap);
}

bool AIDrumAudioProcessor::hasCopiedRegion() const
{
    std::lock_guard<std::mutex> lock (clipboardMutex);
    return clipboardPattern.has_value();
}

void AIDrumAudioProcessor::pasteCopiedRegion()
{
    aidrum::MidiPattern toAppend;
    {
        std::lock_guard<std::mutex> lock (clipboardMutex);
        if (! clipboardPattern.has_value())
            return;
        toAppend = *clipboardPattern;
    }
    std::lock_guard<std::mutex> lock (arrangementMutex);
    arrangement.push_back (std::move (toAppend));
}

std::vector<aidrum::MidiPattern> AIDrumAudioProcessor::getArrangement() const
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    return arrangement;
}

double AIDrumAudioProcessor::getArrangementTotalBeats() const
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    double total = 0.0;
    for (const auto& p : arrangement)
        total += std::max (0.001, p.lengthInBeats);
    return total;
}

double AIDrumAudioProcessor::getPlayheadBeats() const
{
    return playheadBeats.load (std::memory_order_acquire);
}

aidrum::MidiPattern AIDrumAudioProcessor::getCurrentPattern() const
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    return arrangement.empty() ? aidrum::MidiPattern{} : arrangement.back();
}

// ============================================================================
// v0.8.0 Manual Mode
// ============================================================================

bool AIDrumAudioProcessor::isManualMode() const
{
    return manualModeActive.load (std::memory_order_acquire);
}

void AIDrumAudioProcessor::setManualMode (bool shouldBeOn)
{
    manualModeActive.store (shouldBeOn, std::memory_order_release);
    // Restart playhead when flipping modes so the user hears the
    // new source from beat 1.
    playheadBeats.store (0.0, std::memory_order_release);
}

namespace
{
    // 16th note = 0.25 beats.
    constexpr double kStepBeats = 0.25;
}

void AIDrumAudioProcessor::setManualCell (int midiNote, int stepIndex, float velocity)
{
    setManualCellStep (midiNote, stepIndex, kStepBeats, velocity);
}

void AIDrumAudioProcessor::clearManualCell (int midiNote, int stepIndex)
{
    clearManualCellStep (midiNote, stepIndex, kStepBeats);
}

void AIDrumAudioProcessor::setManualCellStep (int midiNote, int stepIndex,
                                              double stepBeats, float velocity)
{
    if (stepIndex < 0 || stepBeats <= 0.0) return;
    std::lock_guard<std::mutex> lock (manualMutex);

    const double startBeat = stepIndex * stepBeats;
    if (startBeat >= manualPattern.lengthInBeats) return;

    for (auto& n : manualPattern.notes)
    {
        if (n.noteNumber == midiNote
            && std::abs (n.startBeat - startBeat) < stepBeats * 0.45)
        {
            n.velocity = juce::jlimit (0.05f, 1.0f, velocity);
            return;
        }
    }

    aidrum::MidiNote note;
    note.noteNumber = midiNote;
    note.startBeat  = startBeat;
    note.lengthBeat = stepBeats * 0.9;
    note.velocity   = juce::jlimit (0.05f, 1.0f, velocity);
    manualPattern.notes.push_back (note);
}

void AIDrumAudioProcessor::clearManualCellStep (int midiNote, int stepIndex, double stepBeats)
{
    if (stepIndex < 0 || stepBeats <= 0.0) return;
    std::lock_guard<std::mutex> lock (manualMutex);
    const double startBeat = stepIndex * stepBeats;
    manualPattern.notes.erase (
        std::remove_if (manualPattern.notes.begin(), manualPattern.notes.end(),
                        [&] (const aidrum::MidiNote& n)
                        {
                            return n.noteNumber == midiNote
                                && std::abs (n.startBeat - startBeat) < stepBeats * 0.45;
                        }),
        manualPattern.notes.end());
}

void AIDrumAudioProcessor::clearManualPattern()
{
    std::lock_guard<std::mutex> lock (manualMutex);
    manualPattern.notes.clear();
}

aidrum::MidiPattern AIDrumAudioProcessor::getManualPattern() const
{
    std::lock_guard<std::mutex> lock (manualMutex);
    return manualPattern;
}

int AIDrumAudioProcessor::getManualNumBars() const
{
    std::lock_guard<std::mutex> lock (manualMutex);
    return manualNumBars;
}

void AIDrumAudioProcessor::setManualNumBars (int bars)
{
    bars = juce::jlimit (1, 32, bars);
    std::lock_guard<std::mutex> lock (manualMutex);
    manualNumBars = bars;
    manualPattern.lengthInBeats = static_cast<double> (bars * 4);
    // Drop notes that no longer fit.
    const double maxBeat = manualPattern.lengthInBeats;
    manualPattern.notes.erase (
        std::remove_if (manualPattern.notes.begin(), manualPattern.notes.end(),
                        [&] (const aidrum::MidiNote& n) { return n.startBeat >= maxBeat; }),
        manualPattern.notes.end());
}

aidrum::MidiPattern AIDrumAudioProcessor::withActiveKitApplied (aidrum::MidiPattern p) const
{
    const auto kit = static_cast<aidrum::DrumKit> (
        (int) apvts.getRawParameterValue (kParamDrumKit)->load());
    const auto& prof = aidrum::drumKitProfile (kit);

    // GM drum constants (mirror AIBackend's internal ones).
    constexpr int kKickGM = 36, kSnareGM = 38, kSideStickGM = 37, kClapGM = 39;
    constexpr int kClosedHatGM = 42, kPedalHatGM = 44, kOpenHatGM = 46;
    constexpr int kRideGM = 51, kRideBellGM = 53, kCrashGM = 49, kCrashAltGM = 57, kChinaGM = 52;
    constexpr int kLowTomGM = 41, kMidTomGM = 45, kHighTomGM = 48;

    for (auto& n : p.notes)
    {
        if      (n.noteNumber == kKickGM)      n.noteNumber = prof.kick;
        else if (n.noteNumber == kSnareGM)
            n.noteNumber = (n.velocity <= prof.ghostThreshold && prof.ghostSnare != prof.snare)
                             ? prof.ghostSnare : prof.snare;
        else if (n.noteNumber == kSideStickGM) n.noteNumber = prof.sideStick;
        else if (n.noteNumber == kClapGM)      n.noteNumber = prof.clap;
        else if (n.noteNumber == kClosedHatGM) n.noteNumber = prof.closedHat;
        else if (n.noteNumber == kPedalHatGM)  n.noteNumber = prof.pedalHat;
        else if (n.noteNumber == kOpenHatGM)   n.noteNumber = prof.openHat;
        else if (n.noteNumber == kRideGM)      n.noteNumber = prof.ride;
        else if (n.noteNumber == kRideBellGM)  n.noteNumber = prof.rideBell;
        else if (n.noteNumber == kCrashGM)     n.noteNumber = prof.crash;
        else if (n.noteNumber == kCrashAltGM)  n.noteNumber = prof.crashAlt;
        else if (n.noteNumber == kChinaGM)     n.noteNumber = prof.china;
        else if (n.noteNumber == kLowTomGM)    n.noteNumber = prof.lowTom;
        else if (n.noteNumber == kMidTomGM)    n.noteNumber = prof.midTom;
        else if (n.noteNumber == kHighTomGM)   n.noteNumber = prof.highTom;

        n.velocity = juce::jlimit (0.01f, 1.0f, n.velocity * prof.velocityScale);
    }
    return p;
}

void AIDrumAudioProcessor::commitManualPatternAsRegion()
{
    aidrum::MidiPattern remapped;
    {
        std::lock_guard<std::mutex> lock (manualMutex);
        remapped = manualPattern;
    }
    remapped = withActiveKitApplied (std::move (remapped));

    std::lock_guard<std::mutex> lock (arrangementMutex);
    arrangement.push_back (std::move (remapped));
}

bool AIDrumAudioProcessor::writeArrangementAsMidiFile (const juce::File& dest) const
{
    std::vector<aidrum::MidiPattern> snapshot;

    if (manualModeActive.load (std::memory_order_acquire))
    {
        // Manual mode: export just the manual pattern, with active kit remap.
        aidrum::MidiPattern manual;
        {
            std::lock_guard<std::mutex> lock (manualMutex);
            manual = manualPattern;
        }
        snapshot.push_back (withActiveKitApplied (std::move (manual)));
    }
    else
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        snapshot = arrangement;
    }

    double total = 0.0;
    for (const auto& p : snapshot)
        total += std::max (0.0, p.lengthInBeats);

    if (snapshot.empty() || total <= 0.0)
        return false;

    constexpr short kPPQ = 960;

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (500000)); // 120 BPM

    // v1.6.1-rc.7 — apply the INTENSITY-driven velocity shaper at MIDI
    // export time so a saved/dragged-to-DAW MIDI clip carries the same
    // human-feel velocities the user heard in the plugin.
    const float intensity01 = apvts.getRawParameterValue (kParamIntensity)->load();
    juce::Random shapeRng;

    double regionOffset = 0.0;
    for (const auto& region : snapshot)
    {
        for (const auto& note : region.notes)
        {
            if (! isAllowedDrumNote (note.noteNumber))
                continue;
            const double onTicks  = (regionOffset + note.startBeat) * kPPQ;
            const double offTicks = (regionOffset + note.startBeat
                                     + std::max (0.01, note.lengthBeat)) * kPPQ;
            const auto vel = shapeVelocity (note.noteNumber, note.velocity,
                                            intensity01, shapeRng,
                                            ghostMask.load());

            auto on  = juce::MidiMessage::noteOn  (10, note.noteNumber, vel);
            auto off = juce::MidiMessage::noteOff (10, note.noteNumber);
            on .setTimeStamp (onTicks);
            off.setTimeStamp (offTicks);
            seq.addEvent (on);
            seq.addEvent (off);
        }
        regionOffset += std::max (0.0, region.lengthInBeats);
    }

    auto eot = juce::MidiMessage::endOfTrack();
    eot.setTimeStamp (regionOffset * kPPQ);
    seq.addEvent (eot);

    seq.updateMatchedPairs();
    seq.sort();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kPPQ);
    file.addTrack (seq);

    dest.deleteFile();
    if (auto stream = dest.createOutputStream())
    {
        file.writeTo (*stream);
        stream->flush();
        return true;
    }
    return false;
}

void AIDrumAudioProcessor::renderArrangementToMidiBuffer (juce::MidiBuffer& midiOut,
                                                          int               numSamples,
                                                          double            sampleRate,
                                                          double            bpm,
                                                          bool              hostDrivesPlayhead)
{
    std::vector<aidrum::MidiPattern> snapshot;

    if (manualModeActive.load (std::memory_order_acquire))
    {
        aidrum::MidiPattern manual;
        {
            std::lock_guard<std::mutex> lock (manualMutex);
            manual = manualPattern;
        }
        snapshot.push_back (withActiveKitApplied (std::move (manual)));
    }
    else
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        snapshot = arrangement;
    }

    if (snapshot.empty())
        return;

    double total = 0.0;
    for (const auto& p : snapshot)
        total += std::max (0.001, p.lengthInBeats);
    if (total <= 0.0)
        return;

    // v1.6.1-rc.4 — arrangement time scale (HALF / NORMAL / DOUBLE). The
    // playhead advances `scale ×` faster through the arrangement so the
    // full composition audibly halves or doubles in speed without the
    // user changing the host BPM.
    //
    // v1.6.1-rc.5 — when the DAW host drives the playhead via PPQ, the
    // host already owns the musical timeline; overlaying timeScale on top
    // makes consecutive emission windows overlap (DOUBLE: notes fire
    // twice in the overlap) or gap (HALF: notes never emit). Force
    // timeScale = 1.0 in host-driven mode so timeScale only affects the
    // internal standalone transport.
    const double timeScale = hostDrivesPlayhead
        ? 1.0
        : timeScaleFactorForChoice (
            (int) apvts.getRawParameterValue (kParamTimeScale)->load());
    const double secondsPerBeat = 60.0 / std::max (1.0, bpm);
    const double blockBeats     = (static_cast<double> (numSamples) / sampleRate)
                                / secondsPerBeat * timeScale;

    const double blockStartBeat = playheadBeats.load (std::memory_order_acquire);
    const double blockEndBeat   = blockStartBeat + blockBeats;

    // v1.6.1-rc.7 — INTENSITY-driven velocity shaping (per emit). Each
    // call to shapeVelocity rolls fresh jitter so the kit never plays
    // two identical hits in a row.
    const float intensity01 = apvts.getRawParameterValue (kParamIntensity)->load();
    juce::Random shapeRng;

    // v1.6.1-rc.3 — NO PLUGIN-SIDE LOOPING EVER. The user was explicit:
    // "DO NOT LOOP … play each region in the arrangement until the end".
    // Regardless of whether there is one region or ten, playback walks
    // the full arrangement end-to-end exactly once, then auto-stops and
    // rewinds so the next Play press starts from bar 1. If the user
    // wants a loop they should use the DAW's transport loop, not a
    // plugin-side one.
    auto emitNotesInWindow = [&] (double winStart, double winEnd)
    {
        double regionOffset = 0.0;
        for (const auto& region : snapshot)
        {
            const double regionLen = std::max (0.001, region.lengthInBeats);
            for (const auto& note : region.notes)
            {
                if (! isAllowedDrumNote (note.noteNumber))
                    continue;
                const double onBeat  = regionOffset + note.startBeat;
                const double offBeat = onBeat + std::max (0.01, note.lengthBeat);

                if (onBeat >= winStart && onBeat < winEnd)
                {
                    const int sample = static_cast<int> (
                        (onBeat - blockStartBeat) * secondsPerBeat * sampleRate / timeScale);
                    // v1.6.1-rc.7 — every emitted note gets fresh per-hit
                    // velocity jitter from the INTENSITY curve so the kit
                    // feels like real human strikes (no two snare hits at
                    // 60% intensity ever land on exactly the same number).
                    midiOut.addEvent (
                        juce::MidiMessage::noteOn (10, note.noteNumber,
                            shapeVelocity (note.noteNumber, note.velocity,
                                           intensity01, shapeRng,
                                           ghostMask.load())),
                        juce::jlimit (0, numSamples - 1, sample));
                }

                if (offBeat >= winStart && offBeat < winEnd)
                {
                    const int sample = static_cast<int> (
                        (offBeat - blockStartBeat) * secondsPerBeat * sampleRate / timeScale);
                    midiOut.addEvent (juce::MidiMessage::noteOff (10, note.noteNumber),
                                      juce::jlimit (0, numSamples - 1, sample));
                }
            }
            regionOffset += regionLen;
        }
    };

    const double emissionEnd = std::min (blockEndBeat, total);
    emitNotesInWindow (blockStartBeat, emissionEnd);

    const double next = std::min (blockEndBeat, total);
    playheadBeats.store (next, std::memory_order_release);
    if (next >= total - 1.0e-6)
    {
        // End of arrangement: stop and rewind. No wrap, no loop.
        transportState.store ((int) TransportState::Stopped,
                              std::memory_order_release);
        playheadBeats.store (0.0, std::memory_order_release);
    }
}

void AIDrumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&         midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Don't let incoming MIDI leak into our generated pattern output.
    midi.clear();

    // Try to pull tempo + host transport state.
    double bpm = lastBpm.load (std::memory_order_relaxed);
    bool   hostIsPlaying = false;
    bool   hostDrivesPlayhead = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition(); pos.hasValue())
        {
            if (auto hostBpm = pos->getBpm(); hostBpm.hasValue())
                bpm = *hostBpm;

            hostIsPlaying = pos->getIsPlaying();

            // Only let the host own the transport when it exposes a musical
            // position. Standalone commonly has a playhead object too, but no
            // meaningful PPQ timeline; in that case we keep using our internal
            // clock and the UI transport buttons.
            if (auto ppq = pos->getPpqPosition(); ppq.hasValue())
            {
                hostDrivesPlayhead = true;

                if (hostIsPlaying)
                {
                    // v1.6.0 — no internal looping. Clamp host ppq to arrangement
                    // length so we play once through and then emit silence.
                    const double total = getArrangementTotalBeats();
                    if (total > 0.0)
                        playheadBeats.store (std::min (std::max (0.0, *ppq), total),
                                             std::memory_order_release);
                }
            }
        }
    }

    hostTransportSeen.store (hostDrivesPlayhead, std::memory_order_relaxed);
    lastBpm.store (bpm, std::memory_order_relaxed);

    // v1.3.0 Apply the ROOM preset + ROOM AMT knob to the master reverb
    // so every block stays in sync with whatever the user has dialed in.
    {
        const int roomIdx = (int) apvts.getRawParameterValue (kParamRoom)->load();
        const float amt   = apvts.getRawParameterValue (kParamRoomAmount)->load();
        const auto preset = aidrum::roomPresetFor (roomIdx);
        auto& m = busMixer.master_ref();
        m.reverbSize.store (preset.size, std::memory_order_relaxed);
        m.reverbDamp.store (preset.damp, std::memory_order_relaxed);
        m.reverbMix .store (preset.mix * juce::jlimit (0.0f, 1.0f, amt),
                            std::memory_order_relaxed);
    }

    // Decide whether to advance / emit this block.
    const auto state = (TransportState) transportState.load (std::memory_order_acquire);
    const bool shouldPlay = hostDrivesPlayhead ? hostIsPlaying
                                               : (state == TransportState::Playing);

    if (! shouldPlay)
    {
        // Silent block. Still render existing tails through the mixer so
        // held voices decay naturally when the user hits pause.
        busMixer.beginBlock (buffer.getNumSamples());
        if (sampleKit.isActive())
            sampleKit.renderIntoBuses (busMixer, buffer.getNumSamples());
        else
            drumSynth.renderIntoBuses (busMixer, buffer.getNumSamples());
        busMixer.process (buffer);
        return;
    }

    renderArrangementToMidiBuffer (midi,
                                   buffer.getNumSamples(),
                                   getSampleRate(),
                                   bpm,
                                   hostDrivesPlayhead);

    // Feed the freshly-generated MIDI notes into either the sampler
    // (if a kit is loaded) or the physical-model synth (fallback).
    drumSynth.setMasterGain (outputLevel.load (std::memory_order_relaxed));
    const bool useSamples = sampleKit.isActive();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (! msg.isNoteOn()) continue;

        if (useSamples)
            sampleKit.noteOn (msg.getNoteNumber(),
                              msg.getFloatVelocity() * outputLevel.load (std::memory_order_relaxed),
                              meta.samplePosition);
        else
            drumSynth.noteOn (msg.getNoteNumber(),
                              msg.getFloatVelocity(),
                              meta.samplePosition);
    }

    // v1.1.0 — render each voice into its own bus, then mixer sums to output.
    busMixer.beginBlock (buffer.getNumSamples());
    if (useSamples)
        sampleKit.renderIntoBuses (busMixer, buffer.getNumSamples());
    else
        drumSynth.renderIntoBuses (busMixer, buffer.getNumSamples());
    busMixer.process (buffer);
}

// ============================================================================
// v1.0.0 — Transport API
// ============================================================================
void AIDrumAudioProcessor::play()
{
    transportState.store ((int) TransportState::Playing, std::memory_order_release);
}

void AIDrumAudioProcessor::pause()
{
    // Freeze the playhead where it is; voices decay naturally.
    transportState.store ((int) TransportState::Paused, std::memory_order_release);
}

void AIDrumAudioProcessor::stop()
{
    transportState.store ((int) TransportState::Stopped, std::memory_order_release);
    playheadBeats.store (0.0, std::memory_order_release);
    drumSynth.reset();
}

AIDrumAudioProcessor::TransportState AIDrumAudioProcessor::getTransportState() const
{
    return (TransportState) transportState.load (std::memory_order_acquire);
}

void AIDrumAudioProcessor::setOutputLevel (float level01)
{
    outputLevel.store (juce::jlimit (0.0f, 1.0f, level01), std::memory_order_release);
}

float AIDrumAudioProcessor::getOutputLevel() const
{
    return outputLevel.load (std::memory_order_acquire);
}

juce::AudioProcessorEditor* AIDrumAudioProcessor::createEditor()
{
    return new AIDrumAudioProcessorEditor (*this);
}

void AIDrumAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("AIDrumState");
    root.appendChild (apvts.copyState(), nullptr);

    // v1.4.0 — persist loaded kit path + UI scale so the host snapshot
    // restores both when the project reopens.
    juce::ValueTree kit ("Kit");
    kit.setProperty ("path",  getSampleKitPath(), nullptr);
    kit.setProperty ("scale", (double) getUiScale(), nullptr);
    root.appendChild (kit, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void AIDrumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;

    auto root = juce::ValueTree::fromXml (*xml);

    // Back-compat: older saves put APVTS straight at the root.
    if (root.hasType ("state"))
    {
        apvts.replaceState (root);
        return;
    }

    if (auto child = root.getChildWithName ("state"); child.isValid())
        apvts.replaceState (child);

    if (auto kit = root.getChildWithName ("Kit"); kit.isValid())
    {
        setUiScale ((float) (double) kit.getProperty ("scale", 1.0));
        const auto path = kit.getProperty ("path", "").toString();
        if (path.isNotEmpty())
        {
            juce::File f (path);
            if (f.isDirectory()) loadSampleKit (f);
        }
    }
}

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIDrumAudioProcessor();
}
