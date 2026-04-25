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
            double                   playheadBeats = 0.0;
            double                   totalBeats    = 0.0;
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

        // v1.6.1-rc.7 — bitmask of lanes that are currently in "ghost"
        // mode. Bit 0 = CRASH, 1 = RIDE, 2 = HI-HAT, 3 = TOM, 4 = SNARE,
        // 5 = KICK (top → bottom, mirrors the kLanes table). Lanes with
        // their bit set are drawn with a greyed-out label so the user
        // can see at a glance which rows the GHOST button has flipped.
        void setGhostMask (int mask) { ghostMask = mask; repaint(); }
        int  getGhostMask() const    { return ghostMask; }

        // v1.6.1-rc.7 — fires when the user clicks a row label on the
        // left edge of the strip. The editor uses this to remember which
        // lane the GHOST button should target on its next click.
        // 0..5 in the same top→bottom order as the lane table.
        std::function<void (int laneIndex)> onLaneSelected;
        void setSelectedLane (int laneIndex)
        {
            selectedLaneIdx = juce::jlimit (-1, 5, laneIndex);
            repaint();
        }

        // v1.5.0 — right-click (or alt-click) on a region tile calls this with
        // the region's index so the editor can remove it. Empty arrangement is
        // allowed; the `+` button becomes the only interactive element.
        std::function<void (int regionIndex)> onDeleteRegion;

        // v1.6.1-rc.5 — step-sequencer toggle semantics. Left-click on a
        // drawn note immediately deletes it (same motion = toggle off).
        // Left-click on an empty grid cell drops a new note at that lane
        // and beat (toggle on). Ctrl+click still duplicates a clicked
        // note; right-click / alt-click on empty grid deletes the whole
        // region under the cursor.
        std::function<void (int regionIndex, int noteIndex)> onDeleteNote;
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

            // v1.6.1-rc.2 — reserve a narrow column on the left for per-lane
            // labels (CRASH / RIDE / HI-HAT / TOM / SNARE / KICK). 6 lanes
            // now — RIDE is explicit instead of being lumped with crashes.
            constexpr float kLabelColumn = 54.0f;
            constexpr float kHeaderRow   = 14.0f;
            auto labelArea = inner.removeFromLeft (kLabelColumn);
            inner.removeFromLeft (6.0f);                 // gap after labels
            auto headerArea = inner.removeFromTop (kHeaderRow);
            labelArea.removeFromTop (kHeaderRow);

            const double totalBeats = std::max (1.0, last.totalBeats);
            const float  pxPerBeat  = inner.getWidth() / (float) totalBeats;

            cachedGridInner = inner;
            cachedPxPerBeat = pxPerBeat;

            // --- v1.6.1-rc.2 lane definitions — 6 fixed rows, each a distinct
            // colour. Top → bottom: CRASH, RIDE, HI-HAT, TOM, SNARE, KICK.
            // Order mirrors the manual grid so the editor is consistent.
            struct Lane { const char* label; juce::uint32 col; };
            static const Lane kLanes[6] = {
                { "CRASH",  0xffff6f9c },  // pink rose
                { "RIDE",   0xff6ec6ff },  // sky blue (new in rc.2)
                { "HI-HAT", 0xffffc857 },  // amber
                { "TOM",    0xff9d7dff },  // lilac
                { "SNARE",  0xffede7f6 },  // bone white
                { "KICK",   0xff3ee0c1 },  // teal
            };
            const int   kNumLanes = 6;
            const float laneH     = inner.getHeight() / (float) kNumLanes;

            auto laneFor = [] (int n) -> int
            {
                // 0=CRASH (49/55/57/52/china), 1=RIDE (51/53/59), 2=HI-HAT,
                // 3=TOM, 4=SNARE (+ clap / rimshot), 5=KICK
                if (n == 35 || n == 36)                                      return 5;
                if (n == 37 || n == 38 || n == 39 || n == 40)                return 4;
                if (n == 41 || n == 43 || n == 45 || n == 47
                    || n == 48 || n == 50)                                   return 3;
                if (n == 42 || n == 44 || n == 46)                           return 2;
                if (n == 51 || n == 53 || n == 59)                           return 1;
                return 0;  // 49/52/55/57 etc — crashes/china
            };

            // --- Lane labels + alternating lane bands ---------------------
            auto laneFont = juce::Font (juce::FontOptions (9.5f, juce::Font::bold));
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
                const auto labelRect = juce::Rectangle<float> (
                    labelArea.getX() + 2.0f, yTop,
                    labelArea.getWidth() - 6.0f, laneH);
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

                // Region label at top-left of its cell
                if (last.regions.size() > 1)
                {
                    auto lf = juce::Font (juce::FontOptions (9.0f));
                    lf.setExtraKerningFactor (0.22f);
                    g.setFont (lf);
                    g.setColour (juce::Colour (GothicPalette::kMuted).withAlpha (0.75f));
                    g.drawText (juce::String ((int) i + 1),
                                juce::Rectangle<float> (regionX0 + 3.0f, inner.getY() + 1.0f,
                                                        20.0f, 12.0f),
                                juce::Justification::topLeft, false);
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
                    const bool isSelected   = (selectedRegion == (int) i && selectedNote == noteIdx);
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
            // v1.6.1-rc.7 — lane label click: arms a row for the GHOST
            // button. Hit-tested before notes/grid so labels can never
            // accidentally drop a kick on bar 1.
            for (int i = 0; i < (int) cachedLabelRects.size(); ++i)
            {
                if (cachedLabelRects[(size_t) i].contains (e.position))
                {
                    selectedLaneIdx = i;
                    if (onLaneSelected != nullptr) onLaneSelected (i);
                    repaint();
                    return;
                }
            }

            // v1.6.1-rc.7 — shift+drag (or middle button) starts a
            // rectangular selection across the grid. Used purely
            // visually for now (matches the rc.7 brief: "HOVER CLICK
            // AND DRAW A HIGHLIGHTED SECTION ON THE ARRANGEMENT GRID").
            if ((e.mods.isShiftDown() || e.mods.isMiddleButtonDown())
                && cachedGridInner.contains (e.position))
            {
                selecting = true;
                selectionAnchor = e.position;
                selectionRect = juce::Rectangle<float> (e.position, e.position);
                repaint();
                return;
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
            // v1.6.1-rc.7 — rectangular hover-drag highlight on the
            // arrangement grid. Tracks the cursor in either direction
            // (L→R or R→L) and re-paints a translucent overlay.
            if (selecting)
            {
                selectionRect = juce::Rectangle<float> (selectionAnchor, e.position);
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
            // v1.6.1-rc.7 — finishing a hover-drag selection just clears
            // the selecting flag (the rectangle stays visible until the
            // next click). Future versions can hand the rect to a
            // copy/paste pipeline, but the brief only required the
            // visible highlight.
            if (selecting)
            {
                selecting = false;
                repaint();
                return;
            }

            // v1.6.1-rc.5 — mouseUp only clears drag state and handles the
            // append (+) button. Right/alt-click region delete is handled
            // exclusively in mouseDown via handleRegionDelete so a single
            // right-click can only delete one region.
            const auto inner = getLocalBounds().toFloat().reduced (10.5f, 8.5f);
            dragMode = DragMode::None;
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
            //  0=CRASH(49), 1=RIDE(51), 2=HI-HAT(42), 3=TOM(45),
            //  4=SNARE(38), 5=KICK(36)
            static constexpr int kLaneNote[6] = { 49, 51, 42, 45, 38, 36 };

            const float laneH = cachedGridInner.getHeight() / 6.0f;
            int lane = (int) ((p.y - cachedGridInner.getY()) / laneH);
            lane = juce::jlimit (0, 5, lane);

            const double totalBeats = std::max (1.0, last.totalBeats);
            const double absBeat = juce::jlimit (
                0.0,
                totalBeats - 1e-4,
                (p.x - cachedGridInner.getX()) / (double) cachedPxPerBeat);

            // Snap to quarter notes (Logic-style beat cells).
            const double snapped = std::floor (absBeat);

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
            auto gridRect = inner.withTrimmedRight (getAppendButtonBounds (inner).getWidth() + 10.0f)
                                 .withTrimmedLeft  (54.0f + 6.0f)
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

            const bool changed =
                   now.regions.size() != last.regions.size()
                || std::abs (now.totalBeats    - last.totalBeats)    > 1e-6
                || std::abs (now.playheadBeats - last.playheadBeats) > 1e-3;

            if (changed)
            {
                last = std::move (now);
                repaint();
            }
        }

        std::function<Snapshot()> provider;
        bool                      highlightAll = false;
        Snapshot                  last;
        int                       selectedRegion = -1;
        int                       selectedNote   = -1;
        std::vector<NoteHit>      noteHits;

        // v1.6.1-rc.5 — geometry cached from paint() so mouseDown/drag
        // can map cursor coords back to (region, beat, lane) without
        // recomputing the layout.
        juce::Rectangle<float>  cachedGridInner;
        float                   cachedPxPerBeat = 0.0f;
        std::vector<double>     cachedRegionOffsets;

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
        std::array<juce::Rectangle<float>, 6> cachedLabelRects {};

        // v1.6.1-rc.7 — rectangular hover-drag selection. The user can
        // click an empty area, drag to draw a highlight rectangle (left-
        // to-right or right-to-left), and the strip paints the
        // selection while it tracks the cursor. mouseUp clears the
        // selection. Used purely visually right now (no copy/paste yet)
        // but matches the rc.7 brief.
        bool                   selecting        = false;
        juce::Point<float>     selectionAnchor  {};
        juce::Rectangle<float> selectionRect    {};
    };
}
