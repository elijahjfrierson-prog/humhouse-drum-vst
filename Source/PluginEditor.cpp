#include "PluginEditor.h"

using Palette = aidrum::GothicPalette;

// ============================================================================
// PlusButton — circular, glowing purple, central "+".
// ============================================================================
AIDrumAudioProcessorEditor::PlusButton::PlusButton() : juce::Button ("plus") {}

void AIDrumAudioProcessorEditor::PlusButton::paintButton (juce::Graphics& g,
                                                           bool isOver, bool isDown)
{
    auto area = getLocalBounds().toFloat().reduced (4.0f);
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();

    // Outer glow (amplified when glow>0 after a press, or on hover)
    const float glowAmt = juce::jmax (glow, isOver ? 0.35f : 0.0f);
    if (glowAmt > 0.02f)
    {
        for (int i = 3; i >= 1; --i)
        {
            const float k = (float) i;
            g.setColour (juce::Colour (Palette::kAccent).withAlpha (0.12f * glowAmt / k));
            g.fillEllipse (c.x - r - k * 6.0f, c.y - r - k * 6.0f,
                           (r + k * 6.0f) * 2.0f, (r + k * 6.0f) * 2.0f);
        }
    }

    // Dark core
    juce::ColourGradient grad (juce::Colour (Palette::kAccentDeep).brighter (0.1f),
                               c.x, c.y - r,
                               juce::Colour (Palette::kInk),
                               c.x, c.y + r, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    // Thin purple ring
    g.setColour (juce::Colour (Palette::kAccent).withAlpha (isDown ? 1.0f : 0.85f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.6f);

    // "+" glyph
    const float plusW = r * 0.55f;
    const float th    = 2.4f;
    g.setColour (juce::Colour (Palette::kBone));
    g.fillRoundedRectangle (c.x - plusW, c.y - th * 0.5f, plusW * 2.0f, th, th * 0.5f);
    g.fillRoundedRectangle (c.x - th * 0.5f, c.y - plusW, th, plusW * 2.0f, th * 0.5f);
}

// ============================================================================
// MidiDragHandle — sleek chip that kicks off an external drag-drop of the
// current pattern as a .mid file.
// ============================================================================
void AIDrumAudioProcessorEditor::MidiDragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (Palette::kPanel));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (Palette::kAccentDeep));
    g.drawRoundedRectangle (r, 8.0f, 1.25f);

    // Accent left rail
    g.setColour (juce::Colour (Palette::kAccent));
    g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);

    g.setColour (juce::Colour (Palette::kBone));
    auto f = juce::Font (juce::FontOptions (12.0f, juce::Font::bold));
    f.setExtraKerningFactor (0.18f);
    g.setFont (f);
    g.drawText ("DRAG  MIDI  \u2192  DAW", getLocalBounds().withTrimmedLeft (8),
                juce::Justification::centredLeft, false);
}

void AIDrumAudioProcessorEditor::MidiDragHandle::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
}

void AIDrumAudioProcessorEditor::MidiDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6) return;

    tempMidiFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("AI-Drum-VST-pattern.mid");
    if (! processorRef.writeCurrentPatternAsMidiFile (tempMidiFile))
        return;

    dragStarted = true;
    juce::StringArray files { tempMidiFile.getFullPathName() };
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        files, /*canMoveFiles*/ false, this, [] {});
}

