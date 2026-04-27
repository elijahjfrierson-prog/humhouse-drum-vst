#pragma once

#include <JuceHeader.h>

// Minimal, self-contained look-and-feel.
//
// v1.6.1-rc.7 — repainted from "gothic purple" to a monochrome sunburst.
// v1.6.1-rc.14 — recolored AGAIN per user reference: "MAKE IT GOLD WE ARE
// GOLD EVERYWEHERE JUST LIKE THIS PICTURE INSTEAD OF WHITE TO BLACK
// GRADIENT". Background stays deep/near-black (sunburst left→right
// black → amber → bright gold) so labels read against the panel; every
// "accent" / arc / on-state / fill bar is now in the gold family. Class
// names + symbol names stay (GothicLookAndFeel, GothicPalette) so the
// editor and every existing call site keeps working — only the colour
// values shifted.
//
// Thin serif titles, tracked-out labels, thin-arc rotary knobs.
namespace aidrum
{
    struct GothicPalette
    {
        // Background blacks (warmer than rc.7 — slight amber undertone
        // so the gold accents don't look stranded on cold graphite).
        static constexpr juce::uint32 kInk        = 0xff0a0905;  // near-black, amber-tinted
        static constexpr juce::uint32 kPanel      = 0xff15110a;  // panel base
        static constexpr juce::uint32 kPanelEdge  = 0xff2a2418;

        // v1.6.1-rc.14 — Accent family is GOLD. Active arc on every knob,
        // on-state of every button, region intensity strip fill, fill
        // dropdown selection, etc. all use these.
        static constexpr juce::uint32 kAccent     = 0xffe8c14a;  // bright gold
        static constexpr juce::uint32 kAccentSoft = 0xffd4af37;  // antique gold
        static constexpr juce::uint32 kAccentDeep = 0xff5c4214;  // deep amber

        // Foreground / labels (warm cream so they read on the dark panel
        // without competing with the gold accents).
        static constexpr juce::uint32 kBone       = 0xfff5e2a8;  // warm cream
        static constexpr juce::uint32 kSilver     = 0xffc4a766;  // dim gold
        static constexpr juce::uint32 kMuted      = 0xff8a7642;  // bronze

        // v1.6.1-rc.14 — sunburst stops shifted from B/W to dark→gold so
        // the background gradient reads as "gold light" sweeping across
        // the panel (matches the marketing reference image).
        static constexpr juce::uint32 kSunLeft    = 0xff060503;  // jet black, amber-tinted
        static constexpr juce::uint32 kSunMid     = 0xff2e2210;  // dark amber
        static constexpr juce::uint32 kSunRight   = 0xffe8c14a;  // gold glow
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

