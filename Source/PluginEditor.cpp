#include "PluginEditor.h"

AIDrumAudioProcessorEditor::AIDrumAudioProcessorEditor (AIDrumAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (640, 380);

    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    auto addRotary = [this] (juce::Slider& s, juce::Label& l)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        addAndMakeVisible (s);
        l.setJustificationType (juce::Justification::centred);
        l.attachToComponent (&s, false);
        addAndMakeVisible (l);
    };

    addRotary (variationSlider,  variationLabel);
    addRotary (complexitySlider, complexityLabel);
    addRotary (velocitySlider,   velocityLabel);
    addRotary (humanizeSlider,   humanizeLabel);

    // Pattern length combo
    patternLengthBox.addItemList (
        juce::StringArray { "1/16 note", "1/8 note", "1/4 note", "1/2 bar", "1 bar", "2 bars" },
        1);
    patternLengthLabel.setJustificationType (juce::Justification::centred);
    patternLengthLabel.attachToComponent (&patternLengthBox, false);
    addAndMakeVisible (patternLengthBox);
    addAndMakeVisible (patternLengthLabel);

    // Mode combo
    modeBox.addItem ("Groove", 1);
    modeBox.addItem ("Fill",   2);
    modeLabel.setJustificationType (juce::Justification::centred);
    modeLabel.attachToComponent (&modeBox, false);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (modeLabel);

    // Big "+" button → generate a fresh pattern every press.
    plusButton.setButtonText ("+");
    auto plusFont = juce::Font (juce::FontOptions (48.0f, juce::Font::bold));
    plusButton.setColour (juce::TextButton::buttonColourId,   juce::Colour::fromRGB (70, 140, 255));
    plusButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour::fromRGB (40, 110, 220));
    plusButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
    plusButton.setConnectedEdges (0);
    plusButton.onClick = [this]
    {
        const auto mode = (modeBox.getSelectedId() == 2)
                            ? aidrum::GenerationMode::Fill
                            : aidrum::GenerationMode::Groove;
        processorRef.requestGeneration (mode);
    };
    addAndMakeVisible (plusButton);

    plusHelper.setJustificationType (juce::Justification::centred);
    plusHelper.setFont (juce::Font (juce::FontOptions (12.0f)));
    plusHelper.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (plusHelper);

    auto& apvts = processorRef.getAPVTS();
    variationAttachment     = std::make_unique<SliderAttachment> (apvts, "variation",     variationSlider);
    complexityAttachment    = std::make_unique<SliderAttachment> (apvts, "complexity",    complexitySlider);
    velocityAttachment      = std::make_unique<SliderAttachment> (apvts, "velocity",      velocitySlider);
    humanizeAttachment      = std::make_unique<SliderAttachment> (apvts, "humanize",      humanizeSlider);
    patternLengthAttachment = std::make_unique<ComboAttachment>  (apvts, "patternLength", patternLengthBox);
    modeAttachment          = std::make_unique<ComboAttachment>  (apvts, "mode",          modeBox);
}

AIDrumAudioProcessorEditor::~AIDrumAudioProcessorEditor() = default;

void AIDrumAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (28, 28, 32));
}

void AIDrumAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    titleLabel.setBounds (area.removeFromTop (32));

    // Top row: 4 knobs.
    auto knobRow = area.removeFromTop (150);
    const int knobW = knobRow.getWidth() / 4;
    variationSlider .setBounds (knobRow.removeFromLeft (knobW).reduced (8, 22));
    complexitySlider.setBounds (knobRow.removeFromLeft (knobW).reduced (8, 22));
    velocitySlider  .setBounds (knobRow.removeFromLeft (knobW).reduced (8, 22));
    humanizeSlider  .setBounds (knobRow.reduced (8, 22));

    // Middle row: pattern length + mode combos.
    auto comboRow = area.removeFromTop (70);
    const int comboW = comboRow.getWidth() / 2;
    patternLengthBox.setBounds (comboRow.removeFromLeft (comboW).reduced (20, 30));
    modeBox         .setBounds (comboRow.reduced (20, 30));

    // Bottom: big round "+" button.
    auto bottom = area.reduced (10);
    const int plusSize = juce::jmin (90, bottom.getHeight());
    const int plusX    = bottom.getCentreX() - plusSize / 2;
    const int plusY    = bottom.getY() + (bottom.getHeight() - plusSize) / 2;
    plusButton.setBounds (plusX, plusY, plusSize, plusSize);
    plusHelper.setBounds (plusX - 40, plusY + plusSize - 6, plusSize + 80, 18);
}
