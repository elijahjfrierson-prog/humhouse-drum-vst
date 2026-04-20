#pragma once

#include "PluginProcessor.h"

#include <JuceHeader.h>

class AIDrumAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AIDrumAudioProcessorEditor (AIDrumAudioProcessor&);
    ~AIDrumAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;

private:
    AIDrumAudioProcessor& processorRef;

    juce::TextButton  generateButton { "Generate" };
    juce::Slider      variationSlider;
    juce::Slider      densitySlider;
    juce::ComboBox    modeBox;
    juce::Label       variationLabel { {}, "Variation" };
    juce::Label       densityLabel   { {}, "Density" };
    juce::Label       modeLabel      { {}, "Mode" };
    juce::Label       titleLabel     { {}, "AI Drum VST" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> variationAttachment;
    std::unique_ptr<SliderAttachment> densityAttachment;
    std::unique_ptr<ComboAttachment>  modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessorEditor)
};
