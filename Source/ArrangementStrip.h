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

        // v1.5.0 — right-click (or alt-click) on a region tile calls this with
        // the region's index so the editor can remove it. Empty arrangement is
        // allowed; the `+` button becomes the only interactive element.
        std::function<void (int regionIndex)> onDeleteRegion;

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (0.5f);

            // Gothic panel
            g.setColour (juce::Colour (GothicPalette::kPanel));
            g.fillRoundedRectangle (bounds, 10.0f);
            g.setColour (juce::Colour (GothicPalette::kPanelEdge));
            g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

            auto inner = bounds.reduced (10.0f, 8.0f);
            const auto appendButton = getAppendButtonBounds (inner);
            inner = inner.withTrimmedRight (appendButton.getWidth() + 10.0f);

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
                // left-side label (outside the note area)
                g.setColour (juce::Colour (kLanes[i].col).withAlpha (0.95f));
                g.drawText (kLanes[i].label,
                            juce::Rectangle<float> (labelArea.getX() + 2.0f, yTop,
                                                    labelArea.getWidth() - 6.0f, laneH),
                            juce::Justification::centredRight, false);
                // lane divider tick inside the note area
                g.setColour (juce::Colour (kLanes[i].col).withAlpha (0.10f));
                g.drawLine (inner.getX(), yTop, inner.getRight(), yTop, 0.6f);
            }

            // --- Background beat grid (faint 16ths, brighter on each beat) --
            g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.25f));
            const int totalSixteenths = (int) std::ceil (totalBeats * 4.0);
            for (int s = 0; s <= totalSixteenths; ++s)
            {
                const float x = inner.getX() + (float) (s * 0.25) * pxPerBeat;
                g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            }
            g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.55f));
            for (int b = 0; b <= (int) std::ceil (totalBeats); ++b)
            {
                const float x = inner.getX() + (float) b * pxPerBeat;
                g.drawLine (x, inner.getY(), x, inner.getBottom(),
                            (b % 4 == 0) ? 1.2f : 0.6f);
            }

            // --- Render each region end-to-end + vertical dividers --------
            double regionOffset = 0.0;
            for (size_t i = 0; i < last.regions.size(); ++i)
            {
                const auto&  region    = last.regions[i];
                const double regionLen = std::max (0.001, region.lengthInBeats);
                const float  regionX0  = inner.getX()
                                         + (float) regionOffset * pxPerBeat;

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

                    // soft outer glow
                    g.setColour (base.withAlpha (0.12f + 0.30f * v));
                    g.fillRoundedRectangle (
                        juce::Rectangle<float> (x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f), 4.0f);
                    // solid block, brightness scales with velocity
                    g.setColour (base.withAlpha (0.55f + 0.45f * v));
                    g.fillRoundedRectangle (juce::Rectangle<float> (x, y, w, h), 3.0f);
                    // thin highlight top edge for a 3-D-ish feel
                    g.setColour (base.withAlpha (0.25f + 0.30f * v).brighter (0.4f));
                    g.drawLine (x + 1.0f, y + 0.5f, x + w - 1.0f, y + 0.5f, 1.0f);
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

        void mouseUp (const juce::MouseEvent& e) override
        {
            const auto inner = getLocalBounds().toFloat().reduced (10.5f, 8.5f);
            if (onAppend != nullptr && getAppendButtonBounds (inner).contains (e.position))
            {
                onAppend();
                return;
            }

            // v1.5.0 — right-click or alt-click deletes the region under the cursor.
            if (onDeleteRegion != nullptr
                && (e.mods.isRightButtonDown() || e.mods.isAltDown())
                && last.totalBeats > 0.0 && ! last.regions.empty())
            {
                // v1.6.1 — skip the label column + header row the paint routine
                // carves out so clicks map to the correct beat.
                auto gridRect = inner.withTrimmedRight (getAppendButtonBounds (inner).getWidth() + 10.0f)
                                     .withTrimmedLeft  (54.0f + 6.0f)
                                     .withTrimmedTop   (14.0f);
                if (! gridRect.contains (e.position)) return;
                const double total = std::max (1.0, last.totalBeats);
                const double rel = (e.position.x - gridRect.getX()) / gridRect.getWidth();
                const double beat = juce::jlimit (0.0, total - 1e-6, rel * total);
                double acc = 0.0;
                for (size_t i = 0; i < last.regions.size(); ++i)
                {
                    const double len = std::max (0.001, last.regions[i].lengthInBeats);
                    if (beat < acc + len) { onDeleteRegion ((int) i); return; }
                    acc += len;
                }
            }
        }

    private:
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
        Snapshot                  last;
    };
}
