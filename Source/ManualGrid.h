#pragma once

#include "GothicLookAndFeel.h"
#include "MidiPattern.h"

#include <JuceHeader.h>

#include <functional>
#include <vector>

namespace aidrum
{
    // Interactive 16-bar step sequencer. Each row is a drum voice
    // (GM note), each column is a 16th-note step across all bars.
    //
    // Click a cell to toggle a note on/off. Drag vertically while
    // holding on an active cell to adjust velocity (default 0.85,
    // floor = 0.25 = ghost). Alt-click removes.
    class ManualGrid : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        struct Row
        {
            juce::String label;     // e.g. "KICK"
            int          midiNote;  // GM drum note
            juce::Colour colour;    // highlight colour for active cells
        };

        ManualGrid()
        {
            // v1.6.0 — simplified 5-row step sequencer (user request). The
            // expanded 11-row grid was confusing; drummers think in KICK /
            // SNARE / HAT / CRASH / TOM buckets, so the manual mode mirrors
            // that. "ADD TO ARRANGEMENT" commits the 16-bar grid as a new
            // region at the end of the arrangement.
            //
            // Ordered top → bottom: cymbals at the top, kick at the bottom
            // — mirrors how drummers notate kits.
            rows = {
                { "CRASH",  49, juce::Colour (GothicPalette::kAccentSoft) },
                { "HI-HAT", 42, juce::Colour (GothicPalette::kAccentSoft) },
                { "TOM",    45, juce::Colour (GothicPalette::kAccent)     },
                { "SNARE",  38, juce::Colour (GothicPalette::kBone)       },
                { "KICK",   36, juce::Colour (GothicPalette::kAccentDeep) },
            };
            setInterceptsMouseClicks (true, false);
        }

        // Callbacks — the editor wires these into the processor.
        std::function<MidiPattern()> provider;                                      // returns current manual pattern
        std::function<void (int midiNote, int step, float velocity)> onSetCell;     // turn on / set vel
        std::function<void (int midiNote, int step)>                 onClearCell;   // remove note

        void setNumBars (int bars) { numBars = juce::jlimit (1, 64, bars); repaint(); }
        int  getNumBars() const    { return numBars; }

