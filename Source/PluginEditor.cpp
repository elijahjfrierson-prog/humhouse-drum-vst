#include "PluginEditor.h"

#if AIDRUM_HAS_BRANDING
 #include "BrandingData.h"
#endif

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

    auto labelFont = juce::Font (juce::FontOptions (10.0f, juce::Font::italic));
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
AIDrumAudioProcessorEditor::KitVisualizer::KitVisualizer()
{
    // v1.6.1-rc.9 — try to load the bundled 3D (Nu Rock) 70's Yamaha
    // kit render from BinaryData. If the asset is missing for any
    // reason we silently fall back to the original vector silhouette
    // in paint() so the editor still shows *something*.
    // v1.6.1-rc.12 — (Bay Grunge) Yamaha Maple kit photo removed; the
    // second bundled kit was pulled at the user's request.
    int sz = 0;
    if (auto* d = BrandingData::getNamedResource ("NuRockYamahaKit_png", sz))
        kitPhotoNuRock = juce::ImageFileFormat::loadFrom (d, (size_t) sz);
}

void AIDrumAudioProcessorEditor::KitVisualizer::setActiveBundledKit (int kitIndex)
{
    // v1.6.1-rc.12 — only one bundled kit ships now. Method retained
    // so legacy callers compile, but kitIndex is ignored.
    juce::ignoreUnused (kitIndex);
}

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

    // v1.6.1-rc.9 — when the bundled 3D kit render is available we paint
    // the photo as the kit visual and skip the original vector
    // silhouette. Yellow flash dots are overlaid at each drum's
    // approximate position on the photo so a hit reads as the real
    // kit being struck. Bus index → drum mapping mirrors
    // DrumBusMixer / DrumSynth::kNumBuses (0=KICK, 1=SNARE, 2=HAT,
    // 3=SMALL TOM, 4=FLOOR TOM, 5=RIDE, 6=L CRASH, 7=R CRASH).
    // v1.6.1-rc.12 — single bundled kit again; (Bay Grunge) Yamaha Maple
    // pulled at user request.
    const juce::Image& kitPhoto = kitPhotoNuRock;
    if (kitPhoto.isValid())
    {
        auto inner = r.reduced (6.0f);
        const float photoAR = (float) kitPhoto.getWidth() / (float) kitPhoto.getHeight();
        const float boxAR   = inner.getWidth() / std::max (1.0f, inner.getHeight());
        juce::Rectangle<float> photoArea = inner;
        if (boxAR > photoAR)
        {
            const float w = inner.getHeight() * photoAR;
            photoArea = inner.withSizeKeepingCentre (w, inner.getHeight());
        }
        else
        {
            const float h = inner.getWidth() / photoAR;
            photoArea = inner.withSizeKeepingCentre (inner.getWidth(), h);
        }
        g.setOpacity (1.0f);
        g.drawImage (kitPhoto, photoArea, juce::RectanglePlacement::centred);

        // Active-kit caption — small italic line top-left so users
        // still see "(Nu Rock) 70's Yamaha" without overpowering the photo.
        auto titleFont = juce::Font (juce::FontOptions (10.0f, juce::Font::italic));
        titleFont.setExtraKerningFactor (0.30f);
        g.setFont (titleFont);
        g.setColour (juce::Colour (Palette::kMuted));
        g.drawText ("ACTIVE KIT",
                    r.toNearestInt().reduced (14, 10),
                    juce::Justification::topLeft, false);
        // v1.6.1-rc.12 — single bundled kit caption.
        const juce::String kitName = juce::String ("(Nu Rock) 70's Yamaha");
        g.setColour (juce::Colour (Palette::kBone));
        g.drawFittedText (kitName,
                          r.toNearestInt().withTrimmedTop (22).reduced (14, 0),
                          juce::Justification::topLeft, 2);

        // Yellow trigger dots (subtle) — relative photo coordinates
        // [0..1, 0..1] sampled from the bundled render so each drum
        // lights at the right point on the kit picture.
        // Bus indices follow aidrum::Bus (DrumBusMixer.h): Kick=0,
        // Snare=1, Toms=2, ClosedHat=3, OpenHat=4, Ride=5, Crash=6,
        // China=7. Toms share a single bus, so SMALL TOM and FLOOR TOM
        // both flash off bus 2; HAT flashes off ClosedHat (3); R CRASH
        // is routed to the China bus (7) per SampleKit.cpp.
        struct DotPos { int bus; float u; float v; float radius; };
        const DotPos kDots[] = {
            { 0, 0.50f, 0.78f, 18.0f },  // KICK   — front bass-drum head
            { 1, 0.36f, 0.62f, 11.0f },  // SNARE  — left of kick, between toms
            { 3, 0.27f, 0.46f, 10.0f },  // HAT    — far-left hi-hat stand
            { 2, 0.46f, 0.50f, 10.0f },  // SMALL TOM — left rack tom (Toms bus)
            { 2, 0.66f, 0.66f, 13.0f },  // FLOOR TOM — right floor tom (Toms bus)
            { 5, 0.74f, 0.40f, 12.0f },  // RIDE   — right side cymbal
            { 6, 0.30f, 0.30f, 12.0f },  // L CRASH — left top cymbal (Crash)
            { 7, 0.62f, 0.32f, 12.0f },  // R CRASH — right top cymbal (China)
        };

        for (const auto& d : kDots)
        {
            const float f = (d.bus >= 0 && d.bus < kNumFlashes)
                          ? flash[d.bus] : 0.0f;
            const float cx = photoArea.getX() + d.u * photoArea.getWidth();
            const float cy = photoArea.getY() + d.v * photoArea.getHeight();
            // Idle dot (very subtle).
            g.setColour (juce::Colour (0xffffd866).withAlpha (0.28f));
            g.fillEllipse (cx - d.radius * 0.45f, cy - d.radius * 0.45f,
                           d.radius * 0.90f, d.radius * 0.90f);
            if (f > 0.01f)
            {
                // Render-video flash on hit: bright yellow halo that
                // grows with velocity, fades on every decayFlashes() tick.
                const float grow = 1.0f + 1.4f * f;
                g.setColour (juce::Colour (0xffffe680).withAlpha (0.60f * f));
                g.fillEllipse (cx - d.radius * grow, cy - d.radius * grow,
                               d.radius * 2.0f * grow, d.radius * 2.0f * grow);
                g.setColour (juce::Colour (0xfffff7b0).withAlpha (0.95f * f));
                g.fillEllipse (cx - d.radius * 0.55f, cy - d.radius * 0.55f,
                               d.radius * 1.10f, d.radius * 1.10f);
            }
        }
        return;
    }

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

    auto titleFont = juce::Font (juce::FontOptions (10.0f, juce::Font::italic));
    titleFont.setExtraKerningFactor (0.30f);
    g.setFont (titleFont);
    g.setColour (juce::Colour (Palette::kMuted));
    g.drawText ("ACTIVE KIT", r.toNearestInt().reduced (14, 10), juce::Justification::topLeft, false);

    const juce::String kitName = "(Nu Rock) 70's Yamaha";
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
            auto lf = juce::Font (juce::FontOptions (8.5f, juce::Font::italic));
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
                auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::italic));
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
            auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::italic));
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
        auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::italic));
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
        auto lf = juce::Font (juce::FontOptions (7.5f, juce::Font::italic));
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
    auto f = juce::Font (juce::FontOptions (11.5f, juce::Font::italic));
    f.setExtraKerningFactor (0.18f);
    g.setFont (f);
    g.drawText ("HIGHLIGHT  ALL  \u2192  DAW", getLocalBounds().withTrimmedLeft (10),
                juce::Justification::centredLeft, false);
}

void AIDrumAudioProcessorEditor::MidiDragHandle::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
    if (onHighlightChange) onHighlightChange (true);
}

