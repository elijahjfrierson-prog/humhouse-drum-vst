#pragma once

#include "GothicLookAndFeel.h"
#include "MidiPattern.h"

#include <JuceHeader.h>

#include <functional>
#include <vector>

namespace aidrum
{
    // v1.6.1-rc.20 — FL-Studio-style piano roll for the manual pattern.
    //
    // Spans the full chromatic range C0..C11 (132 keys). Vertical axis = pitch,
    // horizontal axis = time. Click empty area to drop a unit-length note;
    // click an existing note to grab it (drag left/right = move in time, up/
    // down = move in pitch); drag the right edge of an existing note to
    // resize. Right-click / alt-click any note to delete. Mouse-wheel scrolls
    // vertically; ctrl/cmd+wheel zooms horizontally; shift+wheel scrolls
    // horizontally.
    //
    // Edits the SAME `manualPattern` that ManualGrid edits (provider returns,
    // onAddNote / onRemoveNote / onMoveNote write back via the processor).
    // The grid is the limited 8-row drum subset; piano roll exposes every
    // semitone so the user can program melodic synths / pads / phrases for
    // the Drocetti trap kit on top of the same canvas.
    class PianoRoll : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        // C0 (MIDI 12) ... B10 (MIDI 131). 132 = 11 * 12.
        // Some hosts allow MIDI 0..127 only; we still draw the full FL range
        // since the user may export to a host that supports >127 (e.g. via
        // octave-shifted instruments). Scroll-clamping below limits visible
        // range to what fits the panel anyway.
        static constexpr int kLowestMidi  = 12;   // C0
        static constexpr int kNumKeys     = 132;  // C0..B10 inclusive
        static constexpr int kHighestMidi = kLowestMidi + kNumKeys - 1;

        PianoRoll()
        {
            setInterceptsMouseClicks (true, false);
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
            setWantsKeyboardFocus (true);
        }

        // Provider returns a snapshot of the current manual pattern.
        // onAddNote / onRemoveNote / onMoveNote call back into the processor.
        std::function<MidiPattern()>                                  provider;
        std::function<void (int midiNote, double startBeat,
                            double lengthBeat, float velocity)>       onAddNote;
        std::function<void (int midiNote, double startBeat)>          onRemoveNote;
        std::function<void (int oldMidiNote, double oldStartBeat,
                            int newMidiNote, double newStartBeat,
                            double newLengthBeat)>                    onMoveNote;

        void setNumBars (int bars) { numBars = juce::jlimit (1, 64, bars); repaint(); }
        int  getNumBars() const    { return numBars; }

        // Same step-division semantics as ManualGrid (16/32/64 steps per bar).
        void setStepsPerBar (int s)
        {
            stepsPerBar = (s == 64 ? 64 : s == 32 ? 32 : 16);
            repaint();
        }
        int    getStepsPerBar() const { return stepsPerBar; }
        double stepBeats() const      { return 4.0 / (double) stepsPerBar; }
        int    totalSteps() const     { return numBars * stepsPerBar; }

        // v1.6.1-rc.20 — when ON, click-drop creates a note that plays the
        // FULL sample length regardless of MIDI length. Stored on the note
        // via MidiNote::oneShot. Visual: a small "1S" tag on the note head.
        void setOneShotMode (bool on)  { oneShotMode = on; repaint(); }
        bool getOneShotMode() const    { return oneShotMode; }

        // v1.6.1-rc.20 — vertical scroll in semitones from kLowestMidi.
        // 0 = bottom of view shows C0; clamped so the top key is always
        // <= kHighestMidi.
        void setVerticalScroll (int semitonesFromBottom)
        {
            verticalScroll = juce::jlimit (0, kNumKeys - 12, semitonesFromBottom);
            repaint();
        }
        int  getVerticalScroll() const { return verticalScroll; }

        // v1.6.1-rc.20 — horizontal zoom (pixels per step). Default 14 px
        // shows ~2 bars at 16-step in a 480 px wide panel.
        void setPixelsPerStep (float px)
        {
            pixelsPerStep = juce::jlimit (4.0f, 64.0f, px);
            repaint();
        }
        float getPixelsPerStep() const { return pixelsPerStep; }

    private:
        // ============== Layout helpers ==============
        struct Layout
        {
            float keyboardWidth = 56.0f;
            float headerHeight  = 16.0f;
            float keyHeight     = 12.0f;
        };

