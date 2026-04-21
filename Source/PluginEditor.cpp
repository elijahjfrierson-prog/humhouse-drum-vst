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

    juce::ColourGradient grad (juce::Colour (Palette::kAccentDeep).brighter (0.1f),
                               c.x, c.y - r,
                               juce::Colour (Palette::kInk),
                               c.x, c.y + r, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    g.setColour (juce::Colour (Palette::kAccent).withAlpha (isDown ? 1.0f : 0.85f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.6f);

    const float plusW = r * 0.55f;
    const float th    = 2.4f;
    g.setColour (juce::Colour (Palette::kBone));
    g.fillRoundedRectangle (c.x - plusW, c.y - th * 0.5f, plusW * 2.0f, th, th * 0.5f);
    g.fillRoundedRectangle (c.x - th * 0.5f, c.y - plusW, th, plusW * 2.0f, th * 0.5f);
}

// ============================================================================
// MidiDragHandle — sleek chip that kicks off an external drag-drop of the
// full arrangement as a .mid file.
// ============================================================================
void AIDrumAudioProcessorEditor::MidiDragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (Palette::kPanel));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (Palette::kAccentDeep));
    g.drawRoundedRectangle (r, 8.0f, 1.25f);

    g.setColour (juce::Colour (Palette::kAccent));
    g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);

    g.setColour (juce::Colour (Palette::kBone));
    auto f = juce::Font (juce::FontOptions (11.5f, juce::Font::bold));
    f.setExtraKerningFactor (0.18f);
    g.setFont (f);
    g.drawText ("DRAG  MIDI  \u2192  DAW", getLocalBounds().withTrimmedLeft (10),
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
                     .getChildFile ("HumHouse-Drums-arrangement.mid");
    if (! processorRef.writeArrangementAsMidiFile (tempMidiFile))
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
    setSize (960, 820);

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

    // Arrangement strip (piano-roll grid that grows per region).
    arrangementStrip.setProvider ([this]
    {
        aidrum::ArrangementStrip::Snapshot s;
        s.regions       = processorRef.getArrangement();
        s.totalBeats    = processorRef.getArrangementTotalBeats();
        s.playheadBeats = processorRef.getPlayheadBeats();
        return s;
    });
    addAndMakeVisible (arrangementStrip);

    // Manual grid (v0.8.0) — interactive 16-bar step sequencer.
    manualGrid.provider    = [this] { return processorRef.getManualPattern(); };
    manualGrid.onSetCell   = [this] (int note, int step, float vel)
        { processorRef.setManualCell (note, step, vel); manualGrid.repaint(); };
    manualGrid.onClearCell = [this] (int note, int step)
        { processorRef.clearManualCell (note, step); manualGrid.repaint(); };
    manualGrid.setNumBars (processorRef.getManualNumBars());
    manualGrid.setTooltip ("MANUAL GRID \u2014 click cells to place kick / snare / tom / hat hits. "
                           "Alt-click or right-click to erase. Drag to paint multiple cells. "
                           "Works across all 16 bars; DRUM KIT remaps the timbre in your sampler.");
    manualGrid.setVisible (false);
    addChildComponent (manualGrid);

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
    addRotary (swingSlider,      swingLabel);
    addRotary (fillsSlider,      fillsLabel);

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

    hiHatBox.addItem ("Dynamic", 1);
    hiHatBox.addItem ("Closed",  2);
    hiHatBox.addItem ("Open",    3);
    hiHatBox.addItem ("Ride",    4);
    styleCombo (hiHatBox, hiHatLabel);

    {
        juce::StringArray kitNames;
        for (const auto& n : aidrum::drumKitDisplayNames())
            kitNames.add (juce::String (n));
        drumKitBox.addItemList (kitNames, 1);
    }
    styleCombo (drumKitBox, drumKitLabel);

    // Half-time toggle
    halfTimeButton.setClickingTogglesState (true);
    halfTimeButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
    halfTimeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
    halfTimeButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kMuted));
    halfTimeButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (Palette::kBone));
    addAndMakeVisible (halfTimeButton);

    // APPEND (+) button — now *appends* instead of replacing.
    plusButton.onClick = [this]
    {
        const auto mode = (modeBox.getSelectedId() == 2)
                            ? aidrum::GenerationMode::Fill
                            : aidrum::GenerationMode::Groove;
        processorRef.appendRegion (mode);
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

    // UNDO / CLEAR
    auto styleSmallBtn = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kBone));
    };
    styleSmallBtn (undoButton);
    styleSmallBtn (clearButton);
    styleSmallBtn (saveMidiButton);

    undoButton.onClick  = [this] { processorRef.undoLastRegion();  };
    clearButton.onClick = [this] { processorRef.clearArrangement(); };

    addAndMakeVisible (undoButton);
    addAndMakeVisible (clearButton);

    // MANUAL mode toggle — swaps the arrangement strip for the interactive grid.
    styleSmallBtn (manualButton);
    styleSmallBtn (clearManualButton);
    styleSmallBtn (commitManualButton);
    manualButton.setClickingTogglesState (true);
    manualButton.onClick = [this]
    {
        const bool on = manualButton.getToggleState();
        processorRef.setManualMode (on);
        arrangementStrip.setVisible (! on);
        manualGrid      .setVisible (on);
        clearManualButton  .setVisible (on);
        commitManualButton .setVisible (on);
        undoButton  .setVisible (! on);
        clearButton .setVisible (! on);
        plusHelper.setText (on ? "MANUAL" : "APPEND", juce::dontSendNotification);
        resized();
        repaint();
    };
    clearManualButton.onClick = [this]
    {
        processorRef.clearManualPattern();
        manualGrid.repaint();
    };
    commitManualButton.onClick = [this]
    {
        processorRef.commitManualPatternAsRegion();
        plusButton.bump();
    };
    addAndMakeVisible (manualButton);
    addChildComponent (clearManualButton);   // hidden until MANUAL is on
    addChildComponent (commitManualButton);

    // SAVE MIDI — saves the whole arrangement.
    saveMidiButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Save arrangement as MIDI file",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("HumHouse-Drums-arrangement.mid"),
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
                processorRef.writeArrangementAsMidiFile (file);
            });
    };
    addAndMakeVisible (saveMidiButton);

    addAndMakeVisible (dragHandle);

    // ---------------------------------------------------------------------
    // Tooltips — explain every control in plain English on hover.
    // ---------------------------------------------------------------------
    variationSlider .setTooltip ("VARIATION — re-rolls the pattern seed. Higher = more contrast between appended regions.");
    complexitySlider.setTooltip ("COMPLEXITY — pattern density. Low = sparse, high = busy, ghost-note-heavy.");
    velocitySlider  .setTooltip ("VELOCITY — master MIDI velocity scale (how hard the notes hit).");
    humanizeSlider  .setTooltip ("HUMANIZE — random timing + velocity jitter for a natural, non-robotic feel.");
    swingSlider     .setTooltip ("SWING — shifts off-beat 16ths toward a triplet feel (classic shuffle groove).");
    fillsSlider     .setTooltip ("FILLS — probability that the next APPEND becomes a drum fill instead of a groove.");

    genreBox        .setTooltip ("GENRE — rock, metal, jazz, funk, hip-hop, trap, pop, country… Auto picks one per press.");
    patternLengthBox.setTooltip ("LENGTH — how long each appended region is (1/16 note → 2 bars).");
    modeBox         .setTooltip ("MODE — Groove (time-keeping pattern) or Fill (transition roll).");
    hiHatBox        .setTooltip ("HI-HAT — Dynamic (genre default), or force Closed / Open / Ride cymbal.");
    drumKitBox      .setTooltip ("DRUM KIT — 20 models from jazz Ludwig to thrash Sonor. Each remaps GM notes + velocity / ghost / accent curves for a distinct timbre in your sampler.");
    halfTimeButton  .setTooltip ("HALF-TIME — snare on beat 3 only (kick on 1). Classic hip-hop / shoegaze feel.");

    plusButton      .setTooltip ("APPEND — generate a new region with current settings and add it after the last one.");
    undoButton      .setTooltip ("UNDO — remove the last appended region from the arrangement.");
    clearButton     .setTooltip ("CLEAR — wipe the arrangement and start a fresh single region.");
    dragHandle      .setTooltip ("DRAG MIDI — hold and drag onto a DAW track to drop the full arrangement as a .mid file.");
    saveMidiButton  .setTooltip ("SAVE MIDI — export the full arrangement to a .mid file on disk.");

    manualButton       .setTooltip ("MANUAL — 16-bar click-to-edit grid. Place kicks / snares / toms / hats yourself instead of letting the AI generate.");
    clearManualButton  .setTooltip ("CLEAR GRID — wipe every cell in the manual pattern.");
    commitManualButton .setTooltip ("APPEND TO ARR. — commit the current manual 16-bar pattern as a new region in the arrangement (with DRUM KIT remap).");

    // APVTS attachments
    auto& apvts = processorRef.getAPVTS();
    variationAttachment     = std::make_unique<SliderAttachment> (apvts, "variation",     variationSlider);
    complexityAttachment    = std::make_unique<SliderAttachment> (apvts, "complexity",    complexitySlider);
    velocityAttachment      = std::make_unique<SliderAttachment> (apvts, "velocity",      velocitySlider);
    humanizeAttachment      = std::make_unique<SliderAttachment> (apvts, "humanize",      humanizeSlider);
    swingAttachment         = std::make_unique<SliderAttachment> (apvts, "swing",         swingSlider);
    fillsAttachment         = std::make_unique<SliderAttachment> (apvts, "fillsProb",     fillsSlider);
    genreAttachment         = std::make_unique<ComboAttachment>  (apvts, "genre",         genreBox);
    patternLengthAttachment = std::make_unique<ComboAttachment>  (apvts, "patternLength", patternLengthBox);
    modeAttachment          = std::make_unique<ComboAttachment>  (apvts, "mode",          modeBox);
    hiHatAttachment         = std::make_unique<ComboAttachment>  (apvts, "hiHat",         hiHatBox);
    drumKitAttachment       = std::make_unique<ComboAttachment>  (apvts, "drumKit",       drumKitBox);
    halfTimeAttachment      = std::make_unique<ButtonAttachment> (apvts, "halfTime",      halfTimeButton);

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
    if (manualGrid.isVisible())
        manualGrid.repaint();
}

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

    // Knob row: 6 knobs (Variation, Complexity, Velocity, Humanize, Swing, Fills)
    auto knobRow = area.removeFromTop (130);
    const int knobW = knobRow.getWidth() / 6;
    variationSlider .setBounds (knobRow.removeFromLeft (knobW).reduced (6, 18));
    complexitySlider.setBounds (knobRow.removeFromLeft (knobW).reduced (6, 18));
    velocitySlider  .setBounds (knobRow.removeFromLeft (knobW).reduced (6, 18));
    humanizeSlider  .setBounds (knobRow.removeFromLeft (knobW).reduced (6, 18));
    swingSlider     .setBounds (knobRow.removeFromLeft (knobW).reduced (6, 18));
    fillsSlider     .setBounds (knobRow.reduced (6, 18));

    area.removeFromTop (6);

    // DRUM KIT row — full width, labels 20 kits by genre / brand / style.
    auto kitRow = area.removeFromTop (68);
    drumKitBox.setBounds (kitRow.reduced (8, 26));

    area.removeFromTop (6);

    // Combo row: Genre / Length / Mode / Hi-Hat + Half-Time toggle at far right
    auto comboRow = area.removeFromTop (68);
    const int halfTimeW = 120;
    auto comboArea      = comboRow.withTrimmedRight (halfTimeW + 10);
    const int comboW    = comboArea.getWidth() / 4;
    genreBox        .setBounds (comboArea.removeFromLeft (comboW).reduced (8, 26));
    patternLengthBox.setBounds (comboArea.removeFromLeft (comboW).reduced (8, 26));
    modeBox         .setBounds (comboArea.removeFromLeft (comboW).reduced (8, 26));
    hiHatBox        .setBounds (comboArea.reduced (8, 26));
    halfTimeButton  .setBounds (comboRow.removeFromRight (halfTimeW).reduced (4, 30));

    area.removeFromTop (6);

    // Action row: + APPEND (center), UNDO/CLEAR (left of +), DRAG/SAVE (right of +)
    auto action = area.removeFromTop (88);

    const int plusSize = 80;
    juce::Rectangle<int> plusRect (action.getCentreX() - plusSize / 2,
                                   action.getY() + (action.getHeight() - plusSize) / 2,
                                   plusSize, plusSize);
    plusButton.setBounds (plusRect);
    plusHelper.setBounds (plusRect.getX() - 40, plusRect.getBottom() - 4,
                          plusRect.getWidth() + 80, 18);

    // Left of +: either UNDO/CLEAR (AI mode) or CLEAR GRID/APPEND TO ARR. (manual mode)
    auto leftCluster = juce::Rectangle<int> (action.getX() + 12, action.getY() + 14,
                                             plusRect.getX() - action.getX() - 24,
                                             action.getHeight() - 28);
    const int lBtnH = (leftCluster.getHeight() - 8) / 2;
    undoButton        .setBounds (leftCluster.getX(), leftCluster.getY(),
                                  leftCluster.getWidth(), lBtnH);
    clearManualButton .setBounds (undoButton.getBounds());
    clearButton       .setBounds (leftCluster.getX(), leftCluster.getY() + lBtnH + 8,
                                  leftCluster.getWidth(), lBtnH);
    commitManualButton.setBounds (clearButton.getBounds());

    // Right of +: DRAG-MIDI + SAVE-MIDI stacked
    auto rightCluster = juce::Rectangle<int> (plusRect.getRight() + 12,
                                              action.getY() + 14,
                                              action.getRight() - plusRect.getRight() - 24,
                                              action.getHeight() - 28);
    const int rBtnH = (rightCluster.getHeight() - 8) / 2;
    dragHandle    .setBounds (rightCluster.removeFromTop (rBtnH).reduced (2));
    rightCluster.removeFromTop (8);
    saveMidiButton.setBounds (rightCluster.removeFromTop (rBtnH).reduced (2));

    area.removeFromTop (6);

    // MANUAL mode toggle bar — always visible above the strip/grid.
    auto manualBar = area.removeFromTop (30);
    manualButton.setBounds (manualBar.removeFromLeft (140).reduced (2));

    area.removeFromTop (4);

    // Arrangement strip / manual grid share the remaining area.
    arrangementStrip.setBounds (area);
    manualGrid      .setBounds (area);
}
