#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StarterGrooves.generated.h"
#include "StarterGrooveKitFilter.h"
#include "FillLibrary.generated.h"
#include "ProceduralFills.h"

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

    // v1.6.1-rc.14 — Fill Density knob. SEPARATE axis from "Fill Selector"
    // (kParamFillComplexity, which picks WHICH archetype to play) and
    // separate from the per-region INTENSITY which scales velocity.
    // Density controls HOW MANY MIDI notes are packed into the fill:
    //   0.00 → archetype baseline (sparse 8th rolls / quarter-note hits)
    //   0.55 → +16th / +32nd subdivision overlays kick in
    //   1.00 → 64th-spray, doubled snares, packed tom cascades
    // Consumed by aidrum::fillgen::generate() at splice time.
    constexpr const char* kParamFillDensity     = "fillDensity";

    // v1.6.1-rc.4 — arrangement playback time scale (½× / 1× / 2×).
    // Affects the rate the playhead advances through the arrangement so
    // the user can audition HALF TIME / NORMAL / DOUBLE TIME without
    // changing the host BPM.
    constexpr const char* kParamTimeScale       = "timeScale";

    // v1.6.1-rc.7 — intensity knob (0..1 stored; 0..127 displayed).
    // Drives the base velocity + per-hit fluctuation curve applied at
    // MIDI emit time. See shapeVelocity() below.
    constexpr const char* kParamIntensity       = "intensity";
    // v1.6.1-rc.19 — TRAP MODE toggle. When true:
    //   * arrangement strip lane labels swap
    //     (L Crash → SYNTH, R Crash → PAD, Ride → PHRASE,
    //      Small/Floor Tom → PERC)
    //   * the bundled kit auto-switches to "Drocetti" so the same MIDI
    //     notes already trigger trap-flavoured one-shots
    //   * the sample picker tree shows trap-style sub-categories first
    constexpr const char* kParamTrapMode        = "trapMode";
    // v1.6.1-rc.19 — per-lane SAMPLE PICKER override. 8 ints, one per
    // arrangement lane (top→bottom: R CRASH/PAD, L CRASH/SYNTH,
    // RIDE/PHRASE, HI-HAT, SMALL TOM/PERC, FLOOR TOM/PERC, SNARE, KICK).
    // Value layout: 0 = "auto" (pick by velocity within active kit's
    // slot, current behaviour); 1..N = pin layer N-1 in the active
    // kit's slot for that lane. Lets the user say "I want pad #5 on R
    // CRASH, snare #3 on the snare lane" without leaving the
    // arrangement strip. The IDs are kept stable across rc.19+ so
    // host-saved choices round-trip.
    constexpr const char* kParamLaneSampleRCrash    = "laneSampRCrash";
    constexpr const char* kParamLaneSampleLCrash    = "laneSampLCrash";
    constexpr const char* kParamLaneSampleRide      = "laneSampRide";
    constexpr const char* kParamLaneSampleHat       = "laneSampHat";
    constexpr const char* kParamLaneSampleSmallTom  = "laneSampSmallTom";
    constexpr const char* kParamLaneSampleFloorTom  = "laneSampFloorTom";
    constexpr const char* kParamLaneSampleSnare     = "laneSampSnare";
    constexpr const char* kParamLaneSampleKick      = "laneSampKick";

    // v1.6.1-rc.12 — single bundled kit again. The (Bay Grunge) Yamaha
    // Maple kit was pulled in rc.12 (user: "take out the second drum
    // kit it is a liability and not routed correctly all together i do
    // not want to see it in the new update"). The plugin now ships one
    // crispy default kit — (Nu Rock) 70's Yamaha — and the LOAD KIT
    // path is how users bring in their own samples. The internal name
    // below is the WAV-bundle prefix scanned by SampleKit::loadBundled().
    // v1.6.1-rc.17 — second bundled kit. The "HeavyStudio" prefix points at
    // user-supplied recordings (Kick / Snare / Floor Tom / Small Tom ×2 /
    // L+R Crash / Ride) sliced into one-shots by tools/build_heavy_studio_kit.py
    // and aliased onto NuRockYamaha hat / china / ride-bell samples for the
    // voices we don't have separate Heavy recordings of. Distinct timbre vs.
    // NuRockYamaha: heavier kick attack, thicker snare body, longer cymbal
    // sustain. ComboBox order MUST match SampleKit::loadBundled() recognition.
    // v1.6.1-rc.19 — third bundled kit: "Drocetti" (originally Drocetti — renamed 2025-04-20;
    // user requested rename 2025-04-20). User-original trap pack: 120
    // normalised one-shots covering kicks, snares, claps, closed/open
    // hats, perc, toms, crashes, ride PLUS new trap-only sub-categories
    // — synth, pad, phrase, 808, bass, vox, fx — that get routed onto
    // existing voice slots (synth → Crash, pad → China, phrase → Ride,
    // perc → MidTom, 808 → Kick, bass → SideStick, vox → RideBell,
    // fx → China). Selecting "Drocetti" + flipping TRAP MODE on relabels
    // the arrangement strip lanes (L Crash → Synth, R Crash → Pad,
    // Ride → Phrase, Toms → Perc) so the SAME MIDI now drives a
    // trap-flavoured palette without any new voice infrastructure.
    // v1.6.1-rc.26 — Heavy Studio dropped at user's request ("just the
    // trap kit and my nu rock kit"). Two bundled kits remain: NuRock
    // (default rock palette) + Drocetti (trap). Index 1 used to be
    // HeavyStudio; saved sessions pinned to that index now resolve to
    // Drocetti via the array order. SampleKit::loadBundled also accepts
    // "HeavyStudio" defensively and falls back to NuRockYamaha so any
    // host-saved-state with the old prefix still loads cleanly.
    const juce::StringArray kBundledKitChoices {
        "NuRockYamaha",
        "Drocetti"
    };
    const juce::StringArray kBundledKitDisplayNames {
        "(Nu Rock) 70's Yamaha",
        "(Drocetti) Trap Kit"
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

    // v1.6.1-rc.13 — added 4-bar / 8-bar / 16-bar options so COMPOSE can
    // produce full 8-bar loops by default. Index 7 ("8 bars") is the new
    // default — the user explicitly asked for "FULL 8 BAR PATTERNS WITH
    // EVERY COMPOSITION".
    // v1.6.1-rc.18 — default bumped to index 8 ("16 bars") per user spec:
    // "THE ARRANMGENT WILL NOW BE 16 BARS INSTEAD OF 8 TO FULL ADD A NEW
    // ONE". 8 bars still selectable from the dropdown.
    const juce::StringArray kPatternLengthChoices {
        "1/16 note", "1/8 note", "1/4 note", "1/2 bar", "1 bar", "2 bars",
        "4 bars", "8 bars", "16 bars"
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
        case 6: return 16.0;  // 4 bars (v1.6.1-rc.13)
        case 7: return 32.0;  // 8 bars (v1.6.1-rc.13)
        case 8: return 64.0;  // 16 bars (v1.6.1-rc.18 — new default)
        default: return 64.0; // v1.6.1-rc.18: default to 16 bars (was 8 bars)
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

    // v1.6.1-rc.12 — non-zero per-bus reverb sends so the master reverb
    // tail carries audible signal at default mixer settings. Without
    // these the mixer's REV knobs felt "dead" because the master mix
    // (set by the Room Amount knob) had nothing to wet at default.
    // Cymbals lean wettest (they're what the ear identifies as "in a
    // room"), kick stays driest so the low end doesn't smear.
    busMixer.params_ref ((int) aidrum::Bus::Kick)     .reverbSend.store (0.05f);
    busMixer.params_ref ((int) aidrum::Bus::Snare)    .reverbSend.store (0.18f);
    busMixer.params_ref ((int) aidrum::Bus::ClosedHat).reverbSend.store (0.15f);
    busMixer.params_ref ((int) aidrum::Bus::OpenHat)  .reverbSend.store (0.20f);
    busMixer.params_ref ((int) aidrum::Bus::Ride)     .reverbSend.store (0.22f);
    busMixer.params_ref ((int) aidrum::Bus::Crash)    .reverbSend.store (0.25f);
    busMixer.params_ref ((int) aidrum::Bus::China)    .reverbSend.store (0.25f);
    busMixer.params_ref ((int) aidrum::Bus::Toms)     .reverbSend.store (0.18f);

    // v1.6.1-rc.12 — push the current Room preset values into the master
    // reverb once on construction so default sessions hear reverb
    // immediately. Subsequent Room/RoomAmount changes go through
    // parameterChanged → applyRoomPresetToMaster.
    applyRoomPresetToMaster();

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

    // v1.6.1-rc.19 — TRAP MODE flip: swap the bundled kit to "Drocetti"
    // when toggled ON, and back to the user-selected base kit when
    // toggled OFF. We don't need to regenerate the arrangement; the
    // ArrangementStrip + lane labels react to the same APVTS bool in
    // the editor, and the underlying MIDI notes are unchanged.
    if (id == kParamTrapMode)
    {
        const bool trap = apvts.getRawParameterValue (kParamTrapMode)->load() > 0.5f;
        if (trap)
        {
            loadBundledKit ("Drocetti");
        }
        else
        {
            const int kitIdx = (int) apvts.getRawParameterValue (kParamBundledKit)->load();
            if (kitIdx >= 0 && kitIdx < kBundledKitChoices.size())
                loadBundledKit (kBundledKitChoices[kitIdx]);
        }
        return;
    }

    // v1.6.1-rc.19 — per-lane SAMPLE PICKER override. Wire each of the
    // 8 lane params straight into SampleKit's atomic override array so
    // the next noteOn picks the pinned layer. No regen needed.
    {
        struct LaneIdMap { const char* id; int lane; };
        static const LaneIdMap kLaneMap[] = {
            { kParamLaneSampleRCrash,    0 },
            { kParamLaneSampleLCrash,    1 },
            { kParamLaneSampleRide,      2 },
            { kParamLaneSampleHat,       3 },
            { kParamLaneSampleSmallTom,  4 },
            { kParamLaneSampleFloorTom,  5 },
            { kParamLaneSampleSnare,     6 },
            { kParamLaneSampleKick,      7 },
        };
        for (const auto& m : kLaneMap)
        {
            if (id == m.id)
            {
                const int v = (int) apvts.getRawParameterValue (m.id)->load();
                sampleKit.setLaneOverride (m.lane, v);
                return;
            }
        }
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
    // v1.6.1-rc.12 — Room dropdown + Room Amount knob now push their
    // values into the master reverb engine ONCE on change (not every
    // audio block). The mixer panel's REV / DEPTH knobs are then the
    // live source of truth for the running reverb tail — dragging them
    // is audible immediately because they aren't being clobbered every
    // block by the room preset.
    if (id == kParamRoom || id == kParamRoomAmount)
    {
        applyRoomPresetToMaster();
        return;
    }

    // v1.6.1-rc.18 — Devin Review fix (4th pass): the FILL UI was
    // hidden in this RC (button + knob + dropdown all 0-sized) but
    // the underlying APVTS parameter `kParamFillsProb` is still
    // exposed for DAW automation. It is not consumed anywhere in the
    // current generation pipeline, so any automation event was
    // falling through to `regenerateCurrentRegion()` and destroying
    // the user's pattern. Same class of bug as `kParamFillComplexity`
    // above — skip regen.
    if (id == kParamStepDiv || id == kParamTimeScale
     || id == kParamIntensity
     || id == kParamFillComplexity
     || id == kParamFillDensity
     || id == kParamFillsProb)
        return;

    regenerateCurrentRegion();
}

void AIDrumAudioProcessor::applyRoomPresetToMaster()
{
    const int   roomIdx = (int) apvts.getRawParameterValue (kParamRoom)->load();
    const float amt     = juce::jlimit (0.0f, 1.0f,
                              apvts.getRawParameterValue (kParamRoomAmount)->load());
    const auto preset = aidrum::roomPresetFor (roomIdx);
    auto& m = busMixer.master_ref();
    m.reverbSize.store (preset.size, std::memory_order_relaxed);
    m.reverbDamp.store (preset.damp, std::memory_order_relaxed);
    m.reverbMix .store (preset.mix * amt, std::memory_order_relaxed);
}

void AIDrumAudioProcessor::regenerateCurrentRegion()
{
    // v1.6.1-rc.11 — Devin Review flagged this as a 🔴 priority-inversion
    // bug in rc.11: backend.generate() does substantial CPU work (RNG +
    // sort + applyDrummerPost + applyKit + finalize) and used to run
    // while the arrangement mutex was held. processBlock() also takes
    // that mutex (renderArrangementToMidiBuffer / getArrangementTotalBeats),
    // so a knob tweak on the message thread would block the audio
    // thread for the entire generation, dropping samples.
    //
    // Fix: take the mutex once to read the snapshot we need (length +
    // isFill + region count), drop it, run the expensive generate()
    // unlocked, then re-acquire briefly to swap the result in. We
    // re-validate `arrangement.back()` after re-acquiring because the
    // user could have appended/deleted regions while we were generating.

    bool   wasFill         = false;
    double existingLen     = 0.0;
    int    phraseBar       = 0;
    float  savedRegionInt  = -1.0f;  // v1.6.1-rc.14 — preserve per-region INTENSITY
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        if (arrangement.empty())
            return;
        wasFill        = arrangement.back().isFill;
        existingLen    = arrangement.back().lengthInBeats;
        phraseBar      = static_cast<int> (arrangement.size()) - 1;
        savedRegionInt = arrangement.back().regionIntensity;
    }

    // v1.6.1-rc.5 — preserve the region's fill/groove slot across live
    // regen. The old code always used Groove mode, silently converting
    // any Fill region into a Groove the moment the user touched a knob.
    auto req = buildRequestForMode (wasFill ? aidrum::GenerationMode::Fill
                                            : aidrum::GenerationMode::Groove);
    req.phraseBar = phraseBar;
    if (existingLen > 0.0) req.lengthInBeats = existingLen;

    auto regenerated = backend.generate (req);
    regenerated.isFill          = wasFill;
    // v1.6.1-rc.14 — restore the user's per-region INTENSITY override
    // (Devin Review caught this — any APVTS knob tweak silently reset
    // the override to the inherit-global sentinel).
    regenerated.regionIntensity = savedRegionInt;

    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        // Region count could have changed while we were unlocked; only
        // commit if there's still something to swap and the slot we
        // sized for is still the back of the arrangement.
        if (arrangement.empty())
            return;
        if (phraseBar != static_cast<int> (arrangement.size()) - 1)
            return; // user appended a new region in the meantime — drop our regen
        arrangement.back() = std::move (regenerated);
    }
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
        kPatternLengthChoices, 8)); // v1.6.1-rc.18 default: 16 bars (was 8 bars)

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
        // v1.6.1-rc.21 — step size driven by the procedural archetype
        // count (27 in rc.21) so the parameter quantisation matches the
        // FILL dropdown's index space exactly. fillLibrary().size() and
        // the procedural archetype count must remain aligned, otherwise
        // setFillIndex / getCurrentFillIndex / req.fillIndex compute
        // diverging indices for the same APVTS value.
        const int numFills = std::max (1, (int) aidrum::fillgen::kArchetypeCount);
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

    // v1.6.1-rc.14 — Fill Density. Default 0.50 = balanced 16th overlay.
    // 0.00 = sparse archetype baseline; 1.00 = 64th-saturated spray.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamFillDensity, 1 }, "Fill Density",
        juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f / 127.0f, kKnobSkew),
        0.50f));

    // v1.5.0 — Manual grid step division (Logic-style 1/16, 1/32, 1/64).
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamStepDiv, 1 }, "Step Div",
        kStepDivChoices, 0));

    // v1.6.1-rc.4 — arrangement playback time scale.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamTimeScale, 1 }, "Time Scale",
        kTimeScaleChoices, 1)); // default: NORMAL

    // v1.6.1-rc.19 — TRAP MODE bool. See declaration comment above.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kParamTrapMode, 1 }, "Trap Mode", false));

    // v1.6.1-rc.19 — 8 per-lane SAMPLE PICKER overrides. 0 = auto
    // (current behaviour: velocity-driven layer pick). 1..N pins
    // a specific layer index in that lane's slot. Range capped at 32
    // — generous enough for the bundled kits' largest slot
    // (Drocetti hat_closed = 10) plus headroom for future kits.
    {
        const juce::StringArray laneIds {
            kParamLaneSampleRCrash,   kParamLaneSampleLCrash,
            kParamLaneSampleRide,     kParamLaneSampleHat,
            kParamLaneSampleSmallTom, kParamLaneSampleFloorTom,
            kParamLaneSampleSnare,    kParamLaneSampleKick
        };
        const juce::StringArray laneNames {
            "Lane R-Crash Sample", "Lane L-Crash Sample",
            "Lane Ride Sample",    "Lane Hat Sample",
            "Lane Small-Tom Sample", "Lane Floor-Tom Sample",
            "Lane Snare Sample",   "Lane Kick Sample"
        };
        for (int i = 0; i < laneIds.size(); ++i)
        {
            params.push_back (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { laneIds[i].toRawUTF8(), 1 },
                laneNames[i], 0, 32, 0));
        }
    }

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
    // v1.6.1-rc.14 — procedural archetypes. fillLibrary() is kept for
    // state round-trip but the UI dropdown reads from the procedural
    // archetype name table.
    // v1.6.1-rc.21 — grew from 22 → 27 (5 tom-focused bases added).
    // This is THE single source of truth for the fill index space —
    // parameter step (createParameterLayout), getCurrentFillIndex,
    // setFillIndex, getAllFillNames, AIBackend::buildRequestForMode
    // and spliceMandatoryFillIntoRegion all key off this constant so
    // the FILL dropdown / parameter / engine stay in lock-step.
    return aidrum::fillgen::kArchetypeCount;
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
    const int idx = getCurrentFillIndex();
    return juce::String (aidrum::fillgen::archetypeName (idx));
}