        Layout currentLayout() const
        {
            Layout L;
            const float h = juce::jmax (1.0f, (float) getHeight() - L.headerHeight);
            // Show ~36 keys vertically by default (3 octaves) so each
            // black/white key gets a usable row. User scrolls to see more.
            L.keyHeight = juce::jlimit (8.0f, 18.0f, h / 36.0f);
            return L;
        }

        juce::Rectangle<float> gridArea() const
        {
            auto r = getLocalBounds().toFloat();
            const auto L = currentLayout();
            return r.withTrimmedLeft (L.keyboardWidth)
                    .withTrimmedTop  (L.headerHeight);
        }

        int visibleKeyCount() const
        {
            const auto g = gridArea();
            const auto L = currentLayout();
            return juce::jmax (12, (int) std::floor (g.getHeight() / L.keyHeight));
        }

        int topVisibleMidi() const
        {
            const int bottom = kLowestMidi + verticalScroll;
            const int vis    = visibleKeyCount();
            return juce::jmin (kHighestMidi, bottom + vis - 1);
        }

        // Map a MIDI note to its row Y centre inside the grid (or NaN if
        // outside the visible window).
        float yForMidi (int midi) const
        {
            const auto g = gridArea();
            const auto L = currentLayout();
            const int bottom = kLowestMidi + verticalScroll;
            const int row    = midi - bottom;          // 0 = bottom row
            if (row < 0 || row >= visibleKeyCount()) return std::numeric_limits<float>::quiet_NaN();
            return g.getBottom() - ((float) row + 0.5f) * L.keyHeight;
        }

        int midiForY (float y) const
        {
            const auto g = gridArea();
            const auto L = currentLayout();
            if (y < g.getY() || y > g.getBottom()) return -1;
            const int row = (int) std::floor ((g.getBottom() - y) / L.keyHeight);
            const int bottom = kLowestMidi + verticalScroll;
            const int m = bottom + row;
            return (m >= kLowestMidi && m <= kHighestMidi) ? m : -1;
        }

        float xForBeat (double beat) const
        {
            const auto g = gridArea();
            const float stepsFromZero = (float) (beat / stepBeats());
            return g.getX() + stepsFromZero * pixelsPerStep;
        }

        double beatForX (float x) const
        {
            const auto g = gridArea();
            const float stepsFromZero = (x - g.getX()) / pixelsPerStep;
            return juce::jmax (0.0, (double) stepsFromZero * stepBeats());
        }

        static bool isBlackKey (int midi)
        {
            const int pc = ((midi % 12) + 12) % 12;
            return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        }