void AIDrumAudioProcessorEditor::MidiDragHandle::mouseUp (const juce::MouseEvent&)
{
    if (onHighlightChange) onHighlightChange (false);
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

    // v1.4.0 — resizable editor + persisted UI scale. JUCE applies the
    // AffineTransform around setSize() so the DAW reports the correct
    // outer size to the host window manager.
    setResizable (true, true);
    // v1.6.1-rc.10 — minimum drops from 720 × 690 to 560 × 540 so the
    // 55 % stop is reachable; max stays sane for the new 1.00 ceiling.
    setResizeLimits (528, 506, 1100, 1050);
    const float initialScale = juce::jlimit (0.55f, 1.10f, processorRef.getUiScale());
    setSize ((int) std::round (960.0f * initialScale),
             (int) std::round (920.0f * initialScale));
    setTransform (juce::AffineTransform::scale (initialScale));

    // Title — v1.6.1-rc.7 attempt to load the bundled HumHouse crest from
    // BinaryData; if present, paint it instead of the text title. Falling
    // back to the kerned text means installs that strip the branding blob
    // still get a readable masthead.
   #if AIDRUM_HAS_BRANDING
    {
        int dataSize = 0;
        if (auto* data = BrandingData::getNamedResource ("HumHouseLogo_png", dataSize))
            logoImage = juce::ImageFileFormat::loadFrom (data, (size_t) dataSize);
    }
   #endif

    {
        auto f = juce::Font (juce::FontOptions (28.0f, juce::Font::plain));
        f.setExtraKerningFactor (0.35f);
        titleLabel.setFont (f);
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setColour (juce::Label::textColourId, juce::Colour (Palette::kBone));
        if (! logoImage.isValid())
            addAndMakeVisible (titleLabel);

        auto s = juce::Font (juce::FontOptions (11.0f, juce::Font::italic));
        s.setExtraKerningFactor (0.4f);
        subtitleLabel.setFont (s);
        subtitleLabel.setJustificationType (juce::Justification::centred);
        subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (Palette::kMuted));
        // v1.6.1-rc.8 — user asked to keep "just the hum house haunt on the top".
        // The crest already says "HUMHOUSE DRUMS" inside the artwork, so the
        // text masthead became redundant. Subtitle hides whenever the crest
        // is loaded; the text fallback is only used on installs that strip
        // the branding blob.
        if (! logoImage.isValid())
            addAndMakeVisible (subtitleLabel);
    }

    // Arrangement strip (piano-roll grid that grows per region).
    arrangementStrip.setProvider ([this]
    {
        aidrum::ArrangementStrip::Snapshot s;
        s.regions             = processorRef.getArrangement();
        s.totalBeats          = processorRef.getArrangementTotalBeats();
        s.playheadBeats       = processorRef.getPlayheadBeats();
        // v1.6.1-rc.16 — paste pill only lights up when the processor
        // actually has a region copied. Polled at 30 Hz via the strip's
        // internal timer so the brightness flips in real time after
        // pressing C / Ctrl+C.
        s.clipboardHasContent = processorRef.hasCopiedRegion();
        return s;
    });
    arrangementStrip.onAppend = [this]
    {
        // v1.6.1-rc.14 — MODE toggle hidden; every appended region is a
        // Groove. Procedural fills are auto-spliced on bar 8 of every
        // 8-bar block by spliceMandatoryFillIntoRegion().
        const auto mode = aidrum::GenerationMode::Groove;
        juce::ignoreUnused (modeBox);
        processorRef.appendRegion (mode);
        plusButton.bump();
        arrangementStrip.repaint();
    };
    // v1.5.0 — right-click / alt-click a region to delete it. Arrangement is
    // allowed to go empty; + on the far right creates the first new region.
    arrangementStrip.onDeleteRegion = [this] (int idx)
    {
        processorRef.deleteRegion (idx);
        arrangementStrip.repaint();
    };
    // v1.6.1-rc.4 — per-note click editing in the arrangement.
    arrangementStrip.onDeleteNote = [this] (int region, int note)
    {
        processorRef.deleteNoteInRegion (region, note);
        arrangementStrip.repaint();
    };
    // v1.6.1-rc.11 — Devin Review: route the drag-select multi-delete
    // through the batched processor entry so all victim notes erase
    // under a single arrangementMutex acquisition. Prevents deferred
    // APVTS callbacks from invalidating note indices mid-batch.
    arrangementStrip.onDeleteNotes = [this] (std::vector<std::pair<int, int>> victims)
    {
        processorRef.deleteNotesInRegions (std::move (victims));
        arrangementStrip.repaint();
    };
    arrangementStrip.onDuplicateNote = [this] (int region, int note)
    {
        processorRef.duplicateNoteInRegion (region, note);
        arrangementStrip.repaint();
    };
    // v1.6.1-rc.5 — left-click an empty cell to drop a new note
    // (step-sequencer toggle). Drag across cells to paint a run.
    arrangementStrip.onAddNote = [this] (int region, double startBeat,
                                         int noteNumber, float velocity)
    {
        processorRef.addNoteToRegion (region, noteNumber, startBeat, 0.25, velocity);
        arrangementStrip.repaint();
    };

    // v1.6.1-rc.7 — clicking a row label arms that lane for the GHOST
    // button. The bitmask + greyed-out label re-paint happens when
    // GHOST is then clicked (see ghostButton.onClick further down).
    arrangementStrip.onLaneSelected = [this] (int laneIdx)
    {
        ghostSelectedLane = laneIdx;
        arrangementStrip.setSelectedLane (laneIdx);
    };

    // v1.6.1-rc.19 — right-click on a lane label opens the per-lane
    // SAMPLE PICKER popup. Lists every layer in the active kit's slot
    // for that lane, plus an "Auto (velocity-driven)" option which
    // clears the override. Selecting a layer pins it for that lane on
    // every subsequent noteOn, regardless of velocity. Lets the user
    // dial in "I want pad #5 on R CRASH, snare #3 on the snare lane"
    // without leaving the arrangement strip.
    arrangementStrip.onLanePickerRequested =
        [this] (int laneIdx, juce::Point<int> screenPos)
    {
        static const std::array<const char*, 8> kLaneParamIds {
            "laneSampRCrash", "laneSampLCrash",
            "laneSampRide",   "laneSampHat",
            "laneSampSmallTom","laneSampFloorTom",
            "laneSampSnare",  "laneSampKick"
        };
        static const std::array<const char*, 8> kLaneNamesDefault {
            "R CRASH", "L CRASH", "RIDE", "HI-HAT",
            "SMALL TOM", "FLOOR TOM", "SNARE", "KICK"
        };
        static const std::array<const char*, 8> kLaneNamesTrap {
            "PAD", "SYNTH", "PHRASE", "HI-HAT",
            "PERC (HI)", "PERC (LO)", "SNARE", "808"
        };

        if (laneIdx < 0 || laneIdx >= 8) return;

        auto& kit = processorRef.getSampleKit();
        const int numLayers = kit.numLayersForLane (laneIdx);
        const bool trap = arrangementStrip.getTrapMode();
        const auto* paramId = kLaneParamIds[(size_t) laneIdx];
        const juce::String laneName =
            (trap ? kLaneNamesTrap : kLaneNamesDefault)[(size_t) laneIdx];

        auto* p = processorRef.getAPVTS().getParameter (paramId);
        const int currentOverride = p != nullptr
            ? (int) ((juce::AudioParameterInt*) p)->get()
            : 0;

        juce::PopupMenu menu;
        menu.addSectionHeader (laneName + " — sample picker");
        menu.addItem (1, "Auto (velocity-driven)",
                      true, currentOverride == 0);
        menu.addSeparator();

        if (numLayers <= 0)
        {
            menu.addItem (-1,
                "(no kit loaded — pick a Bundled Kit first)",
                false, false);
        }
        else
        {
            // v1.6.1-rc.20 — folder-tree subsector picker. Group layers
            // by the part of their filename stem that comes before the
            // trailing "_NN" velocity index. So "pad_dark_01",
            // "pad_dark_02", "pad_atmos_01" become two sub-menus
            // ("Pad / Dark", "Pad / Atmos"). Stems without an obvious
            // sub-category (e.g. plain "kick" or "snare") fall under a
            // synthesised "(Default)" group so they still render.
            const auto names = kit.layerNamesForLane (laneIdx);

            auto subcat = [] (juce::String stem) -> juce::String
            {
                // v1.6.1-rc.20-fix2 — velocity suffix is already stripped
                // at load time by SampleKit::stripVelocitySuffix, so we
                // can use the stem itself (post leading kit-prefix strip)
                // as the group key. Splitting at the last underscore was
                // wrong: it conflated "bass_808" with "bass" and produced
                // misleading sub-menu titles. Distinct stems naturally
                // form distinct groups; identical stems (the velocity
                // ladder of one sample, e.g. all 12 "kick" layers) all
                // land in one bucket and we label the items with their
                // velocity index below.
                const int dd = stem.indexOf ("__");
                if (dd >= 0) stem = stem.substring (dd + 2);
                if (stem.isEmpty()) return "(default)";
                return stem;
            };

            std::map<juce::String, std::vector<int>> groups;
            std::vector<juce::String> orderedKeys;
            for (int i = 0; i < numLayers; ++i)
            {
                const auto stem = (i < names.size() ? names[i] : juce::String());
                const auto key  = subcat (stem);
                if (groups.find (key) == groups.end())
                    orderedKeys.push_back (key);
                groups[key].push_back (i);
            }

            const bool flat = (orderedKeys.size() <= 1);
            for (const auto& key : orderedKeys)
            {
                const auto& idxs = groups[key];
                juce::PopupMenu sub;
                juce::PopupMenu* target = flat ? &menu : &sub;
                // v1.6.1-rc.20-fix2 — number layers within a group so the
                // user can tell apart 12 velocity layers that all share
                // the same post-strip stem (e.g. kick layers 1..12 in
                // the Drocetti kit). Numbering is per-group / 1-based
                // so the first kick is "kick (1)", the second is
                // "kick (2)", etc. — reads as "soft → hard".
                int withinGroup = 0;
                for (int i : idxs)
                {
                    ++withinGroup;
                    const int itemId = 2 + i; // 1 reserved for "Auto"
                    const bool ticked = (currentOverride == i + 1);
                    const auto stem = (i < names.size() ? names[i] : juce::String());
                    juce::String label;
                    if (idxs.size() == 1)
                        label = stem.isNotEmpty()
                                ? stem
                                : juce::String ("Sample ") + juce::String (i + 1);
                    else if (stem.isNotEmpty())
                        label = stem + " (" + juce::String (withinGroup) + ")";
                    else
                        label = juce::String ("Sample ") + juce::String (i + 1);
                    target->addItem (itemId, label, true, ticked);
                }
                if (! flat)
                {
                    auto pretty = key.replaceCharacter ('_', ' ');
                    pretty = pretty.toLowerCase();
                    menu.addSubMenu (pretty, sub);
                }
            }
        }

        juce::PopupMenu::Options opts;
        opts = opts.withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1));

        menu.showMenuAsync (opts,
            [this, laneIdx, paramId] (int chosen)
            {
                if (chosen <= 0) return;
                auto* pp = processorRef.getAPVTS().getParameter (paramId);
                if (pp == nullptr) return;

                const int newOverride = (chosen == 1) ? 0 : (chosen - 1);
                const float norm = pp->convertTo0to1 ((float) newOverride);
                pp->beginChangeGesture();
                pp->setValueNotifyingHost (norm);
                pp->endChangeGesture();
                // SampleKit::setLaneOverride is also called via the
                // processor's parameterChanged listener — duplicate
                // call here is cheap (atomic store) and guarantees
                // the change is visible even if the listener is
                // momentarily blocked.
                processorRef.getSampleKit()
                            .setLaneOverride (laneIdx, newOverride);
                arrangementStrip.repaint();
            });
    };

    // v1.6.1-rc.28 — per-lane PIANO ROLL chromatic note picker. Click
    // the ▦ icon next to a lane label and a popup opens listing every
    // chromatic note from C-1 to G9 (-24..+24 semitone offset around
    // the lane's default GM trigger). Selecting a note pins the
    // transpose for that lane on every subsequent noteOn, so the user
    // can retune kick-by-fifth, snare-up-an-octave, ride-down-3-semis
    // without leaving the arrangement view. Reset entry sets 0
    // semitones (default pitch). Persists with project state via the
    // 8 laneXpos* APVTS params registered in createParameterLayout.
    arrangementStrip.onLanePianoRollRequested =
        [this] (int laneIdx, juce::Point<int> screenPos)
    {
        static const std::array<const char*, 8> kLaneXposIds {
            "laneXposRCrash", "laneXposLCrash",
            "laneXposRide",   "laneXposHat",
            "laneXposSmallTom","laneXposFloorTom",
            "laneXposSnare",  "laneXposKick"
        };
        static const std::array<const char*, 8> kLaneNamesDefault {
            "R CRASH", "L CRASH", "RIDE", "HI-HAT",
            "SMALL TOM", "FLOOR TOM", "SNARE", "KICK"
        };
        static const std::array<const char*, 8> kLaneNamesTrap {
            "PAD", "SYNTH", "PHRASE", "HI-HAT",
            "PERC (HI)", "PERC (LO)", "SNARE", "808"
        };
        // Default trigger note (the GM note the lane plays at +0
        // transpose). Used to render the popup labels as actual
        // pitches so the user reads "+3 semis (D♯1)" instead of a
        // bare "+3". Same lane index ordering as kLaneNamesDefault.
        // Must match ArrangementStrip's kLaneNote table at
        // ArrangementStrip.h:1168 — Floor Tom is 43 (Mid Tom 2),
        // not 41 (Low Floor Tom). Drift between the two tables
        // misnames every entry in the popup label.
        static const std::array<int, 8> kLaneRootNote {
            57 /*A4 / China*/, 49 /*Crash 1*/, 51 /*Ride*/,
            42 /*Closed Hat*/, 48 /*Small Tom*/, 43 /*Floor Tom*/,
            38 /*Snare*/,      36 /*Kick*/
        };

        if (laneIdx < 0 || laneIdx >= 8) return;

        const bool trap = arrangementStrip.getTrapMode();
        const auto* paramId = kLaneXposIds[(size_t) laneIdx];
        const juce::String laneName =
            (trap ? kLaneNamesTrap : kLaneNamesDefault)[(size_t) laneIdx];

        auto* p = processorRef.getAPVTS().getParameter (paramId);
        const int currentXpos = p != nullptr
            ? (int) ((juce::AudioParameterInt*) p)->get()
            : 0;

        auto noteName = [] (int midi) -> juce::String
        {
            static const char* names[] = {
                "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
            };
            const int oct = (midi / 12) - 1;
            return juce::String (names[((midi % 12) + 12) % 12])
                 + juce::String (oct);
        };

        juce::PopupMenu menu;
        menu.addSectionHeader (laneName + " — piano roll (transpose)");
        // ID 1 = reset to 0 semitones; semitone offsets map to IDs
        // (semitone + 100) so we can pack -24..+24 into a single
        // popup without ID collisions.
        menu.addItem (1,
            "Reset (0 semitones — " + noteName (kLaneRootNote[(size_t) laneIdx]) + ")",
            true, currentXpos == 0);
        menu.addSeparator();

        // -24..-1 (down two octaves), then +1..+24 (up two octaves).
        // Group into "Down" / "Up" submenus so the popup stays compact.
        juce::PopupMenu down, up;
        for (int s = -24; s <= -1; ++s)
        {
            const int target = kLaneRootNote[(size_t) laneIdx] + s;
            const int id = s + 100; // 76..99
            const juce::String label =
                (s >= 0 ? juce::String ("+") : juce::String())
                + juce::String (s) + " semis (" + noteName (target) + ")";
            down.addItem (id, label, true, currentXpos == s);
        }
        for (int s = 1; s <= 24; ++s)
        {
            const int target = kLaneRootNote[(size_t) laneIdx] + s;
            const int id = s + 100; // 101..124
            const juce::String label =
                (s >= 0 ? juce::String ("+") : juce::String())
                + juce::String (s) + " semis (" + noteName (target) + ")";
            up.addItem (id, label, true, currentXpos == s);
        }
        menu.addSubMenu ("Down (-1..-24)", down);
        menu.addSubMenu ("Up (+1..+24)",   up);

        juce::PopupMenu::Options opts;
        opts = opts.withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1));

        menu.showMenuAsync (opts,
            [this, laneIdx, paramId] (int chosen)
            {
                if (chosen <= 0) return;
                auto* pp = processorRef.getAPVTS().getParameter (paramId);
                if (pp == nullptr) return;

                const int newXpos = (chosen == 1) ? 0 : (chosen - 100);
                const float norm = pp->convertTo0to1 ((float) newXpos);
                pp->beginChangeGesture();
                pp->setValueNotifyingHost (norm);
                pp->endChangeGesture();
                processorRef.getSampleKit()
                            .setLaneTranspose (laneIdx, newXpos);
                arrangementStrip.repaint();
            });
    };

    // v1.6.1-rc.14 — per-region INTENSITY drag-strip. Click + drag the
    // gold bar at the bottom of any region tile to set that region's
    // velocity vibe (soft pre-chorus → slammed chorus → somber bridge);
    // right-click clears the override (region inherits the global
    // INTENSITY knob). Audio thread reads the value at MIDI emit time
    // via shapeVelocity, so no regen is needed.
    arrangementStrip.onRegionIntensityChanged = [this] (int regionIdx, float v)
    {
        processorRef.setRegionIntensity (regionIdx, v);
        arrangementStrip.repaint();
    };

    // v1.6.1-rc.15 — clicking the per-region INTENSITY mini-knob (now
    // painted next to every region's number) just nudges the strip to
    // repaint; the region itself doesn't need to be "selected" because
    // the per-region intensity is read from regionIntensity at MIDI
    // emit time, independent of any global "active region" concept.
    // v1.6.1-rc.16 — per-region COPY / PASTE buttons. The C pill in
    // a region's header snapshots that region's pattern; the P pill
    // overwrites that region with the snapshot. Region's INTENSITY
    // override is preserved by pasteCopiedRegionInto so dialed-in
    // pre-chorus / chorus / bridge survives the paste.
    arrangementStrip.onCopyRegion = [this] (int regionIdx)
    {
        processorRef.copyRegionToClipboard (regionIdx);
    };
    arrangementStrip.onPasteRegion = [this] (int regionIdx)
    {
        processorRef.pasteCopiedRegionInto (regionIdx);
        arrangementStrip.repaint();
    };

    arrangementStrip.onRegionIntensitySelected = [this] (int /*regionIdx*/)
    {
        arrangementStrip.repaint();
    };

    // v1.6.1-rc.7 — Cmd/Ctrl + two-finger trackpad scroll on the strip
    // grows or shrinks the visible cell width. Implemented at the
    // editor layer because the strip doesn't own its bounds — we
    // resize the editor's overall scale via setUiScale() so the rest
    // of the UI tracks. ±0.05 per scroll tick, clamped 0.7..1.6.
    arrangementStrip.onZoom = [this] (float dy)
    {
        const float cur = processorRef.getUiScale();
        // v1.6.1-rc.10 — Cmd-scroll zoom range matches the new clamp
        // so trackpad zoom can't push the editor past 1.00 either.
        const float next = juce::jlimit (0.55f, 1.10f, cur + dy * 0.05f);
        processorRef.setUiScale (next);
        // setTransform must accompany setSize, otherwise the editor
        // resizes but the rendering scale stays at the previous factor
        // — content gets cropped or letterboxed. Mirrors the UI-SCALE
        // slider's handler.
        setTransform (juce::AffineTransform::scale (next));
        setSize ((int) std::round (960.0f * next),
                 (int) std::round (920.0f * next));
    };

    arrangementStrip.setWantsKeyboardFocus (true);
    addAndMakeVisible (arrangementStrip);

    // Manual grid (v0.8.0) — interactive 16-bar step sequencer.
    manualGrid.provider    = [this] { return processorRef.getManualPattern(); };
    manualGrid.onSetCell   = [this] (int note, int step, float vel)
    {
        processorRef.setManualCellStep (note, step, manualGrid.stepBeats(), vel);
        manualGrid.repaint();
    };
    manualGrid.onClearCell = [this] (int note, int step)
    {
        processorRef.clearManualCellStep (note, step, manualGrid.stepBeats());
        manualGrid.repaint();
    };
    manualGrid.setNumBars (processorRef.getManualNumBars());
    manualGrid.setTooltip ("MANUAL GRID \u2014 click cells to place kick / snare / tom / hat hits. "
                           "Alt-click or right-click to erase. Drag to paint multiple cells. "
                           "Works across all 16 bars; DRUM KIT remaps the timbre in your sampler.");
    manualGrid.setVisible (false);
    addChildComponent (manualGrid);

    // v1.6.1-rc.24 — the FL-Studio-style chromatic piano roll component
    // was removed. Manual editing is the step grid only; the host's
    // own piano roll drives chromatic input via the rc.24 host-MIDI
    // capture path in processBlock.

    xyPad.bind (&complexitySlider, &velocitySlider);
    addAndMakeVisible (xyPad);
    addAndMakeVisible (kitVisualizer);
    kitVisualizer.setSelectedKit (0);
    // v1.6.1-rc.12 — only one bundled kit; setActiveBundledKit() is now
    // a no-op but the call is left in place for legacy state restore.
    kitVisualizer.setActiveBundledKit (0);

    complexitySlider.setVisible (false);
    velocitySlider  .setVisible (false);
    complexityLabel .setVisible (false);
    velocityLabel   .setVisible (false);

    // Rotary knobs
    //
    // v1.6.1-rc.7 — mouse-drag sensitivity tuned so a full rotation of
    // the knob = a full rotation of the parameter, not "barely touch and
    // it skids to the end". Skewed value range (kKnobSkew = 0.55 in the
    // processor) gives the slow-glide-on-the-bottom / fast-glide-on-the-
    // top feel the user asked for. Velocity-based dragging is also
    // disabled so flicks don't overshoot.
    //
    // v1.6.1-rc.8 — bumped sensitivity from 480 → 640 px/rotation
    // (user: "lets just get the knobs and intensity scales smoother")
    // and added a 0.001 step + 18 px snap-tolerance so micro-tweaks
    // around the centre of the dial finally feel like real knurling
    // instead of a step-jump. Modifier key (Cmd/Ctrl) drops sensitivity
    // by 4× for fine-trim work, matching the Logic Pro convention.
    auto addRotary = [this] (juce::Slider& s, juce::Label& l)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setMouseDragSensitivity (640);
        s.setVelocityBasedMode (false);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f, true);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (Palette::kBone));
        addAndMakeVisible (s);

        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::italic));
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
    addRotary (swingSlider,          swingLabel);
    addRotary (fillsSlider,          fillsLabel);
    addRotary (fillComplexitySlider, fillComplexityLabel);
    addRotary (roomAmountSlider,     roomAmountLabel);
    addRotary (intensitySlider,      intensityLabel);
    addRotary (fillDensitySlider,    fillDensityLabel);

    // v1.6.1-rc.14 — Fill Density 0..1 stored, displayed as 0..100 %.
    fillDensitySlider.textFromValueFunction = [] (double v)
        { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    fillDensitySlider.setTooltip (
        "FILL DENSITY (0..100 %) — how many MIDI notes are packed into the "
        "procedurally-generated fill at the end of every 8-bar block. "
        "Light = sparse archetype baseline; high = 64th-note saturated rolls "
        "with doubled snares + tom cascades.");

    // v1.6.1-rc.7 — INTENSITY displayed as 0..127 (the MIDI velocity it
    // maps to). The underlying parameter stays 0..1 in APVTS so save
    // files survive parameter-range changes. Tooltip explains the
    // ±1..4% per-hit fluctuation behaviour.
    intensitySlider.textFromValueFunction = [] (double v)
        { return juce::String (juce::roundToInt (v * 127.0)); };
    intensitySlider.setTooltip (
        "INTENSITY (0..127) \u2014 base MIDI velocity for every emitted hit. "
        "Each hit fluctuates \u00b11..4% around this value (kick/snare stay "
        "stable, hats breathe, ghosts vary the most) so the kit feels human.");

    // v1.6.1-rc.7 — fillComplexitySlider is repurposed as the storage
    // backing for the FILL SELECTOR cycler. Hide the rotary control,
    // keep the slider alive (so its APVTS attachment still updates the
    // param), and the cycler buttons + name label become the visible
    // UI for the user.
    fillComplexitySlider.setVisible (false);
    fillComplexityLabel.setVisible (false);
    roomAmountSlider.setRange (0.0, 1.0, 0.001);
    roomAmountSlider.setValue (0.25);
    roomAmountSlider.textFromValueFunction = [] (double v)
        { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };

    variationSlider .setTooltip ("VARIATION — re-rolls the next-generated groove with a different seed so each + press sounds fresh.");
    humanizeSlider  .setTooltip ("HUMANIZE — random micro-timing / velocity jitter. 0 = machine-tight, 1 = loose drummer feel.");
    swingSlider     .setTooltip ("SWING — shuffles the off-beat 16ths late. 0 = straight, 1 = fully triplet swing.");
    fillsSlider     .setTooltip ("FILLS — probability the last region of a phrase ends in a snare/tom fill instead of a groove loop.");
    fillComplexitySlider.setTooltip ("FILL CX — intricacy of the fill rudiments (flam accents, hertas, tom rolls, 32nd builds). Independent of overall COMPLEXITY.");

    // Combos
    auto styleCombo = [this] (juce::ComboBox& c, juce::Label& l)
    {
        addAndMakeVisible (c);
        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::italic));
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

    // v1.6.1-rc.13 — must mirror kPatternLengthChoices in PluginProcessor.cpp
    // (9 entries). Default APVTS index is 7 ("8 bars") — without the new
    // items the ComboBox renders blank and the user can't pick the new
    // multi-bar lengths.
    patternLengthBox.addItemList (
        juce::StringArray { "1/16 note", "1/8 note", "1/4 note", "1/2 bar",
                            "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 1);
    styleCombo (patternLengthBox, patternLengthLabel);

    // v1.6.1-rc.14 — MODE toggle removed from the visible UI per user
    // request: "take out fill mode and implement that all into your idea
    // of the generative fills … fills should be sourced by you from now
    // and genrative with every single pattern". Every region is now a
    // groove with a procedural fill auto-spliced on bar 8 of every 8-bar
    // block (see spliceMandatoryFillIntoRegion). The APVTS parameter is
    // kept (state round-trip / DAW automation lanes) but the combo is
    // never shown; appendRegion() always uses GenerationMode::Groove.
    modeBox.addItem ("Groove", 1);
    modeBox.addItem ("Fill",   2);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.setVisible (false);
    modeLabel.setVisible (false);

    hiHatBox.addItem ("Dynamic", 1);
    hiHatBox.addItem ("Closed",  2);
    hiHatBox.addItem ("Open",    3);
    hiHatBox.addItem ("Ride",    4);
    styleCombo (hiHatBox, hiHatLabel);

    // v1.6.1-rc.6 — single bundled kit. User feedback: "stick to one
    // drum kit that sounds AMAZING ... this plug in will be based
    // around adding your own drumkit samples mainly with one crispy
    // sounding drumkit instead of 5". The combo still exists (so state
    // round-trips), but only holds the single default; the real kit
    // variety comes from LOAD KIT.
    // v1.6.1-rc.7 — single shipped kit re-labelled "Nu Rock Kit" per
    // the rc.7 brief ("in the active kit replace the name with Nu Rock
    // Kit instead of ludwig jazz"). Internally the bundled kit is still
    // the rc.6 Thrash profile, just rebranded for the user-facing UI.
    // v1.6.1-rc.8 — user requested rename: "name the kit (Nu Rock) 70's Yamaha".
    // v1.6.1-rc.12 — single bundled kit; (Bay Grunge) Yamaha Maple was
    // pulled at the user's request. Order MUST match kBundledKitChoices
    // in PluginProcessor.cpp so the ComboBoxAttachment to "bundledKit"
    // hits the right WAV-prefix bucket.
    // v1.6.1-rc.17 — second bundled kit added (HeavyStudio). Display order
    // MUST match kBundledKitChoices in PluginProcessor.cpp.
    // v1.6.1-rc.19 — third bundled kit added (Drocetti — user trap pack).
    // Order still MUST match kBundledKitChoices in PluginProcessor.cpp,
    // otherwise the ComboBoxAttachment to "bundledKit" silently misroutes
    // index 2 (Drocetti) to whatever sits in the editor's slot 2.
    // v1.6.1-rc.26 — Heavy Studio pulled (user: "just the trap kit and
    // my nu rock kit"). Two-entry combo. Order MUST stay in lockstep
    // with kBundledKitChoices in PluginProcessor.cpp so the
    // ComboBoxAttachment to "bundledKit" hits the right WAV-prefix.
    drumKitBox.addItemList (
        juce::StringArray { "(Nu Rock) 70's Yamaha",
                            "(Drocetti) Trap Kit" }, 1);
    styleCombo (drumKitBox, drumKitLabel);
    // NB: drumKitBox.onChange is wired up further down, AFTER the APVTS
    // ComboBoxAttachment is created — the attachment ctor steals
    // onChange, so chaining must happen post-attachment. See the
    // "chain custom onChange handlers" block at the bottom of this ctor.

    // v1.5.0 — step-division combo (for the manual grid).
    stepDivBox.addItemList (juce::StringArray { "1/16", "1/32", "1/64" }, 1);
    styleCombo (stepDivBox, stepDivLabel);
    // NB: stepDivBox.onChange is wired up post-attachment for the same
    // reason as drumKitBox above.
    stepDivBox.setTooltip ("STEP DIV — manual grid draw resolution. 1/16 = Logic default, 1/64 = 4x denser for tight ghost-note work.");

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
    patternLengthBox .setTooltip ("LENGTH — how long each appended region is (1/16 note → 16 bars; default 8 bars).");
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

    // v1.6.1-rc.19 — TRAP MODE toggle. Same visual style as HALF-TIME
    // so it slots naturally next to it on the right combos column.
    trapModeButton.setClickingTogglesState (true);
    trapModeButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
    trapModeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
    trapModeButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kMuted));
    trapModeButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (Palette::kBone));
    trapModeButton.setTooltip (
        "TRAP MODE — relabels arrangement lanes (L Crash → SYNTH, "
        "R Crash → PAD, Ride → PHRASE, Toms → PERC, Kick → 808) and "
        "auto-selects the Drocetti trap kit so the same MIDI plays "
        "trap-flavoured one-shots. Right-click any lane label for a "
        "per-lane sample picker.");
    addAndMakeVisible (trapModeButton);
    trapModeButton.onClick = [this]
    {
        arrangementStrip.setTrapMode (trapModeButton.getToggleState());
    };

    // v1.6.1-rc.4 — TIME SCALE: HALF / NORMAL / DOUBLE playback speed.
    // v1.6.1-rc.7 — combo hidden; three dedicated buttons drive the
    // same APVTS param so the user can flip speed with a single click.
    timeScaleBox.addItem ("HALF",   1);
    timeScaleBox.addItem ("NORMAL", 2);
    timeScaleBox.addItem ("DOUBLE", 3);
    timeScaleBox.setSelectedId (2, juce::dontSendNotification);
    timeScaleBox.setTooltip ("TIME — playback speed of the arrangement. HALF = half-time, NORMAL = 1×, DOUBLE = double-time.");
    styleCombo (timeScaleBox, timeScaleLabel);
    timeScaleBox.setVisible (false);
    timeScaleLabel.setVisible (false);

    // v1.6.1-rc.7 — HALF / NORMAL / DOUBLE three-button group above the
    // arrangement grid. Click flips the combo + lights up the active
    // button so the user always sees what speed the arrangement is
    // playing at.
    auto styleTransportToggle = [] (juce::TextButton& b)
    {
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kBone));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colour (Palette::kBone));
    };
    styleTransportToggle (halfButton);
    styleTransportToggle (normalButton);
    styleTransportToggle (doubleButton);
    halfButton  .setTooltip ("HALF-TIME — pattern plays at half speed (each beat lasts twice as long).");
    normalButton.setTooltip ("NORMAL — pattern plays at the host BPM.");
    doubleButton.setTooltip ("DOUBLE-TIME — pattern plays at twice the host BPM (good for double-time choruses).");

    auto setTimeScale = [this] (int id)
    {
        timeScaleBox.setSelectedId (id, juce::sendNotificationSync);
        halfButton  .setToggleState (id == 1, juce::dontSendNotification);
        normalButton.setToggleState (id == 2, juce::dontSendNotification);
        doubleButton.setToggleState (id == 3, juce::dontSendNotification);
    };
    halfButton  .onClick = [setTimeScale] { setTimeScale (1); };
    normalButton.onClick = [setTimeScale] { setTimeScale (2); };
    doubleButton.onClick = [setTimeScale] { setTimeScale (3); };
    // v1.6.1-rc.9 — force NORMAL and hide the HALF/NORMAL/DOUBLE transport
    // group entirely. Switching playback rate at runtime caused samples to
    // be re-resampled by the bundled-kit baker, sometimes pulling in the
    // unintended 70s/80s electronic-kit fallbacks. We keep the param itself
    // so older saved sessions still load, just pinned to NORMAL.
    setTimeScale (2);
    halfButton  .setVisible (false);
    normalButton.setVisible (false);
    doubleButton.setVisible (false);

    // v1.6.1-rc.13 — FILL SELECTOR is now a labeled dropdown of all 22
    // fills (gentle ghost rolls → sludge tom flares). User picks a fill
    // by name in one click instead of stepping through prev/next.
    {
        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::italic));
        f.setExtraKerningFactor (0.45f);
        fillSelectorTitle.setFont (f);
        fillSelectorTitle.setColour (juce::Label::textColourId,
                                     juce::Colour (Palette::kMuted));
        fillSelectorTitle.setJustificationType (juce::Justification::centred);
        // v1.6.1-rc.21 — FILL dropdown restored at user request. Fills still
        // auto-splice on bar 8 of every 8-bar block (driven by COMPLEXITY ×
        // INTENSITY); the dropdown picks the SEED archetype that the auto-
        // fill scheduler lerps from. Selecting a tom-centric fill biases
        // every auto-fill toward floor-tom flares / tom-roll cascades.
        fillSelectorTitle.setVisible (true);

        const auto fillNames = processorRef.getAllFillNames();
        fillSelectorBox.clear (juce::dontSendNotification);
        for (int i = 0; i < fillNames.size(); ++i)
            fillSelectorBox.addItem (fillNames[i], i + 1); // ItemIDs are 1-based
        fillSelectorBox.setSelectedId (processorRef.getCurrentFillIndex() + 1,
                                       juce::dontSendNotification);
        fillSelectorBox.setColour (juce::ComboBox::backgroundColourId,
                                   juce::Colour (Palette::kPanel));
        fillSelectorBox.setColour (juce::ComboBox::textColourId,
                                   juce::Colour (Palette::kBone));
        fillSelectorBox.setColour (juce::ComboBox::outlineColourId,
                                   juce::Colour (Palette::kAccentDeep));
        fillSelectorBox.setColour (juce::ComboBox::arrowColourId,
                                   juce::Colour (Palette::kBone));
        fillSelectorBox.setTooltip (
            "FILL — pick any fill archetype (gentle \u2192 sludge, with "
            "tom-focused bases at the heavy end). Selection is the seed; "
            "auto-fills on the closing bar of every 8-bar block lerp from "
            "here based on COMPLEXITY \u00d7 INTENSITY.");
        fillSelectorBox.onChange = [this]
        {
            const int sel = fillSelectorBox.getSelectedId() - 1;
            if (sel >= 0)
                processorRef.setFillIndex (sel);
        };
        // v1.6.1-rc.21 — FILL dropdown restored at user request.
        fillSelectorBox.setVisible (true);
        // v1.6.1-rc.23 — Devin Review regression fix: the dropdown was
        // configured + setVisible (true) + setBounds called, but never
        // added to the editor's component tree. JUCE only paints
        // children that have been addAndMakeVisible'd; without these
        // calls the FILL dropdown silently disappeared after rc.21.
        addAndMakeVisible (fillSelectorTitle);
        addAndMakeVisible (fillSelectorBox);
    }

    // v1.6.1-rc.7 — GHOST button. Workflow: user clicks a row label on
    // the arrangement strip (sets ghostSelectedLane via the strip's
    // onLaneSelected callback), then clicks GHOST. The selected lane's
    // bit in ghostMask flips; the strip greys-out the row name; new
    // hits in that lane are emitted at ghost-velocity (~0.25). Click
    // GHOST again on the same lane to turn it off.
    ghostButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (Palette::kPanel));
    ghostButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Palette::kAccentDeep));
    ghostButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (Palette::kBone));
    ghostButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (Palette::kBone));
    ghostButton.setTooltip (
        "GHOST \u2014 click an instrument row label on the arrangement "
        "(left side), then click GHOST. The row name greys out and new "
        "hits in that lane are emitted at ghost-velocity (~25% of full).");
    ghostButton.onClick = [this]
    {
        // v1.6.1-rc.8 — 8 lanes now (R CRASH, L CRASH, RIDE, HI-HAT,
        // SMALL TOM, FLOOR TOM, SNARE, KICK).
        if (ghostSelectedLane < 0 || ghostSelectedLane > 7) return;
        ghostMask ^= (1 << ghostSelectedLane);
        processorRef.setGhostMask (ghostMask);
        arrangementStrip.setGhostMask (ghostMask);
    };
    addAndMakeVisible (ghostButton);


    // v1.6.1-rc.3 — APPEND (+) button now picks a random groove from
    // the CURRENT KIT's bucket of 119 grooves (instead of synthesising
    // a new pattern). User explicitly asked to scrap ML-style
    // generation and stay inside the 119-groove library.
    // v1.6.1-rc.9 — COMPOSE molds around what the user already drew.
    // It cycles through the kit's groove bucket Scripter-style and
    // overlays the picked pattern's decoration notes on the last
    // region (preserving every kick / snare / hat the user laid down
    // by hand). RANDOMIZE — below — keeps the original full-replace
    // behavior for users who want a brand-new idea on a single click.
    plusButton.onClick = [this]
    {
        processorRef.composeMoldAroundForKit (drumKitBox.getSelectedItemIndex());
        plusButton.bump();
        arrangementStrip.repaint();
        manualGrid.repaint();
    };
    plusButton.setTooltip ("COMPOSE — molds around your existing pattern: cycles through the kit's groove bucket and overlays decoration (ghost notes, hat ostinatos, kick syncopations) on top of what you already drew. Your kicks/snares/hats are preserved.");
    addAndMakeVisible (plusButton);

    // RANDOMIZE — full pattern replace, sits next to COMPOSE. Styling
    // happens with the rest of the small buttons below where the
    // styleSmallBtn lambda is defined.
    randomizeButton.onClick = [this]
    {
        processorRef.randomizePatternForKit (drumKitBox.getSelectedItemIndex());
        plusButton.bump();
        arrangementStrip.repaint();
        manualGrid.repaint();
    };
    randomizeButton.setTooltip ("RANDOMIZE — rolls a fresh groove from the kit's bucket and replaces the current pattern (the pre-rc.9 COMPOSE behavior).");
    addAndMakeVisible (randomizeButton);

    {
        auto f = juce::Font (juce::FontOptions (11.0f, juce::Font::italic));
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
    styleSmallBtn (randomizeButton);
    styleSmallBtn (saveMidiButton);
    styleSmallBtn (playButton);
    styleSmallBtn (pauseButton);
    styleSmallBtn (stopButton);

    // v1.6.1-rc.5 — use a compact LookAndFeel just for the transport
    // trio so PLAY / PAUSE / STOP always fit inside their boxes.
    playButton .setLookAndFeel (&compactLnf);
    pauseButton.setLookAndFeel (&compactLnf);
    stopButton .setLookAndFeel (&compactLnf);

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

    // v1.6.1-rc.6 — STARTER combobox shows the full 119-groove library.
    // Kit-filtering was removed in rc.6 because the plugin collapsed
    // from 6 built-in kits to a single "Default" (with LOAD KIT for
    // user packs), so filtering by kit index is a no-op now.
    starterBox.setTooltip ("STARTER — pick a hand-played groove from the full library; drops in as a new region at the end of the arrangement.");
    rebuildStarterBox();
    starterBox.onChange = [this]
    {
        const int sel = starterBox.getSelectedId();
        if (sel >= 2)
        {
            const int kit = drumKitBox.getSelectedItemIndex();
            processorRef.appendStarterGrooveForKit (kit, sel - 2);
            plusButton.bump();
            arrangementStrip.repaint();
        }
        starterBox.setSelectedId (1, juce::dontSendNotification);
    };
    starterLabel.setText ("STARTER", juce::dontSendNotification);
    starterLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (starterBox);
    addAndMakeVisible (starterLabel);

    // v1.6.0 — COPY / PASTE region buttons. COPY snapshots the currently
    // selected region (fallback: last region); PASTE appends the snapshot
    // as a new region at the end of the arrangement. Keyboard shortcuts
    // Ctrl+C / Ctrl+V are wired up in the ArrangementStrip.
    styleSmallBtn (copyRegionButton);
    styleSmallBtn (pasteRegionButton);
    copyRegionButton .setTooltip ("COPY — snapshot the selected arrangement region (or the last one if none selected). Paste duplicates it at the end.");
    pasteRegionButton.setTooltip ("PASTE — append the most recently copied region to the end of the arrangement.");
    copyRegionButton.onClick  = [this]
    {
        const auto arr = processorRef.getArrangement();
        const int idx = (int) arr.size() - 1;
        if (idx >= 0)
            processorRef.copyRegionToClipboard (idx);
    };
    pasteRegionButton.onClick = [this]
    {
        processorRef.pasteCopiedRegion();
        arrangementStrip.repaint();
    };
    addAndMakeVisible (copyRegionButton);
    addAndMakeVisible (pasteRegionButton);

    // MANUAL mode toggle — swaps the arrangement strip for the interactive grid.
    styleSmallBtn (manualButton);
    styleSmallBtn (clearManualButton);
    styleSmallBtn (commitManualButton);
    manualButton       .setTooltip ("MANUAL — swap the arrangement for a 16-bar interactive step grid. Click cells to place kick/snare/tom/hat hits, drag to paint, alt-click to erase.");
    clearManualButton  .setTooltip ("CLEAR GRID — erase every cell in the manual step grid.");
    commitManualButton .setTooltip ("COMPOSE TO ARR. — commit the manual pattern into the arrangement as a new region so you can mix it with AI-generated regions.");
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
        plusHelper.setText (on ? "MANUAL" : "COMPOSE", juce::dontSendNotification);
        resized();
        repaint();
    };
    clearManualButton.onClick = [this]
    {
        processorRef.clearManualPattern();
        // v1.6.1-rc.20 — repaint BOTH views so CLEAR GRID flushes the
        // piano roll display whether the user is on the step grid or
        // on the piano roll when they hit it.
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

    // v1.6.1-rc.24 — PIANO ROLL + ONE-SHOT toggles removed alongside
    // the FL-style chromatic piano-roll component. Manual editing is
    // the step grid only; chromatic input flows in via the host's
    // own piano roll through the rc.24 host-MIDI capture path.

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

    // v1.4.0 — LOAD KIT FOLDER: point at any folder of WAV samples
    // (kick.wav / snare.wav / hat_*.wav / tom_*.wav / ride.wav / crash.wav / china.wav)
    // and the plugin plays those instead of the physical-model synth.
    styleSmallBtn (loadKitButton);
    loadKitButton.setTooltip ("LOAD KIT — pick a folder of WAV samples. Expected names: "
                              "kick, snare, snare_ghost, hat_closed, hat_pedal, hat_open, "
                              "tom_high, tom_mid, tom_low, ride, ride_bell, crash, china. "
                              "Optional velocity layers: snare_1.wav … snare_8.wav.");
    loadKitButton.onClick = [this]
    {
        kitFolderChooser = std::make_unique<juce::FileChooser> (
            "Select a drum sample folder",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        kitFolderChooser->launchAsync (
            juce::FileBrowserComponent::openMode
              | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto folder = fc.getResult();
                if (folder == juce::File{} || ! folder.isDirectory()) return;
                const int n = processorRef.loadSampleKit (folder);
                if (n > 0)
                    kitPathLabel.setText (juce::String (n) + " samples: "
                                          + folder.getFileName(),
                                          juce::dontSendNotification);
                else
                    kitPathLabel.setText ("No recognised WAVs in folder",
                                          juce::dontSendNotification);
            });
    };
    addAndMakeVisible (loadKitButton);

    kitPathLabel.setJustificationType (juce::Justification::centredLeft);
    kitPathLabel.setColour (juce::Label::textColourId, juce::Colour (Palette::kMuted));
    {
        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::plain));
        f.setExtraKerningFactor (0.15f);
        kitPathLabel.setFont (f);
    }
    if (processorRef.isSampleKitActive())
        kitPathLabel.setText (juce::File (processorRef.getSampleKitPath()).getFileName(),
                              juce::dontSendNotification);
    else
        kitPathLabel.setText ("Physical-model synth", juce::dontSendNotification);
    addAndMakeVisible (kitPathLabel);

    // UI SCALE slider — resizes the entire editor 75% … 150%.
    // v1.6.1-rc.9 — UI scale snaps to 4 fixed stops (25 / 50 / 75 / 100 %).
    // Internal scale factors are 0.6 / 0.85 / 1.10 / 1.35 so the editor
    // never collapses to unreadable nor explodes outside the host window.
    uiScaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    uiScaleSlider.setRange (1.0, 4.0, 1.0);
    {
        // v1.6.1-rc.10 — initial-stop heuristic shifted down a notch so
        // a first-load (scale 0.85) lands on the 75 % stop instead of
        // bouncing between 50 % and 75 %. Boundaries are the midpoints
        // between adjacent stop scales {0.55, 0.70, 0.85, 1.00}:
        // 0.625, 0.775, 0.925 — so cur=0.85 lands exactly on stop 3
        // (75 %) instead of getting bucketed into stop 2 (50 %) by an
        // off-by-one ceiling. Devin Review caught the original
        // 0.725 / 0.975 / 1.225 thresholds bucketing 0.85 → stop 2.
        const float cur = processorRef.getUiScale();
        const double initStop = (cur < 0.625f) ? 1.0
                              : (cur < 0.775f) ? 2.0
                              : (cur < 0.925f) ? 3.0
                                                : 4.0;
        uiScaleSlider.setValue (initStop, juce::dontSendNotification);
    }
    uiScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 16);
    uiScaleSlider.textFromValueFunction = [] (double v)
    {
        const int pct = (int) std::round (v) * 25;
        return juce::String (pct) + "%";
    };
    uiScaleSlider.setTooltip ("UI SCALE — four fixed stops (25 / 50 / 75 / 100 %). "
                              "Snaps so the editor never lands at an awkward in-between size.");
    uiScaleSlider.onValueChange = [this]
    {
        // v1.6.1-rc.10 — stops compressed from {0.60, 0.85, 1.10, 1.35}
        // down to {0.55, 0.70, 0.85, 1.00}. The user said the previous
        // 100 % stop was "crazy big" on first load — at 1.35 the editor
        // ballooned to 1296 × 1242 px which overran most laptop hosts.
        // 1.00 stop now gives a native 960 × 920 — generous but sane.
        const int   stop = juce::jlimit (1, 4, (int) std::round (uiScaleSlider.getValue()));
        const float s    = (stop == 1) ? 0.55f
                          : (stop == 2) ? 0.70f
                          : (stop == 3) ? 0.85f
                                          : 1.00f;
        processorRef.setUiScale (s);
        setTransform (juce::AffineTransform::scale (s));
        setSize ((int) std::round (960.0f * s), (int) std::round (920.0f * s));
    };
    addAndMakeVisible (uiScaleSlider);

    {
        auto f = juce::Font (juce::FontOptions (10.0f, juce::Font::italic));
        f.setExtraKerningFactor (0.35f);
        uiScaleLabel.setFont (f);
    }
    uiScaleLabel.setJustificationType (juce::Justification::centredRight);
    uiScaleLabel.setColour (juce::Label::textColourId, juce::Colour (Palette::kMuted));
    addAndMakeVisible (uiScaleLabel);

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
    fillsSlider     .setTooltip ("FILLS — probability that the next COMPOSE becomes a drum fill instead of a groove.");

    genreBox        .setTooltip ("GENRE — rock, metal, jazz, funk, hip-hop, trap, pop, country… Auto picks one per press.");
    patternLengthBox.setTooltip ("LENGTH — how long each appended region is (1/16 note → 16 bars; default 8 bars).");
    modeBox         .setTooltip ("MODE — Groove (time-keeping pattern) or Fill (transition roll).");
    hiHatBox        .setTooltip ("HI-HAT — Dynamic (genre default), or force Closed / Open / Ride cymbal.");
    drumKitBox      .setTooltip ("DRUM KIT — 20 models from jazz Ludwig to thrash Sonor. Each remaps GM notes + velocity / ghost / accent curves for a distinct timbre in your sampler.");
    halfTimeButton  .setTooltip ("HALF-TIME — snare on beat 3 only (kick on 1). Classic hip-hop / shoegaze feel.");

    plusButton      .setTooltip ("COMPOSE — generate a new region with current settings and add it after the last one.");
    undoButton      .setTooltip ("UNDO — remove the last appended region from the arrangement.");
    clearButton     .setTooltip ("CLEAR — wipe the arrangement and start a fresh single region.");
    dragHandle      .setTooltip ("HIGHLIGHT ALL → DAW — click to highlight every region in the arrangement, then drag onto a DAW track to drop the full multi-region arrangement as one .mid file.");
    dragHandle.onHighlightChange = [this] (bool on)
    {
        arrangementStrip.setHighlightAll (on);
    };
    saveMidiButton  .setTooltip ("SAVE MIDI — export the full arrangement to a .mid file on disk.");
    playButton      .setTooltip ("PLAY — starts the built-in audio engine in Standalone. In a DAW, playback follows host transport.");
    pauseButton     .setTooltip ("PAUSE — freezes playback at the current position.");
    stopButton      .setTooltip ("STOP — stops playback and rewinds to beat 1.");

    manualButton       .setTooltip ("MANUAL — 16-bar click-to-edit grid. Place kicks / snares / toms / hats yourself instead of letting the AI generate.");
    clearManualButton  .setTooltip ("CLEAR GRID — wipe every cell in the manual pattern.");
    commitManualButton .setTooltip ("COMPOSE TO ARR. — commit the current manual 16-bar pattern as a new region in the arrangement (with DRUM KIT remap).");

    // APVTS attachments
    auto& apvts = processorRef.getAPVTS();
    variationAttachment     = std::make_unique<SliderAttachment> (apvts, "variation",     variationSlider);
    complexityAttachment    = std::make_unique<SliderAttachment> (apvts, "complexity",    complexitySlider);
    velocityAttachment      = std::make_unique<SliderAttachment> (apvts, "velocity",      velocitySlider);
    humanizeAttachment      = std::make_unique<SliderAttachment> (apvts, "humanize",      humanizeSlider);
    swingAttachment         = std::make_unique<SliderAttachment> (apvts, "swing",         swingSlider);
    fillsAttachment         = std::make_unique<SliderAttachment> (apvts, "fillsProb",     fillsSlider);
    fillComplexityAttachment = std::make_unique<SliderAttachment>(apvts, "fillComplexity", fillComplexitySlider);
    stepDivAttachment       = std::make_unique<ComboAttachment>  (apvts, "stepDiv",       stepDivBox);
    genreAttachment         = std::make_unique<ComboAttachment>  (apvts, "genre",         genreBox);
    patternLengthAttachment = std::make_unique<ComboAttachment>  (apvts, "patternLength", patternLengthBox);
    modeAttachment          = std::make_unique<ComboAttachment>  (apvts, "mode",          modeBox);
    hiHatAttachment         = std::make_unique<ComboAttachment>  (apvts, "hiHat",         hiHatBox);
    // v1.5.0 — drumKitBox is the bundled-kit selector (5 CC0 kits).
    drumKitAttachment       = std::make_unique<ComboAttachment>  (apvts, "bundledKit",    drumKitBox);
    roomAttachment          = std::make_unique<ComboAttachment>  (apvts, "room",          roomBox);
    roomAmountAttachment    = std::make_unique<SliderAttachment> (apvts, "roomAmount",    roomAmountSlider);
    halfTimeAttachment      = std::make_unique<ButtonAttachment> (apvts, "halfTime",      halfTimeButton);
    trapModeAttachment      = std::make_unique<ButtonAttachment> (apvts, "trapMode",      trapModeButton);
    // v1.6.1-rc.19 — sync the strip with the persisted trapMode value.
    // ButtonAttachment fires our onClick the first time the host pushes
    // a saved value; we still call setTrapMode here so the very first
    // paint after a reload shows the correct labels even before the
    // user toggles the button.
    arrangementStrip.setTrapMode (trapModeButton.getToggleState());
    timeScaleAttachment     = std::make_unique<ComboAttachment>   (apvts, "timeScale",     timeScaleBox);
    intensityAttachment     = std::make_unique<SliderAttachment>  (apvts, "intensity",     intensitySlider);
    fillDensityAttachment   = std::make_unique<SliderAttachment>  (apvts, "fillDensity",   fillDensitySlider);

    // v1.6.1-rc.7 — chain custom onChange handlers AFTER the APVTS
    // ComboBoxAttachment ctors. Each ctor steals onChange to sync its
    // parameter, so we capture the attachment-installed lambda and
    // assign a new lambda that calls the attachment first (so the
    // APVTS param updates) and our UI side-effect second. Without
    // this, HALF/NORMAL/DOUBLE were visually responsive but the
    // timeScale param never moved (playback speed never changed),
    // and similarly drumKit / stepDiv changes never refreshed the
    // visualizer or manual grid.
    {
        auto attached = std::move (stepDivBox.onChange);
        stepDivBox.onChange = [this, attached = std::move (attached)]
        {
            if (attached) attached();
            const int idx = stepDivBox.getSelectedItemIndex();
            const int spb = (idx == 2 ? 64 : idx == 1 ? 32 : 16);
            manualGrid.setStepsPerBar (spb);
            arrangementStrip.setStepsPerBar (spb);
            // v1.6.1-rc.20 — keep the piano roll's grid + snap resolution
            // (rc.24 — piano-roll mirror call removed.)
        };
    }
    {
        auto attached = std::move (drumKitBox.onChange);
        drumKitBox.onChange = [this, attached = std::move (attached)]
        {
            if (attached) attached();
            const int kitIdx = drumKitBox.getSelectedItemIndex();
            kitVisualizer.setSelectedKit (kitIdx);
            // v1.6.1-rc.12 — single bundled kit; setActiveBundledKit is a
            // no-op but kept so legacy onChange chains still build.
            kitVisualizer.setActiveBundledKit (kitIdx);
            rebuildStarterBox();
            processorRef.remapLastRegionToKit (kitIdx);
            arrangementStrip.repaint();
        };
    }
    {
        auto attached = std::move (timeScaleBox.onChange);
        timeScaleBox.onChange = [this, attached = std::move (attached)]
        {
            if (attached) attached();
            const int id = timeScaleBox.getSelectedId();
            halfButton  .setToggleState (id == 1, juce::dontSendNotification);
            normalButton.setToggleState (id == 2, juce::dontSendNotification);
            doubleButton.setToggleState (id == 3, juce::dontSendNotification);
        };
    }

    startTimerHz (30);
}

