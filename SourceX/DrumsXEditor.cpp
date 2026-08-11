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

    //==============================================================================
    namespace
    {
        /** The file the host reads after the drag call returns, so it has to
            outlive the gesture: one stable path, rewritten per drag. */
        juce::File& dragFile()
        {
            static juce::File file;
            return file;
        }

        bool dragRunning = false;
    }

    bool midiDragInProgress()
    {
        return dragRunning;
    }

    bool startMidiDrag (juce::Component& source, DrumsXProcessor& proc, int numBars)
    {
        if (dragRunning)
            return false;

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("HumHouse Drums X");
        folder.createDirectory();

        auto& file = dragFile();
        file = folder.getChildFile ("HumHouse Drums X " + juce::String (numBars) + " bars.mid");
        if (! proc.exportArrangementMidi (file, numBars))
            return false;

        dragRunning = true;
        const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
            juce::StringArray (file.getFullPathName()), false, &source,
            [] { dragRunning = false; });

        if (! started)
            dragRunning = false;

        return started;
    }

    void PhraseView::mouseDown (const juce::MouseEvent&)
    {
    }

    void PhraseView::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.getDistanceFromDragStart() >= 10)
            startMidiDrag (*this, proc, proc.totalArrangementBars());
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

        g.setColour (DrumsXLookAndFeel::textDim());
        g.setFont (uiFont (10.0f, true));
        g.drawText ("DRAG TO DAW", r.reduced (12, 6), juce::Justification::topRight, false);
    }

    //==============================================================================
    namespace
    {
        constexpr int kBlockWidth = 124;
        constexpr int kBlockGap   = 8;
        constexpr int kPlusWidth  = 46;

        juce::Colour sectionColour (int section)
        {
            switch (section)
            {
                case SectionIntro:  return juce::Colour (0xff3c5a7a);
                case SectionChorus: return juce::Colour (0xff8a5a1e);
                case SectionBridge: return juce::Colour (0xff5a3c7a);
                case SectionOutro:  return juce::Colour (0xff3c6a58);
                case SectionFill:   return juce::Colour (0xff7a3c46);
                default:            return juce::Colour (0xff44484f);
            }
        }
    }

    ArrangementStrip::ArrangementStrip (DrumsXProcessor& p) : proc (p)
    {
        startTimerHz (8);
    }

    int ArrangementStrip::preferredWidth() const
    {
        return proc.numSections() * (kBlockWidth + kBlockGap) + kPlusWidth + kBlockGap * 2;
    }

    juce::Rectangle<int> ArrangementStrip::blockBounds (int index) const
    {
        return { kBlockGap + index * (kBlockWidth + kBlockGap), 4,
                 kBlockWidth, getHeight() - 8 };
    }

    juce::Rectangle<int> ArrangementStrip::plusBounds() const
    {
        return { kBlockGap + proc.numSections() * (kBlockWidth + kBlockGap), 4,
                 kPlusWidth, getHeight() - 8 };
    }

    void ArrangementStrip::timerCallback()
    {
        const int count    = proc.numSections();
        const int selected = proc.getSelectedSection();
        const int bars     = proc.totalArrangementBars();

        // Knob moves write into the selected block without changing any of the
        // counters, so the meters need the block contents in the comparison.
        std::uint32_t state = 2166136261u;
        const auto mix = [&state] (std::uint32_t v)
        {
            state = (state ^ v) * 16777619u;
        };

        for (const auto& sec : proc.getArrangement())
        {
            mix ((std::uint32_t) sec.id);
            mix ((std::uint32_t) sec.numBars);
            mix ((std::uint32_t) sec.section);
            mix ((std::uint32_t) juce::roundToInt (sec.complexity * 1000.0f));
            mix ((std::uint32_t) juce::roundToInt (sec.intensity  * 1000.0f));
            mix ((std::uint32_t) juce::roundToInt (sec.velocity   * 1000.0f));
            mix ((std::uint32_t) juce::roundToInt (sec.fillAmount * 1000.0f));
            mix ((std::uint32_t) juce::roundToInt (sec.swing      * 1000.0f));
            mix ((std::uint32_t) (sec.halfTime ? 1 : 0));
            mix ((std::uint32_t) sec.variationRhythm);
            mix ((std::uint32_t) sec.variationCymbal);
        }

        if (count == lastCount && selected == lastSelected && bars == lastBars
            && state == lastState)
            return;

        lastCount    = count;
        lastSelected = selected;
        lastBars     = bars;
        lastState    = state;

        if (getWidth() != preferredWidth())
            setSize (preferredWidth(), getHeight());
        repaint();
    }

    void ArrangementStrip::paint (juce::Graphics& g)
    {
        const auto blocks   = proc.getArrangement();
        const int  selected = proc.getSelectedSection();

        int startBar = 0;
        for (int i = 0; i < (int) blocks.size(); ++i)
        {
            const auto& sec = blocks[(std::size_t) i];
            const auto  r   = blockBounds (i).toFloat();
            const bool  on  = i == selected;

            g.setColour (sectionColour (sec.section).withAlpha (on ? 1.0f : 0.6f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (on ? DrumsXLookAndFeel::accent() : DrumsXLookAndFeel::line());
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, on ? 2.0f : 1.0f);

            g.setColour (DrumsXLookAndFeel::text());
            g.setFont (uiFont (12.0f, true));
            g.drawText (juce::String (sectionName (sec.section)).toUpperCase(),
                        r.reduced (8.0f, 5.0f).removeFromTop (15.0f).toNearestInt(),
                        juce::Justification::centredLeft, false);

            g.setColour (DrumsXLookAndFeel::textDim());
            g.setFont (uiFont (10.5f));
            g.drawText ("BARS " + juce::String (startBar + 1) + "-"
                            + juce::String (startBar + std::max (1, sec.numBars)),
                        r.reduced (8.0f, 4.0f).withTrimmedTop (17.0f).removeFromTop (13.0f).toNearestInt(),
                        juce::Justification::centredLeft, false);

            // How hard the block is played and how busy it is, so the shape of
            // the song is readable without clicking through every block.
            const auto meters = r.reduced (8.0f, 6.0f).removeFromBottom (12.0f);
            const auto drawMeter = [&] (juce::Rectangle<float> m, float v, juce::Colour c)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRoundedRectangle (m, 2.0f);
                g.setColour (c);
                g.fillRoundedRectangle (m.withWidth (juce::jmax (2.0f, m.getWidth() * v)), 2.0f);
            };
            drawMeter (meters.withHeight (4.0f), sec.velocity, DrumsXLookAndFeel::accent());
            drawMeter (meters.withHeight (4.0f).translated (0.0f, 7.0f), sec.complexity,
                       DrumsXLookAndFeel::accent().withAlpha (0.55f));
            startBar += std::max (1, sec.numBars);
        }

        const auto plus = plusBounds().toFloat();
        g.setColour (DrumsXLookAndFeel::panelHi());
        g.fillRoundedRectangle (plus, 5.0f);
        g.setColour (DrumsXLookAndFeel::accent());
        g.drawRoundedRectangle (plus.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (uiFont (22.0f, true));
        g.drawText ("+", plus.toNearestInt(), juce::Justification::centred, false);
    }

    void ArrangementStrip::showBlockMenu (int index)
    {
        juce::PopupMenu types;
        for (int t = 0; t < (int) NumSections; ++t)
            types.addItem (100 + t, sectionName (t));

        juce::PopupMenu lengths;
        for (const int bars : { 2, 4, 8, 16, 32 })
            lengths.addItem (200 + bars, juce::String (bars) + " bars");

        juce::PopupMenu m;
        m.addSubMenu ("Section", types);
        m.addSubMenu ("Length", lengths);
        m.addItem (2, "Duplicate");
        m.addItem (3, "Delete", proc.numSections() > 1);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<ArrangementStrip> (this), index] (int result)
                         {
                             if (safe == nullptr)
                                 return;
                             if (result >= 200)      safe->proc.setSectionBars (index, result - 200);
                             else if (result >= 100) safe->proc.setSectionType (index, result - 100);
                             else if (result == 2)   safe->proc.duplicateSection (index);
                             else if (result == 3)   safe->proc.removeSection (index);
                             safe->repaint();
                         });
    }

    int ArrangementStrip::blockAt (juce::Point<int> p) const
    {
        for (int i = 0; i < proc.numSections(); ++i)
            if (blockBounds (i).contains (p))
                return i;
        return -1;
    }

    void ArrangementStrip::mouseDrag (const juce::MouseEvent& e)
    {
        // A block drags the song out as MIDI, like dragging a Logic region.
        if (e.getDistanceFromDragStart() >= 10 && blockAt (e.getMouseDownPosition()) >= 0)
            startMidiDrag (*this, proc, proc.totalArrangementBars());
    }

    void ArrangementStrip::mouseDown (const juce::MouseEvent& e)
    {
        if (plusBounds().contains (e.getPosition()))
        {
            proc.addSection();
            setSize (preferredWidth(), getHeight());
            repaint();
            return;
        }

        for (int i = 0; i < proc.numSections(); ++i)
        {
            if (! blockBounds (i).contains (e.getPosition()))
                continue;

            if (e.mods.isPopupMenu())
                showBlockMenu (i);
            else
                proc.setSelectedSection (i);
            repaint();
            return;
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
        gestureLane = gestureStep = -1;
        adjusting = false;

        int lane = 0, step = 0;
        if (! cellAt (e.getPosition(), lane, step))
            return;
        erasing = e.mods.isRightButtonDown() || proc.getManualStep (lane, step) > 0.0f;
        gestureLane  = lane;
        gestureStep  = step;
        gestureValue = erasing ? 0.0f : 0.8f;

        proc.setManualStep (lane, step, gestureValue);
        repaint();
    }

    void ManualPatternGrid::mouseDrag (const juce::MouseEvent& e)
    {
        if (gestureLane < 0)
            return;

        int lane = 0, step = 0;
        const bool inCell   = cellAt (e.getPosition(), lane, step);
        const bool sameCell = ! inCell || (lane == gestureLane && step == gestureStep);

        // A vertical drag that never leaves the clicked cell sets that one
        // note's velocity, so nudging the mouse cannot place anything else.
        if (! adjusting && sameCell && ! erasing
            && std::abs (e.getDistanceFromDragStartY()) >= 6
            && std::abs (e.getDistanceFromDragStartY()) > std::abs (e.getDistanceFromDragStartX()))
            adjusting = true;

        if (adjusting)
        {
            const float v = juce::jlimit (0.1f, 1.0f,
                                          0.8f - (float) e.getDistanceFromDragStartY() / 160.0f);
            proc.setManualStep (gestureLane, gestureStep, v);
            repaint();
            return;
        }

        if (sameCell)
            return;

        // The pointer genuinely travelled to another cell: paint that one, in
        // the mode the gesture started in, and continue from there.
        proc.setManualStep (lane, step, gestureValue);
        gestureLane = lane;
        gestureStep = step;
        repaint();
    }

    void ManualPatternGrid::mouseUp (const juce::MouseEvent&)
    {
        gestureLane = gestureStep = -1;
        adjusting = false;
    }

    //==============================================================================
    MidiDragButton::MidiDragButton (DrumsXProcessor& p, const juce::String& caption)
        : juce::TextButton (caption), proc (p)
    {
    }

    void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragged || midiDragInProgress() || e.getDistanceFromDragStart() < 8)
            return juce::TextButton::mouseDrag (e);

        dragged = startMidiDrag (*this, proc, proc.totalArrangementBars());
    }

    void MidiDragButton::mouseUp (const juce::MouseEvent& e)
    {
        // Swallow the click that ends a drag, so the export menu does not open
        // on top of the host once the file has been dropped.
        if (dragged)
        {
            dragged = false;
            setState (buttonNormal);
            return;
        }

        juce::TextButton::mouseUp (e);
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

            /** Set while the editor mirrors the persisted selection into the
                list, so re-opening the UI does not re-apply the character's
                defaults over the saved XY position. */
            bool suppressApply = false;

            void selectedRowsChanged (int row) override
            {
                if (row < 0 || suppressApply)
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
          exportButton (p, "EXPORT MIDI"),
          pad (p), phraseView (p), arrangement (p), manualGrid (p)
    {
        setLookAndFeel (&lnf);
        auto& state = proc.getAPVTS();

        for (auto* tab : { &mainTab, &detailsTab, &kitTab, &mixTab })
        {
            tab->setClickingTogglesState (false);
            addAndMakeVisible (*tab);
        }
        mainTab.onClick    = [this] { setPage (Page::main); };
        detailsTab.onClick = [this] { setPage (Page::details); };
        kitTab.onClick     = [this] { setPage (Page::kit); };
        mixTab.onClick     = [this] { setPage (Page::mix); };

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
        exportButton.setTooltip ("Click to export, or drag this straight into the host");

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
        scaleBox.addItemList ({ "75%", "85%", "100%", "115%", "125%", "150%" }, 1);
        {
            const float scales[] { 0.75f, 0.85f, 1.0f, 1.15f, 1.25f, 1.5f };
            int best = 2;
            for (int i = 0; i < 6; ++i)
                if (std::abs (scales[i] - proc.getUiScale()) < std::abs (scales[best] - proc.getUiScale()))
                    best = i;
            scaleBox.setSelectedId (best + 1, juce::dontSendNotification);
        }
        scaleBox.onChange = [this]
        {
            static const float scales[] = { 0.75f, 0.85f, 1.0f, 1.15f, 1.25f, 1.50f };
            const int idx = juce::jlimit (0, 5, scaleBox.getSelectedId() - 1);
            proc.setUiScale (scales[idx]);
            applyScale();
        };

        // ---- MAIN ---------------------------------------------------------
        auto model = std::make_unique<CharacterListModel> (proc);
        auto* modelPtr = model.get();
        characterModel = std::move (model);
        characterList.setModel (characterModel.get());
        characterList.setRowHeight (34);
        characterList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        characterList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        modelPtr->suppressApply = true;
        characterList.selectRow ((int) state.getRawParameterValue (pid::preset)->load(),
                                 true, true);
        modelPtr->suppressApply = false;
        addAndMakeVisible (characterList);
        addAndMakeVisible (pad);
        addAndMakeVisible (phraseView);

        // The arrangement scrolls sideways, so the song can keep growing past
        // the width of the window.
        arrangement.setSize (arrangement.preferredWidth(), 62);
        arrangementView.setViewedComponent (&arrangement, false);
        arrangementView.setScrollBarsShown (false, true, false, true);
        addAndMakeVisible (arrangementView);

        complexityKnob   = std::make_unique<LabelledKnob> (state, pid::complexity,   "Complexity");
        intensityKnob    = std::make_unique<LabelledKnob> (state, pid::intensity,    "Loud");
        // The block's own dynamics, deliberately its own control: the pad
        // chooses the take, this decides how hard the section is played.
        sectionLevelKnob = std::make_unique<LabelledKnob> (state, pid::sectionLevel, "Intensity");
        fillsKnob        = std::make_unique<LabelledKnob> (state, pid::fillAmount,   "Fills");
        swingKnob        = std::make_unique<LabelledKnob> (state, pid::swing,        "Swing");
        for (auto* k : { complexityKnob.get(), intensityKnob.get(), sectionLevelKnob.get(),
                         fillsKnob.get(), swingKnob.get() })
            addAndMakeVisible (*k);

        sectionLevelKnob->slider.setTooltip ("How hard this arrangement block is played. "
                                             "Independent of the performance pad.");

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
        kitViewport.setViewedComponent (&kitRowsHolder, false);
        kitViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (kitViewport);

        for (int lane = 0; lane < NumLanes; ++lane)
        {
            auto& row = kitRows[(std::size_t) lane];

            row.name = std::make_unique<juce::Label>();
            row.name->setText (drumsXLaneName (lane), juce::dontSendNotification);
            row.name->setFont (uiFont (12.5f));
            kitRowsHolder.addAndMakeVisible (*row.name);

            row.enable = std::make_unique<juce::ToggleButton> ("");
            kitRowsHolder.addAndMakeVisible (*row.enable);
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
            kitRowsHolder.addAndMakeVisible (*row.prev);
            kitRowsHolder.addAndMakeVisible (*row.next);

            row.layers = std::make_unique<juce::Label>();
            row.layers->setFont (uiFont (11.0f));
            row.layers->setColour (juce::Label::textColourId, DrumsXLookAndFeel::textDim());
            row.layers->setJustificationType (juce::Justification::centred);
            kitRowsHolder.addAndMakeVisible (*row.layers);

            // Every mixer control is an automatable parameter, so the mix is
            // saved with the project rather than living only in the UI.
            const auto mixerSlider = [this, &state] (std::unique_ptr<juce::Slider>& slider,
                                                     const juce::String& paramID,
                                                     juce::Slider::SliderStyle style)
            {
                slider = std::make_unique<juce::Slider> (style, juce::Slider::NoTextBox);
                // The wheel belongs to the lane list: a stray wheel over a row
                // must scroll, not silently move somebody's mix.
                slider->setScrollWheelEnabled (false);
                kitRowsHolder.addAndMakeVisible (*slider);
                sliderAttachments.push_back (
                    std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        state, paramID, *slider));
            };

            mixerSlider (row.gain, pid::laneGain (lane), juce::Slider::LinearHorizontal);
            mixerSlider (row.pan,  pid::lanePan (lane),  juce::Slider::LinearHorizontal);
            mixerSlider (row.tune, pid::laneTune (lane), juce::Slider::RotaryHorizontalVerticalDrag);
            mixerSlider (row.damp, pid::laneDamp (lane), juce::Slider::RotaryHorizontalVerticalDrag);
        }

        {
            const auto kits = proc.getAvailableKits();
            for (int i = 0; i < kits.size(); ++i)
                kitBox.addItem (kits[i], i + 1);
            kitBox.setSelectedId (proc.getSelectedKit() + 1, juce::dontSendNotification);
            kitBox.setTextWhenNoChoicesAvailable ("No installed kits");
            kitBox.onChange = [this] { proc.selectKit (kitBox.getSelectedId() - 1); };
            addAndMakeVisible (kitBox);
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

        // ---- MIX ----------------------------------------------------------
        mixViewport.setViewedComponent (&mixRowsHolder, false);
        mixViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (mixViewport);

        for (int lane = 0; lane < NumLanes; ++lane)
        {
            auto& row = mixRows[(std::size_t) lane];

            row.name = std::make_unique<juce::Label>();
            row.name->setText (drumsXLaneName (lane), juce::dontSendNotification);
            row.name->setFont (uiFont (12.5f));
            mixRowsHolder.addAndMakeVisible (*row.name);

            const auto knob = [this, &state] (std::unique_ptr<juce::Slider>& slider,
                                             const juce::String& paramID)
            {
                slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                         juce::Slider::NoTextBox);
                slider->setScrollWheelEnabled (false);
                mixRowsHolder.addAndMakeVisible (*slider);
                sliderAttachments.push_back (
                    std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        state, paramID, *slider));
            };

            knob (row.comp, pid::laneComp (lane));
            knob (row.send, pid::laneSend (lane));
            knob (row.tune, pid::laneTune (lane));
            knob (row.damp, pid::laneDamp (lane));
        }

        roomSizeKnob = std::make_unique<LabelledKnob> (state, pid::roomSize,    "Room Size");
        roomDampKnob = std::make_unique<LabelledKnob> (state, pid::roomDamping, "Room Damp");
        roomMixKnob  = std::make_unique<LabelledKnob> (state, pid::roomMix,     "Room Return");
        for (auto* k : { roomSizeKnob.get(), roomDampKnob.get(), roomMixKnob.get() })
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
        mixTab.setToggleState     (p == Page::mix,     juce::dontSendNotification);

        const bool main = p == Page::main;
        const bool det  = p == Page::details;
        const bool kitp = p == Page::kit;
        const bool mixp = p == Page::mix;

        characterList.setVisible (main);
        pad.setVisible (main);
        phraseView.setVisible (main);
        arrangementView.setVisible (main);
        for (auto* k : { complexityKnob.get(), intensityKnob.get(), sectionLevelKnob.get(),
                         fillsKnob.get(), swingKnob.get() })
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

        kitViewport.setVisible (kitp);
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
        kitBox.setVisible (kitp);
        kitNameLabel.setVisible (kitp);
        outputKnob->setVisible (kitp);
        for (auto* k : { micBlendKnob.get(), bleedKnob.get(), crushKnob.get() })
            k->setVisible (kitp);

        mixViewport.setVisible (mixp);
        for (auto& row : mixRows)
        {
            row.name->setVisible (mixp);
            row.comp->setVisible (mixp);
            row.send->setVisible (mixp);
            row.tune->setVisible (mixp);
            row.damp->setVisible (mixp);
        }
        for (auto* k : { roomSizeKnob.get(), roomDampKnob.get(), roomMixKnob.get() })
            k->setVisible (mixp);

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
            kitNameLabel.setText ("KIT - " + proc.getKit().getKitName().toUpperCase()
                                      + "   (" + proc.getContentDescription() + ")",
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
        m.addItem (5, "Export the whole arrangement ("
                      + juce::String (proc.totalArrangementBars()) + " bars)...");
        m.addSeparator();
        m.addItem (4, "Export per-instrument MIDI (16 bars)...");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportButton),
                         [safe = juce::Component::SafePointer<DrumsXEditor> (this)] (int result)
                         {
                             if (result == 0 || safe == nullptr)
                                 return;

                             auto& proc    = safe->proc;
                             auto& chooser = safe->chooser;

                             const int bars = result == 1 ? 8
                                            : result == 2 ? 16
                                            : result == 3 ? 32
                                            : result == 5 ? proc.totalArrangementBars() : 16;
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

                             chooser->launchAsync (flags, [safe, bars, perInstrument] (const juce::FileChooser& fc)
                             {
                                 const auto target = fc.getResult();
                                 if (safe == nullptr || target == juce::File())
                                     return;
                                 if (perInstrument)
                                     safe->proc.exportPerInstrumentMidi (target.getChildFile ("HumHouse Drums X"), bars);
                                 else
                                     safe->proc.exportArrangementMidi (target.withFileExtension ("mid"), bars);
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
            drawPanel (g, arrangementView.getBounds().expanded (0, 16).withTrimmedBottom (16),
                       "Arrangement  -  click a section to edit it, + to add another");
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
        else if (page == Page::mix)
        {
            drawPanel (g, juce::Rectangle<int> (16, 70, getWidth() - 32, getHeight() - 150),
                       "Instrument Processing");
            g.setColour (DrumsXLookAndFeel::textDim());
            g.setFont (uiFont (10.5f, true));
            g.drawText ("PIECE", juce::Rectangle<int> (48,  96, 90, 14), juce::Justification::centredLeft);
            g.drawText ("COMP",  juce::Rectangle<int> (170, 96, 60, 14), juce::Justification::centredLeft);
            g.drawText ("ROOM",  juce::Rectangle<int> (240, 96, 60, 14), juce::Justification::centredLeft);
            g.drawText ("TUNE",  juce::Rectangle<int> (310, 96, 60, 14), juce::Justification::centredLeft);
            g.drawText ("DAMP",  juce::Rectangle<int> (380, 96, 60, 14), juce::Justification::centredLeft);
        }
        else
        {
            drawPanel (g, juce::Rectangle<int> (16, 70, getWidth() - 32, getHeight() - 150), "Kit & Mix");
            g.setColour (DrumsXLookAndFeel::textDim());
            g.setFont (uiFont (10.5f, true));
            g.drawText ("ON",      juce::Rectangle<int> (32,  96, 40, 14), juce::Justification::centredLeft);
            g.drawText ("PIECE",   juce::Rectangle<int> (72,  96, 90, 14), juce::Justification::centredLeft);
            g.drawText ("SAMPLE",  juce::Rectangle<int> (176, 96, 110, 14), juce::Justification::centredLeft);
            g.drawText ("LEVEL",   juce::Rectangle<int> (330, 96, 90, 14), juce::Justification::centredLeft);
            g.drawText ("PAN",     juce::Rectangle<int> (490, 96, 90, 14), juce::Justification::centredLeft);
            g.drawText ("TUNE",    juce::Rectangle<int> (620, 96, 40, 14), juce::Justification::centredLeft);
            g.drawText ("DAMP",    juce::Rectangle<int> (652, 96, 40, 14), juce::Justification::centredLeft);
        }
    }

    void DrumsXEditor::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (54).reduced (20, 12);
        header.removeFromLeft (240);

        for (auto* tab : { &mainTab, &detailsTab, &kitTab, &mixTab })
        {
            tab->setBounds (header.removeFromLeft (80).reduced (3, 0));
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
            case Page::mix:     layoutMix (r);     break;
        }
    }

    void DrumsXEditor::layoutMain (juce::Rectangle<int> r)
    {
        r.reduce (16, 16);

        auto left = r.removeFromLeft (250);
        characterList.setBounds (left.removeFromTop (250).reduced (10, 26).withTrimmedTop (-16));
        r.removeFromLeft (16);

        auto strip2 = r.removeFromBottom (66);
        arrangementView.setBounds (strip2.withTrimmedTop (16));
        arrangement.setSize (arrangement.preferredWidth(),
                             juce::jmax (24, arrangementView.getMaximumVisibleHeight()));

        auto bottom = r.removeFromBottom (132);
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

        // Three rows so the block's own Intensity sits beside the pad's knobs
        // without pushing the variation buttons off the page.
        const int rowH = 86;
        const int kw   = right.getWidth() / 2;
        LabelledKnob* const rows[3][2]
        {
            { complexityKnob.get(),   intensityKnob.get() },
            { sectionLevelKnob.get(), fillsKnob.get() },
            { swingKnob.get(),        nullptr }
        };

        for (const auto& row : rows)
        {
            auto area = right.removeFromTop (rowH);
            auto leftCell = area.removeFromLeft (kw);
            row[0]->setBounds (leftCell.reduced (6, 4));
            if (row[1] != nullptr)
                row[1]->setBounds (area.reduced (6, 4));
        }

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

        // All 30 articulations scroll inside the panel: a fixed row list cannot
        // fit them, and the UI scale is a transform, so it never adds room.
        const int rowH = 30;
        const int viewTop = 112;
        kitViewport.setBounds (24, viewTop, 700, 580 - viewTop - 4);
        kitRowsHolder.setSize (kitViewport.getMaximumVisibleWidth(), rowH * NumLanes + 6);

        int y = 4;
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            auto& row = kitRows[(std::size_t) lane];
            row.enable->setBounds (8,   y + 4, 24, 22);
            row.name->setBounds (48,  y, 100, rowH);
            row.prev->setBounds (152, y + 3, 26, 24);
            row.layers->setBounds (180, y, 60, rowH);
            row.next->setBounds (242, y + 3, 26, 24);
            row.gain->setBounds (306, y + 4, 150, 22);
            row.pan->setBounds (466, y + 4, 120, 22);
            row.tune->setBounds (596, y + 2, 26, 26);
            row.damp->setBounds (628, y + 2, 26, 26);
            y += rowH;
        }

        kitNameLabel.setBounds (740, 110, 260, 24);
        kitBox.setBounds (740, 138, 240, 28);
        loadKitButton.setBounds (740, 172, 200, 28);
        outputKnob->setBounds (740, 214, 110, 110);
        micBlendKnob->setBounds (860, 214, 110, 110);
        bleedKnob->setBounds (740, 330, 110, 110);
        crushKnob->setBounds (860, 330, 110, 110);
    }

    void DrumsXEditor::layoutMix (juce::Rectangle<int> r)
    {
        juce::ignoreUnused (r);

        const int rowH = 30;
        const int viewTop = 112;
        mixViewport.setBounds (24, viewTop, 460, 580 - viewTop - 4);
        mixRowsHolder.setSize (mixViewport.getMaximumVisibleWidth(), rowH * NumLanes + 6);

        int y = 4;
        for (int lane = 0; lane < NumLanes; ++lane)
        {
            auto& row = mixRows[(std::size_t) lane];
            row.name->setBounds (24, y, 120, rowH);
            row.comp->setBounds (150, y + 2, 26, 26);
            row.send->setBounds (220, y + 2, 26, 26);
            row.tune->setBounds (290, y + 2, 26, 26);
            row.damp->setBounds (360, y + 2, 26, 26);
            y += rowH;
        }

        roomSizeKnob->setBounds (520, 130, 120, 120);
        roomDampKnob->setBounds (650, 130, 120, 120);
        roomMixKnob->setBounds  (780, 130, 120, 120);
    }
}