        static juce::String keyLabel (int midi)
        {
            static const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F",
                                              "F#", "G", "G#", "A", "A#", "B" };
            const int pc  = ((midi % 12) + 12) % 12;
            const int oct = (midi / 12) - 1; // C0 = MIDI 12
            return juce::String (kNames[pc]) + juce::String (oct);
        }

    public:
        // ============== Paint ==============
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (GothicPalette::kInk));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

            const auto L      = currentLayout();
            const auto grid   = gridArea();
            const int  visKeys = visibleKeyCount();
            const int  bottom  = kLowestMidi + verticalScroll;

            // ---- Keyboard column (left) ----
            auto kb = r.withWidth (L.keyboardWidth).withTrimmedTop (L.headerHeight);
            g.setColour (juce::Colour (GothicPalette::kPanel));
            g.fillRect (kb);

            auto labelFont = juce::Font (juce::FontOptions (8.5f, juce::Font::italic));
            labelFont.setExtraKerningFactor (0.20f);
            g.setFont (labelFont);

            for (int i = 0; i < visKeys; ++i)
            {
                const int   m   = bottom + i;
                if (m > kHighestMidi) break;
                const float y   = grid.getBottom() - ((float) i + 1) * L.keyHeight;
                const auto  row = juce::Rectangle<float> (kb.getX(), y, kb.getWidth(), L.keyHeight);

                if (isBlackKey (m))
                {
                    g.setColour (juce::Colours::black.withAlpha (0.85f));
                    g.fillRect (row.reduced (0.0f, 0.5f));
                }
                else
                {
                    g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.92f));
                    g.fillRect (row.reduced (0.0f, 0.5f));
                }

                // Label every C row.
                const int pc = ((m % 12) + 12) % 12;
                if (pc == 0)
                {
                    g.setColour (juce::Colour (GothicPalette::kAccentDeep));
                    g.drawText (keyLabel (m),
                                row.reduced (4.0f, 0.0f),
                                juce::Justification::centredLeft, false);
                }
            }
            g.setColour (juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.45f));
            g.drawLine (kb.getRight(), kb.getY(), kb.getRight(), kb.getBottom(), 1.0f);

            // ---- Top header (bar numbers) ----
            auto hdr = r.withTrimmedLeft (L.keyboardWidth).withHeight (L.headerHeight);
            g.setColour (juce::Colour (GothicPalette::kPanel).withAlpha (0.7f));
            g.fillRect (hdr);
            g.setColour (juce::Colour (GothicPalette::kMuted));
            for (int b = 0; b < numBars; ++b)
            {
                const float x = grid.getX() + (float) (b * stepsPerBar) * pixelsPerStep;
                if (x > hdr.getRight()) break;
                g.drawText (juce::String (b + 1),
                            juce::Rectangle<float> (x + 4.0f, hdr.getY(), 28.0f, hdr.getHeight()),
                            juce::Justification::centredLeft, false);
            }

            // ---- Grid rows (alternating black-key / white-key shading) ----
            g.reduceClipRegion (grid.toNearestIntEdges());
            for (int i = 0; i < visKeys; ++i)
            {
                const int   m = bottom + i;
                if (m > kHighestMidi) break;
                const float y = grid.getBottom() - ((float) i + 1) * L.keyHeight;
                const auto  row = juce::Rectangle<float> (grid.getX(), y, grid.getWidth(), L.keyHeight);
                if (isBlackKey (m))
                {
                    g.setColour (juce::Colour (GothicPalette::kPanel).withAlpha (0.55f));
                    g.fillRect (row);
                }
                // Highlight every C row with a faint accent line so 12-key
                // octave boundaries pop out without the user having to count.
                const int pc = ((m % 12) + 12) % 12;
                if (pc == 0)
                {
                    g.setColour (juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.18f));
                    g.fillRect (row);
                }
            }

            // ---- Vertical grid lines (steps / beats / bars) ----
            const int totalStep = totalSteps();
            for (int s = 0; s <= totalStep; ++s)
            {
                const float x = grid.getX() + (float) s * pixelsPerStep;
                if (x > grid.getRight()) break;
                const bool isBar  = (s % stepsPerBar) == 0;
                const bool isBeat = (s % (stepsPerBar / 4)) == 0;
                g.setColour (juce::Colour (GothicPalette::kAccent)
                               .withAlpha (isBar ? 0.45f : isBeat ? 0.18f : 0.08f));
                g.fillRect (x - (isBar ? 0.75f : 0.4f), grid.getY(),
                            isBar ? 1.4f : 0.8f, grid.getHeight());
            }

            // ---- Notes ----
            if (provider)
            {
                const auto pattern = provider();
                for (const auto& n : pattern.notes)
                {
                    const float y = yForMidi (n.noteNumber);
                    if (! std::isfinite (y)) continue;
                    const float x  = xForBeat (n.startBeat);
                    const float w  = juce::jmax (3.0f,
                                       (float) (n.lengthBeat / stepBeats()) * pixelsPerStep);
                    const float h  = juce::jmax (4.0f, L.keyHeight - 2.0f);
                    const auto  rr = juce::Rectangle<float> (x, y - h * 0.5f, w, h);

                    auto base = juce::Colour (GothicPalette::kAccent)
                                  .interpolatedWith (juce::Colour (GothicPalette::kBone), 0.20f);
                    base = base.withAlpha (0.40f + 0.60f
                                              * juce::jlimit (0.0f, 1.0f, n.velocity));
                    g.setColour (base);
                    g.fillRoundedRectangle (rr, 2.5f);

                    g.setColour (juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.85f));
                    g.drawRoundedRectangle (rr, 2.5f, 1.0f);

                    if (n.oneShot && rr.getWidth() > 14.0f)
                    {
                        g.setColour (juce::Colour (GothicPalette::kBone).withAlpha (0.85f));
                        auto tagFont = juce::Font (juce::FontOptions (8.0f, juce::Font::bold));
                        g.setFont (tagFont);
                        g.drawText ("1S",
                                    rr.reduced (3.0f, 1.0f),
                                    juce::Justification::centredLeft, false);
                    }
                }
            }
        }

        // ============== Mouse ==============
        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto pos = e.position;
            int        m   = midiForY (pos.y);
            if (m < 0) return;
            // v1.6.1-rc.20-fix4 — the FL panel exposes MIDI 12..143
            // (C0..B10) but addManualNote clamps stored notes to
            // 0..127. If we let the unclamped m reach noteAt(), every
            // click on rows MIDI 128..143 looks up a note that's
            // never there (it was stored at 127), so each click drops
            // a fresh duplicate, alt-click delete becomes a no-op, and
            // drag/resize never engages on those rows. Clamp once,
            // up-front, so every downstream lookup uses the same key
            // the processor stores.
            m = juce::jlimit (0, 127, m);
            const double beat = beatForX (pos.x);

            const bool  altOrRight = e.mods.isAltDown() || e.mods.isPopupMenu();
            const auto* hit        = noteAt (m, beat);

            if (hit != nullptr)
            {
                if (altOrRight)
                {
                    if (onRemoveNote) onRemoveNote (hit->noteNumber, hit->startBeat);
                    repaint();
                    return;
                }

                // Resize if grabbing the right 6-px edge, else move.
                const float headX = xForBeat (hit->startBeat);
                const float tailX = xForBeat (hit->startBeat + hit->lengthBeat);
                drag.active        = true;
                drag.origNote      = hit->noteNumber;
                drag.origStartBeat = hit->startBeat;
                drag.origLengthBeat= hit->lengthBeat;
                drag.origVelocity  = hit->velocity;
                drag.mode          = (pos.x >= tailX - 6.0f) ? DragMode::Resize : DragMode::Move;
                drag.grabBeatOffset= beat - hit->startBeat;
                drag.grabPitchOffset = m - hit->noteNumber;
                return;
            }

            // v1.6.1-rc.20 — right-click / alt-click on empty space is a
            // no-op (matches ManualGrid's clear-cell semantics + the class
            // header's documented behaviour: alt/right is the delete
            // gesture, plain left-click is the create gesture). Without
            // this an alt/right-click on empty space would drop a fresh
            // note and arm a Resize drag, which is the exact opposite of
            // what the user asked for.
            if (altOrRight) return;

            // Empty area → drop a fresh note. Snap to the current step.
            // v1.6.1-rc.20 — clamp to the same range the processor stores
            // (note 0..127, start within pattern length) so drag.orig*
            // matches what addManualNote actually persists. Without this
            // a click on a row above MIDI 127 (FL exposes up to B10/143)
            // would leave the drag state pointing at a note that doesn't
            // exist in the processor, breaking the immediate resize drag.
            const double maxStart = juce::jmax (0.0,
                (double) totalSteps() * stepBeats() - stepBeats());
            const int    storedNote  = juce::jlimit (0, 127, m);
            const double snapped     = juce::jlimit (0.0, maxStart, snapToStep (beat));
            const double len         = stepBeats();
            const float  vel         = 0.85f;
            if (onAddNote) onAddNote (storedNote, snapped, len, vel);
            drag.active        = true;
            drag.origNote      = storedNote;
            drag.origStartBeat = snapped;
            drag.origLengthBeat= len;
            drag.origVelocity  = vel;
            drag.mode          = DragMode::Resize;
            drag.grabBeatOffset = 0.0;
            drag.grabPitchOffset = 0;
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! drag.active) return;
            const auto pos = e.position;
            int        m   = midiForY (pos.y);
            if (m < 0) return;
            // v1.6.1-rc.20-fix4 — same clamp as mouseDown so move-
            // mode drags from rows that midiForY reports as 128..143
            // can still resolve correctly against the processor's
            // 0..127 storage. (Resize-mode never reads m for pitch.)
            m = juce::jlimit (0, 127, m);
            const double beat = beatForX (pos.x);

            // v1.6.1-rc.20 — clamp to the same range the processor stores
            // BEFORE calling onMoveNote, then mirror the clamped values
            // into drag.orig*. Otherwise out-of-range moves get silently
            // dropped by moveManualNote() while the PianoRoll's drag state
            // moves on, so subsequent drag events search for a note key
            // that no longer matches and the gesture goes dead.
            const double maxStart = juce::jmax (0.0,
                (double) totalSteps() * stepBeats() - stepBeats());

            if (drag.mode == DragMode::Resize)
            {
                const double maxLen = juce::jmax (stepBeats() * 0.5,
                    (double) totalSteps() * stepBeats() - drag.origStartBeat);
                const double newLen = juce::jlimit (
                    stepBeats() * 0.5, maxLen,
                    snapToStep (beat - drag.origStartBeat));
                if (onMoveNote)
                    onMoveNote (drag.origNote, drag.origStartBeat,
                                drag.origNote, drag.origStartBeat, newLen);
                drag.origLengthBeat = newLen;
            }
            else
            {
                const int    newNote   = juce::jlimit (0, 127,
                                            m - drag.grabPitchOffset);
                const double newStart  = juce::jlimit (0.0, maxStart,
                                            snapToStep (beat - drag.grabBeatOffset));
                if (onMoveNote)
                    onMoveNote (drag.origNote, drag.origStartBeat,
                                newNote, newStart, drag.origLengthBeat);
                drag.origNote      = newNote;
                drag.origStartBeat = newStart;
            }
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            drag = {};
        }

        void mouseWheelMove (const juce::MouseEvent& e,
                             const juce::MouseWheelDetails& w) override
        {
            const bool zoomMod   = e.mods.isCtrlDown() || e.mods.isCommandDown();
            const bool horizontalScroll = e.mods.isShiftDown();
            if (zoomMod)
            {
                const float factor = (w.deltaY > 0.0f) ? 1.15f : 0.87f;
                setPixelsPerStep (pixelsPerStep * factor);
            }
            else if (horizontalScroll)
            {
                // (Future) horizontal scroll. For now the strip is fully
                // visible — bars per pattern <= 32 fits at 14 px/step in
                // most window widths. Left as a no-op so the wheel doesn't
                // hijack vertical scroll while shift is held.
            }
            else
            {
                // v1.6.1-rc.20-fix3 — guard against horizontal-only
                // trackpad gestures (deltaX != 0, deltaY == 0): the
                // ternary below evaluates to -1 for deltaY == 0 and
                // would scroll the keyboard down 3 semitones every
                // time the user nudged sideways. Ignore zero-Y
                // wheel events instead.
                if (std::abs (w.deltaY) < 0.001f) return;
                const int step = (w.deltaY > 0.0f) ? 1 : -1;
                setVerticalScroll (verticalScroll + step * 3);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            if (! provider) return;
            // v1.6.1-rc.20-fix3 — noteAt() already calls provider() once,
            // which acquires manualMutex and deep-copies the whole
            // MidiPattern. The redundant provider() call here doubled
            // mutex contention + heap allocation on every mouseMove,
            // for a copy that was never read.
            // v1.6.1-rc.20-fix4 — clamp the same way mouseDown does so
            // hover-state cursor changes also work on the high MIDI
            // rows (otherwise the cursor never switches to drag/resize
            // for notes that the user just placed via mouseDown).
            int        m   = juce::jlimit (0, 127, midiForY (e.position.y));
            const auto beat = beatForX (e.position.x);
            if (m < 0) { setMouseCursor (juce::MouseCursor::CrosshairCursor); return; }

            const auto* hit = noteAt (m, beat);
            if (hit != nullptr)
            {
                const float tailX = xForBeat (hit->startBeat + hit->lengthBeat);
                if (e.position.x >= tailX - 6.0f)
                {
                    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
                setMouseCursor (juce::MouseCursor::DraggingHandCursor);
                return;
            }
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
        }

    private:
        // ============== Misc ==============
        const MidiNote* noteAt (int midi, double beat) const
        {
            if (! provider) return nullptr;
            // Hold the snapshot in a static thread_local so the returned
            // pointer is valid for the duration of the immediate caller.
            // (We never store it across event loop iterations.)
            thread_local MidiPattern snap;
            snap = provider();
            for (const auto& n : snap.notes)
            {
                if (n.noteNumber != midi) continue;
                if (beat < n.startBeat) continue;
                if (beat > n.startBeat + n.lengthBeat) continue;
                return &n;
            }
            return nullptr;
        }

        double snapToStep (double beat) const
        {
            const double sb = stepBeats();
            return std::round (beat / sb) * sb;
        }

        // ============== State ==============
        int   numBars       = 8;
        int   stepsPerBar   = 16;
        int   verticalScroll = 24;   // start ~C2..C5 so the user lands in the
                                     // octaves Drocetti pads/synths sit in
        float pixelsPerStep  = 14.0f;
        bool  oneShotMode    = false;

        enum class DragMode { Move, Resize };
        struct Drag
        {
            bool     active = false;
            DragMode mode   = DragMode::Move;
            int      origNote = 0;
            double   origStartBeat = 0.0;
            double   origLengthBeat = 0.25;
            float    origVelocity = 0.85f;
            double   grabBeatOffset = 0.0;
            int      grabPitchOffset = 0;
        };
        Drag drag;
    };
}
