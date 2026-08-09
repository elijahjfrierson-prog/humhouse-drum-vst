#include "DrumsXEditor.h"

namespace hhx
{
    namespace
    {
        constexpr int kBaseWidth  = 1040;
        constexpr int kBaseHeight = 660;

        juce::Font uiFont (float size, bool bold = false)
        {
            return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        }

        void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title = {})
        {
            g.setColour (DrumsXLookAndFeel::panel());
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            g.setColour (DrumsXLookAndFeel::line());
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);

            if (title.isNotEmpty())
            {
                g.setColour (DrumsXLookAndFeel::textDim());
                g.setFont (uiFont (11.0f, true));
                g.drawText (title.toUpperCase(), r.reduced (12, 8).removeFromTop (14),
                            juce::Justification::topLeft, false);
            }
        }
    }

    //==============================================================================
    LabelledKnob::LabelledKnob (juce::AudioProcessorValueTreeState& state,
                                const juce::String& paramID,
                                const juce::String& caption,
                                bool bipolar)
        : captionText (caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setDoubleClickReturnValue (true, state.getParameter (paramID) != nullptr
                                                ? state.getParameter (paramID)->getDefaultValue() : 0.5);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
    }

    void LabelledKnob::resized()
    {
        auto r = getLocalBounds();
        r.removeFromTop (14);
        r.removeFromBottom (14);
        slider.setBounds (r);
    }

    void LabelledKnob::paint (juce::Graphics& g)
    {
        g.setColour (DrumsXLookAndFeel::textDim());
        g.setFont (uiFont (11.0f, true));
        g.drawText (captionText.toUpperCase(), getLocalBounds().removeFromTop (13),
                    juce::Justification::centred, false);

        g.setColour (DrumsXLookAndFeel::text());
        g.setFont (uiFont (12.0f));
        g.drawText (slider.getTextFromValue (slider.getValue()),
                    getLocalBounds().removeFromBottom (13), juce::Justification::centred, false);
    }

    //==============================================================================
    PerformancePad::PerformancePad (DrumsXProcessor& p) : proc (p)
    {
        startTimerHz (20);
    }

    void PerformancePad::timerCallback()
    {
        const auto& state = proc.getAPVTS();
        const float x = state.getRawParameterValue (pid::complexity)->load();
        const float y = state.getRawParameterValue (pid::intensity)->load();
        if (! juce::approximatelyEqual (x, lastX) || ! juce::approximatelyEqual (y, lastY))
        {
            lastX = x;
            lastY = y;
            repaint();
        }
    }

    void PerformancePad::moveTo (juce::Point<float> p, bool)
    {
        const auto r = getLocalBounds().toFloat().reduced (10.0f);
        const float x = juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
        const float y = juce::jlimit (0.0f, 1.0f, 1.0f - (p.y - r.getY()) / juce::jmax (1.0f, r.getHeight()));

        if (auto* cp = proc.getAPVTS().getParameter (pid::complexity))
            cp->setValueNotifyingHost (x);
        if (auto* ip = proc.getAPVTS().getParameter (pid::intensity))
            ip->setValueNotifyingHost (y);
        repaint();
    }

    void PerformancePad::mouseDown (const juce::MouseEvent& e)
    {
        dragging = true;
        if (auto* cp = proc.getAPVTS().getParameter (pid::complexity)) cp->beginChangeGesture();
        if (auto* ip = proc.getAPVTS().getParameter (pid::intensity))  ip->beginChangeGesture();
        moveTo (e.position, true);
    }

    void PerformancePad::mouseDrag (const juce::MouseEvent& e)
    {
        moveTo (e.position, true);
    }

    void PerformancePad::mouseUp (const juce::MouseEvent&)
    {
        dragging = false;
        if (auto* cp = proc.getAPVTS().getParameter (pid::complexity)) cp->endChangeGesture();
        if (auto* ip = proc.getAPVTS().getParameter (pid::intensity))  ip->endChangeGesture();
    }

    void PerformancePad::paint (juce::Graphics& g)
    {
        const auto full = getLocalBounds().toFloat();
        g.setColour (DrumsXLookAndFeel::panel());
        g.fillRoundedRectangle (full, 6.0f);
        g.setColour (DrumsXLookAndFeel::line());
        g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);

        const auto r = full.reduced (10.0f);

        // Preset dot grid — every dot is a place where the corpus actually has
        // takes, so the pad never lands somewhere with nothing behind it.
        for (int ix = 0; ix < 9; ++ix)
        {
            for (int iy = 0; iy < 9; ++iy)
            {
                const float px = r.getX() + r.getWidth()  * (float) ix / 8.0f;
                const float py = r.getBottom() - r.getHeight() * (float) iy / 8.0f;
                const bool axis = (ix % 4 == 0) || (iy % 4 == 0);
                g.setColour (axis ? DrumsXLookAndFeel::line().brighter (0.25f)
                                  : DrumsXLookAndFeel::line());
                g.fillEllipse (px - 1.4f, py - 1.4f, 2.8f, 2.8f);
            }
        }

        // Landing zone: the real takes the engine would actually reach for from
        // here, so the pad never promises a position the corpus cannot play.
        for (const auto index : proc.getLandingZone (12))
        {
            if (index < 0 || index >= proc.getCorpus().numBeats())
                continue;

            const auto& phrase = proc.getCorpus().beat (index);
            const float px = r.getX() + juce::jlimit (0.0f, 1.0f, phrase.complexity) * r.getWidth();
            const float py = r.getBottom() - juce::jlimit (0.0f, 1.0f, phrase.intensity) * r.getHeight();
            g.setColour (DrumsXLookAndFeel::accent().withAlpha (0.45f));
            g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ px, py }), 1.2f);
        }

        const float x = proc.getAPVTS().getRawParameterValue (pid::complexity)->load();
        const float y = proc.getAPVTS().getRawParameterValue (pid::intensity)->load();
        const juce::Point<float> puck { r.getX() + x * r.getWidth(),
                                        r.getBottom() - y * r.getHeight() };

        g.setColour (DrumsXLookAndFeel::accent().withAlpha (0.18f));
        g.drawLine (r.getX(), puck.y, r.getRight(), puck.y, 1.0f);
        g.drawLine (puck.x, r.getY(), puck.x, r.getBottom(), 1.0f);

        g.setColour (DrumsXLookAndFeel::accent().withAlpha (dragging ? 0.35f : 0.22f));
        g.fillEllipse (juce::Rectangle<float> (44.0f, 44.0f).withCentre (puck));
        g.setColour (DrumsXLookAndFeel::accent());
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (puck));

        g.setColour (DrumsXLookAndFeel::textDim());
        g.setFont (uiFont (10.5f, true));
        g.drawText ("LOUD",   full.reduced (8.0f).removeFromTop (14).toNearestInt(), juce::Justification::centred);
        g.drawText ("SOFT",   full.reduced (8.0f).removeFromBottom (14).toNearestInt(), juce::Justification::centred);
        g.drawText ("SIMPLE", full.reduced (8.0f).toNearestInt(), juce::Justification::centredLeft);
        g.drawText ("COMPLEX", full.reduced (8.0f).toNearestInt(), juce::Justification::centredRight);
    }

    //==============================================================================
    PhraseView::PhraseView (DrumsXProcessor& p) : proc (p)
    {
        startTimerHz (30);
    }

    void PhraseView::timerCallback()
    {
        bool needsRepaint = proc.isPlaying();

        if (auto tl = proc.getTimeline())
        {
            if (tl->hash != lastHash)
            {
                lastHash = tl->hash;
                needsRepaint = true;
            }
        }

        if (needsRepaint)
            repaint();
    }

    void PhraseView::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds();
        drawPanel (g, r);

        auto tl = proc.getTimeline();
        if (tl == nullptr || tl->hits.empty())
            return;

        const int   viewBars     = 4;
        const float beatsPerBar  = tl->beatsPerBar;
        const double playBeat    = proc.getPlayheadBeats();
        const int    firstBar    = (int) std::floor (playBeat / beatsPerBar / viewBars) * viewBars;
        const float  windowStart = (float) firstBar * beatsPerBar;
        const float  windowBeats = beatsPerBar * viewBars;

        const auto area = r.reduced (10, 8);
        const float laneH = (float) area.getHeight() / (float) NumLanes;

        // Bar lines and beat ticks.
        for (int b = 0; b <= viewBars * (int) std::round (beatsPerBar); ++b)
        {
            const float t = (float) b / (float) std::max (1, (int) std::round (windowBeats));
            const float px = area.getX() + t * area.getWidth();
            const bool barLine = (b % std::max (1, (int) std::round (beatsPerBar))) == 0;
            g.setColour (barLine ? DrumsXLookAndFeel::line().brighter (0.3f)
                                 : DrumsXLookAndFeel::line().withAlpha (0.5f));
            g.drawVerticalLine ((int) px, (float) area.getY(), (float) area.getBottom());
        }

        for (const auto& h : tl->hits)
        {
            const float local = h.beat - windowStart;
            if (local < 0.0f || local >= windowBeats)
                continue;
            const float px = area.getX() + (local / windowBeats) * area.getWidth();
            const float py = area.getY() + laneH * (float) h.lane;
            const float a  = 0.28f + 0.72f * ((float) h.velocity / 127.0f);
            g.setColour (DrumsXLookAndFeel::accent().withAlpha (a));
            g.fillRoundedRectangle (px - 1.5f, py + 1.0f, 3.0f, juce::jmax (2.0f, laneH - 2.0f), 1.5f);
        }

        const float playX = area.getX()
                          + (float) ((playBeat - windowStart) / windowBeats) * area.getWidth();
        if (playX >= area.getX() && playX <= area.getRight())
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawVerticalLine ((int) playX, (float) area.getY(), (float) area.getBottom());
        }
    }

    //==============================================================================
    ManualPatternGrid::ManualPatternGrid (DrumsXProcessor& p) : proc (p) {}

    bool ManualPatternGrid::cellAt (juce::Point<int> p, int& lane, int& step) const
    {
        const auto area = getLocalBounds().reduced (76, 4);
        if (! area.contains (p))
            return false;
        const float cellW = (float) area.getWidth() / (float) DrumsXProcessor::kManualSteps;
        const float cellH = (float) area.getHeight() / (float) NumLanes;
        step = juce::jlimit (0, DrumsXProcessor::kManualSteps - 1,
                             (int) ((float) (p.x - area.getX()) / cellW));
        lane = juce::jlimit (0, NumLanes - 1, (int) ((float) (p.y - area.getY()) / cellH));
        return true;
    }

    void ManualPatternGrid::mouseDown (const juce::MouseEvent& e)
    {
        int lane = 0, step = 0;
        if (! cellAt (e.getPosition(), lane, step))
            return;
        const bool erase = e.mods.isRightButtonDown() || proc.getManualStep (lane, step) > 0.0f;
        proc.setManualStep (lane, step, erase ? 0.0f : 0.8f);
        repaint();
    }

    void ManualPatternGrid::mouseDrag (const juce::MouseEvent& e)
    {
        int lane = 0, step = 0;
        if (! cellAt (e.getPosition(), lane, step))
            return;
        if (e.mods.isRightButtonDown())
        {
            proc.setManualStep (lane, step, 0.0f);
        }
        else if (proc.getManualStep (lane, step) > 0.0f)
        {
            // Vertical drag on a placed step sets its velocity.
            const float v = juce::jlimit (0.1f, 1.0f, 0.8f - (float) e.getDistanceFromDragStartY() / 120.0f);
            proc.setManualStep (lane, step, v);
        }
        else
        {
            proc.setManualStep (lane, step, 0.8f);
        }
        repaint();
    }

    void ManualPatternGrid::paint (juce::Graphics& g)
    {
        drawPanel (g, getLocalBounds());
        const auto area = getLocalBounds().reduced (76, 4);
        const float cellW = (float) area.getWidth() / (float) DrumsXProcessor::kManualSteps;
        const float cellH = (float) area.getHeight() / (float) NumLanes;
        const bool  live  = proc.isManualMode();

        for (int lane = 0; lane < NumLanes; ++lane)
        {
            const float y = area.getY() + cellH * (float) lane;
            g.setColour (live ? DrumsXLookAndFeel::text() : DrumsXLookAndFeel::textDim());
            g.setFont (uiFont (10.0f));
            g.drawText (drumsXLaneName (lane),
                        juce::Rectangle<float> ((float) getLocalBounds().getX() + 6.0f, y,
                                                68.0f, cellH).toNearestInt(),
                        juce::Justification::centredLeft, false);

            for (int step = 0; step < DrumsXProcessor::kManualSteps; ++step)
            {
                const juce::Rectangle<float> cell (area.getX() + cellW * (float) step + 1.0f,
                                                   y + 1.0f, cellW - 2.0f, cellH - 2.0f);
                const float v = proc.getManualStep (lane, step);
                const bool  beat = (step % 4) == 0;
                g.setColour (v > 0.0f ? DrumsXLookAndFeel::accent().withAlpha (live ? 0.35f + 0.65f * v : 0.25f)
                                      : (beat ? DrumsXLookAndFeel::panelHi()
                                              : DrumsXLookAndFeel::panelHi().darker (0.35f)));
                g.fillRoundedRectangle (cell, 2.0f);
            }
        }

        // Bar divider.
        const float mid = area.getX() + area.getWidth() * 0.5f;
        g.setColour (DrumsXLookAndFeel::line().brighter (0.3f));
        g.drawVerticalLine ((int) mid, (float) area.getY(), (float) area.getBottom());
    }

    //==============================================================================
    namespace
    {
        class CharacterListModel : public juce::ListBoxModel
        {
        public:
            explicit CharacterListModel (DrumsXProcessor& p) : proc (p) {}

            int getNumRows() override { return (int) characters().size(); }

            void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
            {
                if (row < 0 || row >= getNumRows())
                    return;
                const juce::Rectangle<float> r (2.0f, 1.0f, (float) width - 4.0f, (float) height - 2.0f);
                if (selected)
                {
                    g.setColour (DrumsXLookAndFeel::accent().withAlpha (0.18f));
                    g.fillRoundedRectangle (r, 4.0f);
                    g.setColour (DrumsXLookAndFeel::accent());
                    g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
                }
                g.setColour (selected ? DrumsXLookAndFeel::text() : DrumsXLookAndFeel::textDim());
                g.setFont (uiFont (13.0f, selected));
                g.drawText (characters()[(std::size_t) row].name, r.reduced (12.0f, 0.0f),
                            juce::Justification::centredLeft, true);
            }

            void selectedRowsChanged (int row) override
            {
                if (row < 0)
                    return;
                if (auto* p = proc.getAPVTS().getParameter (pid::preset))
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) row));
                proc.applyCharacter (row);
            }

        private:
            DrumsXProcessor& proc;
        };
    }

    //==============================================================================
    DrumsXEditor::DrumsXEditor (DrumsXProcessor& p)
        : AudioProcessorEditor (&p), proc (p),
          pad (p), phraseView (p), manualGrid (p)
    {
        setLookAndFeel (&lnf);
        auto& state = proc.getAPVTS();

        for (auto* tab : { &mainTab, &detailsTab, &kitTab })
        {
            tab->setClickingTogglesState (false);
            addAndMakeVisible (*tab);
        }
        mainTab.onClick    = [this] { setPage (Page::main); };
        detailsTab.onClick = [this] { setPage (Page::details); };
        kitTab.onClick     = [this] { setPage (Page::kit); };

        addAndMakeVisible (playButton);
        playButton.onClick = [this]
        {
            if (proc.isPlaying()) proc.stop(); else proc.play();
            playButton.setToggleState (proc.isPlaying(), juce::dontSendNotification);
            playButton.setButtonText (proc.isPlaying() ? "STOP" : "PLAY");
        };

        addAndMakeVisible (regenButton);
        regenButton.onClick = [this] { proc.regenerate(); };

        addAndMakeVisible (exportButton);
        exportButton.onClick = [this] { exportMenu(); };

        // Kit-piece lane strip: each group can be dropped out of the performance
        // or pushed down to ghost notes without touching the mix.
        {
            struct GroupSpec { const char* name; int first, last; };
            const GroupSpec specs[]
            {
                { "KICK",   LaneKick,      LaneKick },
                { "SNARE",  LaneSnare,     LaneSnareRoll },
                { "HATS",   LaneHatClosed, LaneHatBell },
                { "RIDE",   LaneRideBow,   LaneRideCrash },
                { "CRASH",  LaneCrashL,    LaneSplash },
                { "TOMS",   LaneTom1,      LaneTom4 },
                { "PERC",   LanePerc,      LanePerc }
            };

            for (const auto& spec : specs)
            {
                LaneGroup group;
                for (int lane = spec.first; lane <= spec.last; ++lane)
                    group.lanes.push_back (lane);

                const auto lanes = group.lanes;
                const auto flip = [this, lanes] (bool ghost)
                {
                    auto& apvts = proc.getAPVTS();
                    const auto id = [ghost] (int lane)
                    { return ghost ? pid::laneGhost (lane) : pid::laneEnable (lane); };

                    bool on = false;
                    if (const auto* raw = apvts.getRawParameterValue (id (lanes.front())))
                        on = raw->load() > 0.5f;

                    for (const auto lane : lanes)
                        if (auto* param = apvts.getParameter (id (lane)))
                            param->setValueNotifyingHost (on ? 0.0f : 1.0f);
                };

                group.in = std::make_unique<juce::TextButton> (spec.name);
                group.in->setClickingTogglesState (false);
                group.in->onClick = [flip] { flip (false); };
                addAndMakeVisible (*group.in);

                group.ghost = std::make_unique<juce::TextButton> ("G");
                group.ghost->setClickingTogglesState (false);
                group.ghost->onClick = [flip] { flip (true); };
                addAndMakeVisible (*group.ghost);

                laneGroups.push_back (std::move (group));
            }
        }

        addAndMakeVisible (scaleBox);
        scaleBox.addItemList ({ "75%", "85%", "100%", "115%", "130%", "150%" }, 1);
        {
            const float scales[] { 0.75f, 0.85f, 1.0f, 1.15f, 1.3f, 1.5f };
            int best = 2;
            for (int i = 0; i < 6; ++i)
                if (std::abs (scales[i] - proc.getUiScale()) < std::abs (scales[best] - proc.getUiScale()))
                    best = i;
            scaleBox.setSelectedId (best + 1, juce::dontSendNotification);
        }
        scaleBox.onChange = [this]
        {
            static const float scales[] = { 0.75f, 0.85f, 1.0f, 1.15f, 1.30f, 1.50f };
            const int idx = juce::jlimit (0, 5, scaleBox.getSelectedId() - 1);
            proc.setUiScale (scales[idx]);
            applyScale();
        };

        // ---- MAIN ---------------------------------------------------------
        characterModel = std::make_unique<CharacterListModel> (proc);
        characterList.setModel (characterModel.get());
        characterList.setRowHeight (34);
        characterList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        characterList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        characterList.selectRow ((int) state.getRawParameterValue (pid::preset)->load(),
                                 true, true);
        addAndMakeVisible (characterList);
        addAndMakeVisible (pad);
        addAndMakeVisible (phraseView);

        complexityKnob = std::make_unique<LabelledKnob> (state, pid::complexity, "Complexity");
        intensityKnob  = std::make_unique<LabelledKnob> (state, pid::intensity,  "Intensity");
        fillsKnob      = std::make_unique<LabelledKnob> (state, pid::fillAmount, "Fills");
        swingKnob      = std::make_unique<LabelledKnob> (state, pid::swing,      "Swing");
        for (auto* k : { complexityKnob.get(), intensityKnob.get(), fillsKnob.get(), swingKnob.get() })
            addAndMakeVisible (*k);

        // Variation buttons: two rows of four, kick/snare on top, cymbals below.
        for (int group = 0; group < 2; ++group)
        {
            for (int i = 0; i < 4; ++i)
            {
                auto b = std::make_unique<juce::TextButton> (juce::String (i + 1));
                b->setClickingTogglesState (false);
                const auto id = group == 0 ? juce::String (pid::variationRhythm)
                                           : juce::String (pid::variationCymbal);
                auto* raw = b.get();
                b->onClick = [this, id, i, raw]
                {
                    if (auto* param = proc.getAPVTS().getParameter (id))
                        param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
                    juce::ignoreUnused (raw);
                };
                addAndMakeVisible (*b);
                variationButtons.push_back (std::move (b));
            }
        }

        // ---- DETAILS ------------------------------------------------------
        const std::pair<const char*, const char*> detailSpecs[] = {
            { pid::feel,           "Feel" },
            { pid::ghost,          "Ghost Notes" },
            { pid::hatOpenness,    "Hat Openness" },
            { pid::humanize,       "Humanize" },
            { pid::fillComplexity, "Fill Complexity" },
        };
        for (const auto& spec : detailSpecs)
        {
            auto k = std::make_unique<LabelledKnob> (state, spec.first, spec.second,
                                                     juce::String (spec.first) == pid::feel);
            addAndMakeVisible (*k);
            detailKnobs.push_back (std::move (k));
        }

        const auto addCombo = [&] (juce::ComboBox& box, const char* paramID)
        {
            addAndMakeVisible (box);
            comboAttachments.push_back (
                std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramID, box));
        };
        fillBarsBox.addItemList ({ "1 Bar", "2 Bars" }, 1);
        phraseBarsBox.addItemList ({ "1 Bar", "2 Bars", "4 Bars" }, 1);
        swingGridBox.addItemList ({ "1/8", "1/16" }, 1);
        timeSigDenBox.addItemList ({ "2", "4", "8", "16" }, 1);
        tempoModeBox.addItemList ({ "Follow Host", "Manual" }, 1);
        addCombo (fillBarsBox,   pid::fillBars);
        addCombo (phraseBarsBox, pid::phraseBars);
        addCombo (swingGridBox,  pid::swingGrid);
        addCombo (timeSigDenBox, pid::timeSigDen);
        addCombo (tempoModeBox,  pid::tempoMode);

        timeSigNumBox.addItemList ({ "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" }, 1);
        timeSigNumBox.setSelectedId ((int) state.getRawParameterValue (pid::timeSigNum)->load() - 1,
                                     juce::dontSendNotification);
        timeSigNumBox.onChange = [this]
        {
            if (auto* param = proc.getAPVTS().getParameter (pid::timeSigNum))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) (timeSigNumBox.getSelectedId() + 1)));
        };
        addAndMakeVisible (timeSigNumBox);

        bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
        addAndMakeVisible (bpmSlider);
        sliderAttachments.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, pid::manualBpm, bpmSlider));

        for (auto* t : { &rideToggle, &halfTimeToggle, &manualToggle })
            addAndMakeVisible (*t);
        buttonAttachments.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pid::rideMode, rideToggle));
        buttonAttachments.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pid::halfTime, halfTimeToggle));
        buttonAttachments.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pid::manualMode, manualToggle));

        addAndMakeVisible (clearManualButton);
        clearManualButton.onClick = [this] { proc.clearManual(); manualGrid.repaint(); };
        addAndMakeVisible (manualGrid);

        // ---- KIT ----------------------------------------------------------
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            auto& row = kitRows[(std::size_t) lane];

            row.name = std::make_unique<juce::Label>();
            row.name->setText (drumsXLaneName (lane), juce::dontSendNotification);
            row.name->setFont (uiFont (12.5f));
            addAndMakeVisible (*row.name);

            row.enable = std::make_unique<juce::ToggleButton> ("");
            addAndMakeVisible (*row.enable);
            row.enableAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, pid::laneEnable (lane), *row.enable);

            // Sample switch: steps through the lane's sample set, the control
            // the previous version never had.
            const auto bump = [this, lane] (int delta)
            {
                if (auto* param = proc.getAPVTS().getParameter (pid::laneSwitch (lane)))
                {
                    const auto* raw = proc.getAPVTS().getRawParameterValue (pid::laneSwitch (lane));
                    const float next = juce::jlimit (-4.0f, 4.0f, raw->load() + (float) delta);
                    param->setValueNotifyingHost (param->convertTo0to1 (next));
                }
            };
            row.prev = std::make_unique<juce::TextButton> ("<");
            row.next = std::make_unique<juce::TextButton> (">");
            row.prev->onClick = [bump] { bump (-1); };
            row.next->onClick = [bump] { bump (1); };
            addAndMakeVisible (*row.prev);
            addAndMakeVisible (*row.next);

            row.layers = std::make_unique<juce::Label>();
            row.layers->setFont (uiFont (11.0f));
            row.layers->setColour (juce::Label::textColourId, DrumsXLookAndFeel::textDim());
            row.layers->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*row.layers);

            // Every mixer control is an automatable parameter, so the mix is
            // saved with the project rather than living only in the UI.
            const auto mixerSlider = [this, &state] (std::unique_ptr<juce::Slider>& slider,
                                                     const juce::String& paramID,
                                                     juce::Slider::SliderStyle style)
            {
                slider = std::make_unique<juce::Slider> (style, juce::Slider::NoTextBox);
                addAndMakeVisible (*slider);
                sliderAttachments.push_back (
                    std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        state, paramID, *slider));
            };

            mixerSlider (row.gain, pid::laneGain (lane), juce::Slider::LinearHorizontal);
            mixerSlider (row.pan,  pid::lanePan (lane),  juce::Slider::LinearHorizontal);
            mixerSlider (row.tune, pid::laneTune (lane), juce::Slider::RotaryHorizontalVerticalDrag);
            mixerSlider (row.damp, pid::laneDamp (lane), juce::Slider::RotaryHorizontalVerticalDrag);
        }

        addAndMakeVisible (loadKitButton);
        loadKitButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a kit folder of WAV samples",
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto folder = fc.getResult();
                                      if (folder.isDirectory())
                                          proc.getKit().loadKitFolder (folder);
                                  });
        };

        kitNameLabel.setFont (uiFont (12.5f, true));
        kitNameLabel.setColour (juce::Label::textColourId, DrumsXLookAndFeel::accent());
        addAndMakeVisible (kitNameLabel);

        outputKnob = std::make_unique<LabelledKnob> (state, pid::outputLevel, "Output");
        addAndMakeVisible (*outputKnob);

        micBlendKnob = std::make_unique<LabelledKnob> (state, pid::micBlend, "Mic Blend");
        bleedKnob    = std::make_unique<LabelledKnob> (state, pid::bleed,    "Bleed");
        crushKnob    = std::make_unique<LabelledKnob> (state, pid::crush,    "Mono Crush");
        for (auto* k : { micBlendKnob.get(), bleedKnob.get(), crushKnob.get() })
            addAndMakeVisible (*k);

        setResizable (false, false);
        proc.setUiScale (proc.getUiScale());
        applyScale();
        setPage (Page::main);
        startTimerHz (8);
    }

    DrumsXEditor::~DrumsXEditor()
    {
        characterList.setModel (nullptr);
        setLookAndFeel (nullptr);
    }

    void DrumsXEditor::applyScale()
    {
        const float s = juce::jlimit (0.5f, 2.0f, proc.getUiScale());

        setTransform (juce::AffineTransform::scale (s));

        // The host/standalone window only follows us when our bounds actually
        // change, so nudge the size when the transform is the only difference.
        if (getWidth() == kBaseWidth && getHeight() == kBaseHeight)
            setSize (kBaseWidth, kBaseHeight - 1);

        setSize (kBaseWidth, kBaseHeight);
    }

    void DrumsXEditor::ensureWindowSize()
    {
        auto* top = getTopLevelComponent();
        if (top == nullptr || top == this)
            return;

        const float s = juce::jlimit (0.5f, 2.0f, proc.getUiScale());
        const int wantW = juce::roundToInt (kBaseWidth  * s);
        const int wantH = juce::roundToInt (kBaseHeight * s);

        // Some window managers hand back a collapsed frame if they answer the
        // first resize request before the editor exists; re-apply if so.
        if (top->getWidth() < wantW / 2 || top->getHeight() < wantH / 2)
            applyScale();
    }

    void DrumsXEditor::setPage (Page p)
    {
        page = p;
        mainTab.setToggleState    (p == Page::main,    juce::dontSendNotification);
        detailsTab.setToggleState (p == Page::details, juce::dontSendNotification);
        kitTab.setToggleState     (p == Page::kit,     juce::dontSendNotification);

        const bool main = p == Page::main;
        const bool det  = p == Page::details;
        const bool kitp = p == Page::kit;

        characterList.setVisible (main);
        pad.setVisible (main);
        phraseView.setVisible (main);
        for (auto* k : { complexityKnob.get(), intensityKnob.get(), fillsKnob.get(), swingKnob.get() })
            k->setVisible (main);
        for (auto& b : variationButtons)
            b->setVisible (main);
        for (auto& group : laneGroups)
        {
            group.in->setVisible (main);
            group.ghost->setVisible (main);
        }

        for (auto& k : detailKnobs)
            k->setVisible (det);
        for (auto* c : { &fillBarsBox, &phraseBarsBox, &swingGridBox, &timeSigNumBox, &timeSigDenBox, &tempoModeBox })
            c->setVisible (det);
        bpmSlider.setVisible (det);
        rideToggle.setVisible (det);
        halfTimeToggle.setVisible (det);
        manualToggle.setVisible (det);
        clearManualButton.setVisible (det);
        manualGrid.setVisible (det);

        for (auto& row : kitRows)
        {
            row.name->setVisible (kitp);
            row.enable->setVisible (kitp);
            row.prev->setVisible (kitp);
            row.next->setVisible (kitp);
            row.layers->setVisible (kitp);
            row.gain->setVisible (kitp);
            row.pan->setVisible (kitp);
            row.tune->setVisible (kitp);
            row.damp->setVisible (kitp);
        }
        loadKitButton.setVisible (kitp);
        kitNameLabel.setVisible (kitp);
        outputKnob->setVisible (kitp);
        for (auto* k : { micBlendKnob.get(), bleedKnob.get(), crushKnob.get() })
            k->setVisible (kitp);

        resized();
        repaint();
    }

    void DrumsXEditor::timerCallback()
    {
        playButton.setToggleState (proc.isPlaying(), juce::dontSendNotification);
        playButton.setButtonText (proc.isPlaying() ? "STOP" : "PLAY");

        if (startupChecks > 0)
        {
            --startupChecks;
            ensureWindowSize();
        }

        if (page == Page::main)
        {
            for (auto& group : laneGroups)
            {
                const auto flag = [this] (const juce::String& id)
                {
                    const auto* raw = proc.getAPVTS().getRawParameterValue (id);
                    return raw != nullptr && raw->load() > 0.5f;
                };
                group.in->setToggleState (flag (pid::laneEnable (group.lanes.front())),
                                          juce::dontSendNotification);
                group.ghost->setToggleState (flag (pid::laneGhost (group.lanes.front())),
                                            juce::dontSendNotification);
            }
        }

        if (page == Page::kit)
        {
            kitNameLabel.setText ("KIT - " + proc.getKit().getKitName().toUpperCase(),
                                  juce::dontSendNotification);
            for (int lane = 0; lane < NumLanes; ++lane)
            {
                const int layers = proc.getKit().numLayersForLane (lane);
                const int offset = proc.getKit().getLaneSampleSwitch (lane);
                kitRows[(std::size_t) lane].layers->setText (
                    layers == 0 ? "-"
                                : juce::String (offset) + " / " + juce::String (layers)
                                      + "x" + juce::String (proc.getKit().numVariantsForLane (lane, 0)),
                    juce::dontSendNotification);
            }
        }
    }

    void DrumsXEditor::exportMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Export 8 bars...");
        m.addItem (2, "Export 16 bars...");
        m.addItem (3, "Export 32 bars...");
        m.addSeparator();
        m.addItem (4, "Export per-instrument MIDI (16 bars)...");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportButton),
                         [this] (int result)
                         {
                             if (result == 0)
                                 return;

                             const int bars = result == 1 ? 8 : (result == 2 ? 16 : (result == 3 ? 32 : 16));
                             const bool perInstrument = result == 4;

                             chooser = std::make_unique<juce::FileChooser> (
                                 perInstrument ? "Choose a folder for the per-instrument MIDI files"
                                               : "Save the arrangement as MIDI",
                                 juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                     .getChildFile ("HumHouse Drums X.mid"),
                                 perInstrument ? juce::String() : juce::String ("*.mid"));

                             const auto flags = perInstrument
                                 ? (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories)
                                 : (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting);

                             chooser->launchAsync (flags, [this, bars, perInstrument] (const juce::FileChooser& fc)
                             {
                                 const auto target = fc.getResult();
                                 if (target == juce::File())
                                     return;
                                 if (perInstrument)
                                     proc.exportPerInstrumentMidi (target.getChildFile ("HumHouse Drums X"), bars);
                                 else
                                     proc.exportArrangementMidi (target.withFileExtension ("mid"), bars);
                             });
                         });
    }

    //==============================================================================
    void DrumsXEditor::paint (juce::Graphics& g)
    {
        g.fillAll (DrumsXLookAndFeel::bg());

        auto header = getLocalBounds().removeFromTop (54);
        g.setColour (DrumsXLookAndFeel::panel());
        g.fillRect (header);
        g.setColour (DrumsXLookAndFeel::line());
        g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());

        g.setColour (DrumsXLookAndFeel::text());
        g.setFont (uiFont (19.0f, true));
        g.drawText ("HUMHOUSE", header.reduced (20, 0).removeFromLeft (120),
                    juce::Justification::centredLeft, false);
        g.setColour (DrumsXLookAndFeel::accent());
        g.setFont (uiFont (19.0f, true));
        g.drawText ("DRUMS X", header.reduced (20, 0).withTrimmedLeft (118).removeFromLeft (110),
                    juce::Justification::centredLeft, false);

        if (page == Page::main)
        {
            drawPanel (g, characterList.getBounds().expanded (10, 26), "Character");
            drawPanel (g, phraseView.getBounds().expanded (0, 18).withTrimmedBottom (18), "Performance");
        }
        else if (page == Page::details)
        {
            drawPanel (g, juce::Rectangle<int> (16, 70, getWidth() - 32, 150), "Feel & Fills");
            drawPanel (g, juce::Rectangle<int> (16, 230, getWidth() - 32, 110), "Metre & Tempo");

            g.setColour (DrumsXLookAndFeel::textDim());
            g.setFont (uiFont (10.5f, true));
            g.drawText ("TIME SIG",   juce::Rectangle<int> (150, 240, 120, 13), juce::Justification::centredLeft);
            g.drawText ("TEMPO",      juce::Rectangle<int> (330, 240, 120, 13), juce::Justification::centredLeft);
            g.drawText ("BPM",        juce::Rectangle<int> (478, 240, 120, 13), juce::Justification::centredLeft);
            g.drawText ("SWING GRID", juce::Rectangle<int> (736, 240, 120, 13), juce::Justification::centredLeft);
        }
        else
        {
            drawPanel (g, juce::Rectangle<int> (16, 70, getWidth() - 32, getHeight() - 150), "Kit & Mix");
            g.setColour (DrumsXLookAndFeel::textDim());
            g.setFont (uiFont (10.5f, true));
            g.drawText ("ON",      juce::Rectangle<int> (34,  96, 40, 14), juce::Justification::centredLeft);
            g.drawText ("PIECE",   juce::Rectangle<int> (74,  96, 90, 14), juce::Justification::centredLeft);
            g.drawText ("SAMPLE",  juce::Rectangle<int> (176, 96, 110, 14), juce::Justification::centredLeft);
            g.drawText ("LEVEL",   juce::Rectangle<int> (330, 96, 90, 14), juce::Justification::centredLeft);
            g.drawText ("PAN",     juce::Rectangle<int> (490, 96, 90, 14), juce::Justification::centredLeft);
            g.drawText ("TUNE",    juce::Rectangle<int> (616, 96, 40, 14), juce::Justification::centredLeft);
            g.drawText ("DAMP",    juce::Rectangle<int> (650, 96, 40, 14), juce::Justification::centredLeft);
        }
    }

    void DrumsXEditor::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (54).reduced (20, 12);
        header.removeFromLeft (240);

        for (auto* tab : { &mainTab, &detailsTab, &kitTab })
        {
            tab->setBounds (header.removeFromLeft (92).reduced (3, 0));
            header.removeFromLeft (2);
        }

        scaleBox.setBounds (header.removeFromRight (78).reduced (2, 0));
        header.removeFromRight (8);
        exportButton.setBounds (header.removeFromRight (110).reduced (2, 0));
        header.removeFromRight (6);
        regenButton.setBounds (header.removeFromRight (110).reduced (2, 0));
        header.removeFromRight (6);
        playButton.setBounds (header.removeFromRight (76).reduced (2, 0));

        switch (page)
        {
            case Page::main:    layoutMain (r);    break;
            case Page::details: layoutDetails (r); break;
            case Page::kit:     layoutKit (r);     break;
        }
    }

    void DrumsXEditor::layoutMain (juce::Rectangle<int> r)
    {
        r.reduce (16, 16);

        auto left = r.removeFromLeft (250);
        characterList.setBounds (left.removeFromTop (250).reduced (10, 26).withTrimmedTop (-16));
        r.removeFromLeft (16);

        auto bottom = r.removeFromBottom (150);
        phraseView.setBounds (bottom.withTrimmedTop (18));

        auto strip = r.removeFromBottom (34).reduced (0, 4);
        const int cells = juce::jmax (1, (int) laneGroups.size());
        for (int i = 0; i < (int) laneGroups.size(); ++i)
        {
            auto cell = strip.removeFromLeft (strip.getWidth() / (cells - i)).reduced (3, 0);
            laneGroups[(std::size_t) i].ghost->setBounds (cell.removeFromRight (26));
            laneGroups[(std::size_t) i].in->setBounds (cell.withTrimmedRight (2));
        }

        auto right = r.removeFromRight (240);
        auto knobRow = right.removeFromTop (110);
        const int kw = knobRow.getWidth() / 2;
        complexityKnob->setBounds (knobRow.removeFromLeft (kw).reduced (6));
        intensityKnob->setBounds (knobRow.reduced (6));

        auto knobRow2 = right.removeFromTop (110);
        fillsKnob->setBounds (knobRow2.removeFromLeft (kw).reduced (6));
        swingKnob->setBounds (knobRow2.reduced (6));

        // Variation button rows.
        for (int group = 0; group < 2; ++group)
        {
            auto rowArea = right.removeFromTop (40).reduced (6, 6);
            for (int i = 0; i < 4; ++i)
            {
                const std::size_t index = (std::size_t) (group * 4 + i);
                if (index < variationButtons.size())
                    variationButtons[index]->setBounds (rowArea.removeFromLeft (rowArea.getWidth() / (4 - i)).reduced (3, 0));
            }
        }

        pad.setBounds (r.reduced (0, 0));
    }

    void DrumsXEditor::layoutDetails (juce::Rectangle<int> r)
    {
        juce::ignoreUnused (r);

        auto knobs = juce::Rectangle<int> (28, 92, getWidth() - 56, 118);
        const int kw = knobs.getWidth() / juce::jmax (1, (int) detailKnobs.size() + 2);
        for (auto& k : detailKnobs)
            k->setBounds (knobs.removeFromLeft (kw).reduced (8, 4));

        auto fillCol = knobs.removeFromLeft (kw * 2).reduced (10, 20);
        fillBarsBox.setBounds (fillCol.removeFromTop (26));
        fillCol.removeFromTop (8);
        phraseBarsBox.setBounds (fillCol.removeFromTop (26));

        timeSigNumBox.setBounds (juce::Rectangle<int> (150, 258, 84, 26));
        timeSigDenBox.setBounds (juce::Rectangle<int> (240, 258, 72, 26));
        tempoModeBox.setBounds (juce::Rectangle<int> (330, 258, 130, 26));
        bpmSlider.setBounds     (juce::Rectangle<int> (478, 258, 240, 26));
        swingGridBox.setBounds  (juce::Rectangle<int> (736, 258, 80, 26));

        rideToggle.setBounds (juce::Rectangle<int> (28, 296, 200, 24));
        halfTimeToggle.setBounds (juce::Rectangle<int> (232, 296, 150, 24));
        manualToggle.setBounds (juce::Rectangle<int> (392, 296, 160, 24));
        clearManualButton.setBounds (juce::Rectangle<int> (560, 296, 72, 24));

        manualGrid.setBounds (16, 348, getWidth() - 32, getHeight() - 364);
    }

    void DrumsXEditor::layoutKit (juce::Rectangle<int> r)
    {
        juce::ignoreUnused (r);

        int y = 116;
        const int rowH = 30;
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            auto& row = kitRows[(std::size_t) lane];
            row.enable->setBounds (32, y + 4, 24, 22);
            row.name->setBounds (72, y, 100, rowH);
            row.prev->setBounds (176, y + 3, 26, 24);
            row.layers->setBounds (204, y, 60, rowH);
            row.next->setBounds (266, y + 3, 26, 24);
            row.gain->setBounds (330, y + 4, 150, 22);
            row.pan->setBounds (490, y + 4, 120, 22);
            row.tune->setBounds (620, y + 2, 26, 26);
            row.damp->setBounds (652, y + 2, 26, 26);
            y += rowH;
        }

        kitNameLabel.setBounds (740, 110, 260, 24);
        loadKitButton.setBounds (740, 142, 200, 28);
        outputKnob->setBounds (740, 190, 110, 110);
        micBlendKnob->setBounds (860, 190, 110, 110);
        bleedKnob->setBounds (740, 306, 110, 110);
        crushKnob->setBounds (860, 306, 110, 110);
    }
}
