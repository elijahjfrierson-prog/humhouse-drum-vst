#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StarterGrooves.generated.h"
#include "StarterGrooveKitFilter.h"

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

    const juce::StringArray kBundledKitChoices {
        "PopRock", "NuRock", "AltRock", "IndieLofi", "Thrash"
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

    // v1.4.0 — auto-load the bundled PopRock kit so the plugin makes
    // real-sample sound out of the box. User can override with LOAD KIT
    // or the KIT combo (v1.5.0: 5 bundled kits).
    sampleKit.prepare (48000.0, 0);
    const int bundled = sampleKit.loadBundled ("PopRock");
    if (bundled > 0)
    {
        std::lock_guard<std::mutex> lock (loadedKitPathMutex);
        loadedKitPath = "Built-in PopRock";
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
    // transport params.
    if (id == kParamRoom || id == kParamRoomAmount || id == kParamStepDiv)
        return;

    regenerateCurrentRegion();
}

void AIDrumAudioProcessor::regenerateCurrentRegion()
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (arrangement.empty())
        return;

    auto req = buildRequestForMode (aidrum::GenerationMode::Groove);
    req.phraseBar = static_cast<int> (arrangement.size()) - 1;

    // Keep the existing fill/groove slot — a user who generated a fill
    // shouldn't have a knob drag turn it back into a groove.
    const auto existingLen = arrangement.back().lengthInBeats;
    if (existingLen > 0.0) req.lengthInBeats = existingLen;

    arrangement.back() = backend.generate (req);
}

APVTS::ParameterLayout AIDrumAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamVariation, 1 }, "Variation",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamComplexity, 1 }, "Complexity",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamVelocity, 1 }, "Velocity",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.9f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamHumanize, 1 }, "Humanize",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f));

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
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamFillsProb, 1 }, "Fills",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

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
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f));

    // v1.5.0 — Bundled kit dropdown (5 CC0 kits, each with a distinct
    // snare/kick character arc). Replaces the old 20-kit voicing combo
    // for the user-facing "which drums do I want" choice.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamBundledKit, 1 }, "Bundled Kit",
        kBundledKitChoices, 0)); // default: PopRock

    // v1.5.0 — Fill complexity knob, independent of overall complexity.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamFillComplexity, 1 }, "Fill Complexity",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f));

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
        std::lock_guard<std::mutex> lock (loadedKitPathMutex);
        loadedKitPath = folder.getFullPathName();
    }
    return n;
}

void AIDrumAudioProcessor::unloadSampleKit()
{
    sampleKit.unload();
    std::lock_guard<std::mutex> lock (loadedKitPathMutex);
    loadedKitPath.clear();
}

int AIDrumAudioProcessor::loadBundledKit (const juce::String& kitName)
{
    const int n = sampleKit.loadBundled (kitName);
    if (n > 0)
    {
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
    uiScale.store (juce::jlimit (0.75f, 1.5f, s), std::memory_order_relaxed);
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
    req.fillComplexity = apvts.getRawParameterValue (kParamFillComplexity)->load();

    // v1.6.0 — which of the 5 bundled character kits is loaded drives the
    // kick/snare placement profile (PopRock = straight, NuRock = syncopated,
    // AltRock = laid-back, IndieLofi = half-time, Thrash = double-kick drive).
    const int bundledIndex = (int) apvts.getRawParameterValue (kParamBundledKit)->load();
    if (bundledIndex >= 0 && bundledIndex < (int) aidrum::BundledKit::Count)
        req.bundledKit = static_cast<aidrum::BundledKit> (bundledIndex);
    return req;
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
    return static_cast<int> (aidrum::starterGrooveLibrary().size());
}

juce::String AIDrumAudioProcessor::starterGrooveName (int index) const
{
    const auto& lib = aidrum::starterGrooveLibrary();
    if (index < 0 || index >= static_cast<int> (lib.size()))
        return {};
    return juce::String (std::string (lib[(size_t) index].name));
}

void AIDrumAudioProcessor::appendStarterGroove (int index)
{
    const auto& lib = aidrum::starterGrooveLibrary();
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
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (bucket.empty())
        return;
    std::mt19937_64 rng (static_cast<std::uint64_t> (std::random_device{}()));
    std::uniform_int_distribution<size_t> pick (0, bucket.size() - 1);
    appendStarterGroove (bucket[pick (rng)]);
}

void AIDrumAudioProcessor::remapLastRegionToKit (int kitIndex)
{
    const auto& bucket = aidrum::starterIndicesForKit (kitIndex);
    if (bucket.empty())
        return;

    std::mt19937_64 rng (static_cast<std::uint64_t> (std::random_device{}()));
    std::uniform_int_distribution<size_t> pick (0, bucket.size() - 1);
    const auto& lib = aidrum::starterGrooveLibrary();
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
            const auto vel = static_cast<juce::uint8> (
                juce::jlimit (1.0f, 127.0f, note.velocity * 127.0f));

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
                    midiOut.addEvent (
                        juce::MidiMessage::noteOn (10, note.noteNumber,
                            static_cast<juce::uint8> (
                                juce::jlimit (1.0f, 127.0f, note.velocity * 127.0f))),
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
