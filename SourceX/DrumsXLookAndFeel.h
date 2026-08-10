#pragma once

#include <JuceHeader.h>

namespace hhx
{
    /** Dark slate + gold styling: thin arc knobs, flat panels, no bevels. */
    class DrumsXLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        static juce::Colour bg()        { return juce::Colour (0xff141619); }
        static juce::Colour panel()     { return juce::Colour (0xff1c1f24); }
        static juce::Colour panelHi()   { return juce::Colour (0xff23272e); }
        static juce::Colour line()      { return juce::Colour (0xff32373f); }
        static juce::Colour text()      { return juce::Colour (0xffe6e8ec); }
        static juce::Colour textDim()   { return juce::Colour (0xff8b929c); }
        static juce::Colour accent()    { return juce::Colour (0xffe8b23a); }
        static juce::Colour accentDim() { return juce::Colour (0x55e8b23a); }

        DrumsXLookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, bg());
            setColour (juce::Label::textColourId, text());
            setColour (juce::Slider::textBoxTextColourId, text());
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::ComboBox::backgroundColourId, panelHi());
            setColour (juce::ComboBox::textColourId, text());
            setColour (juce::ComboBox::outlineColourId, line());
            setColour (juce::ComboBox::arrowColourId, textDim());
            setColour (juce::PopupMenu::backgroundColourId, panelHi());
            setColour (juce::PopupMenu::textColourId, text());
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent().withAlpha (0.25f));
            setColour (juce::PopupMenu::highlightedTextColourId, text());
            setColour (juce::TextButton::buttonColourId, panelHi());
            setColour (juce::TextButton::buttonOnColourId, accent());
            setColour (juce::TextButton::textColourOffId, text());
            setColour (juce::TextButton::textColourOnId, juce::Colour (0xff17181b));
            setColour (juce::ToggleButton::textColourId, text());
            setColour (juce::ToggleButton::tickColourId, accent());
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                               float pos, float startAngle, float endAngle,
                               juce::Slider& s) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
            const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const auto angle  = startAngle + pos * (endAngle - startAngle);
            const float thick = juce::jmax (2.5f, radius * 0.13f);

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius - thick, radius - thick,
                                 0.0f, startAngle, endAngle, true);
            g.setColour (line());
            g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
            const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;

            juce::Path value;
            value.addCentredArc (centre.x, centre.y, radius - thick, radius - thick,
                                 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (s.isEnabled() ? accent() : accent().withAlpha (0.35f));
            g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            // Centre disc + pointer.
            g.setColour (panelHi());
            g.fillEllipse (juce::Rectangle<float> (radius * 1.34f, radius * 1.34f).withCentre (centre));
            g.setColour (line());
            g.drawEllipse (juce::Rectangle<float> (radius * 1.34f, radius * 1.34f).withCentre (centre), 1.0f);

            juce::Path pointer;
            pointer.addRoundedRectangle (-1.2f, -radius * 0.66f, 2.4f, radius * 0.42f, 1.2f);
            pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
            g.setColour (text());
            g.fillPath (pointer);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                   const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override
        {
            const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            auto fill = b.getToggleState() ? accent() : backgroundColour;
            if (down)             fill = fill.brighter (0.15f);
            else if (highlighted) fill = fill.brighter (0.07f);

            g.setColour (fill);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (b.getToggleState() ? accent() : line());
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                               bool highlighted, bool down) override
        {
            const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const auto box = r.withWidth (juce::jmin (18.0f, r.getHeight())).reduced (1.0f);

            g.setColour (b.getToggleState() ? accent() : panelHi());
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (b.getToggleState() ? accent() : line());
            g.drawRoundedRectangle (box, 3.0f, 1.0f);

            if (b.getToggleState())
            {
                g.setColour (juce::Colour (0xff17181b));
                juce::Path tick;
                tick.startNewSubPath (box.getX() + box.getWidth() * 0.25f, box.getCentreY());
                tick.lineTo (box.getCentreX() - 1.0f, box.getBottom() - box.getHeight() * 0.3f);
                tick.lineTo (box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.3f);
                g.strokePath (tick, juce::PathStrokeType (2.0f));
            }

            g.setColour (b.isEnabled() ? text() : textDim());
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (b.getButtonText(), r.withTrimmedLeft (box.getWidth() + 8.0f),
                        juce::Justification::centredLeft, true);
            juce::ignoreUnused (highlighted, down);
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override
        {
            return juce::Font (juce::FontOptions (13.5f));
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool,
                           int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
            g.setColour (panelHi());
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (line());
            g.drawRoundedRectangle (r, 4.0f, 1.0f);

            juce::Path arrow;
            const float cx = r.getRight() - 14.0f, cy = r.getCentreY();
            arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
            arrow.lineTo (cx, cy + 3.0f);
            arrow.lineTo (cx + 4.0f, cy - 2.0f);
            g.setColour (box.isEnabled() ? accent() : textDim());
            g.strokePath (arrow, juce::PathStrokeType (1.6f));
        }
    };
}
