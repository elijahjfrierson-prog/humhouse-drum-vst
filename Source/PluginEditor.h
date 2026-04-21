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
    // Custom draggable "chip": initiates an external drag-and-drop of a
    // temporary .mid file when the user drags it out of the window.
    class MidiDragHandle : public juce::Component
    {
    public:
        explicit MidiDragHandle (AIDrumAudioProcessor& p) : processorRef (p) {}

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

    private:
        AIDrumAudioProcessor& processorRef;
        bool   dragStarted = false;
        juce::File tempMidiFile;
    };

    AIDrumAudioProcessor& processorRef;

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

    juce::ComboBox    genreBox;
    juce::Label       genreLabel      { {}, "Genre" };

    juce::ComboBox    modeBox;
    juce::Label       modeLabel       { {}, "Mode" };

    juce::TextButton  saveMidiButton  { "Save MIDI..." };
    MidiDragHandle    dragHandle      { processorRef };

    std::unique_ptr<juce::FileChooser> fileChooser;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> variationAttachment;
    std::unique_ptr<SliderAttachment> complexityAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<SliderAttachment> humanizeAttachment;
    std::unique_ptr<ComboAttachment>  patternLengthAttachment;
    std::unique_ptr<ComboAttachment>  genreAttachment;
    std::unique_ptr<ComboAttachment>  modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessorEditor)
};