void AIDrumAudioProcessor::cycleFillSelector (int direction)
{
    const int n = getFillLibrarySize();
    if (n <= 1) return;
    const int dir = (direction >= 0) ? 1 : -1;
    const int next = ((getCurrentFillIndex() + dir) % n + n) % n;
    setFillIndex (next);
}

// v1.6.1-rc.13 — direct fill-index setter for the dropdown UI. The
// dropdown shows every fill by name (22 originals + 5 tom bases in
// rc.21) and lets the user pick one directly instead of cycling
// through with prev/next buttons.
void AIDrumAudioProcessor::setFillIndex (int idx)
{
    const int n = getFillLibrarySize();
    if (n <= 1) return;
    const int clamped = juce::jlimit (0, n - 1, idx);
    if (auto* p = apvts.getParameter (kParamFillComplexity))
    {
        const float norm = (float) clamped / (float) (n - 1);
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    }
}

// v1.6.1-rc.14 — return procedural archetype names for the FILL
// dropdown. Order matches aidrum::fillgen::generate() so the
// dropdown index == the archetype index. Light → sludge complexity
// ramp.
juce::StringArray AIDrumAudioProcessor::getAllFillNames() const
{
    juce::StringArray names;
    const int n = getFillLibrarySize();
    names.ensureStorageAllocated (n);
    for (int i = 0; i < n; ++i)
        names.add (juce::String (aidrum::fillgen::archetypeName (i)));
    return names;
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
    // v1.6.1-rc.21 — use getFillLibrarySize() (the procedural archetype
    // count) as the index space, NOT fillLibrary().size(). The two used
    // to be the same; rc.21 added 5 procedural tom archetypes plus 5
    // matching fillLibrary entries, and we key the parameter step off
    // the procedural count. AIBackend::makeFill() already wraps with
    // `% lib.size()` so out-of-range indices fall back gracefully.
    const int numFills = getFillLibrarySize();
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
        if (n == 52 || n == 55 || n == 57)                         return 0; // china / R CRASH / splash
        // v1.6.1-rc.20-fix4 — non-drum MIDI pitches (chromatic notes
        // placed via the FL-style piano roll) used to fall through to
        // "return 0", which is the R CRASH / PAD lane. With the rc.20
        // chromatic bypass in place those notes now reach shapeVelocity,
        // and shapeVelocity reads ghostMask & (1 << lane). If the user
        // had ghosted R CRASH, every chromatic synth/pad/phrase note
        // got crushed to ghost velocity. Return -1 instead so the
        // `lane >= 0` guard in shapeVelocity skips ghost-masking on
        // non-drum notes entirely.
        return -1;
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
            // Snare / side-stick — v1.6.1-rc.18: widened from 0.50 to 1.65
            // so consecutive snares span ~14% of the velocity range
            // instead of ~2.5%. This is the fix for the user feedback
            // "those snare rolls sound like gunshots" — adjacent hits
            // were collapsing into a single perceived velocity, so the
            // ear couldn't separate them. Combined with the L/R stick
            // multipliers (×0.93 / ×1.04) and the 8 ms timing jitter,
            // every snare in a roll now lands at a distinct velocity.
            case 37: case 38: case 39: case 40:              return 1.65f;
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
        // v1.6.1-rc.18 — snare-like notes (37/38/39/40) get a much wider
        // SYMMETRIC jitter band (-22% to +12% of full range) on top of
        // the already-larger fluctuation factor so back-to-back snares
        // in a roll land at obviously-distinct velocities, eliminating
        // the "gunshot" fusion artefact. Other voices keep the original
        // tight, asymmetric ±5% feel.
        const bool isSnareLike = (noteNumber == 37 || noteNumber == 38
                               || noteNumber == 39 || noteNumber == 40);
        const float lowPct   = isSnareLike ? -22.0f : -4.0f * factor;
        const float highPct  = isSnareLike ?  12.0f :  1.0f * factor;
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

    // v1.6.1-rc.18 — REMOVED: phrase-cap auto-promotion to Fill, and
    // the fillsProb random promotion. The COMPOSE / (+) button used to
    // silently flip every 8th region (phraseBar % 8 == 7) into a
    // Fill-only region, which the user reports as "FIX THE REGION
    // COMPOSE BUTTON AND + BUTTON IT ADDS DILLS INSTEAD OF MT FULL
    // GROOVES". Fills are now embedded *inside* every groove via
    // spliceMandatoryFillIntoRegion(); the (+) button always appends
    // a full groove pattern, never a fill-only region.
    (void) requestedMode;

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

// v1.6.1-rc.14 — per-region INTENSITY accessors. The arrangement strip
// shows a small slider/strip per region tile that drives setRegionIntensity
// for that index. value < 0 (or NaN) restores the "inherit global" sentinel
// so a fresh region falls back to the global INTENSITY knob until the
// user dials this region in.
void AIDrumAudioProcessor::setRegionIntensity (int index, float intensity01)
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (index < 0 || index >= static_cast<int> (arrangement.size()))
        return;
    auto& region = arrangement[(size_t) index];
    region.regionIntensity = (intensity01 < 0.0f)
                                ? -1.0f
                                : juce::jlimit (0.0f, 1.0f, intensity01);
}

