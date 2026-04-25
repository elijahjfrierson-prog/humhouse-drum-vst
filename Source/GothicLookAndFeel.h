#pragma once

#include <JuceHeader.h>

// Minimal, self-contained look-and-feel.
//
// v1.6.1-rc.7 — repainted from "gothic purple" to a monochrome sunburst
// (black → graphite grey → bone white) per the rc.7 brief: "change the
// color of the OVERALL PROJECT TO black, grey and white in a gradient
// sunburst like shift from left to right". The class names + symbol
// names stay (GothicLookAndFeel, GothicPalette) so the editor and other
// callers don't have to change; only the color values shifted.
//
// Thin serif titles, tracked-out labels, thin-arc rotary knobs.
namespace aidrum
{
    struct GothicPalette
    {
        // Background blacks
        static constexpr juce::uint32 kInk        = 0xff0a0a0c;  // near-black
        static constexpr juce::uint32 kPanel      = 0xff141416;  // panel base
        static constexpr juce::uint32 kPanelEdge  = 0xff2a2a2e;

        // "Accent" is now a high-contrast bone-white so the active arc on
        // every knob and the on-state of every button reads against the
        // black/grey background.
        static constexpr juce::uint32 kAccent     = 0xfff5f5f5;  // bone white
        static constexpr juce::uint32 kAccentSoft = 0xffd6d6d6;  // light grey
        static constexpr juce::uint32 kAccentDeep = 0xff3c3c40;  // graphite

        // Foreground / labels
        static constexpr juce::uint32 kBone       = 0xfff3f3f3;
        static constexpr juce::uint32 kSilver     = 0xffb8b8bc;
        static constexpr juce::uint32 kMuted      = 0xff7a7a82;

        // v1.6.1-rc.7 — explicit sunburst stops (left → right).
        // Used by PluginEditor::paint() for the background gradient.
        static constexpr juce::uint32 kSunLeft    = 0xff050507;  // jet black
        static constexpr juce::uint32 kSunMid     = 0xff2d2d31;  // mid graphite
        static constexpr juce::uint32 kSunRight   = 0xffe6e6e8;  // bone glow
    };