        // v1.6.1-rc.15 — fully 3D-rendered gold rotary. Drop shadow,
        // recessed socket, brushed-gold dome with top-left specular,
        // beveled inner cap, indicator wedge with glow. Mirrors the
        // photoreal depth language of the bundled drum kit render so
        // every knob sits in the panel instead of floating on it.
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float pos, float angStart, float angEnd,
                               juce::Slider&) override
        {
            const auto fullBounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
            const float R     = juce::jmin (fullBounds.getWidth(), fullBounds.getHeight()) * 0.5f;
            const auto  centre = fullBounds.getCentre();

            const auto goldHi   = juce::Colour (GothicPalette::kAccent);
            const auto goldMid  = juce::Colour (GothicPalette::kAccentSoft);
            const auto goldLo   = juce::Colour (GothicPalette::kAccentDeep);
            const auto bone     = juce::Colour (GothicPalette::kBone);
            const auto ink      = juce::Colour (GothicPalette::kInk);

            // --- Drop shadow under the whole knob (depth on the panel)
            {
                juce::DropShadow ds (juce::Colours::black.withAlpha (0.55f), 8, { 0, 3 });
                juce::Path shape;
                shape.addEllipse (centre.x - R, centre.y - R, R * 2.0f, R * 2.0f);
                ds.drawForPath (g, shape);
            }

            // --- Recessed socket: dark radial well the dome sits inside
            {
                const float socketR = R + 1.0f;
                juce::ColourGradient grad (
                    ink.darker (0.5f),                    centre.x, centre.y + socketR,
                    goldLo.withMultipliedAlpha (0.85f),   centre.x, centre.y - socketR,
                    false);
                grad.addColour (0.7, ink);
                g.setGradientFill (grad);
                g.fillEllipse (centre.x - socketR, centre.y - socketR, socketR * 2.0f, socketR * 2.0f);

                // socket inner ring shadow (AO crease)
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.drawEllipse (centre.x - R, centre.y - R, R * 2.0f, R * 2.0f, 1.0f);
            }

            // --- Outer arc track + active arc, sitting on the socket lip
            const float trackR = R - 2.5f;
            const float trackW = 3.2f;
            const float angCur = angStart + pos * (angEnd - angStart);
            {
                juce::Path track;
                track.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f, angStart, angEnd, true);
                g.setColour (goldLo.withAlpha (0.85f));
                g.strokePath (track, juce::PathStrokeType (trackW,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));

                // Soft glow under the active arc — feels lit-up
                juce::Path glow;
                glow.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f, angStart, angCur, true);
                g.setColour (goldHi.withAlpha (0.30f));
                g.strokePath (glow, juce::PathStrokeType (trackW * 2.6f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
                juce::Path active;
                active.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f, angStart, angCur, true);
                g.setColour (goldHi);
                g.strokePath (active, juce::PathStrokeType (trackW,
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }

            // --- 3D dome body: brushed-gold radial gradient, light from
            //     the top-left, dark at the bottom-right
            const float domeR = R * 0.78f;
            {
                juce::ColourGradient dome (
                    goldHi.brighter (0.45f),  centre.x - domeR * 0.55f, centre.y - domeR * 0.55f,
                    goldLo,                   centre.x + domeR * 0.65f, centre.y + domeR * 0.65f,
                    true);
                dome.addColour (0.55, goldMid);
                g.setGradientFill (dome);
                g.fillEllipse (centre.x - domeR, centre.y - domeR, domeR * 2.0f, domeR * 2.0f);
            }

            // Concentric brushed-metal rings (machined-knurl feel)
            for (int i = 0; i < 3; ++i)
            {
                const float ringR = domeR - 2.5f - (float) i * 2.0f;
                if (ringR <= 4.0f) break;
                g.setColour (goldLo.withAlpha (0.18f + 0.10f * (float) i));
                g.drawEllipse (centre.x - ringR, centre.y - ringR, ringR * 2.0f, ringR * 2.0f, 0.55f);
            }

            // Specular highlight (top-left) — small bright crescent
            {
                const float specR = domeR * 0.82f;
                juce::ColourGradient spec (
                    juce::Colours::white.withAlpha (0.55f), centre.x - domeR * 0.45f, centre.y - domeR * 0.55f,
                    juce::Colours::white.withAlpha (0.0f),  centre.x - domeR * 0.05f, centre.y - domeR * 0.05f,
                    true);
                g.setGradientFill (spec);
                juce::Path arc;
                arc.startNewSubPath (centre.x - specR, centre.y);
                arc.addCentredArc (centre.x, centre.y, specR, specR, 0.0f,
                                   juce::degreesToRadians (-150.0f),
                                   juce::degreesToRadians (-30.0f), true);
                g.fillPath (arc);
            }

            // Crisp inner cap (the recessed face the indicator sits on)
            const float capR = domeR * 0.62f;
            {
                juce::ColourGradient cap (
                    ink.brighter (0.10f), centre.x, centre.y - capR,
                    ink.darker  (0.45f),  centre.x, centre.y + capR,
                    false);
                g.setGradientFill (cap);
                g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);

                // Bevel: bright top edge
                g.setColour (goldHi.withAlpha (0.55f));
                juce::Path topBevel;
                topBevel.addCentredArc (centre.x, centre.y, capR, capR, 0.0f,
                                        juce::degreesToRadians (-130.0f),
                                        juce::degreesToRadians ( -50.0f), true);
                g.strokePath (topBevel, juce::PathStrokeType (1.2f));

                // Bevel: dark bottom edge
                g.setColour (juce::Colours::black.withAlpha (0.65f));
                juce::Path botBevel;
                botBevel.addCentredArc (centre.x, centre.y, capR, capR, 0.0f,
                                        juce::degreesToRadians (  50.0f),
                                        juce::degreesToRadians ( 130.0f), true);
                g.strokePath (botBevel, juce::PathStrokeType (1.0f));
            }

            // --- Indicator wedge: tapered gold pointer with luminous tip
            {
                const float tipR  = domeR - 1.5f;
                const float baseR = capR  + 1.0f;
                const float wBase = 2.4f;
                const float wTip  = 1.0f;
                juce::Path wedge;
                wedge.startNewSubPath (-wBase, -baseR);
                wedge.lineTo           ( wBase, -baseR);
                wedge.lineTo           ( wTip,  -tipR);
                wedge.lineTo           (-wTip,  -tipR);
                wedge.closeSubPath();

                const auto rot = juce::AffineTransform::rotation (angCur).translated (centre);

                // Pointer glow halo
                g.setColour (goldHi.withAlpha (0.45f));
                g.fillPath (wedge, rot.followedBy (juce::AffineTransform::scale (1.6f, 1.0f, centre.x, centre.y)));

                // Pointer body — bone with gold gradient
                juce::ColourGradient ptr (
                    bone.brighter (0.25f), centre.x, centre.y - tipR,
                    goldHi,                centre.x, centre.y - baseR,
                    false);
                g.setGradientFill (ptr);
                g.fillPath (wedge, rot);

                // Bright tip dot
                const float tx = centre.x + std::sin (angCur) *  tipR;
                const float ty = centre.y + -std::cos (angCur) * tipR;
                juce::ignoreUnused (tx, ty);
            }
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