        // v1.5.0 — step subdivision. 16 = 1/16 notes (Logic default), 32 = 1/32,
        // 64 = 1/64. Changes the draw resolution of the manual grid.
        void setStepsPerBar (int s)
        {
            stepsPerBar = (s == 64 ? 64 : s == 32 ? 32 : 16);
            repaint();
        }
        int  getStepsPerBar() const { return stepsPerBar; }
        double stepBeats() const { return 4.0 / (double) stepsPerBar; }
        int totalSteps() const { return numBars * stepsPerBar; }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (GothicPalette::kInk));
            g.fillRoundedRectangle (r, 8.0f);

            g.setColour (juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

            const float labelW = 66.0f;
            const auto  grid   = r.reduced (8.0f).withTrimmedLeft (labelW);
            const float rowH   = grid.getHeight() / (float) rows.size();
            const float stepW  = grid.getWidth()  / (float) totalSteps();

            // Row labels + alternating row band
            auto f = juce::Font (juce::FontOptions (9.5f, juce::Font::bold));
            f.setExtraKerningFactor (0.25f);
            g.setFont (f);
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const float y = grid.getY() + (float) i * rowH;
                if ((i & 1) == 0)
                {
                    g.setColour (juce::Colour (GothicPalette::kPanel).withAlpha (0.55f));
                    g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
                }
                g.setColour (juce::Colour (GothicPalette::kMuted));
                g.drawText (rows[i].label,
                            juce::Rectangle<float> (r.getX() + 4.0f, y, labelW - 8.0f, rowH),
                            juce::Justification::centredRight, false);
            }

            // Vertical grid lines — thick every beat (4 steps), thicker every bar (16 steps).
            for (int s = 0; s <= totalSteps(); ++s)
            {
                const float x = grid.getX() + (float) s * stepW;
                const bool  isBar  = (s % stepsPerBar) == 0;
                const bool  isBeat = (s % (stepsPerBar / 4)) == 0;
                g.setColour (juce::Colour (GothicPalette::kAccent)
                               .withAlpha (isBar ? 0.45f : isBeat ? 0.18f : 0.08f));
                g.fillRect (x - (isBar ? 0.75f : 0.4f), grid.getY(),
                            isBar ? 1.4f : 0.8f, grid.getHeight());
            }
            g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (0.3f));
            g.drawLine (grid.getX(), grid.getBottom(), grid.getRight(), grid.getBottom(), 0.8f);

            // Active cells — drawn per-note from provider snapshot.
            if (provider)
            {
                const auto pattern = provider();
                const double patternBeats = juce::jmax (1.0, pattern.lengthInBeats);
                const double sb = stepBeats();
                const int    stepsInPattern = (int) std::round (patternBeats / sb);

                for (const auto& n : pattern.notes)
                {
                    const int rowIdx = indexOfNote (n.noteNumber);
                    if (rowIdx < 0) continue;
                    const int s = (int) std::round (n.startBeat / sb);
                    if (s < 0 || s >= stepsInPattern) continue;

                    const float x = grid.getX() + (float) s * stepW;
                    const float y = grid.getY() + (float) rowIdx * rowH;
                    const float cw = juce::jmax (2.0f, stepW - 1.2f);
                    const float ch = juce::jmax (4.0f, rowH  - 3.0f);

                    auto colour = rows[(size_t) rowIdx].colour
                                    .withAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, n.velocity));
                    g.setColour (colour);
                    g.fillRoundedRectangle (x + 0.6f, y + 1.5f, cw, ch, 2.0f);
                }
            }

            // Header strip — bar numbers.
            auto hf = juce::Font (juce::FontOptions (9.0f, juce::Font::bold));
            hf.setExtraKerningFactor (0.25f);
            g.setFont (hf);
            g.setColour (juce::Colour (GothicPalette::kMuted));
            for (int b = 0; b < numBars; ++b)
            {
                const float x = grid.getX() + (float) (b * stepsPerBar) * stepW;
                g.drawText (juce::String (b + 1),
                            juce::Rectangle<float> (x + 2.0f, r.getY() + 2.0f, 24.0f, 12.0f),
                            juce::Justification::left, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto hit = hitTest (e.position.toInt());
            if (hit.row < 0) return;

            const int  note  = rows[(size_t) hit.row].midiNote;
            const bool alt   = e.mods.isAltDown() || e.mods.isRightButtonDown();
            const bool onNow = cellActive (note, hit.step);

            if (alt || onNow)
            {
                if (onClearCell) onClearCell (note, hit.step);
                drawingActive = false;
            }
            else
            {
                if (onSetCell) onSetCell (note, hit.step, 0.85f);
                drawingActive = true;
            }
            lastNote = note; lastStep = hit.step;
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto hit = hitTest (e.position.toInt());
            if (hit.row < 0) return;
            const int note = rows[(size_t) hit.row].midiNote;
            if (note == lastNote && hit.step == lastStep) return;

            if (drawingActive)
            {
                if (onSetCell) onSetCell (note, hit.step, 0.85f);
            }
            else
            {
                if (onClearCell) onClearCell (note, hit.step);
            }
            lastNote = note; lastStep = hit.step;
            repaint();
        }

    private:
        struct Hit { int row = -1, step = -1; };

        Hit hitTest (juce::Point<int> p) const
        {
            const float labelW = 66.0f;
            auto grid = getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (labelW);
            if (! grid.contains (p.toFloat())) return {};
            const float rowH  = grid.getHeight() / (float) rows.size();
            const float stepW = grid.getWidth()  / (float) totalSteps();
            const int rowIdx  = (int) ((p.y - grid.getY()) / rowH);
            const int stepIdx = (int) ((p.x - grid.getX()) / stepW);
            if (rowIdx < 0 || rowIdx >= (int) rows.size()) return {};
            if (stepIdx < 0 || stepIdx >= totalSteps()) return {};
            return { rowIdx, stepIdx };
        }

        int indexOfNote (int note) const
        {
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].midiNote == note) return (int) i;
            return -1;
        }

        bool cellActive (int midiNote, int step) const
        {
            if (! provider) return false;
            const auto pattern = provider();
            const double sb = stepBeats();
            for (const auto& n : pattern.notes)
            {
                const int s = (int) std::round (n.startBeat / sb);
                if (s == step && n.noteNumber == midiNote) return true;
            }
            return false;
        }

        std::vector<Row> rows;
        int numBars = 16;
        int stepsPerBar = 16;
        int lastNote = -1, lastStep = -1;
        bool drawingActive = true;
    };
}