    class GothicLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        GothicLookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (GothicPalette::kInk));

            setColour (juce::Slider::thumbColourId,          juce::Colour (GothicPalette::kAccentSoft));
            setColour (juce::Slider::rotarySliderFillColourId,   juce::Colour (GothicPalette::kAccent));
            setColour (juce::Slider::rotarySliderOutlineColourId,juce::Colour (GothicPalette::kAccentDeep));
            setColour (juce::Slider::textBoxTextColourId,    juce::Colour (GothicPalette::kBone));
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

            setColour (juce::Label::textColourId, juce::Colour (GothicPalette::kSilver));

            setColour (juce::ComboBox::backgroundColourId, juce::Colour (GothicPalette::kPanel));
            setColour (juce::ComboBox::outlineColourId,    juce::Colour (GothicPalette::kPanelEdge));
            setColour (juce::ComboBox::textColourId,       juce::Colour (GothicPalette::kBone));
            setColour (juce::ComboBox::arrowColourId,      juce::Colour (GothicPalette::kAccentSoft));

            setColour (juce::PopupMenu::backgroundColourId,         juce::Colour (GothicPalette::kInk));
            setColour (juce::PopupMenu::textColourId,               juce::Colour (GothicPalette::kBone));
            setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (GothicPalette::kAccentDeep));
            setColour (juce::PopupMenu::highlightedTextColourId,    juce::Colour (GothicPalette::kBone));

            setColour (juce::TextButton::buttonColourId,   juce::Colour (GothicPalette::kPanel));
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (GothicPalette::kAccentDeep));
            setColour (juce::TextButton::textColourOffId,  juce::Colour (GothicPalette::kBone));
            setColour (juce::TextButton::textColourOnId,   juce::Colour (GothicPalette::kBone));
        }

        // v1.6.1-rc.7 — silky italic body face. The user asked for the
        // entire project to read "cursive-ish, less blocky" (logo aside).
        // We coerce every Label / Button / ComboBox into italic with
        // generous kerning so caps still feel tracked-out and readable
        // but the overall vibe is closer to a one-sheet than a console.
        // We deliberately preserve the caller's font HEIGHT so existing
        // call-site Font() constructors that pass explicit sizes still
        // win — only the style + tracking come from here.
        juce::Font getLabelFont (juce::Label& l) override
        {
            auto f = juce::Font (juce::FontOptions (l.getFont().getHeight(),
                                                    juce::Font::italic));
            f.setExtraKerningFactor (0.18f);
            return f;
        }

        juce::Font getTextButtonFont (juce::TextButton&, int h) override
        {
            auto f = juce::Font (juce::FontOptions ((float) h * 0.55f,
                                                    juce::Font::italic));
            f.setExtraKerningFactor (0.16f);
            return f;
        }

        juce::Font getComboBoxFont (juce::ComboBox& cb) override
        {
            auto f = juce::Font (juce::FontOptions ((float) cb.getHeight() * 0.45f,
                                                    juce::Font::italic));
            f.setExtraKerningFactor (0.14f);
            return f;
        }

        // Pop-up menus (the STARTER dropdown, etc.) inherit the same
        // silky face so the menu list visually matches the closed combo.
        juce::Font getPopupMenuFont() override
        {
            auto f = juce::Font (juce::FontOptions (14.0f, juce::Font::italic));
            f.setExtraKerningFactor (0.14f);
            return f;
        }

        // Sleek arc rotary: outer dim track, inner glowing arc, bone pointer.
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float pos, float angStart, float angEnd,
                               juce::Slider&) override
        {
            const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
            const float r     = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float trackW = 3.5f;

            // Dim outer track
            {
                juce::Path p;
                p.addCentredArc (centre.x, centre.y, r, r, 0.0f, angStart, angEnd, true);
                g.setColour (juce::Colour (GothicPalette::kAccentDeep).withAlpha (0.55f));
                g.strokePath (p, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Active arc (accent, with a softer glow layer underneath)
            const float angCur = angStart + pos * (angEnd - angStart);
            {
                juce::Path glow;
                glow.addCentredArc (centre.x, centre.y, r, r, 0.0f, angStart, angCur, true);
                g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (0.25f));
                g.strokePath (glow, juce::PathStrokeType (trackW * 2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path active;
                active.addCentredArc (centre.x, centre.y, r, r, 0.0f, angStart, angCur, true);
                g.setColour (juce::Colour (GothicPalette::kAccent));
                g.strokePath (active, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Inner dark disc
            const float innerR = r * 0.74f;
            g.setColour (juce::Colour (GothicPalette::kPanel));
            g.fillEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f);
            g.setColour (juce::Colour (GothicPalette::kPanelEdge));
            g.drawEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

            // Pointer
            juce::Path tick;
            const float tickLen = innerR * 0.78f;
            tick.addRectangle (-1.25f, -innerR + 3.0f, 2.5f, tickLen - 3.0f);
            g.setColour (juce::Colour (GothicPalette::kBone));
            g.fillPath (tick, juce::AffineTransform::rotation (angCur).translated (centre));
        }

        // Subtle gothic combo box.
        void drawComboBox (juce::Graphics& g, int width, int height, bool,
                           int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
            g.setColour (juce::Colour (GothicPalette::kPanel));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colour (GothicPalette::kPanelEdge));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);

            // Small accent triangle
            const float arrowSize = (float) height * 0.28f;
            const float cx = (float) width  - arrowSize - 10.0f;
            const float cy = (float) height * 0.5f;
            juce::Path tri;
            tri.addTriangle (cx, cy - arrowSize * 0.35f,
                             cx + arrowSize, cy - arrowSize * 0.35f,
                             cx + arrowSize * 0.5f, cy + arrowSize * 0.45f);
            g.setColour (juce::Colour (GothicPalette::kAccentSoft));
            g.fillPath (tri);

            juce::ignoreUnused (box);
        }

        // Button: filled rounded rect, purple glow when ON/hover.
        void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                   const juce::Colour&, bool isOver, bool isDown) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool on = b.getToggleState() || isDown;

            auto fill = on ? juce::Colour (GothicPalette::kAccentDeep)
                           : juce::Colour (GothicPalette::kPanel);
            if (isOver) fill = fill.brighter (0.08f);

            // outer glow
            if (on || isOver)
            {
                g.setColour (juce::Colour (GothicPalette::kAccent).withAlpha (isOver ? 0.18f : 0.30f));
                g.fillRoundedRectangle (r.expanded (3.0f), 10.0f);
            }
            g.setColour (fill);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (juce::Colour (GothicPalette::kPanelEdge));
            g.drawRoundedRectangle (r, 8.0f, 1.0f);
        }
    };

    // v1.6.1-rc.5 — thinner kerning + smaller cap so PLAY / PAUSE /
    // STOP labels always fit inside their compact transport boxes.
    class CompactGothicLookAndFeel : public GothicLookAndFeel
    {
    public:
        juce::Font getTextButtonFont (juce::TextButton&, int h) override
        {
            auto f = juce::Font (juce::FontOptions ((float) h * 0.38f, juce::Font::italic));
            f.setExtraKerningFactor (0.02f);
            return f;
        }
    };
}
