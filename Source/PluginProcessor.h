#pragma once

#include "AIBackend.h"
#include "DrumBusMixer.h"
#include "DrumSynth.h"
#include "MidiPattern.h"
#include "SampleKit.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

class AIDrumAudioProcessor : public juce::AudioProcessor,
                             public juce::AudioProcessorValueTreeState::Listener
{
public:
    AIDrumAudioProcessor();
    ~AIDrumAudioProcessor() override;

    // v1.5.0 — live-knob regeneration callback.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

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

    // Removes the most-recently-appended region. v1.5.0: arrangement is
    // allowed to go empty; the user opts back in with the `+` button.
    void undoLastRegion();

    // v1.5.0 — Delete an arbitrary region by index. Allows the user to
    // remove the very first region (unlike Logic, which only lets you
    // delete trailing regions). No-op if index is out of range.
    void deleteRegion (int index);

    // v1.5.0 — Regenerate the most-recently-appended region using the
    // current APVTS settings. Called by the APVTS listener on any knob
    // move so the pattern changes live. Safe to call when arrangement is
    // empty (no-op).
    void regenerateCurrentRegion();

    // v1.6.1-rc.12 — push the current Room dropdown + Room Amount knob
    // values into the master reverb engine. Called from the parameter
    // listener on Room/RoomAmount change and once after construction so
    // the mixer panel knobs become the live source of truth (rather than
    // being clobbered every audio block by the room preset).
    void applyRoomPresetToMaster();

    // Wipes the arrangement. v1.5.0: leaves it truly empty so the + sign
    // (Logic-style) re-appears and the user picks their starting pattern.
    void clearArrangement();

    // v1.6.1-rc.14 — per-region INTENSITY override. Pass < 0 to clear the
    // override (region falls back to the global INTENSITY knob); pass
    // 0..1 to lock that region to its own velocity vibe (soft pre-chorus,
    // slammed chorus, somber bridge). Audio thread reads the value at
    // emit time via shapeVelocity (no regen needed).
    void  setRegionIntensity (int index, float intensity01);
    float getRegionIntensity (int index) const;

    // v1.6.0 — STARTER GROOVES. The plugin ships with a library of
    // hand-played drum grooves (analysed from user-supplied WAVs). This
    // API drops one into the arrangement as a new region. Call with
    // index < 0 or >= starterCount for no-op.
    int  starterGrooveCount() const;
    juce::String starterGrooveName (int index) const;
    void appendStarterGroove (int index);

    // v1.6.1-rc.6 — single bundled kit; STARTER filtering is a no-op.
    // These wrappers exist for API back-compat with the rc.3..rc.5
    // editor; they now just forward to the full STARTER library.
    int  starterGrooveCountForKit (int kitIndex) const;
    juce::String starterGrooveNameForKit (int kitIndex, int subIndex) const;
    void appendStarterGrooveForKit (int kitIndex, int subIndex);
    // Picks a random groove from the kit's subset and appends it.
    void appendRandomGrooveForKit (int kitIndex);

    // v1.6.1-rc.9 — COMPOSE pad: mold around the user's existing pattern
    // instead of replacing it. If the arrangement already has at least
    // one region, this picks the next groove from the kit's bucket
    // (Scripter-style cycler) and *overlays* its idiomatic notes on
    // top of the last region — preserving every kick / snare / hat the
    // user has drawn in manual mode and only adding ghost-snare drags,
    // hat ostinatos, kick syncopations, etc. that the corpus pattern
    // brings. If the arrangement is empty it falls through to
    // appendRandomGrooveForKit so the first click still produces sound.
    void composeMoldAroundForKit (int kitIndex);

    // v1.6.1-rc.9 — RANDOMIZE pad: full-pattern replacement (the
    // original COMPOSE behavior). Picks a fresh groove from the kit's
    // bucket and replaces the last region; if the arrangement is empty
    // it appends one.
    void randomizePatternForKit (int kitIndex);

    // v1.6.1-rc.28 — region-TARGETED edit ops. Driven by the clickable
    // region-number badge in the arrangement strip: user clicks "1" /
    // "2" / etc. on a region's top-left corner and gets a popup menu
    // to randomize / compose / clear THAT specific region without
    // nuking the whole arrangement (he'd been having to clear and
    // restart). All three preserve the region's length and per-region
    // INTENSITY override; only its notes change.
    void randomizeRegion (int regionIndex, int kitIndex);
    void composeMoldRegion (int regionIndex, int kitIndex);
    void clearRegionNotes (int regionIndex);

    // v1.6.1-rc.28 — per-lane "0" / erase ops. Drives the small "0"
    // pills next to each lane label (wipes the lane across the WHOLE
    // arrangement) and the per-region per-lane "0" pills (wipes a
    // single lane in a single region). User flow: build an intro
    // region with no hi-hat / no snare by clicking the "0" pill on
    // those lanes within region 1, then leave hats + snares full in
    // regions 2-N. Lane indices match ArrangementStrip's lane order
    // (0=R CRASH, 1=L CRASH, 2=RIDE, 3=HI-HAT, 4=SMALL TOM,
    //  5=FLOOR TOM, 6=SNARE, 7=KICK). The matching set covers both
    // GM canonical notes AND the active kit's remapped notes so the
    // erase works regardless of which kit is loaded or whether a
    // region was committed pre- or post-remap.
    void eraseLaneInRegion (int regionIndex, int laneIdx);
    void eraseLaneInArrangement (int laneIdx);

    // v1.6.1-rc.9 — auto-fill cadence helper. Returns a fill-shaped
    // MidiPattern that fits the last bar of an 8-bar block, starting
    // its action on beat 2. Pure helper so the editor can paint the
    // cadence on the arrangement strip in advance of the audio
    // engine reaching that bar.
    aidrum::MidiPattern makeAutoFillForKit (int kitIndex, int seed) const;

    // v1.6.1-rc.10 — guarantees a fill at bar-8 beat 2 of every
    // 8-bar block in the supplied region. Stretches the region to
    // a multiple of 32 beats (8 bars at 4/4) if it's shorter, then
    // for each 8-bar boundary clears the last 3 beats of bar 8 and
    // splices in makeAutoFillForKit() shifted to start there. The
    // user has been emphatic across rc.7 → rc.10 that COMPOSE and
    // RANDOMIZE *must always* end on a fill — never a stale loop.
    // Caller must already hold arrangementMutex.
    void spliceMandatoryFillIntoRegion (aidrum::MidiPattern& region,
                                        int seed) const;

    // v1.6.1-rc.13 — tile a starter groove out to a full 8-bar region
    // (32 beats), adding smart corpus-informed flourishes between
    // backbeats: ghost-snare drags, hat ostinato breathing (16ths into
    // 32nds), kick syncopation on the "e" / "and", tom drops on phrase
    // endings. Caller is responsible for any subsequent crash/hat
    // balance + fill splicing — this only produces the base 8-bar bed.
    // v1.6.1-rc.18 — `targetBars` parameterised so COMPOSE / RANDOMIZE
    // honour the APVTS pattern-length choice (default is now 16 bars).
    // Devin Review (rc.18, 2nd pass): hard-coded 8-bar target was
    // shrinking the new 16-bar default back to 8 bars on every press
    // of COMPOSE or RANDOMIZE. The hat / kick / snare flourish loops
    // scale linearly with `targetBars`; the closing fill bar remains
    // owned by spliceMandatoryFillIntoRegion (which already handles
    // 2/4/8/16 bar phrase boundaries).
    aidrum::MidiPattern expandGrooveToTargetBars (const aidrum::MidiPattern& src,
                                                  std::uint64_t              seed,
                                                  int                        targetBars = 16) const;

    // v1.6.1-rc.11 — INTENSITY-driven hat thinning + crash density. The
    // user wants higher INTENSITY to read as "more crash, less hat" in
    // generator output (sludge-metal feel as the knob climbs). Above 92%
    // intensity the generator places NO hi-hats at all. Manual notes are
    // never touched — this is only invoked by COMPOSE / RANDOMIZE.
    // Caller must already hold arrangementMutex.
    void applyIntensityCrashHatBalance (aidrum::MidiPattern& region,
                                        std::uint64_t       seed) const;

    // v1.6.1-rc.3 — when the user changes kits, swap the last-appended
    // arrangement region for a groove from the new kit's bucket so the
    // user immediately hears a pattern that matches the kit's feel.
    // No-op if the arrangement is empty.
    void remapLastRegionToKit (int kitIndex);

    // v1.6.1-rc.4 — per-note editing in the arrangement. Lets the user
    // click a single snare hit in the arrangement strip and delete or
    // duplicate it without touching the rest of the region. Indices are
    // bounds-checked; out-of-range calls are no-ops.
    void deleteNoteInRegion (int regionIndex, int noteIndex);
    // v1.6.1-rc.11 — batch-delete a list of (regionIdx, noteIdx) pairs
    // under a single mutex acquisition. Used by the drag-select multi-
    // delete path so the arrangement can't be mutated by deferred APVTS
    // callbacks (regenerateCurrentRegion etc.) between individual
    // deletes, which would silently invalidate stored note indices.
    void deleteNotesInRegions (std::vector<std::pair<int, int>> noteRefs);
    void duplicateNoteInRegion (int regionIndex, int noteIndex);
    void addNoteToRegion (int regionIndex, int noteNumber,
                          double startBeat, double lengthBeats, float velocity);

    // v1.6.0 — COPY / PASTE region. Copy stores a snapshot of the region
    // at `index`; paste appends the stored snapshot as a new region at the
    // end of the arrangement. Used by the arrangement strip's
    // Ctrl+C / Ctrl+V keybindings and the COPY / PASTE buttons.
    void copyRegionToClipboard (int index);
    bool hasCopiedRegion() const;
    void pasteCopiedRegion();

    // v1.6.1-rc.16 — per-region paste. The user wants COPY+PASTE on
    // every region tile so they can copy region 1 (a verse pattern)
    // straight into region 4 (the second verse). Unlike the legacy
    // pasteCopiedRegion(), this REPLACES the region at `index` with
    // the clipboard contents instead of appending to the end. The
    // region's regionIntensity override is preserved so dialing
    // pre-chorus / chorus / bridge knobs survives the paste.
    void pasteCopiedRegionInto (int index);

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

    // v1.5.0 — variable-resolution manual grid (1/16, 1/32, 1/64). The UI
    // passes its current step-beat size so the underlying pattern is
    // quantized to the chosen subdivision.
    void setManualCellStep   (int midiNote, int stepIndex, double stepBeats, float velocity);
    void clearManualCellStep (int midiNote, int stepIndex, double stepBeats);

    void clearManualPattern();

    // v1.6.1-rc.24 — PianoRoll API removed alongside the FL-style
    // chromatic piano-roll component.

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
    // host doesn't drive the playhead). Play/Pause/Stop mirror the
    // buttons in the UI. v1.6.0 removed loop: arrangement plays the
    // whole composition front→end then stops.
    enum class TransportState : int { Stopped = 0, Playing, Paused };

    void play();
    void pause();
    void stop();
    TransportState getTransportState() const;

    // Audio level, 0..1. UI-facing master drum-synth gain so users can
    // tame or push the built-in synthesized kit.
    void  setOutputLevel (float level01);
    float getOutputLevel() const;

    // --- v1.4.0 Sampler API ---------------------------------------------
    // Loads every recognised WAV in `folder` and hot-swaps it for the
    // physical-model synth. Returns the number of samples loaded (0 on
    // failure). Call on the UI thread; audio thread swaps atomically.
    int  loadSampleKit   (const juce::File& folder);
    void unloadSampleKit();
    juce::String getSampleKitPath() const;
    bool isSampleKitActive() const;

    // v1.6.1-rc.6 — Switch to the CC0 kit compiled into the plugin
    // binary. Only one kit ships now ("Thrash"); the name arg is kept
    // for API compatibility but anything other than "Thrash" falls
    // back to it.
    int loadBundledKit (const juce::String& kitName);

    // --- v1.4.0 UI scale --------------------------------------------------
    // Persisted UI scale factor for the editor (0.75× … 1.5×). Lives on
    // the processor so it survives host saves.
    float getUiScale() const;
    void  setUiScale (float scale);

    // --- v1.6.1-rc.7 FILL SELECTOR ---------------------------------------
    // The Fill Complexity knob was replaced with a cycler that rotates
    // through the 21 user-supplied library fills (Fill_01..Fill_12 + alt
    // variants). Each press of the cycler button advances to the next
    // fill. The selected index is stored in APVTS via kParamFillComplexity
    // (stepped to discrete fill indices) so it round-trips cleanly with
    // host save files. Returns:
    //   getFillLibrarySize()   — number of library fills (21)
    //   getCurrentFillIndex()  — 0-based index of the currently selected fill
    //   getCurrentFillName()   — human-readable label for the UI
    //   cycleFillSelector(dir) — advance by ±1 with wraparound
    int          getFillLibrarySize()  const;
    int          getCurrentFillIndex() const;
    juce::String getCurrentFillName()  const;
    void         cycleFillSelector (int direction);

    // v1.6.1-rc.13 — direct fill picker (dropdown UI). setFillIndex maps
    // the chosen library index to kParamFillComplexity (0..1 normalised);
    // getAllFillNames returns labels in fillLibrary order for the
    // ComboBox. Pure replacement for the prev/next cycler.
    void              setFillIndex (int idx);
    juce::StringArray getAllFillNames() const;

    // --- v1.6.1-rc.7 INTENSITY --------------------------------------------
    // Intensity knob (0..127 UI, stored as 0..1 float). Drives the base
    // velocity + per-hit fluctuation curve applied at MIDI emit time.
    // Returns the raw 0..127 value for display.
    int getIntensity127() const;

    // --- v1.6.1-rc.7 GHOST MASK -------------------------------------------
    // Bitmask of arrangement-strip lanes that are currently in "ghost"
    // mode (bit 0 = CRASH, 1 = RIDE, 2 = HI-HAT, 3 = TOM, 4 = SNARE,
    // 5 = KICK — same order as the strip's lane table). Hits in those
    // lanes are emitted with their velocity scaled by kGhostVelocity
    // (~0.45) so the lane sounds "feathered" without the user having
    // to redraw the pattern. Atomic so the editor + audio thread can
    // both read it without locking.
    int  getGhostMask() const            { return ghostMask.load(); }
    // v1.6.1-rc.8 — widened from 0x3F (6 lanes) to 0xFF so the new
    // SNARE (bit 6) and KICK (bit 7) lanes can be ghosted too. Without
    // this the UI greys out the row label but the audio engine still
    // hears full velocity hits on those lanes.
    void setGhostMask (int mask)         { ghostMask.store (mask & 0xFF); }

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
                                        double            bpm,
                                        bool              hostDrivesPlayhead);

    // Maps the Pattern Length choice index to a length in beats.
    static double patternLengthBeatsFromChoice (int choiceIndex);

    juce::AudioProcessorValueTreeState apvts;
    aidrum::AIBackend                  backend;

    mutable std::mutex                  arrangementMutex;
    std::vector<aidrum::MidiPattern>    arrangement; // concatenated regions

    // v1.6.0 \u2014 region clipboard (COPY / PASTE).
    mutable std::mutex                  clipboardMutex;
    std::optional<aidrum::MidiPattern>  clipboardPattern;

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
    std::atomic<bool>        hostTransportSeen { false };
    std::atomic<float>       outputLevel       { 0.85f };

    // v1.0.0 — synthesized drum voice renderer so the Standalone app
    // makes sound out of the box. Plugin hosts still receive MIDI too.
    aidrum::DrumSynth        drumSynth;

    // v1.1.0 — per-drum mixer with EQ / Comp / Drive / Clip / Dampen / Reverb.
    aidrum::DrumBusMixer     busMixer;

    // v1.4.0 — real-audio sampler that takes over from DrumSynth when a
    // kit folder is loaded.
    aidrum::SampleKit        sampleKit;
    // v1.6.1-rc.10 — default scale toned down from 1.00 → 0.85 so the
    // editor opens at a sane size in Logic instead of "crazy big" on
    // first instantiate. The slider's "75 %" stop also maps here so
    // legacy projects that saved scale=1.0 still come back larger.
    // v1.6.1-rc.14 — default UI scale lifted 0.85 → 1.00 per user
    // request: "fix ui scale to open at 1 and a expand but not weird
    // i find it troubling when it open it to a big ol blob of not
    // enough labels". The slider still snaps {55 %, 70 %, 85 %, 100 %}.
    std::atomic<float>       uiScale { 1.00f };

    // v1.6.1-rc.7 — bitmask of lanes in "ghost" mode (see header doc).
    // Atomic so the editor + audio thread can both touch it without
    // grabbing a lock.
    std::atomic<int>         ghostMask { 0 };
    juce::String             loadedKitPath;
    mutable std::mutex       loadedKitPathMutex;

    // v1.6.1-rc.7 — name of the active bundled kit (empty string when a
    // user-loaded folder is in use, or when nothing is loaded). Stored
    // so prepareToPlay() can re-load the kit at the host's actual sample
    // rate; SampleKit bakes its samples to `sr` at load time, so a kit
    // loaded in the ctor at a placeholder 48 kHz must be re-baked once
    // the DAW reports its real rate (e.g. 44.1 kHz on macOS / Logic).
    juce::String             currentBundledKitName;

    // v1.6.1-rc.7 — Logic-Pro Scripter-style COMPOSE cycler. Per-kit
    // round-robin counter that walks the kit's groove bucket in order.
    // Sized to comfortably exceed the number of drum-kit entries so we
    // never index out of range; unused slots stay at 0.
    std::array<size_t, 64>   composeCycleIndex {};

public:
    // v1.1.0 mixer access for the UI.
    aidrum::DrumBusMixer& getBusMixer() { return busMixer; }
    // v1.2.0 — hit indicator access for the UI.
    aidrum::DrumSynth&    getDrumSynth() { return drumSynth; }
    // v1.4.0 — sample-kit access for the UI.
    aidrum::SampleKit&    getSampleKit()  { return sampleKit; }

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessor)
};
