#pragma once

#include "DrumsXLookAndFeel.h"
#include "DrumsXProcessor.h"

#include <JuceHeader.h>

#include <memory>
#include <vector>

namespace hhx
{
    /** A knob with its caption and live value, laid out as one unit so pages
        can just place rectangles. */
    class LabelledKnob : public juce::Component
    {
    public:
        LabelledKnob (juce::AudioProcessorValueTreeState& state,
                      const juce::String& paramID,
                      const juce::String& caption,
                      bool bipolar = false);

        void resized() override;
        void paint (juce::Graphics&) override;

        juce::Slider slider;

    private:
        juce::String captionText;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    /** Logic's XY performance pad: loud/soft against simple/complex, with the
        preset dot grid behind the puck. Dragging re-selects real takes from
        the corpus rather than nudging a probability. */
    class PerformancePad : public juce::Component,
                           private juce::Timer
    {
    public:
        explicit PerformancePad (DrumsXProcessor&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;
        void moveTo (juce::Point<float> p, bool gesture);

        DrumsXProcessor& proc;
        bool dragging = false;
        float lastX = 0.0f, lastY = 0.0f;
    };

    /** Two bars of the current performance, drawn as a piano-roll-ish strip
        with the playhead — the "what am I about to hear" view. */
    class PhraseView : public juce::Component,
                       private juce::Timer
    {
    public:
        explicit PhraseView (DrumsXProcessor&);
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;
        DrumsXProcessor& proc;
        std::uint64_t lastHash = 0;
    };

    /** The manual step editor: click to place, drag up/down for velocity,
        right-click to erase. Never overwritten by the generator. */
    class ManualPatternGrid : public juce::Component
    {
    public:
        explicit ManualPatternGrid (DrumsXProcessor&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        bool cellAt (juce::Point<int> p, int& lane, int& step) const;
        DrumsXProcessor& proc;
    };

    //==============================================================================
    class DrumsXEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
    {
    public:
        explicit DrumsXEditor (DrumsXProcessor&);
        ~DrumsXEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        enum class Page { main, details, kit };

        void setPage (Page);
        void layoutMain (juce::Rectangle<int>);
        void layoutDetails (juce::Rectangle<int>);
        void layoutKit (juce::Rectangle<int>);
        void timerCallback() override;
        void exportMenu();
        void applyScale();
        void ensureWindowSize();

        struct KitRow
        {
            std::unique_ptr<juce::ToggleButton> enable;
            std::unique_ptr<juce::TextButton>   prev, next;
            std::unique_ptr<juce::Label>        name, layers;
            std::unique_ptr<juce::Slider>       gain, pan, tune, damp;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttach;
        };

        /** One kit-piece group on the performance page: in/out of the take plus
            a ghost-note state, mirroring Logic's kit-piece selector. */
        struct LaneGroup
        {
            std::unique_ptr<juce::TextButton> in, ghost;
            std::vector<int>                  lanes;
        };

        DrumsXProcessor&  proc;
        DrumsXLookAndFeel lnf;

        juce::TextButton  mainTab { "MAIN" }, detailsTab { "DETAILS" }, kitTab { "KIT" };
        juce::TextButton  playButton { "PLAY" }, regenButton { "REGENERATE" }, exportButton { "EXPORT MIDI" };
        juce::ComboBox    scaleBox;
        Page              page = Page::main;

        // MAIN
        juce::ListBox     characterList;
        std::unique_ptr<juce::ListBoxModel> characterModel;
        PerformancePad    pad;
        PhraseView        phraseView;
        std::unique_ptr<LabelledKnob> fillsKnob, swingKnob, complexityKnob, intensityKnob;
        std::vector<std::unique_ptr<juce::TextButton>> variationButtons;
        juce::Label       padCaption;
        std::vector<LaneGroup> laneGroups;

        // DETAILS
        std::vector<std::unique_ptr<LabelledKnob>> detailKnobs;
        juce::ComboBox    fillBarsBox, phraseBarsBox, swingGridBox, timeSigNumBox, timeSigDenBox, tempoModeBox;
        juce::Slider      bpmSlider;
        juce::ToggleButton rideToggle { "Ride instead of hats" };
        juce::ToggleButton halfTimeToggle { "Half time" };
        juce::ToggleButton manualToggle { "Manual pattern" };
        juce::TextButton   clearManualButton { "CLEAR" };
        ManualPatternGrid  manualGrid;

        // KIT
        std::array<KitRow, NumLanes> kitRows;
        juce::Viewport    kitViewport;
        juce::Component   kitRowsHolder;
        juce::TextButton  loadKitButton { "LOAD KIT FOLDER..." };
        juce::Label       kitNameLabel;
        std::unique_ptr<LabelledKnob> outputKnob;
        std::unique_ptr<LabelledKnob> micBlendKnob;
        std::unique_ptr<LabelledKnob> bleedKnob;
        std::unique_ptr<LabelledKnob> crushKnob;
        std::unique_ptr<juce::FileChooser> chooser;
        int startupChecks = 16;

        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>   buttonAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>   sliderAttachments;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsXEditor)
    };
}
