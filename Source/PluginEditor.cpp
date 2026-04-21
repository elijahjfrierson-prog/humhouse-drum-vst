#include "PluginEditor.h"

// ============================================================================
// MidiDragHandle — drag-to-DAW component.
// Works in the VST3 plugin window, the AU window, and the Standalone app.
// ============================================================================
void AIDrumAudioProcessorEditor::MidiDragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour::fromRGB (40, 60, 100));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour::fromRGB (110, 170, 255));
    g.drawRoundedRectangle (r, 8.0f, 2.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("Drag MIDI to DAW", getLocalBounds(),
                juce::Justification::centred, false);
}

void AIDrumAudioProcessorEditor::MidiDragHandle::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
}

void AIDrumAudioProcessorEditor::MidiDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted)
        return;

    if (e.getDistanceFromDragStart() < 6)
        return;

    tempMidiFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("AI-Drum-VST-pattern.mid");

    if (! processorRef.writeCurrentPatternAsMidiFile (tempMidiFile))
        return;

    dragStarted = true;
    juce::StringArray files { tempMidiFile.getFullPathName() };
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        files, /* canMoveFiles = */ false, this,
        [] () {});
}

// ============================================================================
// Editor
// ============================================================================
AIDrumAudioProcessorEditor::AIDrumAudioProcessorEditor (AIDrumAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (720, 480);

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

    // Genre combo
    {
        juce::StringArray genreNames;
        for (const auto& n : aidrum::genreDisplayNames())
            genreNames.add (juce::String (n));
        genreBox.addItemList (genreNames, 1);
    }
    genreLabel.setJustificationType (juce::Justification::centred);
    genreLabel.attachToComponent (&genreBox, false);
    addAndMakeVisible (genreBox);
    addAndMakeVisible (genreLabel);

    // Mode combo
    modeBox.addItem ("Groove", 1);
    modeBox.addItem ("Fill",   2);
    modeLabel.setJustificationType (juce::Justification::centred);
    modeLabel.attachToComponent (&modeBox, false);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (modeLabel);

    // Big "+" button → generate a fresh pattern every press.
    plusButton.setButtonText ("+");
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

    // Save MIDI button → file picker → .mid
    saveMidiButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Save pattern as MIDI file",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("AI-Drum-VST-pattern.mid"),
            "*.mid");

        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode
              | juce::FileBrowserComponent::canSelectFiles
              | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{})
                    return;
                if (file.getFileExtension().isEmpty())
                    file = file.withFileExtension (".mid");
                processorRef.writeCurrentPatternAsMidiFile (file);
            });
    };
    addAndMakeVisible (saveMidiButton);

    addAndMakeVisible (dragHandle);

    auto& apvts = processorRef.getAPVTS();
    variationAttachment     = std::make_unique<SliderAttachment> (apvts, "variation",     variationSlider);
    complexityAttachment    = std::make_unique<SliderAttachment> (apvts, "complexity",    complexitySlider);
    velocityAttachment      = std::make_unique<SliderAttachment> (apvts, "velocity",      velocitySlider);
    humanizeAttachment      = std::make_unique<SliderAttachment> (apvts, "humanize",      humanizeSlider);
    patternLengthAttachment = std::make_unique<ComboAttachment>  (apvts, "patternLength", patternLengthBox);
    genreAttachment         = std::make_unique<ComboAttachment>  (apvts, "genre",         genreBox);
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

    // Top row: 4 knobs
    auto knobRow = area.removeFromTop (150);
    const int knobW = knobRow.getWidth() / 4;
    variationSlider .setBounds (knobRow.removeFromLeft (knobW).reduced (8, 22));
    complexitySlider.setBounds (knobRow.removeFromLeft (knobW).reduced (8, 22));
    velocitySlider  .setBounds (knobRow.removeFromLeft (knobW).reduced (8, 22));
    humanizeSlider  .setBounds (knobRow.reduced (8, 22));

    // Middle row: genre + pattern length + mode combos
    auto comboRow = area.removeFromTop (80);
    const int comboW = comboRow.getWidth() / 3;
    genreBox        .setBounds (comboRow.removeFromLeft (comboW).reduced (12, 34));
    patternLengthBox.setBounds (comboRow.removeFromLeft (comboW).reduced (12, 34));
    modeBox         .setBounds (comboRow.reduced (12, 34));

    // Bottom block: "+" button on left, MIDI export on right
    auto bottom = area.reduced (10);

    const int plusSize = juce::jmin (90, bottom.getHeight());
    juce::Rectangle<int> plusRect (bottom.getX() + bottom.getWidth() / 4 - plusSize / 2,
                                   bottom.getY() + (bottom.getHeight() - plusSize) / 2,
                                   plusSize, plusSize);
    plusButton.setBounds (plusRect);
    plusHelper.setBounds (plusRect.getX() - 40, plusRect.getBottom() - 6,
                          plusRect.getWidth() + 80, 18);

    // Right half: drag handle on top, Save button below.
    auto right = bottom.withTrimmedLeft (bottom.getWidth() / 2).reduced (10, 0);
    const int dragH = juce::jmin (60, right.getHeight() / 2 - 4);
    dragHandle    .setBounds (right.removeFromTop (dragH));
    right.removeFromTop (6);
    saveMidiButton.setBounds (right.removeFromTop (dragH));
}