float AIDrumAudioProcessor::getRegionIntensity (int index) const
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (index < 0 || index >= static_cast<int> (arrangement.size()))
        return -1.0f;
    return arrangement[(size_t) index].regionIntensity;
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

    // v1.6.1-rc.23 — STARTER picker + Scripter-style COMPOSE cycler
    // used to push the raw 1- or 2-bar library pattern straight onto
    // the arrangement with no fill splice. Match the RANDOMIZE path:
    // expand to the user's target pattern length, balance crash/hat
    // against intensity, then splice the mandatory bar-8 fill so every
    // intelligence-pad / starter / cycler region gets a closing-bar
    // fill just like RANDOMIZE / + (mold-around) regions do.
    const double targetBeats = patternLengthBeatsFromChoice (
        (int) apvts.getRawParameterValue (kParamPatternLength)->load());
    const int targetBars = juce::jlimit (1, 64,
        (int) std::lround (targetBeats / 4.0));
    aidrum::MidiPattern pat = expandGrooveToTargetBars (
        lib[(size_t) index].pattern,
        static_cast<std::uint64_t> (index) ^ 0xA1D3ULL,
        targetBars);
    applyIntensityCrashHatBalance (pat,
                                   static_cast<std::uint64_t> (index) ^ 0xC0FEULL);
    spliceMandatoryFillIntoRegion (pat, index);

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
    // v1.6.1-rc.24 — intelligence pad / cycler now draws from SoCal-only
    // (the imported-with-fills pool) per user direction "intelligence
    // pad grooves only randomized grooves added" + "fills must always
    // be in the patterns". Falls back to the full bucket if SoCal is
    // somehow empty in a future build.
    const auto& socal  = aidrum::socalIndicesForKit (kitIndex);
    const auto& bucket = ! socal.empty() ? socal
                                         : aidrum::starterIndicesForKit (kitIndex);
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
    // v1.6.1-rc.24 — COMPOSE / mold-around now draws from the SoCal-only
    // (imported-with-fills) pool to match the cycler + RANDOMIZE pads.
    // User direction: "intelligence pad grooves only randomized grooves
    // added — those imports have fills baked in".
    const auto& socal  = aidrum::socalIndicesForKit (kitIndex);
    const auto& bucket = ! socal.empty() ? socal
                                         : aidrum::starterIndicesForKit (kitIndex);
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

    // v1.6.1-rc.11 — Devin Review 🔴: this used to take arrangementMutex
    // and hold it across applyIntensityCrashHatBalance + the deduping
    // overlay loop + spliceMandatoryFillIntoRegion (std::sort,
    // std::remove_if, RNG, APVTS reads). processBlock() takes the same
    // mutex via getArrangementTotalBeats / renderArrangementToMidiBuffer,
    // so a single COMPOSE click could block the audio thread for
    // hundreds of microseconds and drop samples. Same priority-inversion
    // pattern the reviewer flagged on regenerateCurrentRegion in rc.11
    // — apply the same fix: snapshot under a brief lock, do the heavy
    // work on a local copy, re-acquire briefly to swap.

    aidrum::MidiPattern result;
    bool                hadExisting = false;
    int                 phraseBar   = 0;
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        hadExisting = ! arrangement.empty();
        phraseBar   = static_cast<int> (arrangement.size()) - (hadExisting ? 1 : 0);
        if (hadExisting)
            result = arrangement.back();
    }

    if (! hadExisting)
    {
        // v1.6.1-rc.13 — first click: tile the chosen groove out to a
        // full region with the closing fill on the last bar, instead
        // of a 1- or 2-bar nub.
        // v1.6.1-rc.18 — Devin Review fix: read the APVTS pattern-
        // length choice (default is 16 bars) so COMPOSE doesn't shrink
        // a 16-bar default back to 8. The choice index drives both the
        // arrangement region length and the expansion target.
        const double targetBeats = patternLengthBeatsFromChoice (
            (int) apvts.getRawParameterValue (kParamPatternLength)->load());
        const int targetBars = juce::jlimit (1, 64,
            (int) std::lround (targetBeats / 4.0));
        result = expandGrooveToTargetBars (lib[(size_t) libIdx].pattern,
                                           static_cast<std::uint64_t> (counter) ^ 0xA110ULL,
                                           targetBars);
        applyIntensityCrashHatBalance (result,
                                       static_cast<std::uint64_t> (counter) ^ 0x5151ULL);
        spliceMandatoryFillIntoRegion (result, (int) counter);

        std::lock_guard<std::mutex> lock (arrangementMutex);
        arrangement.push_back (std::move (result));
        return;
    }

    const aidrum::MidiPattern& src = lib[(size_t) libIdx].pattern;

    // Build a fast (note, quantised-beat) set of what the user already
    // has so we never double-up a kick/snare on the same downbeat.
    auto quant = [] (double b) { return (int) std::llround (b * 8.0); }; // 1/32 grid
    std::set<std::pair<int,int>> existing;
    for (const auto& n : result.notes)
        existing.insert ({ n.noteNumber, quant (n.startBeat) });

    // Phase-align src to result by wrapping its beat positions modulo
    // result.lengthInBeats so an 8-bar src groove decorates a 4-bar
    // user region cleanly.
    const double targetLen = std::max (0.5, result.lengthInBeats);
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
        result.notes.push_back (dec);
        existing.insert (key);
    }

    // Keep notes sorted by start time so any consumer that assumes
    // monotonic order (e.g. older arrangement-strip render code)
    // doesn't get confused.
    std::sort (result.notes.begin(), result.notes.end(),
               [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
               { return a.startBeat < b.startBeat; });

    applyIntensityCrashHatBalance (result,
                                   static_cast<std::uint64_t> (counter) ^ 0xC0DEULL);
    spliceMandatoryFillIntoRegion (result, (int) counter);

    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        // User could have appended/deleted regions while we were working
        // unlocked; only commit if our snapshot is still the back of the
        // arrangement.
        if (arrangement.empty())
            return;
        if (phraseBar != static_cast<int> (arrangement.size()) - 1)
            return;
        arrangement.back() = std::move (result);
    }
}