// ============================================================================
// Editor
// ============================================================================
AIDrumAudioProcessorEditor::AIDrumAudioProcessorEditor (AIDrumAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&gothicLnf);
    setSize (740, 540);

    // Title
    {
        auto f = juce::Font (juce::FontOptions (28.0f, juce::Font::plain));
        f.setExtraKerningFactor (0.35f);
        titleLabel.setFont (f);
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setColour (juce::Label::textColourId, juce::Colour (Palette::kBone));
        addAndMakeVisible (titleLabel);

        auto s = juce::Font (juce::FontOptions (11.0f, juce::Font::italic));
        s.setExtraKerningFactor (0.4f);
        subtitleLabel.setFont (s);
        subtitleLabel.setJustificationType (juce::Justification::centred);
        subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (Palette::kMuted));
        addAndMakeVisible (subtitleLabel);
    }

    // Pattern strip
    patternStrip.setPatternProvider ([this] { return processorRef.getCurrentPattern(); });
    addAndMakeVisible (patternStrip);

    // Rotary knobs
    auto addRotary = [this] (juce::Slider& s, juce::Label& l)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (Palette::kBone));
        addAndMakeVisible (s);

        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::bold));
        f.setExtraKerningFactor (0.35f);
        l.setFont (f);
        l.setColour (juce::Label::textColourId, juce::Colour (Palette::kMuted));
        l.setJustificationType (juce::Justification::centred);
        l.attachToComponent (&s, false);
        addAndMakeVisible (l);
    };

    addRotary (variationSlider,  variationLabel);
    addRotary (complexitySlider, complexityLabel);
    addRotary (velocitySlider,   velocityLabel);
    addRotary (humanizeSlider,   humanizeLabel);

    // Combos
    auto styleCombo = [this] (juce::ComboBox& c, juce::Label& l)
    {
        addAndMakeVisible (c);
        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::bold));
        f.setExtraKerningFactor (0.35f);
        l.setFont (f);
        l.setColour (juce::Label::textColourId, juce::Colour (Palette::kMuted));
        l.setJustificationType (juce::Justification::centred);
        l.attachToComponent (&c, false);
        addAndMakeVisible (l);
    };

    {
        juce::StringArray names;
        for (const auto& n : aidrum::genreDisplayNames())
            names.add (juce::String (n));
        genreBox.addItemList (names, 1);
    }
    styleCombo (genreBox, genreLabel);

    patternLengthBox.addItemList (
        juce::StringArray { "1/16 note", "1/8 note", "1/4 note", "1/2 bar", "1 bar", "2 bars" }, 1);
    styleCombo (patternLengthBox, patternLengthLabel);

    modeBox.addItem ("Groove", 1);
    modeBox.addItem ("Fill",   2);
    styleCombo (modeBox, modeLabel);

    // Summon (+) button
    plusButton.onClick = [this]
    {
        const auto mode = (modeBox.getSelectedId() == 2)
                            ? aidrum::GenerationMode::Fill
                            : aidrum::GenerationMode::Groove;
        processorRef.requestGeneration (mode);
        plusButton.bump();
    };
    addAndMakeVisible (plusButton);

    {
        auto f = juce::Font (juce::FontOptions (11.0f, juce::Font::bold));
        f.setExtraKerningFactor (0.45f);
        plusHelper.setFont (f);
        plusHelper.setColour (juce::Label::textColourId, juce::Colour (Palette::kAccentSoft));
        plusHelper.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (plusHelper);
    }

    // Save MIDI
    saveMidiButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
    saveMidiButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
    saveMidiButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kBone));
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
                if (file == juce::File{}) return;
                if (file.getFileExtension().isEmpty())
                    file = file.withFileExtension (".mid");
                processorRef.writeCurrentPatternAsMidiFile (file);
            });
    };
    addAndMakeVisible (saveMidiButton);

    addAndMakeVisible (dragHandle);

    // APVTS attachments
    auto& apvts = processorRef.getAPVTS();
    variationAttachment     = std::make_unique<SliderAttachment> (apvts, "variation",     variationSlider);
    complexityAttachment    = std::make_unique<SliderAttachment> (apvts, "complexity",    complexitySlider);
    velocityAttachment      = std::make_unique<SliderAttachment> (apvts, "velocity",      velocitySlider);
    humanizeAttachment      = std::make_unique<SliderAttachment> (apvts, "humanize",      humanizeSlider);
    genreAttachment         = std::make_unique<ComboAttachment>  (apvts, "genre",         genreBox);
    patternLengthAttachment = std::make_unique<ComboAttachment>  (apvts, "patternLength", patternLengthBox);
    modeAttachment          = std::make_unique<ComboAttachment>  (apvts, "mode",          modeBox);

    startTimerHz (30);
}

