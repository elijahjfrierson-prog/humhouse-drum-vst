#include "PluginEditor.h"

using Palette = aidrum::GothicPalette;

namespace
{
    juce::Path makeDrumEllipse (float cx, float cy, float w, float h)
    {
        juce::Path p;
        p.addEllipse (cx - w * 0.5f, cy - h * 0.5f, w, h);
        return p;
    }
}

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
// XYPad — Logic-style intelligence pad. X = complexity, Y = loudness.
// ============================================================================
AIDrumAudioProcessorEditor::XYPad::XYPad()
{
    setTooltip ("INTELLIGENCE PAD — drag the puck. Right = more complexity, up = louder hits.");
    startTimerHz (20);
}

void AIDrumAudioProcessorEditor::XYPad::bind (juce::Slider* complexity, juce::Slider* velocity)
{
    complexitySlider = complexity;
    velocitySlider   = velocity;
}

void AIDrumAudioProcessorEditor::XYPad::timerCallback()
{
    repaint();
}

void AIDrumAudioProcessorEditor::XYPad::updateFromPoint (juce::Point<float> p)
{
    if (complexitySlider == nullptr || velocitySlider == nullptr)
        return;

    auto r = getLocalBounds().toFloat().reduced (18.0f);
    const float x = juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth());
    const float y = juce::jlimit (0.0f, 1.0f, 1.0f - ((p.y - r.getY()) / r.getHeight()));

    complexitySlider->setValue (x, juce::sendNotificationSync);
    velocitySlider  ->setValue (y, juce::sendNotificationSync);
    repaint();
}

void AIDrumAudioProcessorEditor::XYPad::mouseDown (const juce::MouseEvent& e)
{
    updateFromPoint (e.position);
}

void AIDrumAudioProcessorEditor::XYPad::mouseDrag (const juce::MouseEvent& e)
{
    updateFromPoint (e.position);
}

void AIDrumAudioProcessorEditor::XYPad::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto r = b.reduced (8.0f);

    g.setColour (juce::Colour (Palette::kPanel));
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (juce::Colour (Palette::kPanelEdge));
    g.drawRoundedRectangle (r, 16.0f, 1.0f);

    auto pad = r.reduced (18.0f);
    juce::ColourGradient grad (juce::Colour (Palette::kAccentDeep).brighter (0.2f),
                               pad.getX(), pad.getBottom(),
                               juce::Colour (Palette::kInk),
                               pad.getRight(), pad.getY(), false);
    grad.addColour (0.5, juce::Colour (Palette::kAccent).withAlpha (0.25f));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (pad, 12.0f);

    g.setColour (juce::Colour (Palette::kBone).withAlpha (0.08f));
    for (int i = 1; i < 4; ++i)
    {
        const float x = pad.getX() + pad.getWidth() * (float) i / 4.0f;
        const float y = pad.getY() + pad.getHeight() * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, pad.getY(), pad.getBottom());
        g.drawHorizontalLine ((int) y, pad.getX(), pad.getRight());
    }

    auto labelFont = juce::Font (juce::FontOptions (10.0f, juce::Font::bold));
    labelFont.setExtraKerningFactor (0.30f);
    g.setFont (labelFont);
    g.setColour (juce::Colour (Palette::kMuted));
    g.drawText ("INTELLIGENCE PAD", r.toNearestInt().reduced (14, 10), juce::Justification::topLeft, false);
    g.drawText ("LOUDER", juce::Rectangle<int> ((int) pad.getX(), (int) pad.getY() - 2, (int) pad.getWidth(), 16), juce::Justification::centredTop, false);
    g.drawText ("SIMPLER", juce::Rectangle<int> ((int) pad.getX() - 6, (int) pad.getBottom() - 10, 70, 16), juce::Justification::bottomLeft, false);
    g.drawText ("BUSIER", juce::Rectangle<int> ((int) pad.getRight() - 64, (int) pad.getBottom() - 10, 70, 16), juce::Justification::bottomRight, false);

    const float cx = complexitySlider != nullptr ? (float) complexitySlider->getValue() : 0.5f;
    const float cy = velocitySlider   != nullptr ? (float) velocitySlider->getValue()   : 0.7f;
    const float px = pad.getX() + pad.getWidth()  * cx;
    const float py = pad.getBottom() - pad.getHeight() * cy;

    g.setColour (juce::Colour (Palette::kAccent).withAlpha (0.15f));
    g.fillEllipse (px - 18.0f, py - 18.0f, 36.0f, 36.0f);
    g.setColour (juce::Colour (Palette::kAccentSoft));
    g.fillEllipse (px - 9.0f, py - 9.0f, 18.0f, 18.0f);
    g.setColour (juce::Colour (Palette::kBone));
    g.drawEllipse (px - 9.0f, py - 9.0f, 18.0f, 18.0f, 1.2f);
}

