#pragma once

#include "GothicLookAndFeel.h"
#include "MidiPattern.h"

#include <JuceHeader.h>

#include <functional>
#include <vector>

namespace aidrum
{
    // Multi-region piano-roll strip. Draws every pattern in an arrangement
    // end-to-end, with vertical dividers between regions and a playhead
    // scrubbing across the whole chain.
    //
    // Each new region appended via the + button grows the grid to the right.
    class ArrangementStrip : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
    {
    public:
        struct Snapshot
        {
            std::vector<MidiPattern> regions;
            double                   playheadBeats       = 0.0;
            double                   totalBeats          = 0.0;
            // v1.6.1-rc.16 — true when the processor has a region in
            // its clipboard. The PASTE pill renders bright (armed) only
            // when this is true so the user gets a visual cue that
            // pressing P will actually do something.
            bool                     clipboardHasContent = false;
        };

        ArrangementStrip()
        {
            setTooltip ("ARRANGEMENT — chained grooves/fills left-to-right. Click the + on the far right to append another region like Logic Drummer.");
            startTimerHz (30);
        }

        ~ArrangementStrip() override { stopTimer(); }

        void setProvider (std::function<Snapshot()> p) { provider = std::move (p); }
        std::function<void()> onAppend;

        // v1.6.1-rc.3 — when on, every region is drawn with a bright
        // highlight border so the user can see the whole arrangement is
        // "selected" as one block before dragging it to the DAW.
        void setHighlightAll (bool on) { highlightAll = on; repaint(); }

        // v1.6.1-rc.8 — bitmask of lanes that are currently in "ghost"
        // mode. Bit 0 = R CRASH, 1 = L CRASH, 2 = RIDE, 3 = HI-HAT,
        // 4 = SMALL TOM, 5 = FLOOR TOM, 6 = SNARE, 7 = KICK
        // (top → bottom, mirrors the kLanes table). Lanes with their bit
        // set are drawn with a greyed-out label so the user can see at a
        // glance which rows the GHOST button has flipped.
        void setGhostMask (int mask) { ghostMask = mask; repaint(); }
        int  getGhostMask() const    { return ghostMask; }

        // v1.6.1-rc.7 — fires when the user clicks a row label on the
        // left edge of the strip. The editor uses this to remember which
        // lane the GHOST button should target on its next click.
        // v1.6.1-rc.8 — 0..7 in the same top→bottom order as the lane
        // table (R CRASH / L CRASH / RIDE / HI-HAT / SMALL TOM / FLOOR
        // TOM / SNARE / KICK).
        std::function<void (int laneIndex)> onLaneSelected;
        void setSelectedLane (int laneIndex)
        {
            selectedLaneIdx = juce::jlimit (-1, 7, laneIndex);
            repaint();
        }

        // v1.6.1-rc.19 — TRAP MODE relabel. When on, the lane labels
        // become R CRASH→PAD, L CRASH→SYNTH, RIDE→PHRASE,
        // SMALL TOM/FLOOR TOM→PERC. The underlying MIDI notes do not
        // change — the kit slots they hit (China / Crash / Ride /
        // Mid-Tom / Low-Tom) are simply re-skinned by the Drocetti
        // bundle which puts pad / synth / phrase / perc samples in
        // those slots. Lane colours shift to a colder cyan/violet
        // trap palette so the user can see at a glance the strip is
        // no longer in rock mode.
        void setTrapMode (bool on)
        {
            if (trapModeOn == on) return;
            trapModeOn = on;
            repaint();
        }
        bool getTrapMode() const { return trapModeOn; }

        // v1.6.1-rc.19 — right-click on a lane label opens the per-
        // lane SAMPLE PICKER. The editor wires this to a juce::PopupMenu
        // that lists every layer in the active kit's slot for that lane,
        // plus an "Auto (velocity-driven)" option which clears the
        // override.
        std::function<void (int laneIdx, juce::Point<int> screenPos)>
            onLanePickerRequested;

        // v1.6.1-rc.28 — per-lane PIANO ROLL button. The user reported
        // they "cand find the piano roll to change the note of the
        // sample" — the rc.19 right-click sample picker was hidden
        // behind a right-click and they didn't discover it. This adds
        // a small ▦ icon next to each lane label that left-clicks open
        // a chromatic note picker for that lane (transpose / retune
        // the sample's trigger note). Same lane index ordering as
        // onLaneSelected: 0=R CRASH, 1=L CRASH, 2=RIDE, 3=HI-HAT,
        // 4=SMALL TOM, 5=FLOOR TOM, 6=SNARE, 7=KICK.
        std::function<void (int laneIdx, juce::Point<int> screenPos)>
            onLanePianoRollRequested;

        // v1.6.1-rc.28 — per-lane GLOBAL ERASE button (the small "0"
        // pill next to each lane label). Click → wipe THAT lane's
        // notes from EVERY region in the arrangement. Lane index
        // ordering matches onLaneSelected: 0=R CRASH, 1=L CRASH,
        // 2=RIDE, 3=HI-HAT, 4=SMALL TOM, 5=FLOOR TOM, 6=SNARE,
        // 7=KICK. Editor wires this to processor.eraseLaneInArrangement.
        std::function<void (int laneIdx)> onLaneZeroClicked;

        // v1.6.1-rc.28 — per-region per-lane ERASE button (the tiny
        // "0" pill stacked vertically along each region tile's left
        // edge). Click → wipe THAT lane's notes from JUST that one
        // region. Use case (user's words): "most songs start soft
        // with intros, so if i could zero out hihhats and snares it
        // would be great to build the arrangement as i go".
        std::function<void (int regionIdx, int laneIdx)>
            onRegionLaneZeroClicked;

        // v1.6.1-rc.10 — sub-beat click resolution. 16 = 1/16, 32 = 1/32,
        // 64 = 1/64. Drives the snap step inside handleAddNote so a
        // 1/64 step-div in the editor actually lets the user place a
        // note on every 1/64 cell instead of being floored to the
        // nearest quarter beat (rc.9 regression).
        void setStepsPerBar (int spb)
        {
            stepsPerBar = juce::jlimit (4, 64, spb);
            repaint();
        }
        int getStepsPerBar() const { return stepsPerBar; }

        // v1.5.0 — right-click (or alt-click) on a region tile calls this with
        // the region's index so the editor can remove it. Empty arrangement is
        // allowed; the `+` button becomes the only interactive element.
        std::function<void (int regionIndex)> onDeleteRegion;

        // v1.6.1-rc.14 — per-region INTENSITY drag-strip. Each region
        // tile shows a thin gold bar at its bottom edge that the user
        // can click+drag (left = soft / right = slammed) to set that
        // region's velocity vibe independently of the global INTENSITY
        // knob. Right-click on the strip clears the override (-1 = inherit
        // global). Pre-chorus soft → chorus slammed → bridge somber, all
        // without touching neighbouring regions. The editor wires this
        // to PluginProcessor::setRegionIntensity.
        std::function<void (int regionIndex, float intensity01)> onRegionIntensityChanged;

        // v1.6.1-rc.15 — fires when the user clicks the per-region
        // INTENSITY mini-knob in a region's header. The editor uses
        // this to "activate / select" that region for editing without
        // changing its intensity (clicking the knob also selects the
        // region; click+drag adjusts).
        std::function<void (int regionIndex)> onRegionIntensitySelected;

        // v1.6.1-rc.16 — per-region COPY / PASTE buttons. Each region
        // tile renders a tiny "C" and "P" button in its header next
        // to the intensity knob. Click "C" on region 1 to snapshot
        // its pattern, then click "P" on region 4 to overwrite that
        // region with the snapshot. Region 4's intensity override is
        // preserved (so pre-chorus / chorus / bridge dial-in survives).
        // The editor wires these to PluginProcessor::copyRegionToClipboard
        // and pasteCopiedRegionInto.
        std::function<void (int regionIndex)> onCopyRegion;
        std::function<void (int regionIndex)> onPasteRegion;

        // v1.6.1-rc.28 — fires when the user clicks the per-region
        // NUMBER badge in a region tile's top-left header. Editor
        // wires this to a popup menu that lets the user randomize /
        // compose-mold / clear / copy / paste / delete THAT specific
        // region in isolation. Replaces the old workflow where the
        // user had to nuke the whole arrangement to reshape a single
        // region. The point is passed in screen coordinates so the
        // popup can be anchored under the badge.
        std::function<void (int regionIndex, juce::Point<int> screenPos)>
            onRegionNumberClicked;

