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
        // v1.6.1-rc.12 — single bundled kit again; setActiveBundledKit()
        // is now a no-op but kept so legacy callers compile.
        void setActiveBundledKit (int kitIndex);
        void pulseBus (int bus, float velocity);
        void decayFlashes (float k);
        void paint (juce::Graphics&) override;
    private:
        int          selectedKit       = 0;
        float        flash[kNumFlashes] {};
        // v1.6.1-rc.9 — bundled 3D Yamaha kit render. Loaded once from
        // BinaryData; when present we paint the photo as the kit
        // visual and overlay small yellow flash dots at each drum's
        // approximate position so hits read as a real kit being
        // played instead of the abstract circle/silhouette.
        // v1.6.1-rc.12 — second-kit (Bay Grunge) photo removed.
        juce::Image  kitPhotoNuRock;
    };

    void timerCallback() override;

    // v1.6.1-rc.3 — rebuild STARTER dropdown to only show grooves
    // belonging to the currently-selected kit's bucket.
    void rebuildStarterBox();

    AIDrumAudioProcessor&            processorRef;
    aidrum::GothicLookAndFeel        gothicLnf;
    aidrum::CompactGothicLookAndFeel compactLnf;
    juce::TooltipWindow       tooltipWindow { this, 350 };

    juce::Label               titleLabel    { {}, "HUMHOUSE  DRUMS" };
    juce::Label               subtitleLabel { {}, "\u2020 hum. house. haunt. \u2020" };

    // v1.6.1-rc.7 — bundled HumHouse logo (greyscale grunge crest). Loaded
    // once from BinaryData and painted into the masthead. When present we
    // hide the text title so the artwork carries the brand on its own.
    juce::Image               logoImage;

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
    // v1.6.1-rc.7 — repurposed as a FILL SELECTOR. The slider is hidden
    // (it remains in the value tree so save files round-trip) and the
    // user advances through fills with the prev/next arrow buttons; the
    // current fill name is displayed in fillSelectorLabel.
    juce::Slider              fillComplexitySlider;
    juce::Label               fillComplexityLabel { {}, "FILL CX" };

    // v1.6.1-rc.13 — FILL SELECTOR is now a labeled dropdown showing all
    // 22 fills by name (gentle ghost rolls → sludge tom flares). The
    // user no longer has to step through with prev/next; they see every
    // option at a glance and click to pick. Cycler API still exists
    // under the hood so MIDI/automation can step through programmatically.
    juce::Label               fillSelectorTitle { {}, "FILL"  };
    juce::ComboBox            fillSelectorBox;

    // v1.6.1-rc.7 — INTENSITY (drives base velocity + per-hit
    // fluctuation). 0..1 stored, displayed as 0..127.
    juce::Slider              intensitySlider;
    juce::Label               intensityLabel  { {}, "INTENSITY" };

    // v1.6.1-rc.7 — HALF / NORMAL / DOUBLE transport buttons. Replaces
    // the timeScaleBox combo so the user can flip playback speed with
    // a single click instead of a dropdown.
    juce::TextButton          halfButton    { "HALF" };
    juce::TextButton          normalButton  { "NORMAL" };
    juce::TextButton          doubleButton  { "DOUBLE" };

    // v1.6.1-rc.7 — GHOST per-instrument toggle. Click an instrument row
    // label on the side of the arrangement, then click GHOST to flip
    // that lane into "ghost-velocity" mode (greyed-out row name +
    // forced ~0.25 velocity tier on every hit).
    juce::TextButton          ghostButton   { "GHOST" };

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
    juce::Label               plusHelper     { {}, "COMPOSE" };
    // v1.6.1-rc.9 — RANDOMIZE pad (full-pattern replace, the
    // pre-rc.9 COMPOSE behavior). Sits next to COMPOSE so users
    // can choose: COMPOSE molds around what they have, RANDOMIZE
    // rolls a fresh idea from scratch.
    juce::TextButton          randomizeButton { "RANDOMIZE" };
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
    std::unique_ptr<SliderAttachment> intensityAttachment;

    // v1.6.1-rc.7 — selection state for GHOST toggle.
    // -1 = no instrument selected. 0..5 = kick/snare/hat/tom/ride/crash.
    // ghostMask: bitmask, bit i set = lane i is ghost-active.
    int  ghostSelectedLane = -1;
    int  ghostMask         = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIDrumAudioProcessorEditor)
};
