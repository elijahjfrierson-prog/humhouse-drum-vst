#pragma once

#include "GothicLookAndFeel.h"
#include "PatternStrip.h"
#include "PluginProcessor.h"

#include <JuceHeader.h>

class AIDrumAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit AIDrumAudioProcessorEditor (AIDrumAudioProcessor&);
    ~AIDrumAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    // Circular glowing "+" — drawn from scratch for the gothic aesthetic.
    class PlusButton : public juce::Button
    {
    public:
        PlusButton();
        void paintButton (juce::Graphics&, bool, bool) override;
        void bump() { glow = 1.0f; }
        void tickGlow() { glow *= 0.88f; if (glow > 0.0f) repaint(); }
    private:
        float glow = 0.0f;
    };

    // Drag-to-DAW chip.
    class MidiDragHandle : public juce::Component
    {
    public:
        explicit MidiDragHandle (AIDrumAudioProcessor& p) : processorRef (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        AIDrumAudioProcessor& processorRef;
        bool      dragStarted = false;
        juce::File tempMidiFile;
    };

    void timerCallback() override;

    AIDrumAudioProcessor&     processorRef;
    aidrum::GothicLookAndFeel gothicLnf;

    juce::Label               titleLabel    { {}, "AI  DRUM  VST" };
    juce::Label               subtitleLabel { {}, "\u2020 generative drum \u2020" };

    aidrum::PatternStrip      patternStrip;

    juce::Slider              variationSlider, complexitySlider, velocitySlider, humanizeSlider;
    juce::Label               variationLabel  { {}, "VARIATION"  };
    juce::Label               complexityLabel { {}, "COMPLEXITY" };
    juce::Label               velocityLabel   { {}, "VELOCITY"   };
    juce::Label               humanizeLabel   { {}, "HUMANIZE"   };

    juce::ComboBox            genreBox;
    juce::ComboBox            patternLengthBox;
    juce::ComboBox            modeBox;
    juce::Label               genreLabel         { {}, "GENRE"    };
    juce::Label               patternLengthLabel { {}, "LENGTH"   };
    juce::Label               modeLabel          { {}, "MODE"     };

    PlusButton                plusButton;
    juce::Label               plusHelper         { {}, "SUMMON" };

    juce::TextButton          saveMidiButton     { "SAVE MIDI" };
    MidiDragHandle            dragHandle         { processorRef };

    std::unique_ptr<juce::FileChooser> fileChooser;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> variationAttachment;
    std::unique_ptr<SliderAttachment> complexityAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<SliderAttachment> humanizeAttachment;
    std::unique_ptr<ComboAttachment>  genreAttachment;
    std::unique_ptr<ComboAttachment>  patternLengthAttachment;
    std::unique_ptr<ComboAttachment>  modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessorEditor)
};
