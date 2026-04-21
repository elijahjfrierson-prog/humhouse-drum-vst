#pragma once

#include "GothicLookAndFeel.h"
#include "MidiPattern.h"

#include <JuceHeader.h>

namespace aidrum
{
    // Horizontal "piano-roll" strip that shows the current pattern as glowing
    // purple bars against a gothic black panel.
    class PatternStrip : public juce::Component,
                         private juce::Timer
    {
    public:
        PatternStrip()
        {
            startTimerHz (30);
        }

        ~PatternStrip() override { stopTimer(); }

        void setPatternProvider (std::function<MidiPattern()> p) { provider = std::move (p); }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (0.5f);

            // Gothic panel
            g.setColour (juce::Colour (GothicPalette::kPanel));
            g.fillRoundedRectangle (bounds, 10.0f);
            g.setColour (juce::Colour (GothicPalette::kPanelEdge));
            g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

            const auto inner = bounds.reduced (10.0f, 8.0f);

            // Vertical beat grid (4 beats by default)
            const double totalBeats = std::max (1.0, lastPattern.lengthInBeats);
            const int    sub        = 4; // 16ths per beat shown as faint lines
            const float  xPerBeat   = inner.getWidth() / (float) totalBeats;

            g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.55f));
            for (int b = 0; b <= (int) std::ceil (totalBeats); ++b)
            {
                const float x = inner.getX() + (float) b * xPerBeat;
                g.drawLine (x, inner.getY(), x, inner.getBottom(), (b % 4 == 0) ? 1.25f : 0.6f);
            }
            g.setColour (juce::Colour (GothicPalette::kPanelEdge).withAlpha (0.25f));
            for (int b = 0; b < (int) std::ceil (totalBeats * sub); ++b)
            {
                const float x = inner.getX() + (float) b * xPerBeat / (float) sub;
                g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            }

            // Map note numbers to vertical lanes. Show kick/snare/hat prominently,
            // everything else gets auto-assigned below.
            // Lane 0 (top): cymbals (hat / ride / crash)
            // Lane 1:       snare / clap
            // Lane 2 (bot): kick / toms
            auto laneFor = [] (int n) -> int
            {
                if (n == 35 || n == 36 || n == 41 || n == 43 || n == 45 || n == 47 || n == 48 || n == 50) return 2;
                if (n == 38 || n == 39 || n == 40 || n == 37) return 1;
                return 0; // default: hats / cymbals
            };

            const float laneCount = 3.0f;
            const float laneH     = inner.getHeight() / laneCount;

            for (const auto& note : lastPattern.notes)
            {
                const float x = inner.getX() + (float) (note.startBeat / totalBeats) * inner.getWidth();
                const float w = std::max (3.0f, (float) (std::max (note.lengthBeat, 0.1) / totalBeats) * inner.getWidth());
                const int   lane = laneFor (note.noteNumber);
                const float y = inner.getY() + (float) lane * laneH + laneH * 0.2f;
                const float h = laneH * 0.6f;

                const float v = juce::jlimit (0.15f, 1.0f, note.velocity);
                auto accent = juce::Colour (GothicPalette::kAccentSoft).withAlpha (0.18f + 0.55f * v);

                // Glow
                g.setColour (accent.withAlpha (accent.getFloatAlpha() * 0.35f));
                g.fillRoundedRectangle (juce::Rectangle<float> (x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f), 4.0f);

                // Core bar
                g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (0.85f * v + 0.15f));
                g.fillRoundedRectangle (juce::Rectangle<float> (x, y, w, h), 3.0f);
            }

            // Subtle header label
            g.setColour (juce::Colour (GothicPalette::kMuted));
            auto f = juce::Font (juce::FontOptions (10.0f));
            f.setExtraKerningFactor (0.25f);
            g.setFont (f);
            g.drawText ("PATTERN", bounds.reduced (12.0f, 4.0f),
                        juce::Justification::topLeft, false);
        }

    private:
        void timerCallback() override
        {
            if (! provider) return;
            auto now = provider();
            if (now.notes.size() != lastPattern.notes.size()
                || std::abs (now.lengthInBeats - lastPattern.lengthInBeats) > 1e-6)
            {
                lastPattern = std::move (now);
                repaint();
            }
        }

        std::function<MidiPattern()> provider;
        MidiPattern                  lastPattern;
    };
}