// v1.6.1-rc.9 — RANDOMIZE: full pattern replace, the original COMPOSE
// behavior. Picks a fresh groove and either replaces the last region
// or appends one if the arrangement is empty.
void AIDrumAudioProcessor::randomizePatternForKit (int kitIndex)
{
    // v1.6.1-rc.24 — RANDOMIZE pulls from SoCal-only (imported-with-
    // fills) per user direction. The COMPOSE / cycler / RANDOMIZE pads
    // now share one pool, every region landed has a fill present
    // (either baked-in or spliced via spliceMandatoryFillIntoRegion).
    const auto& socal  = aidrum::socalIndicesForKit (kitIndex);
    const auto& bucket = ! socal.empty() ? socal
                                         : aidrum::starterIndicesForKit (kitIndex);
    if (bucket.empty())
        return;

    std::mt19937_64 rng (static_cast<std::uint64_t> (std::random_device{}()));
    std::uniform_int_distribution<size_t> pick (0, bucket.size() - 1);
    const auto& lib = aidrum::allStarterGrooves();
    const int libIdx = bucket[pick (rng)];
    if (libIdx < 0 || libIdx >= static_cast<int> (lib.size()))
        return;

    // v1.6.1-rc.11 — Devin Review 🔴: same priority-inversion pattern as
    // composeMoldAroundForKit / regenerateCurrentRegion. Build the
    // randomised region on a local copy (the heavy work — crash/hat
    // balance + fill splice — runs unlocked), then take arrangementMutex
    // briefly to swap the result in. Audio thread no longer waits on
    // RANDOMIZE clicks.
    // v1.6.1-rc.13 — RANDOMIZE always builds a full 8-bar region too,
    // tiling shorter library grooves out to 32 beats with smart subtle
    // variations (ghost-snare drags, hat ostinato breathing, kick
    // syncopation, tom drops on phrase ends). Every Spotify-corpus and
    // SoCal-centerstone groove is now in the random pool — the
    // Intelligence pad rolls from the FULL allStarterGrooves() library
    // (SoCal + analyzer + BFD palettes), not the small subset.
    // v1.6.1-rc.14 — preserve the user's per-region INTENSITY override
    // across RANDOMIZE so re-rolling a groove doesn't silently drop the
    // velocity vibe they set on the region's drag-strip.
    float savedRegionInt = -1.0f;
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        if (! arrangement.empty())
            savedRegionInt = arrangement.back().regionIntensity;
    }

    // v1.6.1-rc.18 — Devin Review fix: RANDOMIZE was hard-coded to
    // 8 bars and shrinking the new 16-bar default. Read the APVTS
    // pattern-length choice so RANDOMIZE preserves region length.
    const double targetBeats = patternLengthBeatsFromChoice (
        (int) apvts.getRawParameterValue (kParamPatternLength)->load());
    const int targetBars = juce::jlimit (1, 64,
        (int) std::lround (targetBeats / 4.0));
    aidrum::MidiPattern result = expandGrooveToTargetBars (
        lib[(size_t) libIdx].pattern,
        static_cast<std::uint64_t> (libIdx) ^ 0xC0FFEEULL,
        targetBars);
    applyIntensityCrashHatBalance (result,
                                   static_cast<std::uint64_t> (libIdx) ^ 0xBEEFULL);
    spliceMandatoryFillIntoRegion (result, libIdx);
    result.regionIntensity = savedRegionInt;

    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (arrangement.empty())
        arrangement.push_back (std::move (result));
    else
        arrangement.back() = std::move (result);
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

// v1.6.1-rc.13 — Smart 8-bar expansion. Takes a (typically 1- or 2-bar)
// starter groove, tiles it out to 32 beats, then sprinkles corpus-
// informed micro-flourishes that read as a real drummer breathing
// through an 8-bar phrase rather than an 8x repeat:
//   * Bars 1-7 carry the tiled groove, lightly humanised in velocity.
//   * Ghost-snare drags between backbeats on bars 2/4/6 (32nd-note
//     0.875 / 2.875 etc., velocity ~0.30..0.45).
//   * Hat ostinato breathes — every other bar replaces a couple of
//     16th hats with 32nd doubles for natural feel.
//   * Kick syncopation on the "e" / "and" (16th off-beats) sprinkled
//     across bars 3/5/7 so the pocket isn't stiff.
//   * Tom drops at the end of bar 4 (mid-phrase punctuation) and bar
//     7 (lead-in to the closing fill on bar 8).
// The closing bar (bar 8) is left untouched here — spliceMandatory-
// FillIntoRegion writes the actual fill there.
aidrum::MidiPattern AIDrumAudioProcessor::expandGrooveToTargetBars (
    const aidrum::MidiPattern& src, std::uint64_t seed, int targetBars) const
{
    constexpr double kBeatsPerBar = 4.0;
    // v1.6.1-rc.18 — Devin Review fix: was hard-coded to 8 bars. The
    // APVTS default is now 16 bars, so we honour whatever the caller
    // passes (COMPOSE / RANDOMIZE both read kParamPatternLength now).
    const int    barsTarget = juce::jlimit (1, 64, targetBars);
    const double kTarget    = (double) barsTarget * kBeatsPerBar;

    aidrum::MidiPattern out;
    out.lengthInBeats = kTarget;

    const double srcLen = (src.lengthInBeats > 1e-6 ? src.lengthInBeats : kBeatsPerBar);

    // Tile the source groove out to the target length (32 beats for an
    // 8-bar region, 64 beats for the new 16-bar default, etc.).
    for (double tileOffset = 0.0; tileOffset < kTarget - 1e-6; tileOffset += srcLen)
    {
        for (const auto& n : src.notes)
        {
            const double absBeat = tileOffset + n.startBeat;
            if (absBeat >= kTarget - 1e-6) break;
            aidrum::MidiNote shifted = n;
            shifted.startBeat = absBeat;
            out.notes.push_back (shifted);
        }
    }

    std::mt19937_64 rng (seed ^ 0x6A09E667F3BCC908ULL);
    auto roll = [&] { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); };

    constexpr int kKick     = 36;
    constexpr int kSnare    = 38;
    constexpr int kClosedHat= 42;
    constexpr int kHighTom  = 48;
    constexpr int kLowTom   = 43;
    constexpr int kFloorTom = 41;

    auto add = [&] (int n, double startBeat, float vel)
    {
        if (startBeat < 0.0 || startBeat >= kTarget - 1e-6) return;
        aidrum::MidiNote m;
        m.noteNumber = n;
        m.startBeat  = startBeat;
        m.lengthBeat = 0.18;
        m.velocity   = juce::jlimit (0.0f, 1.0f, vel);
        out.notes.push_back (m);
    };

    auto hasNoteNear = [&] (int n, double beat, double tol = 0.05) -> bool
    {
        for (const auto& x : out.notes)
            if (x.noteNumber == n && std::abs (x.startBeat - beat) < tol)
                return true;
        return false;
    };

    // v1.6.1-rc.18 — flourish loops now scale with `barsTarget` so
    // a 16-bar region gets a properly populated 16-bar bed (was
    // collapsing back to 8). The closing bar (`barsTarget - 1`,
    // 0-indexed) is always left to spliceMandatoryFillIntoRegion.
    const int kLastBar = barsTarget - 1;

    // Ghost-snare drags between backbeats on every other bar, skipping
    // the closing fill bar.
    for (int bar = 1; bar < kLastBar; bar += 2)
    {
        const double base = bar * kBeatsPerBar;
        // ghost on the "e" of 2 (1.25) and the "ah" of 4 (3.75)
        if (roll() < 0.85f) add (kSnare, base + 1.25, 0.32f + roll() * 0.10f);
        if (roll() < 0.70f) add (kSnare, base + 3.75, 0.30f + roll() * 0.10f);
    }

    // Hat ostinato breathing — turn a couple of 16th hats on every
    // odd bar (1, 3, 5, 7, 9, 11, 13, ...) into 32nd doubles so the
    // hat doesn't feel like a click track. Skip the closing bar.
    for (int bar = 1; bar < kLastBar; bar += 2)
    {
        const double base = bar * kBeatsPerBar;
        for (double off : { 1.0, 2.5 })
            if (roll() < 0.55f && hasNoteNear (kClosedHat, base + off))
                add (kClosedHat, base + off + 0.125, 0.40f + roll() * 0.08f);
    }

    // Kick syncopation on the "e" / "and" across every even bar (2, 4,
    // 6, 8, 10, 12, 14, ...) so the pocket walks instead of stomping.
    for (int bar = 2; bar < kLastBar; bar += 2)
    {
        const double base = bar * kBeatsPerBar;
        if (roll() < 0.55f) add (kKick, base + 1.75, 0.78f);   // and-of-2
        if (roll() < 0.45f) add (kKick, base + 3.25, 0.72f);   // e-of-4
    }

    // Tom drops on phrase endings — mid-phrase (end of every 4th bar)
    // and pre-fill (the bar just before the closing fill). Three-tom
    // descent into the next bar.
    auto tomDrop = [&] (double startBeat)
    {
        add (kHighTom,  startBeat + 0.00, 0.78f);
        add (kLowTom,   startBeat + 0.25, 0.82f);
        add (kFloorTom, startBeat + 0.50, 0.86f);
    };
    for (int bar = 3; bar < kLastBar; bar += 4)
    {
        if (roll() < 0.65f) tomDrop ((double) bar * kBeatsPerBar + 3.25);
    }
    if (kLastBar >= 1)
    {
        const int preFillBar = kLastBar - 1;
        if (roll() < 0.95f)
            tomDrop ((double) preFillBar * kBeatsPerBar + 3.25); // pre-fill lead-in
    }

    std::sort (out.notes.begin(), out.notes.end(),
               [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
               { return a.startBeat < b.startBeat; });

    // Dedupe near-coincident hits per (note, time) so tiling + flourishes
    // never stack two kicks on the same downbeat.
    auto quant = [] (double b) { return (int) std::llround (b * 16.0); }; // 1/64 grid
    std::vector<aidrum::MidiNote> deduped;
    deduped.reserve (out.notes.size());
    std::set<std::pair<int,int>> seen;
    for (const auto& n : out.notes)
    {
        const auto key = std::make_pair (n.noteNumber, quant (n.startBeat));
        if (seen.count (key)) continue;
        seen.insert (key);
        deduped.push_back (n);
    }
    out.notes = std::move (deduped);
    return out;
}