// ============================================================================
// KitVisualizer — vector-ish drumkit silhouette that changes with kit choice.
// ============================================================================
AIDrumAudioProcessorEditor::KitVisualizer::KitVisualizer() = default;

void AIDrumAudioProcessorEditor::KitVisualizer::setSelectedKit (int index)
{
    selectedKit = index;
    repaint();
}

void AIDrumAudioProcessorEditor::KitVisualizer::pulseBus (int bus, float velocity)
{
    if (bus < 0 || bus >= kNumFlashes) return;
    const float v = juce::jlimit (0.0f, 1.0f, 0.35f + velocity * 0.65f);
    if (v > flash[bus]) flash[bus] = v;
}

void AIDrumAudioProcessorEditor::KitVisualizer::decayFlashes (float k)
{
    bool any = false;
    for (int i = 0; i < kNumFlashes; ++i)
    {
        flash[i] *= k;
        if (flash[i] < 0.01f) flash[i] = 0.0f;
        else any = true;
    }
    if (any) repaint();
}

void AIDrumAudioProcessorEditor::KitVisualizer::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (juce::Colour (Palette::kPanel));
    g.fillRoundedRectangle (r, 18.0f);
    g.setColour (juce::Colour (Palette::kPanelEdge));
    g.drawRoundedRectangle (r, 18.0f, 1.0f);

    // v1.3.0 Per-kit tint — each of the 20 kits has a distinct accent
    // colour (walnut amber, sunburst red, jet black, neon pink for 808,
    // cyan for 909, etc.) so the visualizer looks genuinely different
    // per kit instead of every shell being the same brand purple.
    const auto kitAccent = juce::Colour (aidrum::drumKitAccent (
        static_cast<aidrum::DrumKit> (juce::jlimit (0,
            (int) aidrum::DrumKit::Count - 1, selectedKit))));

    auto stage = r.reduced (20.0f);
    const bool electronic = selectedKit >= (int) aidrum::DrumKit::Roland808HipHop;
    const bool metal = selectedKit >= (int) aidrum::DrumKit::SonorSQ2Thrash
                    && selectedKit <= (int) aidrum::DrumKit::YamahaPHXProgMetal;
    const bool jazz = selectedKit <= (int) aidrum::DrumKit::GretschCoolJazz;

    auto titleFont = juce::Font (juce::FontOptions (10.0f, juce::Font::bold));
    titleFont.setExtraKerningFactor (0.30f);
    g.setFont (titleFont);
    g.setColour (juce::Colour (Palette::kMuted));
    g.drawText ("ACTIVE KIT", r.toNearestInt().reduced (14, 10), juce::Justification::topLeft, false);

    const auto& names = aidrum::drumKitDisplayNames();
    const juce::String kitName = juce::String (names[(size_t) juce::jlimit (0, (int) names.size() - 1, selectedKit)]);
    g.setColour (juce::Colour (Palette::kBone));
    g.drawFittedText (kitName, r.toNearestInt().withTrimmedTop (22).reduced (14, 0), juce::Justification::topLeft, 2);

    g.setColour (kitAccent.withAlpha (0.10f));
    g.fillEllipse (stage.getCentreX() - 140.0f, stage.getCentreY() - 110.0f, 280.0f, 220.0f);

    auto drawShell = [&] (juce::Rectangle<float> shell, float rimAlpha, int busIdx, const char* label)
    {
        const float f = (busIdx >= 0 && busIdx < kNumFlashes) ? flash[busIdx] : 0.0f;
        if (f > 0.01f)
        {
            const auto glow = shell.expanded (10.0f + 14.0f * f);
            g.setColour (kitAccent.withAlpha (0.26f * f));
            g.fillEllipse (glow);
        }
        // Shell body tinted with a dark wash of the kit accent on top of
        // bone so each kit reads as its own colour even at rest.
        g.setColour (kitAccent.darker (0.85f).withAlpha (0.55f));
        g.fillEllipse (shell);
        g.setColour (juce::Colour (Palette::kBone).withAlpha (0.18f + 0.45f * f));
        g.fillEllipse (shell.reduced (shell.getHeight() * 0.28f));
        g.setColour (kitAccent.brighter (0.25f).withAlpha (juce::jlimit (0.0f, 1.0f, rimAlpha + f * 0.4f)));
        g.drawEllipse (shell, 2.0f + 1.2f * f);

        if (label != nullptr)
        {
            g.setColour (juce::Colour (Palette::kMuted).withAlpha (0.55f + 0.45f * f));
            auto lf = juce::Font (juce::FontOptions (8.5f, juce::Font::bold));
            lf.setExtraKerningFactor (0.25f);
            g.setFont (lf);
            g.drawText (label, shell.toNearestInt(), juce::Justification::centred, false);
        }
    };

    if (electronic)
    {
        auto body = stage.withSizeKeepingCentre (stage.getWidth() * 0.72f, stage.getHeight() * 0.48f);
        g.setColour (juce::Colour (Palette::kInk).brighter (0.08f));
        g.fillRoundedRectangle (body, 14.0f);
        g.setColour (kitAccent);
        g.drawRoundedRectangle (body, 14.0f, 2.0f);
        const char* padLabel[8] = { "KICK","SNR","TOM","CHH","OHH","RIDE","CRH","CHN" };
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 4; ++col)
            {
                const int idx = row * 4 + col;
                const float f = flash[idx];
                auto pad = juce::Rectangle<float> (body.getX() + 22.0f + col * 52.0f,
                                                   body.getY() + 24.0f + row * 48.0f,
                                                   34.0f, 26.0f);
                g.setColour (juce::Colour (Palette::kBone).withAlpha (0.10f + 0.55f * f));
                g.fillRoundedRectangle (pad, 6.0f);
                g.setColour (kitAccent.withAlpha (juce::jlimit (0.0f, 1.0f, 0.75f + f * 0.25f)));
                g.drawRoundedRectangle (pad, 6.0f, 1.0f + 1.4f * f);
                g.setColour (juce::Colour (Palette::kMuted).withAlpha (0.55f + 0.45f * f));
                auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::bold));
                lf.setExtraKerningFactor (0.25f);
                g.setFont (lf);
                g.drawText (padLabel[idx], pad.toNearestInt(), juce::Justification::centred, false);
            }
        g.setColour (kitAccent);
        g.fillEllipse (body.getRight() - 34.0f, body.getY() + 18.0f, 8.0f, 8.0f);
        g.fillEllipse (body.getRight() - 18.0f, body.getY() + 18.0f, 8.0f, 8.0f);
        return;
    }

    const float kickW = metal ? 126.0f : (jazz ? 86.0f : 106.0f);
    const float kickH = kickW * 0.82f;
    auto kick = juce::Rectangle<float> (stage.getCentreX() - kickW * 0.5f,
                                        stage.getBottom() - kickH - 34.0f,
                                        kickW, kickH);
    drawShell (kick, 0.95f, 0, "KICK");

    auto snare = juce::Rectangle<float> (kick.getX() - (jazz ? 56.0f : 76.0f),
                                         kick.getY() + 22.0f,
                                         jazz ? 56.0f : 64.0f,
                                         jazz ? 28.0f : 32.0f);
    drawShell (snare, 0.85f, 1, "SNARE");

    auto floor = juce::Rectangle<float> (kick.getRight() + 12.0f,
                                         kick.getY() + 20.0f,
                                         metal ? 72.0f : 62.0f,
                                         metal ? 52.0f : 44.0f);
    drawShell (floor, 0.78f, 2, "TOM");

    auto rack1 = juce::Rectangle<float> (kick.getCentreX() - 62.0f,
                                         kick.getY() - 44.0f,
                                         54.0f, 34.0f);
    auto rack2 = juce::Rectangle<float> (kick.getCentreX() + 10.0f,
                                         kick.getY() - 42.0f,
                                         54.0f, 34.0f);
    drawShell (rack1, 0.82f, 2, nullptr);
    if (! jazz) drawShell (rack2, 0.82f, 2, nullptr);

    auto cymbal = [&] (float x, float y, float w, int busIdx, const char* label)
    {
        auto c = juce::Rectangle<float> (x - w * 0.5f, y - 8.0f, w, 16.0f);
        const float f = (busIdx >= 0 && busIdx < kNumFlashes) ? flash[busIdx] : 0.0f;
        if (f > 0.01f)
        {
            g.setColour (kitAccent.withAlpha (0.30f * f));
            g.fillEllipse (c.expanded (8.0f + 10.0f * f));
        }
        g.setColour (juce::Colour (Palette::kBone).withAlpha (0.12f + 0.55f * f));
        g.fillEllipse (c);
        g.setColour (kitAccent.brighter (0.15f).withAlpha (juce::jlimit (0.0f, 1.0f, 0.72f + f * 0.28f)));
        g.drawEllipse (c, 1.5f + 1.0f * f);
        if (label != nullptr)
        {
            g.setColour (juce::Colour (Palette::kMuted).withAlpha (0.55f + 0.45f * f));
            auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::bold));
            lf.setExtraKerningFactor (0.25f);
            g.setFont (lf);
            g.drawText (label, c.toNearestInt(), juce::Justification::centred, false);
        }
    };

    // Left = hi-hat (closed or open — we flash both on any hat hit).
    const float hatFlash = juce::jmax (flash[3], flash[4]);
    {
        auto c = juce::Rectangle<float> (kick.getX() - 10.0f - (jazz ? 38.0f : 46.0f),
                                         kick.getY() - 54.0f - 8.0f,
                                         jazz ? 76.0f : 92.0f, 16.0f);
        if (hatFlash > 0.01f)
        {
            g.setColour (kitAccent.withAlpha (0.30f * hatFlash));
            g.fillEllipse (c.expanded (8.0f + 10.0f * hatFlash));
        }
        g.setColour (juce::Colour (Palette::kBone).withAlpha (0.12f + 0.55f * hatFlash));
        g.fillEllipse (c);
        g.setColour (kitAccent.brighter (0.15f).withAlpha (juce::jlimit (0.0f, 1.0f, 0.72f + hatFlash * 0.28f)));
        g.drawEllipse (c, 1.5f + 1.0f * hatFlash);
        g.setColour (juce::Colour (Palette::kMuted).withAlpha (0.55f + 0.45f * hatFlash));
        auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::bold));
        lf.setExtraKerningFactor (0.25f);
        g.setFont (lf);
        g.drawText ("HAT", c.toNearestInt(), juce::Justification::centred, false);
    }
    cymbal (kick.getRight() + 38.0f, kick.getY() - 60.0f, metal ? 96.0f : 84.0f, 6, "CRASH");
    if (! jazz)
        cymbal (kick.getCentreX() + 94.0f, kick.getY() - 96.0f, metal ? 84.0f : 72.0f, 5, "RIDE");

    if (metal)
    {
        auto chinaRect = juce::Rectangle<float> (kick.getRight() + 56.0f,
                                                 kick.getY() - 120.0f - 8.0f, 88.0f, 20.0f);
        const float f = flash[7];
        if (f > 0.01f)
        {
            g.setColour (kitAccent.withAlpha (0.30f * f));
            g.fillEllipse (chinaRect.expanded (8.0f + 10.0f * f));
        }
        g.setColour (kitAccent.brighter (0.2f).withAlpha (juce::jlimit (0.0f, 1.0f, 0.85f + f * 0.15f)));
        g.drawEllipse (chinaRect, 1.6f + 1.0f * f);
        g.setColour (juce::Colour (Palette::kMuted).withAlpha (0.55f + 0.45f * f));
        auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::bold));
        lf.setExtraKerningFactor (0.25f);
        g.setFont (lf);
        g.drawText ("CHINA", chinaRect.toNearestInt(), juce::Justification::centred, false);
    }

    if (metal)
    {
        auto kick2 = kick.translated (-138.0f, 0.0f);
        kick2.setPosition (kick.getX() - kick.getWidth() - 22.0f, kick.getY());
        drawShell (kick2, 0.92f, 0, nullptr);
    }
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
    setSize (960, 920);

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
    arrangementStrip.onAppend = [this]
    {
        const auto mode = (modeBox.getSelectedId() == 2)
                            ? aidrum::GenerationMode::Fill
                            : aidrum::GenerationMode::Groove;
        processorRef.appendRegion (mode);
        plusButton.bump();
        arrangementStrip.repaint();
    };
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

    xyPad.bind (&complexitySlider, &velocitySlider);
    addAndMakeVisible (xyPad);
    addAndMakeVisible (kitVisualizer);
    kitVisualizer.setSelectedKit (0);

    complexitySlider.setVisible (false);
    velocitySlider  .setVisible (false);
    complexityLabel .setVisible (false);
    velocityLabel   .setVisible (false);

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
    addRotary (roomAmountSlider, roomAmountLabel);
    roomAmountSlider.setRange (0.0, 1.0, 0.001);
    roomAmountSlider.setValue (0.25);
    roomAmountSlider.textFromValueFunction = [] (double v)
        { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };

    variationSlider .setTooltip ("VARIATION — re-rolls the next-generated groove with a different seed so each + press sounds fresh.");
    humanizeSlider  .setTooltip ("HUMANIZE — random micro-timing / velocity jitter. 0 = machine-tight, 1 = loose drummer feel.");
    swingSlider     .setTooltip ("SWING — shuffles the off-beat 16ths late. 0 = straight, 1 = fully triplet swing.");
    fillsSlider     .setTooltip ("FILLS — probability the last region of a phrase ends in a snare/tom fill instead of a groove loop.");

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
    drumKitBox.onChange = [this] { kitVisualizer.setSelectedKit (drumKitBox.getSelectedItemIndex()); };

    roomBox.addItem ("Dry / Studio",  1);
    roomBox.addItem ("Small Room",    2);
    roomBox.addItem ("Garage",        3);
    roomBox.addItem ("Live Bar",      4);
    roomBox.addItem ("Hallway",       5);
    roomBox.addItem ("Big Hall",      6);
    roomBox.addItem ("Stadium",       7);
    roomBox.setSelectedId (1, juce::dontSendNotification);
    styleCombo (roomBox, roomLabel);

    genreBox         .setTooltip ("GENRE — picks the groove vocabulary the AI draws from (rock, jazz, metal, trap, etc.).");
    patternLengthBox .setTooltip ("LENGTH — how long each appended region is in bars.");
    modeBox          .setTooltip ("MODE — GROOVE appends a bar of steady pattern, FILL appends a transition fill.");
    hiHatBox         .setTooltip ("HI-HAT — forces the hat articulation: Dynamic (mix), Closed, Open, or Ride.");
    drumKitBox       .setTooltip ("DRUM KIT — selects one of 20 physically-modelled acoustic/electronic kits. The visualizer flashes each drum as it hits.");
    roomBox          .setTooltip ("ROOM — ambient space the kit is recorded in. Dry = close-miked, Stadium = huge wash.");
    roomAmountSlider .setTooltip ("ROOM AMT — how much of the selected ROOM ambience you hear, 0-100%.");

    // Half-time toggle
    halfTimeButton.setClickingTogglesState (true);
    halfTimeButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
    halfTimeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
    halfTimeButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kMuted));
    halfTimeButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (Palette::kBone));
    halfTimeButton.setTooltip ("HALF-TIME — backbeat moves to 3 instead of 2 & 4, giving every groove a slower, heavier feel.");
    addAndMakeVisible (halfTimeButton);

    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (processorRef.isLooping(), juce::dontSendNotification);
    loopButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
    loopButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
    loopButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kMuted));
    loopButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (Palette::kBone));
    loopButton.setTooltip ("LOOP — when the playhead hits the end of the arrangement, wrap back to bar 1 instead of stopping.");
    loopButton.onClick = [this] { processorRef.setLooping (loopButton.getToggleState()); };
    addAndMakeVisible (loopButton);

    // APPEND (+) button — now *appends* instead of replacing.
    plusButton.onClick = [this]
    {
        const auto mode = (modeBox.getSelectedId() == 2)
                            ? aidrum::GenerationMode::Fill
                            : aidrum::GenerationMode::Groove;
        processorRef.appendRegion (mode);
        plusButton.bump();
        arrangementStrip.repaint();
    };
    plusButton.setTooltip ("APPEND GROOVE (+) — adds a new region to the right of the arrangement so you can keep stacking verses, fills and choruses.");
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
    styleSmallBtn (playButton);
    styleSmallBtn (pauseButton);
    styleSmallBtn (stopButton);

    undoButton     .setTooltip ("UNDO — remove the last appended region from the arrangement.");
    clearButton    .setTooltip ("CLEAR — wipe every region from the arrangement and rewind to bar 1.");
    saveMidiButton .setTooltip ("SAVE MIDI — export the entire arrangement as a .mid file you can drop into any DAW.");
    playButton     .setTooltip ("PLAY — start the internal transport. In a DAW host, this is ignored and the host transport drives playback.");
    pauseButton    .setTooltip ("PAUSE — freeze the playhead in place. Press PLAY to resume from the same position.");
    stopButton     .setTooltip ("STOP — halt playback and rewind the playhead to bar 1.");

    undoButton.onClick  = [this] { processorRef.undoLastRegion(); arrangementStrip.repaint(); };
    clearButton.onClick = [this] { processorRef.clearArrangement(); arrangementStrip.repaint(); };
    playButton.onClick  = [this] { processorRef.play(); arrangementStrip.repaint(); };
    pauseButton.onClick = [this] { processorRef.pause(); };
    stopButton.onClick  = [this] { processorRef.stop(); arrangementStrip.repaint(); };

    addAndMakeVisible (undoButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (pauseButton);
    addAndMakeVisible (stopButton);

    // MANUAL mode toggle — swaps the arrangement strip for the interactive grid.
    styleSmallBtn (manualButton);
    styleSmallBtn (clearManualButton);
    styleSmallBtn (commitManualButton);
    manualButton       .setTooltip ("MANUAL — swap the arrangement for a 16-bar interactive step grid. Click cells to place kick/snare/tom/hat hits, drag to paint, alt-click to erase.");
    clearManualButton  .setTooltip ("CLEAR GRID — erase every cell in the manual step grid.");
    commitManualButton .setTooltip ("APPEND TO ARR. — commit the manual pattern into the arrangement as a new region so you can mix it with AI-generated regions.");
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

    // v1.1.0 — MIXER toggle: slides the per-drum mixer over the arrangement.
    styleSmallBtn (mixerButton);
    mixerButton.setClickingTogglesState (true);
    mixerButton.setTooltip ("MIXER — per-drum channel strips: EQ / Compressor / Drive / Clip / Dampen / Reverb send, plus pan / fader / mute / solo.");
    mixerButton.onClick = [this]
    {
        const bool on = mixerButton.getToggleState();
        mixerPanel.setVisible (on);
        if (on) mixerPanel.toFront (false);
    };
    addAndMakeVisible (mixerButton);
    addChildComponent (mixerPanel);

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
    loopButton      .setTooltip ("LOOP — when enabled, the arrangement wraps to the start automatically at the end.");

    plusButton      .setTooltip ("APPEND — generate a new region with current settings and add it after the last one.");
    undoButton      .setTooltip ("UNDO — remove the last appended region from the arrangement.");
    clearButton     .setTooltip ("CLEAR — wipe the arrangement and start a fresh single region.");
    dragHandle      .setTooltip ("DRAG MIDI — hold and drag onto a DAW track to drop the full arrangement as a .mid file.");
    saveMidiButton  .setTooltip ("SAVE MIDI — export the full arrangement to a .mid file on disk.");
    playButton      .setTooltip ("PLAY — starts the built-in audio engine in Standalone. In a DAW, playback follows host transport.");
    pauseButton     .setTooltip ("PAUSE — freezes playback at the current position.");
    stopButton      .setTooltip ("STOP — stops playback and rewinds to beat 1.");

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
    roomAttachment          = std::make_unique<ComboAttachment>  (apvts, "room",          roomBox);
    roomAmountAttachment    = std::make_unique<SliderAttachment> (apvts, "roomAmount",    roomAmountSlider);
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

    // Drain hit-event counters and pulse the matching drum in the visualizer.
    auto& synth = processorRef.getDrumSynth();
    for (int b = 0; b < KitVisualizer::kNumFlashes; ++b)
    {
        const int hits = synth.readAndResetHitCount (b);
        if (hits > 0)
            kitVisualizer.pulseBus (b, synth.lastHitVelocity (b));
    }
    kitVisualizer.decayFlashes (0.78f);
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

    // ----- Top row: Kit Visualizer | XY Pad | Combo stack -----
    // Combos now own the full right panel height so the DRUM KIT dropdown is
    // always visible; the four rotary knobs (variation / humanize / swing /
    // fills) moved to their own dedicated row below.
    auto top = area.removeFromTop (340);
    auto leftPanel   = top.removeFromLeft  (252);
    auto rightPanel  = top.removeFromRight (236);
    auto centerPanel = top;

    kitVisualizer.setBounds (leftPanel.reduced (2, 4));
    xyPad        .setBounds (centerPanel.reduced (6, 6));

    auto rightCombos = rightPanel.reduced (10, 6);
    // Each combo slot = 22px label (attached above) + 28px combo body + 6px gap.
    auto placeCombo = [&rightCombos] (juce::ComboBox& c)
    {
        auto slot = rightCombos.removeFromTop (56);
        c.setBounds (slot.withTrimmedTop (22).withTrimmedBottom (6));
    };

    placeCombo (drumKitBox);
    placeCombo (genreBox);
    placeCombo (roomBox);
    placeCombo (patternLengthBox);
    placeCombo (modeBox);
    placeCombo (hiHatBox);
    halfTimeButton.setBounds (rightCombos.removeFromTop (28).reduced (0, 2));

    area.removeFromTop (8);

    // ----- Knobs row: Variation | Humanize | Swing | Fills | Room Amount -----
    auto knobsRow = area.removeFromTop (108).reduced (4, 6);
    const int knobW = knobsRow.getWidth() / 5;
    auto placeKnob = [&knobsRow, knobW] (juce::Slider& s)
    {
        auto cell = knobsRow.removeFromLeft (knobW).reduced (6, 10);
        s.setBounds (cell);
    };
    placeKnob (variationSlider);
    placeKnob (humanizeSlider);
    placeKnob (swingSlider);
    placeKnob (fillsSlider);
    placeKnob (roomAmountSlider);

    area.removeFromTop (8);

    // Action row: transport, edit actions, export, with the legacy append
    // button kept small because the primary + now lives on the arrangement strip.
    auto action = area.removeFromTop (92);

    auto transportCluster = action.removeFromLeft (260).reduced (4, 16);
    const int transportW = (transportCluster.getWidth() - 18) / 4;
    playButton .setBounds (transportCluster.removeFromLeft (transportW));
    transportCluster.removeFromLeft (6);
    pauseButton.setBounds (transportCluster.removeFromLeft (transportW));
    transportCluster.removeFromLeft (6);
    stopButton .setBounds (transportCluster.removeFromLeft (transportW));
    transportCluster.removeFromLeft (6);
    loopButton .setBounds (transportCluster.removeFromLeft (transportW));

    auto exportCluster = action.removeFromRight (260).reduced (4, 16);
    dragHandle.setBounds (exportCluster.removeFromTop (26).reduced (2));
    exportCluster.removeFromTop (8);
    saveMidiButton.setBounds (exportCluster.removeFromTop (26).reduced (2));

    auto center = action.reduced (10, 6);
    auto leftCluster = center.removeFromLeft (170).reduced (2, 10);
    const int lBtnH = (leftCluster.getHeight() - 8) / 2;
    undoButton        .setBounds (leftCluster.getX(), leftCluster.getY(),
                                  leftCluster.getWidth(), lBtnH);
    clearManualButton .setBounds (undoButton.getBounds());
    clearButton       .setBounds (leftCluster.getX(), leftCluster.getY() + lBtnH + 8,
                                  leftCluster.getWidth(), lBtnH);
    commitManualButton.setBounds (clearButton.getBounds());

    const int plusSize = 46;
    juce::Rectangle<int> plusRect (center.getCentreX() - plusSize / 2,
                                   center.getCentreY() - plusSize / 2 - 4,
                                   plusSize, plusSize);
    plusButton.setBounds (plusRect);
    plusHelper.setBounds (plusRect.getX() - 28, plusRect.getBottom() - 1,
                          plusRect.getWidth() + 56, 16);

    area.removeFromTop (6);

    // MANUAL / MIXER toggle bar — always visible above the strip/grid.
    auto manualBar = area.removeFromTop (30);
    manualButton.setBounds (manualBar.removeFromLeft (140).reduced (2));
    manualBar.removeFromLeft (6);
    mixerButton.setBounds (manualBar.removeFromLeft (120).reduced (2));
    loopButton.toFront (false);

    area.removeFromTop (4);

    // Arrangement strip / manual grid share the remaining area.
    arrangementStrip.setBounds (area);
    manualGrid      .setBounds (area);
    mixerPanel      .setBounds (area);
}