AIDrumAudioProcessorEditor::~AIDrumAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AIDrumAudioProcessorEditor::timerCallback()
{
    plusButton.tickGlow();
}

// Paint: gothic radial gradient background + thin ornamental horizontal rule.
void AIDrumAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    juce::ColourGradient bg (juce::Colour (Palette::kPanel),
                             b.getCentreX(), b.getY() + 60.0f,
                             juce::Colour (Palette::kInk),
                             b.getX(), b.getBottom(), true);
    g.setGradientFill (bg);
    g.fillRect (b);

    // Thin horizontal rule under the title (purple gradient)
    const float ruleY = 92.0f;
    juce::ColourGradient rule (juce::Colours::transparentBlack, b.getX(), ruleY,
                               juce::Colours::transparentBlack, b.getRight(), ruleY, false);
    rule.addColour (0.5, juce::Colour (Palette::kAccent).withAlpha (0.85f));
    g.setGradientFill (rule);
    g.fillRect (juce::Rectangle<float> (b.getX() + 40.0f, ruleY, b.getWidth() - 80.0f, 1.0f));

    // Center dagger dot ornament
    g.setColour (juce::Colour (Palette::kAccent));
    g.fillEllipse (b.getCentreX() - 2.5f, ruleY - 2.5f, 5.0f, 5.0f);
}

void AIDrumAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    // Header
    titleLabel   .setBounds (area.removeFromTop (38));
    subtitleLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (18); // space for the ornament rule

    // Pattern visualizer
    patternStrip .setBounds (area.removeFromTop (80).reduced (4, 0));
    area.removeFromTop (18);

    // 4-knob row
    auto knobRow = area.removeFromTop (140);
    const int knobW = knobRow.getWidth() / 4;
    variationSlider .setBounds (knobRow.removeFromLeft (knobW).reduced (8, 18));
    complexitySlider.setBounds (knobRow.removeFromLeft (knobW).reduced (8, 18));
    velocitySlider  .setBounds (knobRow.removeFromLeft (knobW).reduced (8, 18));
    humanizeSlider  .setBounds (knobRow.reduced (8, 18));

    area.removeFromTop (8);

    // 3-combo row
    auto comboRow = area.removeFromTop (70);
    const int comboW = comboRow.getWidth() / 3;
    genreBox        .setBounds (comboRow.removeFromLeft (comboW).reduced (12, 28));
    patternLengthBox.setBounds (comboRow.removeFromLeft (comboW).reduced (12, 28));
    modeBox         .setBounds (comboRow.reduced (12, 28));

    area.removeFromTop (6);

    // Bottom row: + on left, drag + save stacked on right.
    auto bottom = area;
    const int plusSize = juce::jmin (100, bottom.getHeight());
    auto plusArea = bottom.removeFromLeft (bottom.getWidth() / 2);
    juce::Rectangle<int> plusRect (plusArea.getCentreX() - plusSize / 2,
                                   plusArea.getY() + (plusArea.getHeight() - plusSize) / 2,
                                   plusSize, plusSize);
    plusButton.setBounds (plusRect);
    plusHelper.setBounds (plusRect.getX() - 40, plusRect.getBottom() - 2,
                          plusRect.getWidth() + 80, 18);

    auto right = bottom.reduced (8, 6);
    const int rowH = juce::jmin (38, right.getHeight() / 2 - 6);
    dragHandle    .setBounds (right.removeFromTop (rowH));
    right.removeFromTop (8);
    saveMidiButton.setBounds (right.removeFromTop (rowH));
}
