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

    // Big round "+" button that auto-generates a new pattern on each press.
    juce::TextButton  plusButton      { "+" };
    juce::Label       plusHelper      { {}, "Generate" };
    juce::Label       titleLabel      { {}, "AI Drum VST" };

    juce::Slider      variationSlider;
    juce::Slider      complexitySlider;
    juce::Slider      velocitySlider;
    juce::Slider      humanizeSlider;

    juce::Label       variationLabel  { {}, "Variation" };
    juce::Label       complexityLabel { {}, "Complexity" };
    juce::Label       velocityLabel   { {}, "Velocity" };
    juce::Label       humanizeLabel   { {}, "Humanize" };

    juce::ComboBox    patternLengthBox;
    juce::Label       patternLengthLabel { {}, "Pattern Length" };

    juce::ComboBox    modeBox;
    juce::Label       modeLabel       { {}, "Mode" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> variationAttachment;
    std::unique_ptr<SliderAttachment> complexityAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<SliderAttachment> humanizeAttachment;
    std::unique_ptr<ComboAttachment>  patternLengthAttachment;
    std::unique_ptr<ComboAttachment>  modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessorEditor)
};