        // v1.6.1-rc.5 — step-sequencer toggle semantics. Left-click on a
        // drawn note immediately deletes it (same motion = toggle off).
        // Left-click on an empty grid cell drops a new note at that lane
        // and beat (toggle on). Ctrl+click still duplicates a clicked
        // note; right-click / alt-click on empty grid deletes the whole
        // region under the cursor.
        std::function<void (int regionIndex, int noteIndex)> onDeleteNote;
        // v1.6.1-rc.11 — batch delete for the drag-select multi-select.
        // Caller (PluginEditor) routes this to a single mutex-locked
        // PluginProcessor::deleteNotesInRegions so the arrangement
        // can't be mutated by deferred APVTS callbacks between
        // individual deletes (Devin Review caught this race in rc.11).
        std::function<void (std::vector<std::pair<int, int>>)> onDeleteNotes;
        std::function<void (int regionIndex, int noteIndex)> onDuplicateNote;
        // New in rc.5 — (regionIndex, localStartBeat, noteNumber, velocity)
        std::function<void (int regionIndex, double localStartBeat,
                            int noteNumber, float velocity)> onAddNote;

        void paint (juce::Graphics& g) override
        {
            // Reset the click-map; rebuilt below as notes are drawn so
            // mouseDown can hit-test individual hits.
            noteHits.clear();

            auto bounds = getLocalBounds().toFloat().reduced (0.5f);

            // Gothic panel
            g.setColour (juce::Colour (GothicPalette::kPanel));
            g.fillRoundedRectangle (bounds, 10.0f);
            g.setColour (juce::Colour (GothicPalette::kPanelEdge));
            g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

            auto inner = bounds.reduced (10.0f, 8.0f);
            const auto appendButton = getAppendButtonBounds (inner);
            inner = inner.withTrimmedRight (appendButton.getWidth() + 10.0f);

            cachedRegionOffsets.clear();
            cachedRegionIntensityHits.clear();
            cachedRegionIntensityKnobs.clear();
            cachedRegionCopyButtons.clear();
            cachedRegionPasteButtons.clear();
            cachedRegionNumberButtons.clear();
            cachedRegionLaneZeroButtons.clear();

            // v1.6.1-rc.2 — reserve a narrow column on the left for per-lane
            // labels (CRASH / RIDE / HI-HAT / TOM / SNARE / KICK). 6 lanes
            // now — RIDE is explicit instead of being lumped with crashes.
            // v1.6.1-rc.9 — widened from 54 to 76 so 'R CRASH' / 'L CRASH'
            // labels render their final 'H' (was clipped at 54).
            // v1.6.1-rc.28 — bumped 76 → 92 to make room for the
            // per-lane "0" / erase pill on the right edge of each
            // label cell, alongside the rc.28 piano-roll icon on
            // the left. Layout per lane row: [piano ▦ 14px][2px gap]
            // [label text 56px][2px gap][zero "0" 14px][4px right pad].
            // The handleRegionDelete mirror below was bumped to match.
            constexpr float kLabelColumn = 92.0f;
            constexpr float kHeaderRow   = 14.0f;
            auto labelArea = inner.removeFromLeft (kLabelColumn);
            inner.removeFromLeft (6.0f);                 // gap after labels
            auto headerArea = inner.removeFromTop (kHeaderRow);
            labelArea.removeFromTop (kHeaderRow);

            const double totalBeats = std::max (1.0, last.totalBeats);
            const float  pxPerBeat  = inner.getWidth() / (float) totalBeats;

            cachedGridInner = inner;
            cachedPxPerBeat = pxPerBeat;

            // --- v1.6.1-rc.8 lane definitions — 8 fixed rows, each a distinct
            // colour. Top → bottom: R CRASH, L CRASH, RIDE, HI-HAT,
            // SMALL TOM, FLOOR TOM, SNARE, KICK. Splits the rc.7 single
            // CRASH lane into L+R and the single TOM lane into Small+Floor
            // so the new (Nu Rock) 70's Yamaha kit's distinct one-shots get
            // their own dedicated lane.
            struct Lane { const char* label; juce::uint32 col; };
            // v1.6.1-rc.19 — two parallel lane palettes. Default = rock /
            // metal labels + warm rose-amber-teal hues; TRAP = pad / synth
            // / phrase / perc / 808 labels + colder cyan-violet hues so
            // the user sees at a glance which mode the strip is in.
            // Lane order is identical between the two so MIDI mapping
            // stays untouched.
            static const Lane kLanesDefault[8] = {
                { "R CRASH",   0xfff04f7e },  // deep rose (right-side)
                { "L CRASH",   0xffff8fa9 },  // pink rose  (left-side)
                { "RIDE",      0xff6ec6ff },  // sky blue
                { "HI-HAT",    0xffffc857 },  // amber
                { "SMALL TOM", 0xff9d7dff },  // lilac
                { "FLOOR TOM", 0xff7558d4 },  // purple
                { "SNARE",     0xffede7f6 },  // bone white
                { "KICK",      0xff3ee0c1 },  // teal
            };
            static const Lane kLanesTrap[8] = {
                { "PAD",       0xff8e7bff },  // electric violet
                { "SYNTH",     0xff52c8ff },  // bright cyan
                { "PHRASE",    0xff39e0c4 },  // mint
                { "HI-HAT",    0xffffc857 },  // amber (kept)
                { "PERC",      0xffff8fcb },  // hot pink
                { "PERC",      0xffd16bff },  // magenta (lower perc / shaker)
                { "SNARE",     0xffede7f6 },  // bone white (kept)
                { "808",       0xff3ee0c1 },  // teal (kick slot now 808-flavoured)
            };
            const Lane* kLanes = trapModeOn ? kLanesTrap : kLanesDefault;
            const int   kNumLanes = 8;
            const float laneH     = inner.getHeight() / (float) kNumLanes;

            auto laneFor = [] (int n) -> int
            {
                // GM-style mapping. Right Crash sits on note 57 (Crash 2)
                // plus China (52/55) since the new kit routes the right
                // crash sample into Kind::China; Left Crash on 49.
                // Floor Tom = LowTom (41/43/45); Small Tom = MidTom/HighTom
                // (47/48/50). Snare-family (37–40) and Kick (35/36) are
                // unchanged.
                if (n == 35 || n == 36)                                      return 7;
                if (n == 37 || n == 38 || n == 39 || n == 40)                return 6;
                if (n == 41 || n == 43 || n == 45)                           return 5; // FLOOR
                if (n == 47 || n == 48 || n == 50)                           return 4; // SMALL
                if (n == 42 || n == 44 || n == 46)                           return 3;
                if (n == 51 || n == 53 || n == 59)                           return 2;
                if (n == 49)                                                 return 1; // L CRASH
                return 0;  // 52/55/57 — china / right crash
            };

            // --- Lane labels + alternating lane bands ---------------------
            auto laneFont = juce::Font (juce::FontOptions (9.5f, juce::Font::italic));
            laneFont.setExtraKerningFactor (0.26f);
            g.setFont (laneFont);
            for (int i = 0; i < kNumLanes; ++i)
            {
                const float yTop = inner.getY() + (float) i * laneH;
                if ((i & 1) == 0)
                {
                    g.setColour (juce::Colour (GothicPalette::kInk).withAlpha (0.55f));
                    g.fillRect (juce::Rectangle<float> (inner.getX(), yTop,
                                                        inner.getWidth(), laneH));
                }

                // v1.6.1-rc.7 — cache the label rect so mouseDown can
                // hit-test it and fire onLaneSelected. The clickable
                // area covers the whole left label column for that row.
                // v1.6.1-rc.28 — reserve a 14 px ▦ icon column on the
                // LEFT of the label so the user has a visible affordance
                // for the per-lane piano roll. The label text shifts
                // right by `kPianoBtnW` and gets a slightly narrower
                // right-aligned area (the rest of the rules — text
                // colour, ghost greying, sample-picker right-click —
                // continue to apply to the WHOLE row including the
                // button column, so right-clicking the icon still
                // opens the sample picker the same way as before).
                constexpr float kPianoBtnW = 14.0f;
                const auto pianoBtnRect = juce::Rectangle<float> (
                    labelArea.getX() + 2.0f, yTop + (laneH - kPianoBtnW) * 0.5f,
                    kPianoBtnW, kPianoBtnW);
                cachedLanePianoButtons[(size_t) i] = pianoBtnRect;

                // v1.6.1-rc.28 — "0" / erase pill on the right edge
                // of each lane label. 14×14 dark-rose pill with a
                // centered "0". Click → wipe lane's notes from ALL
                // regions. Reserve its column from the labelRect so
                // the text still right-aligns cleanly.
                constexpr float kZeroBtnW = 14.0f;
                const auto zeroBtnRect = juce::Rectangle<float> (
                    labelArea.getRight() - 4.0f - kZeroBtnW,
                    yTop + (laneH - kZeroBtnW) * 0.5f,
                    kZeroBtnW, kZeroBtnW);
                cachedLaneZeroButtons[(size_t) i] = zeroBtnRect;

                const auto labelRect = juce::Rectangle<float> (
                    labelArea.getX() + 2.0f + kPianoBtnW + 2.0f, yTop,
                    labelArea.getWidth() - 6.0f - kPianoBtnW - 2.0f
                        - kZeroBtnW - 4.0f,
                    laneH);
                cachedLabelRects[(size_t) i] = labelRect;

                // v1.6.1-rc.7 — selection ring around whichever lane the
                // user has armed for the GHOST button. Sits behind the
                // text so it reads as a soft halo.
                if (i == selectedLaneIdx)
                {
                    g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.12f));
                    g.fillRoundedRectangle (labelRect.reduced (1.0f), 3.0f);
                    g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.6f));
                    g.drawRoundedRectangle (labelRect.reduced (1.0f), 3.0f, 1.0f);
                }

                // v1.6.1-rc.7 — when this lane's bit is set in
                // ghostMask, the row name greys out so the user can see
                // at a glance which lanes the GHOST button has flipped.
                const bool isGhost = (ghostMask & (1 << i)) != 0;
                const auto labelCol = isGhost
                    ? juce::Colour (GothicPalette::kMuted).withAlpha (0.85f)
                    : juce::Colour (kLanes[i].col).withAlpha (0.95f);
                g.setColour (labelCol);
                g.drawText (kLanes[i].label, labelRect,
                            juce::Justification::centredRight, false);

                // v1.6.1-rc.28 — per-lane PIANO ROLL icon. Three thin
                // horizontal lines stacked over a vertical hairline,
                // shaped like a tiny step-grid mini-icon. Painted in
                // the same colour as the lane label (greys out with
                // ghost) so the user reads it as part of the lane
                // group. The whole 14×14 rect is the click target.
                {
                    auto col = labelCol.withAlpha (0.78f);
                    g.setColour (col);
                    const float pad = 2.0f;
                    const auto inside = pianoBtnRect.reduced (pad);
                    // 4 vertical "keys" arranged like a piano roll grid
                    const float keyW = inside.getWidth() / 4.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        const float x = inside.getX() + (float) k * keyW;
                        g.drawRect (juce::Rectangle<float> (
                            x, inside.getY(), keyW, inside.getHeight()), 0.6f);
                        // shaded "black-key" column at every odd index
                        if ((k & 1) != 0)
                        {
                            g.setColour (col.withAlpha (0.35f));
                            g.fillRect (juce::Rectangle<float> (
                                x + 0.6f, inside.getY() + 0.6f,
                                keyW - 1.2f, inside.getHeight() * 0.55f));
                            g.setColour (col);
                        }
                    }
                    // hover/halo: faint outline around the rect so the
                    // affordance is obvious.
                    g.setColour (col.withAlpha (0.25f));
                    g.drawRoundedRectangle (pianoBtnRect.reduced (0.5f),
                                            2.0f, 0.6f);
                }

                // v1.6.1-rc.28 — per-lane "0" / GLOBAL ERASE pill.
                // Dark-rose fill + brighter "0" so it reads as a
                // destructive action without screaming. Click wipes
                // this lane from EVERY region (per-region wipe is the
                // tiny "0" pill on each region tile's left edge).
                {
                    juce::Colour eraseAccent (0xffe25b6c); // muted rose
                    g.setColour (juce::Colour (GothicPalette::kInk).withAlpha (0.92f));
                    g.fillRoundedRectangle (zeroBtnRect, 3.0f);
                    g.setColour (eraseAccent.withAlpha (isGhost ? 0.45f : 0.85f));
                    g.drawRoundedRectangle (zeroBtnRect, 3.0f, 0.9f);
                    auto zf = juce::Font (juce::FontOptions (10.5f));
                    zf.setExtraKerningFactor (0.04f);
                    g.setFont (zf);
                    g.setColour (eraseAccent.withAlpha (isGhost ? 0.55f : 0.95f));
                    g.drawText ("0", zeroBtnRect,
                                juce::Justification::centred, false);
                }

                // lane divider tick inside the note area
                g.setColour (juce::Colour (kLanes[i].col).withAlpha (0.10f));
                g.drawLine (inner.getX(), yTop, inner.getRight(), yTop, 0.6f);
            }

            // --- Background beat grid (Logic-style 1/4-note cells) --------
            // Alternating cell shading every quarter-note so the grid
            // reads as discrete beat cells at a glance, with brighter
            // vertical lines on each beat and bars.
            const int totalBeatsCeil = (int) std::ceil (totalBeats);
            for (int b = 0; b < totalBeatsCeil; ++b)
            {
                if ((b & 1) == 0)
                {
                    const float x0 = inner.getX() + (float) b * pxPerBeat;
                    const float x1 = inner.getX() + (float) (b + 1) * pxPerBeat;
                    g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.08f));
                    g.fillRect (juce::Rectangle<float> (x0, inner.getY(),
                                                        x1 - x0, inner.getHeight()));
                }
            }
            g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.55f));
            for (int b = 0; b <= totalBeatsCeil; ++b)
            {
                const float x = inner.getX() + (float) b * pxPerBeat;
                g.drawLine (x, inner.getY(), x, inner.getBottom(),
                            (b % 4 == 0) ? 1.3f : 0.7f);
            }

            // --- Render each region end-to-end + vertical dividers --------
            double regionOffset = 0.0;
            for (size_t i = 0; i < last.regions.size(); ++i)
            {
                const auto&  region    = last.regions[i];
                const double regionLen = std::max (0.001, region.lengthInBeats);
                const float  regionX0  = inner.getX()
                                         + (float) regionOffset * pxPerBeat;

                cachedRegionOffsets.push_back (regionOffset);

                // v1.6.1-rc.15 — per-region INTENSITY mini-knob in the
                // top-left header of EVERY region tile, painted right
                // next to the region number. The bottom drag-strip is
                // still there but the user said it wasn't visible enough,
                // so we now render a real knob too. Vertical-drag = set
                // 0..1, right-click clears (region inherits global
                // INTENSITY). The number is always shown (even on a
                // single region) so the knob has a label next to it.
                {
                    // v1.6.1-rc.28 — region NUMBER is now a clickable
                    // button-styled badge. User clicks "1" / "2" / etc.
                    // → editor opens a per-region edit popup (randomize
                    // / compose / clear / copy / paste / delete THIS
                    // region, no need to nuke the whole arrangement).
                    // Drawn as a small dark pill with the region index
                    // centered, gold border so it reads as interactive.
                    juce::Rectangle<float> numBadge (
                        regionX0 + 2.0f, inner.getY() + 1.5f, 16.0f, 13.0f);
                    cachedRegionNumberButtons.push_back ({ (int) i, numBadge });

                    g.setColour (juce::Colour (GothicPalette::kInk).withAlpha (0.92f));
                    g.fillRoundedRectangle (numBadge, 3.0f);
                    g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (0.75f));
                    g.drawRoundedRectangle (numBadge, 3.0f, 0.9f);

                    auto lf = juce::Font (juce::FontOptions (9.5f));
                    lf.setExtraKerningFactor (0.18f);
                    g.setFont (lf);
                    g.setColour (juce::Colour (GothicPalette::kAccent));
                    g.drawText (juce::String ((int) i + 1),
                                numBadge,
                                juce::Justification::centred, false);

                    // v1.6.1-rc.28 — per-region per-lane "0" / ERASE
                    // pills. Tiny 8×8 rose pill on the LEFT edge of
                    // each region tile, vertically aligned with each
                    // lane row Y. Lane 0 (R CRASH) row's pill is
                    // shifted right past the number badge so the two
                    // don't overlap. Click → wipe that lane's notes
                    // from THIS region only. Cached for the
                    // mouseDown hit-test below.
                    {
                        const float laneHForRegion =
                            inner.getHeight() / 8.0f;
                        constexpr float kRegionZeroSz = 9.0f;
                        for (int laneRow = 0; laneRow < 8; ++laneRow)
                        {
                            const float laneTop =
                                inner.getY() + laneRow * laneHForRegion;
                            // Lane 0 pill drops just below the number
                            // badge so it doesn't fight the badge's
                            // 16×13 rect; lanes 1-7 center vertically
                            // in their lane row at the region's left
                            // edge.
                            const float pillX = (laneRow == 0)
                                ? regionX0 + 20.0f
                                : regionX0 + 2.0f;
                            const float pillY = laneTop
                                + (laneHForRegion - kRegionZeroSz) * 0.5f;
                            juce::Rectangle<float> pill (
                                pillX, pillY, kRegionZeroSz, kRegionZeroSz);
                            cachedRegionLaneZeroButtons.push_back (
                                { (int) i, laneRow, pill });

                            juce::Colour eraseAccent (0xffe25b6c);
                            g.setColour (juce::Colour (GothicPalette::kInk)
                                            .withAlpha (0.86f));
                            g.fillRoundedRectangle (pill, 2.0f);
                            g.setColour (eraseAccent.withAlpha (0.7f));
                            g.drawRoundedRectangle (pill, 2.0f, 0.7f);
                            auto zf = juce::Font (juce::FontOptions (8.0f));
                            g.setFont (zf);
                            g.setColour (eraseAccent.withAlpha (0.92f));
                            g.drawText ("0", pill,
                                        juce::Justification::centred, false);
                        }
                    }

                    // Mini intensity knob — 18 px square, sits flush with
                    // the region number in the same header band.
                    const float knobSize = 18.0f;
                    juce::Rectangle<float> knob (regionX0 + 18.0f,
                                                 inner.getY() + 1.0f,
                                                 knobSize, knobSize);
                    if (knob.getRight() < inner.getX() + (float) (regionOffset + regionLen) * pxPerBeat - 2.0f)
                    {
                        cachedRegionIntensityKnobs.push_back ({ (int) i, knob });

                        const float ri = region.regionIntensity;
                        const float v01 = ri >= 0.0f ? juce::jlimit (0.0f, 1.0f, ri) : 0.5f;
                        const auto centre = knob.getCentre();
                        const float radius = knobSize * 0.42f;

                        // Backplate
                        g.setColour (juce::Colour (GothicPalette::kInk).withAlpha (0.85f));
                        g.fillEllipse (knob.reduced (1.0f));

                        // Track arc 5/4 turn (-135° → +135°)
                        const float a0 = juce::degreesToRadians (-135.0f);
                        const float a1 = juce::degreesToRadians ( 135.0f);
                        const float aV = a0 + (a1 - a0) * v01;

                        juce::Path track;
                        track.addCentredArc (centre.x, centre.y, radius, radius,
                                             0.0f, a0, a1, true);
                        g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.85f));
                        g.strokePath (track, juce::PathStrokeType (1.6f));

                        // Active arc — gold when override set, dim bronze
                        // when inheriting the global INTENSITY.
                        juce::Path active;
                        active.addCentredArc (centre.x, centre.y, radius, radius,
                                              0.0f, a0, aV, true);
                        const auto goldHi = juce::Colour (GothicPalette::kAccent);
                        const auto goldLo = juce::Colour (GothicPalette::kAccentDeep);
                        if (ri >= 0.0f)
                            g.setColour (goldHi);
                        else
                            g.setColour (juce::Colour (GothicPalette::kMuted).withAlpha (0.55f));
                        g.strokePath (active, juce::PathStrokeType (1.8f));

                        // Tick mark
                        const float tx0 = centre.x + std::cos (aV) * radius * 0.40f;
                        const float ty0 = centre.y + std::sin (aV) * radius * 0.40f;
                        const float tx1 = centre.x + std::cos (aV) * radius * 0.95f;
                        const float ty1 = centre.y + std::sin (aV) * radius * 0.95f;
                        g.setColour (ri >= 0.0f
                                     ? juce::Colour (GothicPalette::kBone).withAlpha (0.95f)
                                     : juce::Colour (GothicPalette::kSilver).withAlpha (0.55f));
                        g.drawLine (tx0, ty0, tx1, ty1, 1.4f);

                        // Hairline ring outside the active arc
                        g.setColour (goldLo.withAlpha (0.55f));
                        g.drawEllipse (knob.reduced (1.0f), 0.6f);
                    }

                    // v1.6.1-rc.16 — per-region COPY + PASTE buttons.
                    // Two 14×14 gold-rimmed pills sit immediately to
                    // the right of the intensity knob in the same
                    // header band. "C" snapshots that region, "P"
                    // pastes the snapshot into that region (replacing
                    // its pattern but preserving its INTENSITY
                    // override). Only render if there's room in the
                    // region tile so 1-bar regions don't smear.
                    const float btnW   = 14.0f;
                    const float btnH   = 14.0f;
                    const float btnGap = 2.0f;
                    const float btnX0  = regionX0 + 18.0f + 18.0f + 2.0f; // after knob
                    const float regionRight = inner.getX()
                                              + (float) (regionOffset + regionLen) * pxPerBeat;
                    juce::Rectangle<float> copyRect (btnX0,
                                                      inner.getY() + 2.0f,
                                                      btnW, btnH);
                    juce::Rectangle<float> pasteRect (btnX0 + btnW + btnGap,
                                                       inner.getY() + 2.0f,
                                                       btnW, btnH);
                    if (pasteRect.getRight() < regionRight - 2.0f)
                    {
                        cachedRegionCopyButtons.push_back ({ (int) i, copyRect });
                        cachedRegionPasteButtons.push_back ({ (int) i, pasteRect });

                        auto paintPill = [&] (juce::Rectangle<float> r,
                                              const juce::String& label,
                                              bool armed)
                        {
                            // Backplate
                            g.setColour (juce::Colour (GothicPalette::kInk).withAlpha (0.85f));
                            g.fillRoundedRectangle (r, 3.0f);
                            // Gold rim — brighter when armed (PASTE armed = clipboard set)
                            g.setColour (armed
                                         ? juce::Colour (GothicPalette::kAccent)
                                         : juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.85f));
                            g.drawRoundedRectangle (r, 3.0f, 0.9f);
                            // Letter
                            auto bf = juce::Font (juce::FontOptions (8.5f, juce::Font::bold));
                            bf.setExtraKerningFactor (0.05f);
                            g.setFont (bf);
                            g.setColour (armed
                                         ? juce::Colour (GothicPalette::kBone)
                                         : juce::Colour (GothicPalette::kBone).withAlpha (0.75f));
                            g.drawText (label, r, juce::Justification::centred, false);
                        };

                        paintPill (copyRect,  "C", true);
                        // v1.6.1-rc.16 — PASTE only lights up when the
                        // processor's clipboard actually has a region.
                        paintPill (pasteRect, "P", last.clipboardHasContent);
                    }
                }

                // Draw notes — each lane uses its own colour so KICK / SNARE /
                // TOM / HI-HAT / CRASH are distinguishable at a glance.
                for (const auto& note : region.notes)
                {
                    const float x = regionX0
                                  + (float) (note.startBeat / regionLen)
                                        * (float) regionLen * pxPerBeat;
                    const float w = std::max (3.0f,
                                              (float) (std::max (note.lengthBeat, 0.1) * pxPerBeat));
                    const int   lane = laneFor (note.noteNumber);
                    const float y    = inner.getY() + (float) lane * laneH + laneH * 0.22f;
                    const float h    = laneH * 0.56f;

                    const float v    = juce::jlimit (0.15f, 1.0f, note.velocity);
                    const auto  base = juce::Colour (kLanes[lane].col);

                    // Pad the hit-box horizontally so short hits (w~3px)
                    // are still easy to click.
                    const juce::Rectangle<float> drawRect (x, y, w, h);
                    const juce::Rectangle<float> hitRect  (x - 2.0f, y - 2.0f,
                                                           std::max (8.0f, w + 4.0f),
                                                           h + 4.0f);
                    const int  noteIdx      = (int) (&note - region.notes.data());
                    const bool isSingleSel  = (selectedRegion == (int) i && selectedNote == noteIdx);
                    bool       isMultiSel   = false;
                    for (const auto& [rs, ns] : selectedNotes)
                        if (rs == (int) i && ns == noteIdx) { isMultiSel = true; break; }
                    const bool isSelected   = isSingleSel || isMultiSel;
                    noteHits.push_back ({ (int) i, noteIdx, hitRect });

                    // soft outer glow
                    g.setColour (base.withAlpha (0.12f + 0.30f * v));
                    g.fillRoundedRectangle (
                        juce::Rectangle<float> (x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f), 4.0f);
                    // solid block, brightness scales with velocity
                    g.setColour (base.withAlpha (0.55f + 0.45f * v));
                    g.fillRoundedRectangle (drawRect, 3.0f);
                    // thin highlight top edge for a 3-D-ish feel
                    g.setColour (base.withAlpha (0.25f + 0.30f * v).brighter (0.4f));
                    g.drawLine (x + 1.0f, y + 0.5f, x + w - 1.0f, y + 0.5f, 1.0f);

                    if (isSelected)
                    {
                        g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.95f));
                        g.drawRoundedRectangle (drawRect.expanded (1.5f), 4.0f, 1.8f);
                    }
                }

                // v1.6.1-rc.3 — when HIGHLIGHT ALL is armed, draw a bright
                // border around every region tile so it's visually obvious
                // that the full arrangement is what gets dragged to the DAW.
                if (highlightAll)
                {
                    const float x0 = regionX0;
                    const float x1 = inner.getX() + (float) (regionOffset + regionLen) * pxPerBeat;
                    g.setColour (juce::Colour (GothicPalette::kAccentSoft).withAlpha (0.28f));
                    g.fillRect (juce::Rectangle<float> (x0, inner.getY(),
                                                        x1 - x0, inner.getHeight()));
                    g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (0.95f));
                    g.drawRect (juce::Rectangle<float> (x0, inner.getY(),
                                                        x1 - x0, inner.getHeight()), 1.8f);
                }

                // v1.6.1-rc.14 — per-region INTENSITY drag-strip. A 6 px
                // gold bar pinned to the bottom edge of every region tile.
                // Width fills with the region's intensity 0..1; an unset
                // region (regionIntensity < 0) shows a thin neutral track
                // so the user can see "this region inherits the global
                // INTENSITY". Click+drag the strip to dial in pre-chorus
                // soft / chorus slammed / bridge somber. Right-click on
                // the strip clears the override.
                {
                    const float regionX1   = inner.getX()
                                              + (float) (regionOffset + regionLen) * pxPerBeat;
                    const float stripH     = 6.0f;
                    const float stripPadY  = 1.5f;
                    const float stripPadX  = 2.0f;
                    juce::Rectangle<float> strip (regionX0 + stripPadX,
                                                  inner.getBottom() - stripH - stripPadY,
                                                  juce::jmax (10.0f,
                                                              regionX1 - regionX0
                                                              - stripPadX * 2.0f),
                                                  stripH);

                    cachedRegionIntensityHits.push_back ({ (int) i, strip });

                    // Track (always shown, dim).
                    g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.55f));
                    g.fillRoundedRectangle (strip, 2.0f);

                    const float ri = region.regionIntensity;
                    if (ri >= 0.0f)
                    {
                        // Filled portion = current intensity. Gold gradient
                        // tracks dial position — dark at 0, bright at 1.
                        const float fill01 = juce::jlimit (0.0f, 1.0f, ri);
                        const auto fillRect = strip.withWidth (strip.getWidth() * fill01);
                        const auto goldDeep = juce::Colour (GothicPalette::kAccentDeep);
                        const auto gold     = juce::Colour (GothicPalette::kAccent);
                        g.setGradientFill (juce::ColourGradient (
                            goldDeep.withAlpha (0.9f), strip.getX(), strip.getY(),
                            gold    .withAlpha (0.95f), strip.getRight(), strip.getY(),
                            false));
                        g.fillRoundedRectangle (fillRect, 2.0f);

                        // Thumb tick
                        g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.95f));
                        const float thumbX = strip.getX() + fillRect.getWidth();
                        g.drawLine (thumbX, strip.getY() - 1.0f,
                                    thumbX, strip.getBottom() + 1.0f, 1.6f);
                    }
                    else
                    {
                        // Sentinel: dotted line + "GLOBAL" caption so the
                        // user can see this region inherits the global
                        // INTENSITY knob.
                        g.setColour (juce::Colour (GothicPalette::kMuted).withAlpha (0.55f));
                        for (float x = strip.getX(); x < strip.getRight(); x += 4.0f)
                            g.fillRect (juce::Rectangle<float> (x, strip.getCentreY() - 0.6f,
                                                                2.0f, 1.2f));
                    }

                    // Frame
                    g.setColour (juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.55f));
                    g.drawRoundedRectangle (strip, 2.0f, 0.7f);
                }

                regionOffset += regionLen;

                // Divider line between regions
                if (i + 1 < last.regions.size())
                {
                    const float x = inner.getX() + (float) regionOffset * pxPerBeat;
                    g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (0.55f));
                    g.drawLine (x, inner.getY(), x, inner.getBottom(), 1.5f);
                }
            }

            // --- v1.6.1-rc.7 hover-drag selection rectangle ----------------
            // Painted after notes / before the playhead so the rectangle
            // highlights the underlying notes without obscuring the cursor.
            if (! selectionRect.isEmpty())
            {
                const auto sel = selectionRect.getIntersection (inner);
                if (sel.getWidth() > 0.5f && sel.getHeight() > 0.5f)
                {
                    g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.10f));
                    g.fillRect (sel);
                    g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.55f));
                    g.drawRect (sel, 1.0f);
                }
            }

            // --- Playhead ---------------------------------------------------
            if (last.totalBeats > 0.0)
            {
                const float phx = inner.getX()
                                + (float) (last.playheadBeats / last.totalBeats)
                                      * inner.getWidth();
                // Glow
                g.setColour (juce::Colour (GothicPalette::kAccentSoft).withAlpha (0.20f));
                g.fillRect (juce::Rectangle<float> (phx - 2.0f, inner.getY(),
                                                    4.0f, inner.getHeight()));
                g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.9f));
                g.drawLine (phx, inner.getY(), phx, inner.getBottom(), 1.2f);
            }

            // Header label
            g.setColour (juce::Colour (GothicPalette::kMuted));
            auto f = juce::Font (juce::FontOptions (10.0f));
            f.setExtraKerningFactor (0.28f);
            g.setFont (f);
            const juce::String header = last.regions.empty()
                ? juce::String ("ARRANGEMENT")
                : ("ARRANGEMENT   "
                   + juce::String ((int) last.regions.size())
                   + ((last.regions.size() == 1) ? " REGION" : " REGIONS"));
            g.drawText (header, bounds.reduced (12.0f, 4.0f),
                        juce::Justification::topLeft, false);

            // Logic-style append button pinned to the far right of the strip.
            const auto btnCentre = appendButton.getCentre();
            const float btnR = appendButton.getWidth() * 0.5f;
            g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (0.12f));
            g.fillEllipse (appendButton.expanded (6.0f));
            g.setColour (juce::Colour (GothicPalette::kAccentDeep).brighter (0.15f));
            g.fillEllipse (appendButton);
            g.setColour (juce::Colour (GothicPalette::kAccentSoft));
            g.drawEllipse (appendButton, 1.2f);
            g.setColour (juce::Colour (GothicPalette::kBone));
            g.fillRoundedRectangle (btnCentre.x - btnR * 0.45f, btnCentre.y - 1.25f,
                                    btnR * 0.9f, 2.5f, 1.0f);
            g.fillRoundedRectangle (btnCentre.x - 1.25f, btnCentre.y - btnR * 0.45f,
                                    2.5f, btnR * 0.9f, 1.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // v1.6.1-rc.16 — per-region COPY / PASTE buttons. Tested
            // FIRST so the C / P pills in the region header always
            // win over the intensity knob and the underlying grid.
            for (const auto& hit : cachedRegionCopyButtons)
            {
                if (! hit.rect.contains (e.position))
                    continue;
                if (onCopyRegion != nullptr)
                    onCopyRegion (hit.regionIdx);
                repaint();
                return;
            }
            for (const auto& hit : cachedRegionPasteButtons)
            {
                if (! hit.rect.contains (e.position))
                    continue;
                if (onPasteRegion != nullptr)
                    onPasteRegion (hit.regionIdx);
                repaint();
                return;
            }

            // v1.6.1-rc.28 — per-region NUMBER badge hit-test. Sits
            // immediately to the LEFT of the intensity knob, so test
            // it first. Click → onRegionNumberClicked → editor opens
            // the per-region edit popup. Right-click ALSO opens the
            // popup (some users right-click out of habit on numbered
            // tiles; better to surface the same menu than to fall
            // through to deleteRegion).
            // v1.6.1-rc.28 — per-region per-lane "0" pills tested
            // BEFORE the region-number badge so a click on lane 0's
            // pill (which sits to the right of the badge) doesn't
            // fall through to the badge. Other lanes have the pill
            // on the region's left edge, well clear of the badge.
            for (const auto& hit : cachedRegionLaneZeroButtons)
            {
                if (! hit.rect.contains (e.position))
                    continue;
                if (onRegionLaneZeroClicked != nullptr)
                    onRegionLaneZeroClicked (hit.regionIdx, hit.laneIdx);
                return;
            }

            for (const auto& hit : cachedRegionNumberButtons)
            {
                if (! hit.rect.contains (e.position))
                    continue;
                if (onRegionNumberClicked != nullptr)
                    onRegionNumberClicked (hit.regionIdx, e.getScreenPosition());
                return;
            }

            // v1.6.1-rc.15 — per-region INTENSITY mini-knob hit-test.
            // Tested AFTER the COPY/PASTE pills (which sit beside the
            // knob) so the buttons win their tiny rectangles. Vertical
            // drag = set 0..1 (drag up to brighten); right-click clears.
            for (const auto& hit : cachedRegionIntensityKnobs)
            {
                if (! hit.rect.contains (e.position))
                    continue;
                if (e.mods.isRightButtonDown() || e.mods.isAltDown())
                {
                    if (onRegionIntensityChanged != nullptr)
                        onRegionIntensityChanged (hit.regionIdx, -1.0f);
                    draggingRegionIntensity = -1;
                    draggingKnobAnchorY     = -1.0f;
                    draggingKnobAnchorVal   =  0.0f;
                    repaint();
                    return;
                }
                draggingRegionIntensity = hit.regionIdx;
                draggingKnobAnchorY     = e.position.y;
                draggingKnobAnchorVal   = (currentRegion (hit.regionIdx).regionIntensity >= 0.0f
                                           ? currentRegion (hit.regionIdx).regionIntensity
                                           : 0.5f);
                draggingKnobIsKnob      = true;
                if (onRegionIntensitySelected != nullptr)
                    onRegionIntensitySelected (hit.regionIdx);
                repaint();
                return;
            }

            // v1.6.1-rc.14 — per-region INTENSITY drag-strip hit-test.
            // Tested before lane labels and note-hits so the gold strip
            // wins over the underlying note grid. Right-click clears
            // the override (region inherits global INTENSITY).
            for (const auto& hit : cachedRegionIntensityHits)
            {
                if (! hit.rect.contains (e.position))
                    continue;
                if (e.mods.isRightButtonDown() || e.mods.isAltDown())
                {
                    if (onRegionIntensityChanged != nullptr)
                        onRegionIntensityChanged (hit.regionIdx, -1.0f);
                    draggingRegionIntensity = -1;
                    draggingKnobIsKnob      = false;
                    repaint();
                    return;
                }
                draggingRegionIntensity = hit.regionIdx;
                draggingKnobIsKnob      = false;
                const float frac = juce::jlimit (0.0f, 1.0f,
                    (e.position.x - hit.rect.getX()) / juce::jmax (1.0f, hit.rect.getWidth()));
                if (onRegionIntensityChanged != nullptr)
                    onRegionIntensityChanged (hit.regionIdx, frac);
                repaint();
                return;
            }

            // v1.6.1-rc.28 — per-lane PIANO ROLL icon click. Hit-test
            // these BEFORE the broader label rect so the icon column
            // takes priority over the GHOST-arm gesture. Left-click
            // opens the chromatic note picker; right-click falls back
            // to the SAMPLE PICKER (same as right-click on the label).
            // v1.6.1-rc.28 — per-lane GLOBAL "0" / erase pill hit-test.
            // Sits on the right edge of each lane label cell. Click →
            // wipe lane across whole arrangement. Test before label /
            // piano-roll buttons because the pill is inside the same
            // labelArea row.
            for (int i = 0; i < (int) cachedLaneZeroButtons.size(); ++i)
            {
                if (cachedLaneZeroButtons[(size_t) i].contains (e.position))
                {
                    if (onLaneZeroClicked != nullptr)
                        onLaneZeroClicked (i);
                    return;
                }
            }

            for (int i = 0; i < (int) cachedLanePianoButtons.size(); ++i)
            {
                if (cachedLanePianoButtons[(size_t) i].contains (e.position))
                {
                    if (e.mods.isPopupMenu() && onLanePickerRequested != nullptr)
                    {
                        const auto screenPos = localPointToGlobal (e.position.toInt());
                        onLanePickerRequested (i, screenPos);
                        return;
                    }
                    if (onLanePianoRollRequested != nullptr)
                    {
                        const auto screenPos = localPointToGlobal (e.position.toInt());
                        onLanePianoRollRequested (i, screenPos);
                        return;
                    }
                }
            }

            // v1.6.1-rc.7 — lane label click: arms a row for the GHOST
            // button. Hit-tested before notes/grid so labels can never
            // accidentally drop a kick on bar 1.
            // v1.6.1-rc.19 — right-click (or ctrl+click on macOS) on a
            // lane label opens the per-lane SAMPLE PICKER popup instead
            // of arming GHOST.
            for (int i = 0; i < (int) cachedLabelRects.size(); ++i)
            {
                if (cachedLabelRects[(size_t) i].contains (e.position))
                {
                    if (e.mods.isPopupMenu() && onLanePickerRequested != nullptr)
                    {
                        const auto screenPos = localPointToGlobal (
                            e.position.toInt());
                        onLanePickerRequested (i, screenPos);
                        return;
                    }
                    selectedLaneIdx = i;
                    if (onLaneSelected != nullptr) onLaneSelected (i);
                    repaint();
                    return;
                }
            }

            // v1.6.1-rc.7 / rc.11 — Shift+drag (or middle button) starts
            // a rectangular selection across the grid. rc.11 brief asked
            // for "click-and-drag highlighting to target one note OR a
            // whole arrangement". Selection now captures every note
            // whose hit-rect intersects the rectangle, and Delete /
            // Backspace removes them all.
            if ((e.mods.isShiftDown() || e.mods.isMiddleButtonDown())
                && cachedGridInner.contains (e.position))
            {
                selecting       = true;
                selectionAnchor = e.position;
                selectionRect   = juce::Rectangle<float> (e.position, e.position);
                selectedNotes.clear();
                repaint();
                return;
            }
            // Plain click outside any note clears any prior selection.
            if (! selectedNotes.empty())
            {
                bool overNote = false;
                for (const auto& nh : noteHits)
                    if (nh.rect.contains (e.position)) { overNote = true; break; }
                if (! overNote)
                {
                    selectedNotes.clear();
                    selectionRect = {};
                    repaint();
                }
            }

            // v1.6.1-rc.5 — step-sequencer toggle. Left-click on a drawn
            // note deletes it; left-click on an empty grid cell drops a
            // new note at that lane / beat. Ctrl+click duplicates.
            for (auto it = noteHits.rbegin(); it != noteHits.rend(); ++it)
            {
                if (! it->rect.contains (e.position))
                    continue;

                if (e.mods.isCtrlDown() || e.mods.isCommandDown())
                {
                    if (onDuplicateNote != nullptr)
                        onDuplicateNote (it->regionIdx, it->noteIdx);
                    repaint();
                    return;
                }
                // Left-click (or right/alt): delete, and enter erase-drag mode.
                if (onDeleteNote != nullptr)
                    onDeleteNote (it->regionIdx, it->noteIdx);
                selectedRegion = -1;
                selectedNote   = -1;
                dragMode = DragMode::Erase;
                repaint();
                return;
            }

            // No note hit — fall back to either paint-a-note (left-click)
            // or delete-the-region (right/alt-click).
            selectedRegion = -1;
            selectedNote   = -1;

            const auto inner = getLocalBounds().toFloat().reduced (10.5f, 8.5f);
            if (e.mods.isRightButtonDown() || e.mods.isAltDown())
            {
                handleRegionDelete (e.position, inner);
            }
            else if (onAddNote != nullptr)
            {
                if (handleAddNote (e.position, /*suppressDuplicates*/ false))
                    dragMode = DragMode::Paint;
            }
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // v1.6.1-rc.14/15 — per-region INTENSITY drag continues.
            // If we latched on the mini-knob (rc.15) use vertical drag
            // — drag UP brightens. If we latched on the bottom strip
            // (rc.14) use horizontal drag.
            if (draggingRegionIntensity >= 0)
            {
                if (draggingKnobIsKnob)
                {
                    const float dy = draggingKnobAnchorY - e.position.y; // up = +
                    const float frac = juce::jlimit (0.0f, 1.0f,
                                                     draggingKnobAnchorVal + dy / 90.0f);
                    if (onRegionIntensityChanged != nullptr)
                        onRegionIntensityChanged (draggingRegionIntensity, frac);
                    repaint();
                    return;
                }

                for (const auto& hit : cachedRegionIntensityHits)
                {
                    if (hit.regionIdx != draggingRegionIntensity)
                        continue;
                    const float frac = juce::jlimit (0.0f, 1.0f,
                        (e.position.x - hit.rect.getX())
                            / juce::jmax (1.0f, hit.rect.getWidth()));
                    if (onRegionIntensityChanged != nullptr)
                        onRegionIntensityChanged (hit.regionIdx, frac);
                    repaint();
                    return;
                }
                return;
            }

            // v1.6.1-rc.7 / rc.11 — rectangular drag-highlight on the
            // arrangement grid. Tracks the cursor in either direction
            // (L→R or R→L), re-paints a translucent overlay, AND
            // captures every note whose hit-rect intersects the
            // rectangle. Captured notes are highlighted (paint()) and
            // can be deleted as a group via Delete/Backspace.
            if (selecting)
            {
                selectionRect = juce::Rectangle<float> (selectionAnchor, e.position);
                selectedNotes.clear();
                for (const auto& nh : noteHits)
                    if (selectionRect.intersects (nh.rect))
                        selectedNotes.emplace_back (nh.regionIdx, nh.noteIdx);
                repaint();
                return;
            }

            // v1.6.1-rc.5 — drag to paint or erase across cells. The
            // dragMode latched in mouseDown decides whether we add or
            // delete as the cursor visits new grid cells.
            if (dragMode == DragMode::None) return;

            if (dragMode == DragMode::Paint)
            {
                // Only paint if the cursor is over an empty cell right now.
                for (auto it = noteHits.rbegin(); it != noteHits.rend(); ++it)
                    if (it->rect.contains (e.position))
                        return;
                handleAddNote (e.position, /*suppressDuplicates*/ true);
                return;
            }

            // Erase mode — delete any note the cursor is now over. Because
            // JUCE's repaint() is asynchronous, `noteHits` (built during the
            // last paint) still contains entries for notes that have already
            // been deleted earlier in this same drag. Their stored noteIdx
            // values no longer match the region's notes vector after it has
            // been shifted down, so we clear the whole map after each
            // successful delete and wait for the next paint to rebuild it
            // with correct indices — this turns multi-note drag-erase into
            // a sequence of one-safe-delete-per-paint instead of chained
            // wrong-index deletes.
            for (auto it = noteHits.rbegin(); it != noteHits.rend(); ++it)
            {
                if (! it->rect.contains (e.position)) continue;
                if (onDeleteNote != nullptr)
                    onDeleteNote (it->regionIdx, it->noteIdx);
                noteHits.clear();
                repaint();
                return;
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            // v1.6.1-rc.7 / rc.11 — finishing a drag selection: the
            // selecting flag clears, but the rectangle + captured note
            // list stay live so Delete/Backspace can act on them. Plain
            // click outside any note clears the selection (handled in
            // mouseDown).
            if (selecting)
            {
                selecting = false;
                if (selectionRect.getWidth() < 2.0f
                    && selectionRect.getHeight() < 2.0f)
                {
                    // Treat tiny rect as a click — discard.
                    selectionRect = {};
                    selectedNotes.clear();
                }
                repaint();
                return;
            }

            // v1.6.1-rc.5 — mouseUp only clears drag state and handles the
            // append (+) button. Right/alt-click region delete is handled
            // exclusively in mouseDown via handleRegionDelete so a single
            // right-click can only delete one region.
            const auto inner = getLocalBounds().toFloat().reduced (10.5f, 8.5f);
            dragMode = DragMode::None;
            draggingRegionIntensity = -1;
            draggingKnobIsKnob      = false;
            draggingKnobAnchorY     = -1.0f;
            lastAddedRegion = -1;
            lastAddedStepBeat = -1.0;
            lastAddedNote = -1;
            if (onAppend != nullptr && getAppendButtonBounds (inner).contains (e.position))
                onAppend();
        }

        // v1.6.1-rc.7 — mouseWheel zoom with two-finger trackpad scroll.
        // Holding Cmd/Ctrl + scrolling vertically grows or shrinks the
        // visible cell width via the onZoom callback (handled by the
        // editor). Without modifier, the wheel delegates to default
        // behaviour so the host can still scroll lists etc.
        std::function<void (float delta)> onZoom;
        void mouseWheelMove (const juce::MouseEvent& e,
                             const juce::MouseWheelDetails& w) override
        {
            // Cmd/Ctrl is required so a plain trackpad scroll still falls
            // through to the host (lists, kit visualizer, etc). Without the
            // modifier the wheel must NOT zoom — that would resize the
            // editor on every two-finger gesture.
            if (onZoom != nullptr
                && (e.mods.isCommandDown() || e.mods.isCtrlDown())
                && std::abs (w.deltaY) > 0.001f)
            {
                onZoom (w.deltaY);
                return;
            }
            juce::Component::mouseWheelMove (e, w);
        }

        // v1.6.1-rc.4 — keyboard handler: Delete removes the selected
        // note, Ctrl+D duplicates it. The editor forwards key events here
        // by making this component a keyboard focus target when the
        // arrangement is showing.
        bool keyPressed (const juce::KeyPress& key) override
        {
            // v1.6.1-rc.11 — multi-select delete. If a drag-select
            // captured one or more notes, Delete/Backspace removes them
            // all atomically. Devin Review flagged that calling
            // onDeleteNote() per-victim released the arrangement mutex
            // between deletes, letting deferred APVTS callbacks
            // (regenerateCurrentRegion) replace the back region and
            // silently invalidate the remaining indices. We now hand
            // the whole victim list to onDeleteNotes which holds the
            // mutex once across the batch (sorting + dedup happens
            // processor-side too, so the strip stays dumb).
            if ((key == juce::KeyPress::deleteKey
                 || key == juce::KeyPress::backspaceKey)
                && ! selectedNotes.empty())
            {
                if (onDeleteNotes != nullptr)
                {
                    onDeleteNotes (selectedNotes);
                }
                else if (onDeleteNote != nullptr)
                {
                    auto victims = selectedNotes;
                    std::sort (victims.begin(), victims.end(),
                               [] (const auto& a, const auto& b)
                               {
                                   if (a.first != b.first) return a.first > b.first;
                                   return a.second > b.second;
                               });
                    for (const auto& [r, n] : victims)
                        onDeleteNote (r, n);
                }
                selectedNotes.clear();
                selectionRect = {};
                repaint();
                return true;
            }

            if (selectedRegion < 0 || selectedNote < 0)
                return false;

            if (key == juce::KeyPress::deleteKey
                || key == juce::KeyPress::backspaceKey)
            {
                if (onDeleteNote) onDeleteNote (selectedRegion, selectedNote);
                selectedRegion = -1;
                selectedNote   = -1;
                repaint();
                return true;
            }
            if (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D')
            {
                if (onDuplicateNote) onDuplicateNote (selectedRegion, selectedNote);
                repaint();
                return true;
            }
            return false;
        }

        bool isSelectedNoteSet() const noexcept
        {
            return selectedRegion >= 0 && selectedNote >= 0;
        }

    private:
        struct NoteHit
        {
            int regionIdx;
            int noteIdx;
            juce::Rectangle<float> rect;
        };

        // v1.6.1-rc.5 — convert a click position to (region, localBeat,
        // midiNote) and emit onAddNote. Returns true on success so
        // mouseDown knows to enter paint-drag mode.
        bool handleAddNote (juce::Point<float> p, bool suppressDuplicates)
        {
            if (onAddNote == nullptr || cachedPxPerBeat <= 0.0f
                || cachedRegionOffsets.empty())
                return false;
            if (! cachedGridInner.contains (p)) return false;

            // Lane → midi note. Order must mirror laneFor() in paint().
            //  0=R CRASH(57), 1=L CRASH(49), 2=RIDE(51), 3=HI-HAT(42),
            //  4=SMALL TOM(48), 5=FLOOR TOM(43), 6=SNARE(38), 7=KICK(36)
            static constexpr int kLaneNote[8] = { 57, 49, 51, 42, 48, 43, 38, 36 };

            const float laneH = cachedGridInner.getHeight() / 8.0f;
            int lane = (int) ((p.y - cachedGridInner.getY()) / laneH);
            lane = juce::jlimit (0, 7, lane);

            const double totalBeats = std::max (1.0, last.totalBeats);
            const double absBeat = juce::jlimit (
                0.0,
                totalBeats - 1e-4,
                (p.x - cachedGridInner.getX()) / (double) cachedPxPerBeat);

            // v1.6.1-rc.10 — snap to the current step-div from the editor
            // (1/16, 1/32, or 1/64). beats-per-step = 4.0 / stepsPerBar
            // because one beat == one quarter note. Falls back to 1/16
            // if setStepsPerBar() was never called.
            // v1.6.1-rc.28 — CRASH SNAP. The user reported the crash
            // lanes (lane 0 = R CRASH, lane 1 = L CRASH) were "too
            // touchy" — clicking near a beat would drop a 1/32 or
            // 1/64 crash that's almost impossible to surgically delete
            // on the small bottom strip. Crashes only ever sound
            // musical on quarter / half / bar boundaries (every cymbal
            // accent in a rock chart lands on a beat or a downbeat),
            // so for crash lanes we override the step-div and snap to
            // the nearest 1/4 note. 1/4 = 1.0 beats; 1/2 = 2.0 beats;
            // bar = 4.0 beats — all multiples of 1.0, so a 1.0-beat
            // grid covers all three placements the user listed and
            // forbids the in-between 1/16 / 1/32 / 1/64 spray. Other
            // lanes (Ride / Hi-Hat / Toms / Snare / Kick) keep the
            // user-selected step-div so 1/16 hat ostinatos and ghost
            // snares still work.
            const int    laneForSnap = juce::jlimit (0, 7,
                (int) ((p.y - cachedGridInner.getY()) / laneH));
            const bool   crashLane = (laneForSnap == 0 || laneForSnap == 1);
            const int    spb = juce::jlimit (4, 64, stepsPerBar);
            const double stepBeats = crashLane ? 1.0 : (4.0 / (double) spb);
            const double snapped = std::floor (absBeat / stepBeats) * stepBeats;

            // Figure out which region this beat lives in. The cached
            // offsets are only rebuilt during paint(); a click that lands
            // between the timer appending a region and the next paint can
            // see last.regions.size() > cachedRegionOffsets.size(), so
            // bound the loop on the smaller of the two.
            int regionIdx = -1;
            double regionStart = 0.0;
            const size_t safeRegions = std::min (last.regions.size(),
                                                 cachedRegionOffsets.size());
            for (size_t i = 0; i < safeRegions; ++i)
            {
                const double start = cachedRegionOffsets[i];
                const double len = std::max (0.001, last.regions[i].lengthInBeats);
                if (snapped < start + len)
                {
                    regionIdx = (int) i;
                    regionStart = start;
                    break;
                }
            }
            if (regionIdx < 0) return false;

            const double localBeat = juce::jmax (0.0, snapped - regionStart);
            const int    noteNumber = kLaneNote[lane];

            if (suppressDuplicates
                && regionIdx == lastAddedRegion
                && std::abs (localBeat - lastAddedStepBeat) < 1e-3
                && noteNumber == lastAddedNote)
            {
                return false;
            }

            onAddNote (regionIdx, localBeat, noteNumber, 0.85f);
            lastAddedRegion = regionIdx;
            lastAddedStepBeat = localBeat;
            lastAddedNote = noteNumber;
            repaint();
            return true;
        }

        void handleRegionDelete (juce::Point<float> p,
                                 juce::Rectangle<float> inner)
        {
            if (onDeleteRegion == nullptr || last.totalBeats <= 0.0
                || last.regions.empty())
                return;
            // v1.6.1-rc.28 — must mirror kLabelColumn in paint() (now
            // 92.0f after rc.28 added the per-lane "0" pill alongside
            // the rc.28 piano-roll icon) plus the 6px gap. Was 54+6
            // in rc.8, 76+6 in rc.9-rc.27, 92+6 here so right-click
            // region-delete hit-tests against the same grid the user
            // sees.
            auto gridRect = inner.withTrimmedRight (getAppendButtonBounds (inner).getWidth() + 10.0f)
                                 .withTrimmedLeft  (92.0f + 6.0f)
                                 .withTrimmedTop   (14.0f);
            if (! gridRect.contains (p)) return;
            const double total = std::max (1.0, last.totalBeats);
            const double rel = (p.x - gridRect.getX()) / gridRect.getWidth();
            const double beat = juce::jlimit (0.0, total - 1e-6, rel * total);
            double acc = 0.0;
            for (size_t i = 0; i < last.regions.size(); ++i)
            {
                const double len = std::max (0.001, last.regions[i].lengthInBeats);
                if (beat < acc + len) { onDeleteRegion ((int) i); return; }
                acc += len;
            }
        }

        static juce::Rectangle<float> getAppendButtonBounds (juce::Rectangle<float> inner)
        {
            const float size = juce::jmin (28.0f, inner.getHeight() * 0.34f);
            return juce::Rectangle<float> (inner.getRight() - size,
                                           inner.getCentreY() - size * 0.5f,
                                           size, size);
        }

        void timerCallback() override
        {
            if (! provider) return;
            auto now = provider();

            // v1.6.1-rc.14 — only the visually-significant fields drive a
            // repaint, but ALWAYS refresh `last` so an externally-triggered
            // repaint() (e.g. after dragging the per-region INTENSITY strip
            // while the transport is stopped) reads fresh region data
            // instead of the stale snapshot.
            const bool changed =
                   now.regions.size() != last.regions.size()
                || std::abs (now.totalBeats    - last.totalBeats)    > 1e-6
                || std::abs (now.playheadBeats - last.playheadBeats) > 1e-3
                // v1.6.1-rc.16 — also repaint when the clipboard
                // state flips so the PASTE pill brightness updates
                // immediately after C/Ctrl+C even when the transport
                // is stopped (otherwise the timer would update `last`
                // silently with no repaint).
                || now.clipboardHasContent != last.clipboardHasContent;

            last = std::move (now);
            if (changed)
                repaint();
        }

        std::function<Snapshot()> provider;
        bool                      highlightAll = false;
        Snapshot                  last;

        // v1.6.1-rc.15 — safe lookup used by the per-region INTENSITY
        // mini-knob hit handler so we can read the live regionIntensity
        // for the clicked region. Falls back to a static empty pattern
        // if the index is stale (shouldn't happen because cachedKnobs
        // is rebuilt on every paint() before mouseDown can fire).
        const MidiPattern& currentRegion (int idx) const noexcept
        {
            static const MidiPattern empty;
            if (idx < 0 || idx >= (int) last.regions.size())
                return empty;
            return last.regions[(size_t) idx];
        }
        int                       selectedRegion = -1;
        int                       selectedNote   = -1;
        std::vector<NoteHit>      noteHits;

        // v1.6.1-rc.5 — geometry cached from paint() so mouseDown/drag
        // can map cursor coords back to (region, beat, lane) without
        // recomputing the layout.
        juce::Rectangle<float>  cachedGridInner;
        float                   cachedPxPerBeat = 0.0f;
        std::vector<double>     cachedRegionOffsets;

        // v1.6.1-rc.14 — per-region intensity drag-strip hit-rects.
        // Rebuilt every paint(); mouseDown/Drag use them to drag the
        // gold strip at the bottom of each region tile.
        struct RegionIntensityHit
        {
            int                   regionIdx = -1;
            juce::Rectangle<float> rect;
        };
        std::vector<RegionIntensityHit> cachedRegionIntensityHits;
        // v1.6.1-rc.15 — top-left mini-knob hit-rects per region. Same
        // RegionIntensityHit shape (regionIdx + rect) but a separate
        // cache so the knob and bottom strip can be hit-tested in
        // priority order.
        std::vector<RegionIntensityHit> cachedRegionIntensityKnobs;
        // v1.6.1-rc.16 — per-region COPY / PASTE button hit-rects.
        // Same RegionIntensityHit shape (regionIdx + rect). Rebuilt
        // every paint() and hit-tested in mouseDown before the
        // intensity knob so the buttons take priority.
        std::vector<RegionIntensityHit> cachedRegionCopyButtons;
        std::vector<RegionIntensityHit> cachedRegionPasteButtons;
        // v1.6.1-rc.28 — per-region NUMBER badge hit-rect (the small
        // "1" / "2" / "3" pill in the top-left of each region tile).
        // Rebuilt every paint(); hit-tested before the intensity knob
        // so the badge wins its tiny 14×14 box. Click fires
        // onRegionNumberClicked → editor opens the per-region edit
        // popup.
        std::vector<RegionIntensityHit> cachedRegionNumberButtons;

        // v1.6.1-rc.28 — per-lane GLOBAL "0" pill hit-rects (one per
        // lane row in the left label column). Same lane-index order
        // as cachedLabelRects: 0=R CRASH … 7=KICK. Click fires
        // onLaneZeroClicked → editor calls processor.eraseLaneInArrangement.
        std::array<juce::Rectangle<float>, 8> cachedLaneZeroButtons {};

        // v1.6.1-rc.28 — per-region per-lane "0" pill hit-rects.
        // Each entry stores the regionIdx + laneIdx + tile-local
        // rect. Rebuilt every paint(); hit-tested in mouseDown
        // BEFORE the per-lane piano roll icon and before the
        // region-number badge so the pill wins its 9×9 box.
        struct RegionLaneHit
        {
            int                    regionIdx = -1;
            int                    laneIdx   = -1;
            juce::Rectangle<float> rect;
        };
        std::vector<RegionLaneHit> cachedRegionLaneZeroButtons;
        int                            draggingRegionIntensity = -1;
        bool                           draggingKnobIsKnob      = false;
        float                          draggingKnobAnchorY     = -1.0f;
        float                          draggingKnobAnchorVal   =  0.0f;

        enum class DragMode { None, Paint, Erase };
        DragMode dragMode        = DragMode::None;
        int      lastAddedRegion = -1;
        double   lastAddedStepBeat = -1.0;
        int      lastAddedNote   = -1;

        // v1.6.1-rc.7 — ghost mask + lane label geometry. Cached during
        // paint() so mouseDown can hit-test the row labels on the left
        // edge of the strip and emit onLaneSelected.
        int                                ghostMask        = 0;
        int                                selectedLaneIdx  = -1;
        int                                stepsPerBar      = 16;
        bool                               trapModeOn       = false; // v1.6.1-rc.19
        std::array<juce::Rectangle<float>, 8> cachedLabelRects {};

        // v1.6.1-rc.28 — cached per-lane piano-roll icon hit-rect so
        // mouseDown can fire onLanePianoRollRequested. Same lane-index
        // ordering as cachedLabelRects.
        std::array<juce::Rectangle<float>, 8> cachedLanePianoButtons {};

        // v1.6.1-rc.7 — rectangular hover-drag selection. The user can
        // click an empty area, drag to draw a highlight rectangle (left-
        // to-right or right-to-left), and the strip paints the
        // selection while it tracks the cursor.
        // v1.6.1-rc.11 — drag-select now stays after mouseUp, captures
        // every note whose hit-rect intersects the selection rectangle,
        // and Delete/Backspace removes all of them. Empty-area click
        // clears the selection. Plain LMB on empty space starts the
        // selection (no Shift modifier required).
        bool                            selecting        = false;
        juce::Point<float>              selectionAnchor  {};
        juce::Rectangle<float>          selectionRect    {};
        std::vector<std::pair<int,int>> selectedNotes    {}; // (regionIdx, noteIdx)
    };
}
