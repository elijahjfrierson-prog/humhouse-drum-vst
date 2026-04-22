#pragma once

#include "ArrangementStrip.h"
#include "DrumKit.h"
#include "GothicLookAndFeel.h"
#include "ManualGrid.h"
#include "MixerPanel.h"
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

    // Drag-to-DAW chip — drags the FULL arrangement as a single .mid file.
    class MidiDragHandle : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        explicit MidiDragHandle (AIDrumAudioProcessor& p) : processorRef (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        // v1.6.1-rc.3 — fires on mouseDown (button pressed → highlight on)
        // and mouseUp (released → highlight off). Used by the editor to
        // visually highlight every region in the arrangement strip while
        // the user is grabbing the full arrangement for drag-to-DAW.
        std::function<void (bool highlight)> onHighlightChange;
    private:
        AIDrumAudioProcessor& processorRef;
        bool      dragStarted = false;
        juce::File tempMidiFile;
    };

    class XYPad : public juce::Component,
                  private juce::Timer,
                  public juce::SettableTooltipClient
    {
    public:
        XYPad();
        void bind (juce::Slider* complexity, juce::Slider* velocity);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        void timerCallback() override;
        void updateFromPoint (juce::Point<float> p);
        juce::Slider* complexitySlider = nullptr;
        juce::Slider* velocitySlider   = nullptr;
    };

    class KitVisualizer : public juce::Component
    {
    public:
        // One flash slot per DrumBusMixer bus (8 total — match DrumSynth::kNumBuses).
        static constexpr int kNumFlashes = 8;

        KitVisualizer();
        void setSelectedKit (int index);
        void pulseBus (int bus, float velocity);
        void decayFlashes (float k);
        void paint (juce::Graphics&) override;
    private:
        int   selectedKit = 0;
        float flash[kNumFlashes] {};
    };

    void timerCallback() override;

    // v1.6.1-rc.3 — rebuild STARTER dropdown to only show grooves
    // belonging to the currently-selected kit's bucket.
    void rebuildStarterBox();

    AIDrumAudioProcessor&     processorRef;
    aidrum::GothicLookAndFeel gothicLnf;
    juce::TooltipWindow       tooltipWindow { this, 350 };

    juce::Label               titleLabel    { {}, "HUMHOUSE  DRUMS" };
    juce::Label               subtitleLabel { {}, "\u2020 hum. house. haunt. \u2020" };

    aidrum::ArrangementStrip  arrangementStrip;
    aidrum::ManualGrid        manualGrid;
    KitVisualizer             kitVisualizer;
    XYPad                     xyPad;

    // Main knobs.
    juce::Slider              variationSlider, complexitySlider, velocitySlider, humanizeSlider;
    juce::Label               variationLabel  { {}, "VARIATION"  };
    juce::Label               complexityLabel { {}, "COMPLEXITY" };
    juce::Label               velocityLabel   { {}, "VELOCITY"   };
    juce::Label               humanizeLabel   { {}, "HUMANIZE"   };

    // Drummer knobs (v0.6.0).
    juce::Slider              swingSlider, fillsSlider;
    juce::Label               swingLabel { {}, "SWING" };
    juce::Label               fillsLabel { {}, "FILLS" };

    // v1.5.0 — fill complexity (independent of overall COMPLEXITY).
    juce::Slider              fillComplexitySlider;
    juce::Label               fillComplexityLabel { {}, "FILL CX" };

    // v1.5.0 — manual grid step division (1/16, 1/32, 1/64).
    juce::ComboBox            stepDivBox;
    juce::Label               stepDivLabel { {}, "STEP DIV" };

    // Combos.
    juce::ComboBox            genreBox, patternLengthBox, modeBox, hiHatBox, drumKitBox, roomBox;
    juce::Label               genreLabel         { {}, "GENRE"    };
    juce::Label               patternLengthLabel { {}, "LENGTH"   };
    juce::Label               modeLabel          { {}, "MODE"     };
    juce::Label               hiHatLabel         { {}, "HI-HAT"   };
    juce::Label               drumKitLabel       { {}, "DRUM KIT" };
    juce::Label               roomLabel          { {}, "ROOM"     };

    // v1.3.0 Room amount knob (0-100% of room ambience effect).
    juce::Slider              roomAmountSlider;
    juce::Label               roomAmountLabel    { {}, "ROOM AMT" };

    // Toggle.
    juce::TextButton          halfTimeButton { "HALF-TIME" };

    // v1.6.1-rc.4 — arrangement playback speed (HALF / NORMAL / DOUBLE).
    juce::ComboBox            timeScaleBox;
    juce::Label               timeScaleLabel { {}, "TIME" };

    // Action buttons.
    PlusButton                plusButton;
    juce::Label               plusHelper     { {}, "APPEND" };
    juce::TextButton          undoButton     { "UNDO" };
    juce::TextButton          clearButton    { "CLEAR" };

    // v1.6.0 STARTER GROOVES dropdown + COPY / PASTE region buttons.
    juce::ComboBox            starterBox;
    juce::Label               starterLabel   { {}, "STARTER" };
    juce::TextButton          copyRegionButton  { "COPY" };
    juce::TextButton          pasteRegionButton { "PASTE" };
    juce::TextButton          saveMidiButton { "SAVE MIDI" };
    juce::TextButton          playButton     { "PLAY" };
    juce::TextButton          pauseButton    { "PAUSE" };
    juce::TextButton          stopButton     { "STOP" };
    MidiDragHandle            dragHandle     { processorRef };

    // v0.8.0 Manual mode.
    juce::TextButton          manualButton   { "MANUAL" };
    juce::TextButton          clearManualButton { "CLEAR GRID" };
    juce::TextButton          commitManualButton { "ADD TO ARRANGEMENT" };

    // v1.1.0 Mixer
    juce::TextButton          mixerButton    { "MIXER" };
    aidrum::MixerPanel        mixerPanel     { processorRef.getBusMixer() };

    // v1.4.0 Sampler loader + UI scale
    juce::TextButton          loadKitButton    { "LOAD KIT" };
    juce::TextButton          unloadKitButton  { "UNLOAD" };
    juce::Label               kitPathLabel;
    juce::Slider              uiScaleSlider;
    juce::Label               uiScaleLabel     { {}, "UI SCALE" };

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> kitFolderChooser;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> variationAttachment;
    std::unique_ptr<SliderAttachment> complexityAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<SliderAttachment> humanizeAttachment;
    std::unique_ptr<SliderAttachment> swingAttachment;
    std::unique_ptr<SliderAttachment> fillsAttachment;
    std::unique_ptr<SliderAttachment> fillComplexityAttachment;
    std::unique_ptr<ComboAttachment>  stepDivAttachment;
    std::unique_ptr<ComboAttachment>  genreAttachment;
    std::unique_ptr<ComboAttachment>  patternLengthAttachment;
    std::unique_ptr<ComboAttachment>  modeAttachment;
    std::unique_ptr<ComboAttachment>  hiHatAttachment;
    std::unique_ptr<ComboAttachment>  drumKitAttachment;
    std::unique_ptr<ComboAttachment>  roomAttachment;
    std::unique_ptr<SliderAttachment> roomAmountAttachment;
    std::unique_ptr<ButtonAttachment> halfTimeAttachment;
    std::unique_ptr<ComboAttachment>  timeScaleAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessorEditor)
};