AIDrumAudioProcessorEditor::~AIDrumAudioProcessorEditor()
{
    stopTimer();
    playButton .setLookAndFeel (nullptr);
    pauseButton.setLookAndFeel (nullptr);
    stopButton .setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AIDrumAudioProcessorEditor::timerCallback()
{
    plusButton.tickGlow();
    if (manualGrid.isVisible())
        manualGrid.repaint();
    // v1.6.1-rc.20 — same playhead/pattern refresh tick for the piano
    // (rc.24 — piano-roll repaint removed; the step grid is the only
    // in-plugin manual editor.)

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

    // v1.6.1-rc.7 — black → graphite → bone sunburst gradient, left to
    // right. Three colour stops (kSunLeft / kSunMid / kSunRight) so the
    // mid grey sits at ~62% across, leaving a longer dark left side and
    // a softer bone glow on the right (matches a sunset-from-the-east
    // shading). Background is filled with a horizontal linear gradient
    // first, then a subtle radial vignette in the bottom-left corner
    // adds depth without polluting the sunburst.
    juce::ColourGradient sun (juce::Colour (Palette::kSunLeft),
                              b.getX(), b.getCentreY(),
                              juce::Colour (Palette::kSunRight),
                              b.getRight(), b.getCentreY(), false);
    sun.addColour (0.62, juce::Colour (Palette::kSunMid));
    g.setGradientFill (sun);
    g.fillRect (b);

    // Soft vignette anchored bottom-left so the title area on the left
    // stays inky and the controls on the right glow.
    juce::ColourGradient vignette (juce::Colour (Palette::kSunLeft).withAlpha (0.55f),
                                   b.getX(), b.getBottom(),
                                   juce::Colours::transparentBlack,
                                   b.getCentreX(), b.getCentreY(), true);
    g.setGradientFill (vignette);
    g.fillRect (b);

    // Thin horizontal rule under the title (silver gradient).
    const float ruleY = 92.0f;
    juce::ColourGradient rule (juce::Colours::transparentBlack, b.getX(), ruleY,
                               juce::Colours::transparentBlack, b.getRight(), ruleY, false);
    rule.addColour (0.5, juce::Colour (Palette::kSilver).withAlpha (0.85f));
    g.setGradientFill (rule);
    g.fillRect (juce::Rectangle<float> (b.getX() + 40.0f, ruleY, b.getWidth() - 80.0f, 1.0f));

    g.setColour (juce::Colour (Palette::kBone));
    g.fillEllipse (b.getCentreX() - 2.5f, ruleY - 2.5f, 5.0f, 5.0f);

    // v1.6.1-rc.7 — bundled HumHouse crest. Painted centred above the
    // rule when present. Aspect-locked, ~78px tall so it sits flush in
    // the existing masthead area without crowding the subtitle.
    if (logoImage.isValid())
    {
        const float maxH    = 76.0f;
        const float aspect  = (float) logoImage.getWidth() / juce::jmax (1, logoImage.getHeight());
        const float drawH   = maxH;
        const float drawW   = drawH * aspect;
        const float topY    = 6.0f;
        juce::Rectangle<float> dest (b.getCentreX() - drawW * 0.5f, topY, drawW, drawH);
        g.setOpacity (1.0f);
        g.drawImage (logoImage, dest, juce::RectanglePlacement::centred);
    }
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
    // v1.6.1-rc.14 — modeBox is hidden (fills are now generative-only,
    // no Auto/Manual toggle). Skipping placeCombo so it doesn't reserve
    // a 56 px gap between LENGTH and HI-HAT.
    placeCombo (hiHatBox);
    placeCombo (stepDivBox);
    placeCombo (timeScaleBox);
    halfTimeButton.setBounds (rightCombos.removeFromTop (28).reduced (0, 2));
    // v1.6.1-rc.19 — TRAP MODE toggle sits directly under HALF-TIME.
    trapModeButton.setBounds (rightCombos.removeFromTop (28).reduced (0, 2));

    area.removeFromTop (8);

    // ----- Knobs row: Variation | Humanize | Swing | Fills | Intensity | Fill Density | Room Amount -----
    // v1.6.1-rc.18 — FILLS knob hidden. The user removed the FILL
    // button entirely ("ALL PATTERNS SHALL HAVE FILL BUILT IN NO MORE
    // FILL BUTTON, IT IS RUINING THE ARRANGMENT"). Fills are now
    // exclusively embedded inside grooves via
    // spliceMandatoryFillIntoRegion(); the standalone fillsProb knob
    // and fill selector dropdown have no role to play. We keep the
    // APVTS parameter alive so saved sessions still load — we just
    // never expose it in the UI.
    auto knobsRow = area.removeFromTop (108).reduced (4, 6);
    const int knobW = knobsRow.getWidth() / 6;
    auto placeKnob = [&knobsRow, knobW] (juce::Slider& s)
    {
        auto cell = knobsRow.removeFromLeft (knobW).reduced (6, 10);
        s.setBounds (cell);
    };
    placeKnob (variationSlider);
    placeKnob (humanizeSlider);
    placeKnob (swingSlider);
    placeKnob (intensitySlider);
    placeKnob (fillDensitySlider);
    placeKnob (roomAmountSlider);

    fillsSlider.setBounds (0, 0, 0, 0);
    fillsLabel .setBounds (0, 0, 0, 0);

    // Hidden controls still need bounds so attachment writes don't
    // touch unrealised peers.
    fillComplexitySlider.setBounds (0, 0, 0, 0);
    fillComplexityLabel .setBounds (0, 0, 0, 0);

    area.removeFromTop (8);

    // Action row: transport, edit actions, export, with the legacy append
    // button kept small because the primary + now lives on the arrangement strip.
    auto action = area.removeFromTop (92);

    auto transportCluster = action.removeFromLeft (260).reduced (4, 16);
    const int transportW = (transportCluster.getWidth() - 12) / 3;
    playButton .setBounds (transportCluster.removeFromLeft (transportW));
    transportCluster.removeFromLeft (6);
    pauseButton.setBounds (transportCluster.removeFromLeft (transportW));
    transportCluster.removeFromLeft (6);
    stopButton .setBounds (transportCluster.removeFromLeft (transportW));

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

    // v1.6.1-rc.9 — RANDOMIZE sits to the right of COMPOSE, vertically
    // aligned with the (+) pad. Same height, narrower width.
    const int rndW = 96, rndH = 26;
    randomizeButton.setBounds (plusRect.getRight() + 14,
                               plusRect.getCentreY() - rndH / 2,
                               rndW, rndH);

    area.removeFromTop (6);

    // v1.6.0 — STARTER GROOVES dropdown + COPY / PASTE region buttons.
    auto starterBar = area.removeFromTop (30);
    starterLabel     .setBounds (starterBar.removeFromLeft (70).reduced (2));
    starterBox       .setBounds (starterBar.removeFromLeft (260).reduced (2));
    starterBar.removeFromLeft (10);
    copyRegionButton .setBounds (starterBar.removeFromLeft (80).reduced (2));
    starterBar.removeFromLeft (4);
    pasteRegionButton.setBounds (starterBar.removeFromLeft (80).reduced (2));

    area.removeFromTop (4);

    // MANUAL / MIXER / LOAD KIT / UI SCALE toggle bar.
    auto manualBar = area.removeFromTop (30);
    manualButton   .setBounds (manualBar.removeFromLeft (120).reduced (2));
    manualBar.removeFromLeft (4);
    // v1.6.1-rc.24 — PIANO ROLL + ONE-SHOT bounds removed.
    mixerButton    .setBounds (manualBar.removeFromLeft (90) .reduced (2));
    manualBar.removeFromLeft (4);
    loadKitButton  .setBounds (manualBar.removeFromLeft (90) .reduced (2));
    manualBar.removeFromLeft (6);

    // UI scale cluster on the right side of the bar.
    auto scaleCluster = manualBar.removeFromRight (200);
    uiScaleLabel .setBounds (scaleCluster.removeFromLeft (60));
    uiScaleSlider.setBounds (scaleCluster.reduced (2, 2));

    // Kit path readout fills whatever horizontal space is left in the middle.
    kitPathLabel.setBounds (manualBar.reduced (4, 0));

    area.removeFromTop (4);

    // v1.6.1-rc.21 — FILL dropdown restored on the bar above the
    // arrangement grid. Title sits left-of-dropdown, dropdown takes ~220 px,
    // GHOST stays anchored on the right. Auto-fills still splice on bar 8
    // of every 8-bar block; the dropdown picks the seed archetype.
    auto rc7Bar = area.removeFromTop (32);
    {
        fillSelectorTitle.setBounds (rc7Bar.removeFromLeft (54).reduced (2));
        fillSelectorBox  .setBounds (rc7Bar.removeFromLeft (220).reduced (2));

        // v1.6.1-rc.9 — HALF / NORMAL / DOUBLE transport buttons removed
        // (the playback-rate switch was triggering the wrong-kit bug).
        halfButton  .setBounds ({});
        normalButton.setBounds ({});
        doubleButton.setBounds ({});

        ghostButton.setBounds (rc7Bar.removeFromRight (96).reduced (2));
    }

    area.removeFromTop (4);

    // Arrangement strip / manual grid share the remaining area.
    arrangementStrip.setBounds (area);
    manualGrid      .setBounds (area);
    mixerPanel      .setBounds (area);
}

// v1.6.1-rc.3 — kit-filtered STARTER dropdown. Rebuilt whenever the kit
// selection changes so the combo only shows the grooves that belong to
// the active kit's bucket.
void AIDrumAudioProcessorEditor::rebuildStarterBox()
{
    starterBox.clear (juce::dontSendNotification);
    starterBox.addItem ("STARTER GROOVE ...", 1);
    const int kit = drumKitBox.getSelectedItemIndex();
    const int n = processorRef.starterGrooveCountForKit (kit);
    for (int i = 0; i < n; ++i)
        starterBox.addItem (processorRef.starterGrooveNameForKit (kit, i), i + 2);
    starterBox.setSelectedId (1, juce::dontSendNotification);
}
