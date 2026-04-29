#pragma once

#include <JuceHeader.h>

namespace humvocal
{

struct HumHousePalette
{
    // Background blacks (cool blue-violet undertone — ultraviolet)
    static constexpr juce::uint32 kInk        = 0xff08060e;  // near-black, violet-tinted
    static constexpr juce::uint32 kPanel      = 0xff100c1a;  // panel base
    static constexpr juce::uint32 kPanelEdge  = 0xff241e38;

    // Ultraviolet purple accent family
    static constexpr juce::uint32 kAccent     = 0xffb44aff;  // bright violet / UV purple
    static constexpr juce::uint32 kAccentSoft = 0xff9933e8;  // medium purple
    static constexpr juce::uint32 kAccentDeep = 0xff3d1a6e;  // deep violet

    // Foreground / labels (cool lavender-white so they read on dark panels)
    static constexpr juce::uint32 kBone       = 0xffe0d4f5;  // lavender cream
    static constexpr juce::uint32 kSilver     = 0xffa08cc8;  // dim lavender
    static constexpr juce::uint32 kMuted      = 0xff6a5a8a;  // dusty violet

    // Sunburst gradient (left-to-right: black → deep purple → bright UV)
    static constexpr juce::uint32 kSunLeft    = 0xff050310;  // jet black, violet-tinted
    static constexpr juce::uint32 kSunMid     = 0xff1e1040;  // deep purple
    static constexpr juce::uint32 kSunRight   = 0xffb44aff;  // UV glow

    // Module strip colours (for the signal chain)
    static constexpr juce::uint32 kModuleActive   = 0xff261840;
    static constexpr juce::uint32 kModuleInactive = 0xff120c20;
    static constexpr juce::uint32 kModuleBorder   = 0xff3a2860;

    // Pitch heatmap
    static constexpr juce::uint32 kHeatCold   = 0xff1a3a6a;  // flat (blue)
    static constexpr juce::uint32 kHeatNeutral= 0xff2a5a2a;  // in-tune (green)
    static constexpr juce::uint32 kHeatHot    = 0xff8a2a1a;  // sharp (red)
};

class HumHouseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HumHouseLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (HumHousePalette::kInk));

        setColour (juce::Slider::thumbColourId,               juce::Colour (HumHousePalette::kAccentSoft));
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (HumHousePalette::kAccent));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (HumHousePalette::kAccentDeep));
        setColour (juce::Slider::textBoxTextColourId,         juce::Colour (HumHousePalette::kBone));
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId,               juce::Colour (HumHousePalette::kAccentDeep));

        setColour (juce::Label::textColourId, juce::Colour (HumHousePalette::kSilver));

        setColour (juce::ComboBox::backgroundColourId, juce::Colour (HumHousePalette::kPanel));
        setColour (juce::ComboBox::outlineColourId,    juce::Colour (HumHousePalette::kPanelEdge));
        setColour (juce::ComboBox::textColourId,       juce::Colour (HumHousePalette::kBone));
        setColour (juce::ComboBox::arrowColourId,      juce::Colour (HumHousePalette::kAccentSoft));

        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (HumHousePalette::kInk));
        setColour (juce::PopupMenu::textColourId,                  juce::Colour (HumHousePalette::kBone));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (HumHousePalette::kAccentDeep));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (HumHousePalette::kBone));

        setColour (juce::TextButton::buttonColourId,   juce::Colour (HumHousePalette::kPanel));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (HumHousePalette::kAccentDeep));
        setColour (juce::TextButton::textColourOffId,  juce::Colour (HumHousePalette::kBone));
        setColour (juce::TextButton::textColourOnId,   juce::Colour (HumHousePalette::kBone));

        setColour (juce::ToggleButton::textColourId,   juce::Colour (HumHousePalette::kBone));
        setColour (juce::ToggleButton::tickColourId,   juce::Colour (HumHousePalette::kAccent));
    }

    // 3D-style rotary knob with ultraviolet arc
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centreX = bounds.getCentreX();
        auto centreY = bounds.getCentreY();
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Background circle
        g.setColour(juce::Colour(HumHousePalette::kPanel));
        g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

        // Outer ring
        juce::Path arcBg;
        arcBg.addCentredArc(centreX, centreY, radius - 2.0f, radius - 2.0f,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(HumHousePalette::kAccentDeep));
        g.strokePath(arcBg, juce::PathStrokeType(3.0f));

        // Active arc (UV purple)
        if (sliderPos > 0.0f)
        {
            juce::Path arcActive;
            arcActive.addCentredArc(centreX, centreY, radius - 2.0f, radius - 2.0f,
                                    0.0f, rotaryStartAngle, angle, true);
            g.setColour(juce::Colour(HumHousePalette::kAccent));
            g.strokePath(arcActive, juce::PathStrokeType(3.0f));
        }

        // Pointer line
        juce::Path pointer;
        auto pointerLen = radius * 0.6f;
        pointer.addRectangle(-1.5f, -pointerLen, 3.0f, pointerLen);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(juce::Colour(HumHousePalette::kAccent));
        g.fillPath(pointer);

        // Centre dot
        g.setColour(juce::Colour(HumHousePalette::kAccentSoft));
        g.fillEllipse(centreX - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
    }

    // Toggle button (module on/off)
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
        auto isOn = button.getToggleState();

        g.setColour(isOn ? juce::Colour(HumHousePalette::kAccentDeep) : juce::Colour(HumHousePalette::kPanel));
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(isOn ? juce::Colour(HumHousePalette::kAccent) : juce::Colour(HumHousePalette::kPanelEdge));
        g.drawRoundedRectangle(bounds, 4.0f, 1.5f);

        g.setColour(isOn ? juce::Colour(HumHousePalette::kBone) : juce::Colour(HumHousePalette::kMuted));
        g.setFont(juce::Font(bounds.getHeight() * 0.45f).italicised());
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
    }

    // Italic font for labels
    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font(13.0f).italicised();
    }
};

} // namespace humvocal