// v1.6.1-rc.13 — auto-fill anchors on the LAST bar of EVERY 8-bar block
// (the user's annotated screenshot: "the fills are generated too early").
// Regions shorter than 8 bars get NO auto-fill — fills only fire when a
// full 8-bar phrase has played out, so the cadence reads as a real
// drummer "closing the eight" rather than a fill on every micro-region.
//
// Behaviour:
//   * Every full 8-bar block in the region gets a fill on bar 8 (the
//     LAST bar of that block). 16-bar regions fire fills at bars 8 and
//     16; 24-bar regions at 8/16/24; etc.
//   * Fill index (0..N-1) is chosen by DENSITY × INTENSITY. The
//     COMPLEXITY knob is the density axis — light grooves draw from
//     the low end (gentle ghost rolls), hectic grooves the top end
//     (sludge tom flares).
//   * The FILL dropdown / cycler offset (-2..+2) still nudges the
//     index so the user can taste-test, but density × intensity is
//     the primary driver. Each successive 8-bar block also rotates
//     the fill so a 16-bar region doesn't repeat the same flourish.
//   * Fills tile end-to-end if shorter than a bar (rc.11 spec).
//   * NEVER stretches the region past its existing length.
void AIDrumAudioProcessor::spliceMandatoryFillIntoRegion (
    aidrum::MidiPattern& region, int seed) const
{
    constexpr double kBeatsPerBar       = 4.0;
    constexpr double kBeatsPerEightBar  = 8.0 * kBeatsPerBar; // 32

    // v1.6.1-rc.16 — phrase-end fill placement matches Logic Pro's
    // Drummer engine: "Drummer automatically places fills at the end
    // of 2-bar, 4-bar, or 8-bar phrases" (newpatch+for+1.6.docx).
    // Pick the largest power-of-two phrase length that fits the
    // region: 8-bar regions get one fill on bar 8; 4-bar regions
    // get one fill on bar 4; 2-bar regions get one on bar 2.
    // Anything < 2 bars stays user-owned (no fill).
    constexpr double kBeatsPerTwoBar  = 2.0 * kBeatsPerBar;
    constexpr double kBeatsPerFourBar = 4.0 * kBeatsPerBar;

    double phraseBeats = 0.0;
    if      (region.lengthInBeats >= kBeatsPerEightBar - 1e-6) phraseBeats = kBeatsPerEightBar;
    else if (region.lengthInBeats >= kBeatsPerFourBar  - 1e-6) phraseBeats = kBeatsPerFourBar;
    else if (region.lengthInBeats >= kBeatsPerTwoBar   - 1e-6) phraseBeats = kBeatsPerTwoBar;
    else
        return;

    // v1.6.1-rc.14 — PROCEDURAL MIDI fill generation. The WAV-onset
    // fillLibrary() patterns are no longer spliced verbatim; instead
    // each 8-bar block calls aidrum::fillgen::generate() which always
    // produces a fully-populated 4-beat MIDI pattern (no leftover dead
    // bar — fixes the "fill leaving a whole other bar left behind"
    // workflow blocker). The 22 archetypes are ordered light → sludge
    // and the dropdown / cycler index maps directly into them.
    constexpr int kNumArchetypes = aidrum::fillgen::kArchetypeCount;

    const float density      = juce::jlimit (0.0f, 1.0f,
                                   apvts.getRawParameterValue (kParamComplexity)->load());
    const float globalIntens = juce::jlimit (0.0f, 1.0f,
                                   apvts.getRawParameterValue (kParamIntensity)->load());
    // v1.6.1-rc.14 — fill velocity / complexity respect this region's
    // own INTENSITY knob if the user dialled one in (sentinel < 0
    // inherits the global). A soft pre-chorus → light fill; chorus
    // slammed → sludgier fill picked up the archetype curve.
    const float intensity    = (region.regionIntensity >= 0.0f)
                                   ? juce::jlimit (0.0f, 1.0f, region.regionIntensity)
                                   : globalIntens;
    const float fcRaw        = juce::jlimit (0.0f, 1.0f,
                                   apvts.getRawParameterValue (kParamFillComplexity)->load());
    const float fillDens     = juce::jlimit (0.0f, 1.0f,
                                   apvts.getRawParameterValue (kParamFillDensity)->load());

    // Density × intensity drives the primary archetype index (light
    // grooves → low archetypes, hectic grooves → sludge). The FILL
    // dropdown bias adds ±2 so the user's hand-pick still nudges the
    // selection without overriding the contextual choice.
    const float densityIntensity = density * intensity;
    const int   primaryIdx       = juce::jlimit (0, kNumArchetypes - 1,
                                       static_cast<int> (std::round (densityIntensity
                                           * (float) (kNumArchetypes - 1))));
    const int   selectorBias     = juce::jlimit (-2, 2,
                                       static_cast<int> (std::round ((fcRaw - 0.5f) * 4.0f)));
    const int   fillIdx          = ((primaryIdx + selectorBias) % kNumArchetypes
                                       + kNumArchetypes) % kNumArchetypes;

    // v1.6.1-rc.16 — iterate phrase blocks of `phraseBeats` (32 / 16 /
    // 8 beats for 8/4/2-bar phrases). The fill always lands in the
    // LAST bar of each phrase block.
    const int numBlocks = static_cast<int> (
        std::floor ((region.lengthInBeats + 1e-6) / phraseBeats));

    for (int block = 0; block < numBlocks; ++block)
    {
        const double phraseStart = block * phraseBeats;
        const double anchor      = phraseStart + (phraseBeats - kBeatsPerBar);
        const double windowEnd   = juce::jmin (anchor + kBeatsPerBar, region.lengthInBeats);

        if (windowEnd - anchor < kBeatsPerBar - 1e-6)
            continue;

        // Rotate archetype across blocks so a 16-bar region's two fills
        // don't repeat verbatim. Density adds a perlin-ish offset too.
        const int   blockFillIdx = ((fillIdx + block) % kNumArchetypes
                                       + kNumArchetypes) % kNumArchetypes;

        // Wipe existing notes inside the fill window so the fill reads clean.
        region.notes.erase (
            std::remove_if (region.notes.begin(), region.notes.end(),
                            [&] (const aidrum::MidiNote& n)
                            { return n.startBeat >= anchor - 1e-6
                                  && n.startBeat <  windowEnd - 1e-6; }),
            region.notes.end());

        // Generate the procedural fill as a fresh 0..4 beat pattern,
        // then translate every note up to the anchor inside this region.
        const std::uint64_t blockSeed =
            (static_cast<std::uint64_t> (seed) << 32) ^
            (static_cast<std::uint64_t> (block) * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t> (blockFillIdx) * 0xC6BC279692B5C323ULL);

        const aidrum::MidiPattern fill =
            aidrum::fillgen::generate (blockFillIdx, fillDens, intensity, blockSeed);

        for (const auto& src : fill.notes)
        {
            const double absBeat = anchor + src.startBeat;
            if (absBeat < anchor - 1e-6 || absBeat >= windowEnd - 1e-9)
                continue;
            aidrum::MidiNote shifted = src;
            shifted.startBeat = absBeat;
            region.notes.push_back (shifted);
        }
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
    constexpr int kRide      = 51;
    constexpr int kSnare     = 38;
    constexpr int kCrashL    = 49;
    constexpr int kCrashR    = 57;

    const float intensity = juce::jlimit (
        0.0f, 1.0f, apvts.getRawParameterValue (kParamIntensity)->load());

    std::mt19937_64 rng (seed ^ 0x9E3779B97F4A7C15ULL);
    auto roll = [&] () { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); };

    // v1.6.1-rc.16 — CRASH-MODE override above 0.85 intensity.
    // User spec verbatim: "WHEN ABOVE 85 INTENSITY TAKE RIDES AND
    // CRASHES OUT OF INTELLIGENCE PAD AND COMPOSITIONS SO CRASHES
    // HIT. L CRASH ON THE 1, THEN R AND L CRASH ON THE 1 1/2 BAR,
    // L CRASH ON THE 2 BAR BOOM BOOM BOOM TYPE FEEL".
    //
    // Implementation: above 0.85 we wipe every existing ride + hat
    // + crash from the region, then stamp a 2-bar BOOM cadence
    // (bar 1 beat 1 = L, bar 1 beat 3 [the "1 1/2"] = R+L doubled,
    // bar 2 beat 1 = L) repeating across the region. No subtle
    // "and-of-4" sprays — this is supposed to be relentless,
    // measured, and CRASH-led.
    if (intensity > 0.85f)
    {
        const double regLenHi = std::max (0.0, region.lengthInBeats);

        region.notes.erase (
            std::remove_if (region.notes.begin(), region.notes.end(),
                            [&] (const aidrum::MidiNote& n)
                            {
                                return n.noteNumber == kClosedHat
                                    || n.noteNumber == kPedalHat
                                    || n.noteNumber == kOpenHat
                                    || n.noteNumber == kRide
                                    || n.noteNumber == kCrashL
                                    || n.noteNumber == kCrashR;
                            }),
            region.notes.end());

        auto stampCrash = [&] (int n, double startBeat, float vel)
        {
            if (startBeat < 0.0 || startBeat >= regLenHi - 1e-3) return;
            aidrum::MidiNote m;
            m.noteNumber = n;
            m.startBeat  = startBeat;
            m.lengthBeat = 0.50;
            m.velocity   = juce::jlimit (0.0f, 1.0f, vel);
            region.notes.push_back (m);
        };

        const int totalBarsHi = static_cast<int> (regLenHi / 4.0 + 1e-6);
        // 2-bar repeating BOOM cadence. We anchor the cycle on every
        // 8-beat block (bar 1 + bar 2) so the cadence stays in phase
        // with the region downbeat regardless of region length.
        for (int barHi = 0; barHi < totalBarsHi; ++barHi)
        {
            const double anchorBar = barHi * 4.0;
            const bool   evenBar   = (barHi % 2 == 0);
            const float  velMain   = 0.95f;
            const float  velHalf   = 0.88f;

            if (evenBar)
            {
                // Bar 1 of the cadence: L on beat 1, R+L on beat 3.
                stampCrash (kCrashL, anchorBar,         velMain);
                stampCrash (kCrashR, anchorBar + 2.0,   velHalf);
                stampCrash (kCrashL, anchorBar + 2.0,   velHalf);
            }
            else
            {
                // Bar 2 of the cadence: L alone on beat 1.
                stampCrash (kCrashL, anchorBar,         velMain);
            }
        }

        std::sort (region.notes.begin(), region.notes.end(),
                   [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
                   { return a.startBeat < b.startBeat; });
        return;
    }

    // v1.6.1-rc.13 — user spec: "AUTOMATE MORE CRASHES IN PATTERNS".
    // Crash placement now starts at intensity > 0.30 (was 0.55) so the
    // sludge L↔R bounce is audible across most of the knob range, not
    // just the top quarter.
    if (intensity <= 0.30f + 1e-3f)
        return;

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

    // v1.6.1-rc.13 — much denser crash placement across the bar (user:
    // "AUTOMATE MORE CRASHES IN PATTERNS", "MORE ALTERNATE CRASH HIT
    // VARIATIONS, MORE CRASH CENTERED"). Density tiers:
    //   0.30 .. 0.55   → L↔R on every bar 1 of an 8-bar block (bars 1, 5)
    //   0.55 .. 0.75   → L↔R on every odd bar (1, 3, 5, 7) + half-time
    //                    "and" of 4 lead-ins
    //   0.75 .. 0.92   → crash on every bar, alternating L↔R + every
    //                    "and" of 4 lead-in + half of bar-2/6 accents
    //   ≥ 0.92         → double L+R on every phrase top, single
    //                    alternations on every bar, accents on "e" /
    //                    "and" of 4 across all bars (sludge spray)
    const int totalBars = static_cast<int> (regLen / 4.0 + 1e-6);
    for (int bar = 0; bar < totalBars; ++bar)
    {
        const double anchor      = bar * 4.0;
        const bool   isPhraseTop = (bar % 4 == 0); // bar 1, 5, 9, …
        const float  vel         = 0.82f + 0.13f * intensity;

        if (intensity >= 0.92f)
        {
            // Sludge spray — every bar gets a crash, phrase tops doubled.
            if (isPhraseTop)
            {
                place (kCrashL, anchor, vel);
                place (kCrashR, anchor, vel);
            }
            else
            {
                place (((bar / 2) & 1) ? kCrashL : kCrashR, anchor, vel * 0.92f);
            }
        }
        else if (intensity >= 0.75f)
        {
            // Driving — every bar, alternating L↔R.
            place ((bar & 1) ? kCrashR : kCrashL, anchor, vel);
        }
        else if (intensity >= 0.55f)
        {
            // Mid — even bars (0, 2, 4, 6), L↔R alternating across the
            // even-bar series via `(bar / 2) & 1` since `bar & 1` would
            // always be 0 inside the gate (Devin Review caught the dead
            // L/R selector — would have pinned every hit to L).
            if ((bar & 1) == 0 || isPhraseTop)
                place (((bar / 2) & 1) ? kCrashR : kCrashL, anchor, vel);
        }
        else
        {
            // Low (0.30 .. 0.55) — phrase tops only (bars 0, 4, 8, …),
            // L↔R alternating across the phrase-top series via
            // `(bar / 4) & 1` since `bar & 1` would always be 0 here.
            if (isPhraseTop)
                place (((bar / 4) & 1) ? kCrashR : kCrashL, anchor, vel);
        }
    }

    // "and" of 4 lead-ins (beat 3.5 of each bar) — start showing up at
    // 0.45 intensity, scale density up from there.
    if (intensity >= 0.45f)
    {
        const float prob = juce::jlimit (0.0f, 1.0f,
                                          (intensity - 0.45f) / 0.45f);
        for (int bar = 0; bar < totalBars; ++bar)
        {
            if (roll() >= prob) continue;
            const double accent = bar * 4.0 + 3.5;
            const int    note   = (bar & 1) ? kCrashL : kCrashR;
            place (note, accent, 0.70f + 0.20f * intensity);
        }
    }

    // "e" of 4 (beat 3.25) sludge accents — kick in at 0.75 intensity.
    if (intensity >= 0.75f)
    {
        const float prob = juce::jlimit (0.0f, 1.0f,
                                          (intensity - 0.75f) / 0.25f);
        for (int bar = 0; bar < totalBars; ++bar)
        {
            if (roll() >= prob) continue;
            const double accent = bar * 4.0 + 3.25;
            const int    note   = ((bar / 2) & 1) ? kCrashR : kCrashL;
            place (note, accent, 0.65f + 0.20f * intensity);
        }
    }

    // v1.6.1-rc.18 — RIDE re-injection on the metal/sludge band.
    // User feedback verbatim: "WDF HAPPENED TO THE RIDES BRING ALL OF
    // THOSE BACK". Rides went sparse because the hat-removal pass above
    // thinned the time-keeping voice across the 0.55-0.92 band, leaving
    // long stretches with no shimmer between the crashes. Restore the
    // ride 8th-note pulse on bars 2 and 4 of every 4-bar block (the
    // sludge / Sabbath ride placement) so the high-end re-engages
    // between L↔R crash slams. Skipped above 0.85 because that band is
    // CRASH-MODE-only by spec. RideBell punctuation on phrase tops at
    // 0.75+ for the chest-thumping metal "ding" lead-ins.
    if (intensity >= 0.55f && intensity <= 0.85f)
    {
        constexpr int kRideBell = 53;
        const float velRide   = 0.62f + 0.18f * intensity;
        const float velBell   = 0.78f + 0.12f * intensity;
        for (int bar = 0; bar < totalBars; ++bar)
        {
            const bool rideBar = (bar % 4 == 1) || (bar % 4 == 3);
            if (! rideBar) continue;
            const double anchor = bar * 4.0;
            for (int eighth = 0; eighth < 8; ++eighth)
            {
                const double pos = anchor + eighth * 0.5;
                place (kRide, pos, velRide);
            }
            if (intensity >= 0.75f && (bar % 4 == 3))
                place (kRideBell, anchor, velBell);
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
    // v1.6.1-rc.14 — preserve the user's per-region INTENSITY override
    // when swapping in a fresh library pattern.
    const float savedRegionInt = arrangement.back().regionIntensity;
    arrangement.back() = lib[(size_t) libIdx].pattern;
    arrangement.back().regionIntensity = savedRegionInt;
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

void AIDrumAudioProcessor::deleteNotesInRegions (std::vector<std::pair<int, int>> noteRefs)
{
    // v1.6.1-rc.11 — Devin Review flagged that the per-note loop in
    // ArrangementStrip released the arrangement mutex between deletes,
    // letting deferred APVTS callbacks (regenerateCurrentRegion) fire
    // and replace the entire last region between two of our deletes —
    // which silently invalidated every stored note index for that
    // region. Holding the mutex across the whole batch closes that
    // race. Sort descending by (regionIdx, noteIdx) so erasing earlier
    // indices doesn't shift the indices of still-pending deletions.
    std::sort (noteRefs.begin(), noteRefs.end(),
               [] (const auto& a, const auto& b)
               {
                   if (a.first != b.first) return a.first > b.first;
                   return a.second > b.second;
               });
    // Drop exact duplicates that survived the sort (shouldn't happen
    // from the drag-select rect, but it's a cheap defensive check
    // because erasing the same index twice would corrupt the vector).
    noteRefs.erase (std::unique (noteRefs.begin(), noteRefs.end()),
                    noteRefs.end());

    std::lock_guard<std::mutex> lock (arrangementMutex);
    for (const auto& [regionIndex, noteIndex] : noteRefs)
    {
        if (regionIndex < 0 || regionIndex >= static_cast<int> (arrangement.size()))
            continue;
        auto& region = arrangement[(size_t) regionIndex];
        if (noteIndex < 0 || noteIndex >= static_cast<int> (region.notes.size()))
            continue;
        region.notes.erase (region.notes.begin() + noteIndex);
    }
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
    // v1.6.1-rc.16 — keep region.notes sorted by startBeat. The render
    // and export paths both apply snare L/R stick alternation by
    // counting snare hits in vector order; if the vector isn't
    // chronological a user-duplicated snare lands in the wrong hand
    // (subtle but musicians hear it).
    std::stable_sort (region.notes.begin(), region.notes.end(),
                      [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
                      { return a.startBeat < b.startBeat; });
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
    // v1.6.1-rc.16 — sort by startBeat; render/export count snare hits
    // in vector order to assign L/R stick voicing.
    std::stable_sort (region.notes.begin(), region.notes.end(),
                      [] (const aidrum::MidiNote& a, const aidrum::MidiNote& b)
                      { return a.startBeat < b.startBeat; });
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

void AIDrumAudioProcessor::pasteCopiedRegionInto (int index)
{
    // v1.6.1-rc.16 — per-region paste. Replaces the region at `index`
    // with the clipboard snapshot instead of appending. Preserves
    // that region's INTENSITY override so the user's pre-chorus /
    // chorus / bridge dial-in survives the paste.
    aidrum::MidiPattern snapshot;
    {
        std::lock_guard<std::mutex> lock (clipboardMutex);
        if (! clipboardPattern.has_value())
            return;
        snapshot = *clipboardPattern;
    }
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (index < 0 || index >= static_cast<int> (arrangement.size()))
        return;
    const float savedRegionInt = arrangement[(size_t) index].regionIntensity;
    arrangement[(size_t) index] = std::move (snapshot);
    arrangement[(size_t) index].regionIntensity = savedRegionInt;
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

// v1.6.1-rc.24 — PianoRoll API removed alongside the FL-style
// chromatic piano-roll component. The step grid (setManualCellStep /
// clearManualCellStep) is the only manual editor. Chromatic input
// is now provided by the host's piano roll via processBlock's
// host-MIDI capture path.

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
    // v1.6.1-rc.17 — split the tom GM family into FOUR voices so the
    // ProceduralFills cascade (kFloorTom 41 / kLowTom 43 / kMidTom 45 /
    // kHighTom 48) routes to four distinct prof.* slots. Previously GM 43
    // had no remap and passed through untouched: on host kits where 43
    // wasn't mapped it fell to the nearest mapped voice (often the snare),
    // which was the user-reported "toms at snares" symptom.
    constexpr int kFloorTomGM = 41, kLowTomGM = 43, kMidTomGM = 45, kHighTomGM = 48;

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
        else if (n.noteNumber == kFloorTomGM)  n.noteNumber = prof.floorTom;
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

    // v1.6.1-rc.23 — Mandatory bar-8 fill splice on EVERY committed
    // region, regardless of source. Previously only AI-composed paths
    // (composeMoldAroundForKit / randomizePatternForKit) called the
    // splice, so user-drawn manual / piano-roll / step-grid patterns
    // were committed without the closing-bar fill. User explicitly
    // asked: "ALL PATTERNS INCLUDING INTELLIGENCE PAD HAVE FILLS".
    // The splice walks the region's lengthInBeats and only acts on
    // ≥2-bar regions, so 1-bar manual sketches stay untouched.
    //
    // Devin Review fix: splice MUST run before withActiveKitApplied.
    // The procedural fill generator (aidrum::fillgen::generate) always
    // emits GM note numbers (kSnare=38, kKick=36, ...). If we spliced
    // after the kit remap, fill notes would stay in GM while user
    // notes got pushed to kit-specific note numbers (e.g. snare
    // GM38 → 40 on Thrash) and would also miss the per-kit
    // velocityScale that withActiveKitApplied applies to every note.
    // Splicing first keeps both user + fill notes in GM, then the
    // single remap pass converts them together.
    {
        const auto seed = static_cast<int> (
            std::hash<std::size_t>{} (remapped.notes.size())
            ^ static_cast<std::size_t> (remapped.lengthInBeats * 1000.0));
        spliceMandatoryFillIntoRegion (remapped, seed);
    }

    remapped = withActiveKitApplied (std::move (remapped));
    // v1.6.1-rc.20-fix5 — tag the committed region so render +
    // export skip the GM-drum whitelist for it. Without this, every
    // chromatic synth/pad/phrase note placed via the FL piano roll
    // would be silently scrubbed once the user leaves manual mode.
    remapped.isManualOrigin = true;

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

    // v1.6.1-rc.16 — resolve the active kit's snare + ghost-snare note
    // numbers so the L/R alternation also catches manual-mode snare hits
    // for kits that remap GM 38 (e.g. Thrash/Sludge → 40, Jazz → 37,
    // 808 Trap → 39). Without this, manual mode on remapped kits would
    // skip the alternation and revert to the one-handed feel.
    const auto activeKit = static_cast<aidrum::DrumKit> (
        (int) apvts.getRawParameterValue (kParamDrumKit)->load());
    const auto& activeProf  = aidrum::drumKitProfile (activeKit);
    const int   activeSnare = activeProf.snare;
    const int   activeGhost = activeProf.ghostSnare;

    double regionOffset = 0.0;
    for (const auto& region : snapshot)
    {
        // v1.6.1-rc.14 — per-region intensity override, identical to the
        // live-render path so dragged-out MIDI carries the same velocities.
        const float regionI = (region.regionIntensity >= 0.0f)
                                ? juce::jlimit (0.0f, 1.0f, region.regionIntensity)
                                : intensity01;

        // v1.6.1-rc.16 — snare LEFT/RIGHT stick alternation MUST also
        // apply at MIDI export time so a clip dragged into the DAW
        // carries the same two-handed feel the user heard in the
        // plugin. Tempo is fixed at 120 BPM here (500000 us/quarter,
        // tempoMetaEvent above), so secondsPerBeat = 0.5.
        constexpr double kExportSecondsPerBeat = 0.5;
        const double leftBeatShift  = (-2.5 / 1000.0) / kExportSecondsPerBeat;
        const double rightBeatShift = ( 1.0 / 1000.0) / kExportSecondsPerBeat;
        constexpr int kSnareGM      = 38;
        int snareHitIdx = 0;

        // v1.6.1-rc.20-fix3 — the GM-drum whitelist (rc.3) exists to
        // scrub tambourines/shakers/claps out of the 119 STARTER
        // seeds. Manual-mode patterns come from the FL-style piano
        // roll which is intentionally full-chromatic (C0..B10) so
        // the user can program melodic synth/pad/phrase voicings on
        // top of the Drocetti trap kit. Filtering manual notes
        // through the drum whitelist silently drops every pitch
        // outside MIDI 35..59, defeating the entire piano-roll
        // feature. Bypass the filter when manual mode is active.
        // v1.6.1-rc.20-fix5 — also bypass for arrangement regions
        // that were committed via commitManualPatternAsRegion (ADD TO
        // ARRANGEMENT button on the piano roll). Without this, the
        // user's chromatic notes would survive live manual playback
        // and instantly disappear the moment they pressed ADD TO
        // ARRANGEMENT and switched out of manual mode.
        const bool allowAllNotes =
            manualModeActive.load (std::memory_order_acquire)
            || region.isManualOrigin;

        for (const auto& note : region.notes)
        {
            if (! allowAllNotes && ! isAllowedDrumNote (note.noteNumber))
                continue;

            double rawOnBeat  = note.startBeat;
            double rawLenBeat = std::max (0.01, note.lengthBeat);
            float  rawVel     = note.velocity;

            const bool isSnareLikeNote = (note.noteNumber == kSnareGM)
                                       || (note.noteNumber == activeSnare)
                                       || (note.noteNumber == activeGhost);
            if (isSnareLikeNote)
            {
                const bool leftHand = (snareHitIdx % 2) == 0;
                rawOnBeat  += leftHand ? leftBeatShift  : rightBeatShift;
                // Clamp so a snare on beat 0 never lands at a negative
                // tick (the export sequence does not accept negative
                // timestamps; live-render path also clamps for the same
                // reason).
                rawOnBeat   = std::max (0.0, rawOnBeat);
                rawVel     *= leftHand ? 0.93f : 1.04f;
                rawLenBeat *= leftHand ? 0.95  : 1.05;
                ++snareHitIdx;
            }

            const double onTicks  = (regionOffset + rawOnBeat) * kPPQ;
            const double offTicks = (regionOffset + rawOnBeat + rawLenBeat) * kPPQ;
            const auto vel = shapeVelocity (note.noteNumber, rawVel,
                                            regionI, shapeRng,
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

    // v1.6.1-rc.20-fix3 — capture once so the inner loop knows
    // whether to bypass the GM-drum whitelist for chromatic
    // piano-roll notes. See export-path comment for context.
    const bool manualLive =
        manualModeActive.load (std::memory_order_acquire);

    if (manualLive)
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
    // v1.6.1-rc.16 — resolve the active kit's snare + ghost-snare so
    // the L/R alternation below also catches manual-mode snare hits on
    // kits that remap GM 38 (Thrash/Sludge → 40, Jazz → 37, 808 → 39).
    const auto activeKit = static_cast<aidrum::DrumKit> (
        (int) apvts.getRawParameterValue (kParamDrumKit)->load());
    const auto& activeProf       = aidrum::drumKitProfile (activeKit);
    const int   activeSnareNote  = activeProf.snare;
    const int   activeGhostSnare = activeProf.ghostSnare;

    auto emitNotesInWindow = [&] (double winStart, double winEnd)
    {
        double regionOffset = 0.0;
        for (const auto& region : snapshot)
        {
            const double regionLen = std::max (0.001, region.lengthInBeats);
            // v1.6.1-rc.14 — per-region INTENSITY override. Sentinel < 0
            // means "inherit the global INTENSITY knob"; any non-negative
            // value clamped 0..1 wins so the user can program a soft
            // pre-chorus → slammed chorus → somber bridge by spinning
            // each region's own intensity dial.
            const float regionI = (region.regionIntensity >= 0.0f)
                                    ? juce::jlimit (0.0f, 1.0f, region.regionIntensity)
                                    : intensity01;

            // v1.6.1-rc.16 — snare LEFT/RIGHT stick alternation.
            // User spec: "ADD A Left STICK AND Right STICK FOR SNARE
            // THE HITS SHOULD SOUND APPARENTLY DIFFERENT … THERE ARE
            // TWO HANDS PLAYING THE DRUMS NOT ONE HAND PLAYING AS FAST
            // AS POSSIBLE." Implementation: count snare hits per
            // region in startBeat order and apply alternating L/R
            // micro-shifts in timing, velocity, and length so adjacent
            // snares feel like two hands trading off rather than one
            // hand machine-gunning a single sample.
            //   Left  hand (idx 0,2,4…): -2.5 ms behind grid, vel ×0.93,
            //                            length ×0.95 (slightly choked)
            //   Right hand (idx 1,3,5…): +1.0 ms ahead of grid, vel ×1.04,
            //                            length ×1.05 (slightly fuller)
            // The asymmetry was modelled on the SoCal_*.wav reference
            // loops the user supplied — the stronger hand lands a hair
            // late and louder, the weaker hand a hair early and softer.
            int   snareHitIdx = 0;
            const double leftBeatShift  = (-2.5 / 1000.0) / secondsPerBeat;
            const double rightBeatShift = ( 1.0 / 1000.0) / secondsPerBeat;
            constexpr int kSnareGM = 38;

            // v1.6.1-rc.20-fix5 — per-region bypass so committed
            // manual regions (isManualOrigin) keep their chromatic
            // notes after the user leaves manual mode. See export-
            // path comment for the full reasoning.
            const bool allowAllNotes = manualLive || region.isManualOrigin;

            for (const auto& note : region.notes)
            {
                if (! allowAllNotes && ! isAllowedDrumNote (note.noteNumber))
                    continue;

                double rawOnBeat  = note.startBeat;
                double rawLenBeat = std::max (0.01, note.lengthBeat);
                float  rawVel     = note.velocity;

                const bool isSnareLikeNote = (note.noteNumber == kSnareGM)
                                           || (note.noteNumber == activeSnareNote)
                                           || (note.noteNumber == activeGhostSnare);
                if (isSnareLikeNote)
                {
                    const bool leftHand = (snareHitIdx % 2) == 0;
                    rawOnBeat  += leftHand ? leftBeatShift  : rightBeatShift;
                    // Clamp so a snare hit at startBeat 0.0 in the
                    // first region never lands at a negative onBeat —
                    // negative onBeats fall before the playhead window
                    // (winStart >= 0) and would be silently dropped,
                    // leaving an orphan note-off.
                    rawOnBeat   = std::max (0.0, rawOnBeat);
                    rawVel     *= leftHand ? 0.93f          : 1.04f;
                    rawLenBeat *= leftHand ? 0.95           : 1.05;
                    ++snareHitIdx;
                }

                const double onBeat  = regionOffset + rawOnBeat;
                const double offBeat = onBeat + std::max (0.01, rawLenBeat);

                if (onBeat >= winStart && onBeat < winEnd)
                {
                    const int sample = static_cast<int> (
                        (onBeat - blockStartBeat) * secondsPerBeat * sampleRate / timeScale);
                    midiOut.addEvent (
                        juce::MidiMessage::noteOn (10, note.noteNumber,
                            shapeVelocity (note.noteNumber, rawVel,
                                           regionI, shapeRng,
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

    // v1.6.1-rc.24 — Logic Pro Drummer / FL Studio piano roll / any
    // host MIDI compat. Previously midi.clear() ran first thing, which
    // wiped every host noteOn before the sampler ever saw it — that's
    // why the plugin sounded "muted" on a Logic Drummer track and FL
    // Studio's piano roll couldn't drive the kit. Capture inbound host
    // noteOns up-front and dispatch them through the active-kit GM
    // remap into the same sampler/synth path our COMPOSE/RANDOMIZE
    // arrangement uses, so the plugin behaves like any standard drum
    // sampler (Battery / Superior Drummer / EZdrummer / Addictive
    // Drums) AND keeps emitting its own arrangement on top.
    drumSynth.setMasterGain (outputLevel.load (std::memory_order_relaxed));
    const bool useSamples = sampleKit.isActive();
    const auto activeKitForHost = static_cast<aidrum::DrumKit> (
        (int) apvts.getRawParameterValue (kParamDrumKit)->load());
    const auto& profForHost     = aidrum::drumKitProfile (activeKitForHost);

    auto fireHostMidiNote = [&] (int note, float vel, int sampleOffset)
    {
        // Mirror withActiveKitApplied()'s GM → kit remap so the host
        // can program with standard GM drum note numbers (kick=36,
        // snare=38, hat=42, ride=51, etc.) and have them route to the
        // active kit's specific slots. Kits like Thrash / Sludge / 808
        // remap GM 38 to 40 / 39, and applying the same lookup here
        // keeps host MIDI in sync with the COMPOSE/RANDOMIZE path.
        constexpr int kKickGM = 36, kSnareGM = 38, kSideStickGM = 37, kClapGM = 39;
        constexpr int kClosedHatGM = 42, kPedalHatGM = 44, kOpenHatGM = 46;
        constexpr int kRideGM = 51, kRideBellGM = 53, kCrashGM = 49,
                      kCrashAltGM = 57, kChinaGM = 52;
        constexpr int kFloorTomGM = 41, kLowTomGM = 43, kMidTomGM = 45, kHighTomGM = 48;
        if      (note == kKickGM)      note = profForHost.kick;
        else if (note == kSnareGM)
            note = (vel <= profForHost.ghostThreshold
                    && profForHost.ghostSnare != profForHost.snare)
                     ? profForHost.ghostSnare : profForHost.snare;
        else if (note == kSideStickGM) note = profForHost.sideStick;
        else if (note == kClapGM)      note = profForHost.clap;
        else if (note == kClosedHatGM) note = profForHost.closedHat;
        else if (note == kPedalHatGM)  note = profForHost.pedalHat;
        else if (note == kOpenHatGM)   note = profForHost.openHat;
        else if (note == kRideGM)      note = profForHost.ride;
        else if (note == kRideBellGM)  note = profForHost.rideBell;
        else if (note == kCrashGM)     note = profForHost.crash;
        else if (note == kCrashAltGM)  note = profForHost.crashAlt;
        else if (note == kChinaGM)     note = profForHost.china;
        else if (note == kFloorTomGM)  note = profForHost.floorTom;
        else if (note == kLowTomGM)    note = profForHost.lowTom;
        else if (note == kMidTomGM)    note = profForHost.midTom;
        else if (note == kHighTomGM)   note = profForHost.highTom;
        vel = juce::jlimit (0.01f, 1.0f, vel * profForHost.velocityScale);

        const float gain = vel * outputLevel.load (std::memory_order_relaxed);
        if (useSamples) sampleKit.noteOn (note, gain, sampleOffset);
        else            drumSynth.noteOn (note, vel, sampleOffset);
    };

    // Dispatch host noteOns BEFORE midi.clear() so the kit responds to
    // the host's piano roll / Drummer Editor / MIDI keyboard. We do
    // this BEFORE the shouldPlay gate too, so the user can audition
    // notes from the DAW even when the plugin's internal transport is
    // stopped (matches every other commercial drum sampler).
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (! msg.isNoteOn()) continue;
        fireHostMidiNote (msg.getNoteNumber(),
                          msg.getFloatVelocity(),
                          meta.samplePosition);
    }

    // Don't echo host MIDI back out as our plugin's MIDI output. The
    // outgoing buffer is reserved for our COMPOSE/RANDOMIZE arrangement
    // (which hosts that record plugin MIDI back to a region capture).
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

    // v1.6.1-rc.12 — Room preset / Room Amount values are now pushed into
    // the master reverb only on change (see parameterChanged →
    // applyRoomPresetToMaster), NOT every audio block. The mixer panel's
    // per-bus REV + DEPTH knobs and the master REV MIX/SIZE/DAMP atomics
    // are now the live source of truth so dragging them is audible
    // immediately instead of being clobbered every block.

    // Decide whether to advance / emit this block.
    const auto state = (TransportState) transportState.load (std::memory_order_acquire);
    const bool shouldPlay = hostDrivesPlayhead ? hostIsPlaying
                                               : (state == TransportState::Playing);

    if (! shouldPlay)
    {
        // Silent (transport-wise) block. We still render any voices in
        // flight — including the host MIDI noteOns we just dispatched
        // above, plus any held tails from the previous block — so the
        // kit stays responsive to host MIDI when the plugin's internal
        // transport is paused.
        busMixer.beginBlock (buffer.getNumSamples());
        if (useSamples) sampleKit.renderIntoBuses (busMixer, buffer.getNumSamples());
        else            drumSynth.renderIntoBuses (busMixer, buffer.getNumSamples());
        busMixer.process (buffer);
        return;
    }

    renderArrangementToMidiBuffer (midi,
                                   buffer.getNumSamples(),
                                   getSampleRate(),
                                   bpm,
                                   hostDrivesPlayhead);

    // Feed the freshly-generated arrangement MIDI notes into either the
    // sampler (if a kit is loaded) or the physical-model synth fallback.
    // These notes are already kit-remapped by renderArrangementToMidiBuffer
    // (which calls withActiveKitApplied on each region), so we don't
    // double-apply the GM remap here — only the master output gain.
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
