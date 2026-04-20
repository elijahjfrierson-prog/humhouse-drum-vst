#include "PluginEditor.h"

AIDrumAudioProcessorEditor::AIDrumAudioProcessorEditor (AIDrumAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (520, 300);

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

    addRotary (variationSlider, variationLabel);
    addRotary (densitySlider,   densityLabel);

    modeBox.addItem ("Groove", 1);
    modeBox.addItem ("Fill",   2);
    modeLabel.setJustificationType (juce::Justification::centred);
    modeLabel.attachToComponent (&modeBox, false);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (modeLabel);

    generateButton.onClick = [this]
    {
        const auto mode = (modeBox.getSelectedId() == 2)
                            ? aidrum::GenerationMode::Fill
                            : aidrum::GenerationMode::Groove;
        processorRef.requestGeneration (mode);
    };
    addAndMakeVisible (generateButton);

    auto& apvts = processorRef.getAPVTS();
    variationAttachment = std::make_unique<SliderAttachment> (apvts, "variation", variationSlider);
    densityAttachment   = std::make_unique<SliderAttachment> (apvts, "density",   densitySlider);
    modeAttachment      = std::make_unique<ComboAttachment>  (apvts, "mode",      modeBox);
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

    auto controls = area.removeFromTop (180);
    const int knobW = controls.getWidth() / 3;
    variationSlider.setBounds (controls.removeFromLeft (knobW).reduced (8, 22));
    densitySlider  .setBounds (controls.removeFromLeft (knobW).reduced (8, 22));
    modeBox        .setBounds (controls.reduced (8, 70));

    generateButton.setBounds (area.reduced (80, 10));
}
