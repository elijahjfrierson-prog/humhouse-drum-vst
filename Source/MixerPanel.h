#pragma once

#include "DrumBusMixer.h"
#include "GothicLookAndFeel.h"

#include <JuceHeader.h>

#include <array>
#include <functional>

// ============================================================================
// MixerPanel — per-drum channel-strip view with labelled value readouts
// inspired by MODO Drum / Logic Kit Designer. Each drum bus exposes:
// 3-band EQ · Comp · Drive · Clip · Dampen (%) · Depth (-100..+100) ·
// Reverb send · Pan · Gain (dB) · Mute · Solo.
// ============================================================================
namespace aidrum
{
    class MixerPanel : public juce::Component
    {
    public:
        // How to format the number displayed underneath each knob.
        enum class Unit { Db, Percent, Pan, Depth, Plain };

        explicit MixerPanel (DrumBusMixer& mixerIn) : mixer (mixerIn)
        {
            for (int i = 0; i < kNumDrumBuses; ++i)
            {
                auto& s = strips[(size_t) i];
                s.label.setText (busLabel (i), juce::dontSendNotification);
                s.label.setJustificationType (juce::Justification::centred);
                s.label.setColour (juce::Label::textColourId,
                                   juce::Colour (aidrum::GothicPalette::kBone));
                s.label.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
                addAndMakeVisible (s.label);

                auto& p = mixer.params_ref (i);
                attachKnob (s.eqLow,   "LOW",    -12.0f, 12.0f, 0.1f,  0.0f, &p.eqLowDb,     Unit::Db);
                attachKnob (s.eqMid,   "MID",    -12.0f, 12.0f, 0.1f,  0.0f, &p.eqMidDb,     Unit::Db);
                attachKnob (s.eqHigh,  "HIGH",   -12.0f, 12.0f, 0.1f,  0.0f, &p.eqHighDb,    Unit::Db);
                attachKnob (s.comp,    "COMP",    0.0f,  1.0f,  0.01f, 0.0f, &p.compAmount,  Unit::Percent);
                attachKnob (s.drive,   "DRIVE",   0.0f,  1.0f,  0.01f, 0.0f, &p.drive,       Unit::Percent);
                attachKnob (s.clip,    "CLIP",    0.05f, 1.0f,  0.01f, 1.0f, &p.clipCeiling, Unit::Plain);
                attachKnob (s.dampen,  "DAMPEN",  0.0f,  1.0f,  0.01f, 0.0f, &p.dampen,      Unit::Percent);
                attachKnob (s.depth,   "DEPTH",  -1.0f,  1.0f,  0.01f, 0.0f, &p.depth,       Unit::Depth);
                attachKnob (s.reverb,  "REV",     0.0f,  1.0f,  0.01f, 0.0f, &p.reverbSend,  Unit::Percent);
                attachKnob (s.pan,     "PAN",    -1.0f,  1.0f,  0.01f, 0.0f, &p.pan,         Unit::Pan);
                attachKnob (s.gain,    "GAIN",  -60.0f, 12.0f, 0.1f,   0.0f, &p.gainDb,      Unit::Db);

                s.mute.setButtonText ("M");
                s.mute.setClickingTogglesState (true);
                s.mute.setTooltip ("Mute this drum bus");
                s.mute.setColour (juce::TextButton::buttonColourId,
                                  juce::Colour (aidrum::GothicPalette::kPanel));
                s.mute.setColour (juce::TextButton::buttonOnColourId,
                                  juce::Colour (0xffcc3344));
                s.mute.setColour (juce::TextButton::textColourOffId,
                                  juce::Colour (aidrum::GothicPalette::kBone));
                s.mute.setColour (juce::TextButton::textColourOnId,
                                  juce::Colours::white);
                s.mute.setToggleState (p.mute.load(), juce::dontSendNotification);
                s.mute.onClick = [&p, &s]()
                {
                    p.mute.store (s.mute.getToggleState(), std::memory_order_relaxed);
                };
                addAndMakeVisible (s.mute);

                s.solo.setButtonText ("S");
                s.solo.setClickingTogglesState (true);
                s.solo.setTooltip ("Solo this drum bus");
                s.solo.setColour (juce::TextButton::buttonColourId,
                                  juce::Colour (aidrum::GothicPalette::kPanel));
                s.solo.setColour (juce::TextButton::buttonOnColourId,
                                  juce::Colour (aidrum::GothicPalette::kAccent));
                s.solo.setColour (juce::TextButton::textColourOffId,
                                  juce::Colour (aidrum::GothicPalette::kBone));
                s.solo.setColour (juce::TextButton::textColourOnId,
                                  juce::Colours::white);
                s.solo.setToggleState (p.solo.load(), juce::dontSendNotification);
                s.solo.onClick = [&p, &s]()
                {
                    p.solo.store (s.solo.getToggleState(), std::memory_order_relaxed);
                };
                addAndMakeVisible (s.solo);
            }

            // Master strip
            masterLabel.setText ("MASTER", juce::dontSendNotification);
            masterLabel.setJustificationType (juce::Justification::centred);
            masterLabel.setColour (juce::Label::textColourId,
                                   juce::Colour (aidrum::GothicPalette::kAccent));
            masterLabel.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
            addAndMakeVisible (masterLabel);

            auto& m = mixer.master_ref();
            attachKnob (masterGain,       "GAIN",    -24.0f, 12.0f, 0.1f,  0.0f,  &m.gainDb,     Unit::Db);
            attachKnob (masterReverbMix,  "REV MIX",   0.0f,  1.0f, 0.01f, 0.22f, &m.reverbMix,  Unit::Percent);
            attachKnob (masterReverbSize, "REV SIZE",  0.0f,  1.0f, 0.01f, 0.55f, &m.reverbSize, Unit::Percent);
            attachKnob (masterReverbDamp, "REV DAMP",  0.0f,  1.0f, 0.01f, 0.50f, &m.reverbDamp, Unit::Percent);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (aidrum::GothicPalette::kInk));
            g.fillRect (b);

            auto header = b.removeFromTop (32.0f);
            g.setColour (juce::Colour (aidrum::GothicPalette::kPanel));
            g.fillRect (header);
            g.setColour (juce::Colour (aidrum::GothicPalette::kAccent));
            g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
            g.drawText (juce::String::fromUTF8 ("\u2020 MIXER  \u00b7  EQ \u00b7 COMP \u00b7 DRIVE \u00b7 CLIP \u00b7 DAMPEN \u00b7 DEPTH \u00b7 PAN \u00b7 GAIN"),
                        header, juce::Justification::centred);

            const int totalStrips = kNumDrumBuses + 1;
            const float stripW = b.getWidth() / (float) totalStrips;
            for (int i = 0; i < totalStrips; ++i)
            {
                auto sb = juce::Rectangle<float> (b.getX() + i * stripW, b.getY(),
                                                  stripW - 2.0f, b.getHeight()).reduced (3.0f);
                g.setColour (juce::Colour (aidrum::GothicPalette::kPanel));
                g.fillRoundedRectangle (sb, 10.0f);
                g.setColour (juce::Colour (aidrum::GothicPalette::kPanelEdge));
                g.drawRoundedRectangle (sb, 10.0f, 1.0f);

                if (i == kNumDrumBuses)
                {
                    g.setColour (juce::Colour (aidrum::GothicPalette::kAccent).withAlpha (0.28f));
                    g.drawRoundedRectangle (sb, 10.0f, 2.0f);
                }
            }
        }

        void resized() override
        {
            auto b = getLocalBounds();
            b.removeFromTop (32); // header

            const int totalStrips = kNumDrumBuses + 1;
            const int stripW = b.getWidth() / totalStrips;

            for (int i = 0; i < kNumDrumBuses; ++i)
            {
                auto& s = strips[(size_t) i];
                auto r = juce::Rectangle<int> (b.getX() + i * stripW, b.getY(),
                                               stripW - 2, b.getHeight()).reduced (6);

                s.label.setBounds (r.removeFromTop (22));

                layoutKnobRow (r, { &s.eqLow, &s.eqMid, &s.eqHigh });
                layoutKnobRow (r, { &s.comp, &s.drive });
                layoutKnobRow (r, { &s.clip, &s.dampen });
                layoutKnobRow (r, { &s.depth, &s.reverb });
                layoutKnobRow (r, { &s.pan });

                auto ms = r.removeFromTop (20);
                s.mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (3, 0));
                s.solo.setBounds (ms.reduced (3, 0));

                // Tall gain knob at the bottom.
                auto gr = r.removeFromTop (90);
                layoutKnobRow (gr, { &s.gain }, 90);
            }

            auto r = juce::Rectangle<int> (b.getX() + kNumDrumBuses * stripW, b.getY(),
                                           stripW - 2, b.getHeight()).reduced (6);
            masterLabel.setBounds (r.removeFromTop (22));
            layoutKnobRow (r, { &masterReverbMix });
            layoutKnobRow (r, { &masterReverbSize });
            layoutKnobRow (r, { &masterReverbDamp });
            auto mg = r.removeFromTop (110);
            layoutKnobRow (mg, { &masterGain }, 110);
        }

    private:
        // A knob + its label + its live value readout, laid out as one cell.
        struct LabeledKnob : public juce::Component
        {
            juce::Slider  slider;
            juce::Label   caption;
            juce::Label   value;

            LabeledKnob()
            {
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
                slider.setColour (juce::Slider::rotarySliderFillColourId,
                                  juce::Colour (aidrum::GothicPalette::kAccent));
                slider.setColour (juce::Slider::rotarySliderOutlineColourId,
                                  juce::Colour (aidrum::GothicPalette::kPanelEdge));
                addAndMakeVisible (slider);

                caption.setJustificationType (juce::Justification::centred);
                caption.setColour (juce::Label::textColourId,
                                   juce::Colour (aidrum::GothicPalette::kSilver));
                caption.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
                caption.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (caption);

                value.setJustificationType (juce::Justification::centred);
                value.setColour (juce::Label::textColourId,
                                 juce::Colour (aidrum::GothicPalette::kBone));
                value.setFont (juce::Font (juce::FontOptions (10.0f)));
                value.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (value);
            }

            void resized() override
            {
                auto r = getLocalBounds();
                caption.setBounds (r.removeFromTop (12));
                value  .setBounds (r.removeFromBottom (13));
                slider .setBounds (r.reduced (2));
            }
        };

        struct Strip
        {
            juce::Label       label;
            LabeledKnob       gain, pan;
            LabeledKnob       eqLow, eqMid, eqHigh;
            LabeledKnob       comp, drive, clip, dampen, depth, reverb;
            juce::TextButton  mute, solo;
        };

        static juce::String formatValue (Unit u, double v)
        {
            switch (u)
            {
                case Unit::Db:
                {
                    const juce::String sign = v >= 0 ? "+" : "";
                    return sign + juce::String (v, 1) + " dB";
                }
                case Unit::Percent:
                    return juce::String ((int) std::round (v * 100.0)) + " %";
                case Unit::Pan:
                {
                    const int pct = (int) std::round (std::abs (v) * 100.0);
                    if (pct == 0) return juce::String ("C");
                    return (v < 0 ? "L" : "R") + juce::String (pct);
                }
                case Unit::Depth:
                {
                    const int pct = (int) std::round (v * 100.0);
                    if (pct == 0) return juce::String ("0");
                    return (pct > 0 ? "+" : "") + juce::String (pct);
                }
                case Unit::Plain:
                    return juce::String (v, 2);
            }
            return {};
        }

        static juce::String tooltipForUnit (const juce::String& name, Unit u)
        {
            switch (u)
            {
                case Unit::Db:       return name + " (decibels)";
                case Unit::Percent:  return name + " (0-100%)";
                case Unit::Pan:      return name + " (L100 \u2190 C \u2192 R100)";
                case Unit::Depth:    return name + " ( \u2013100 front  \u2022  0 natural  \u2022  +100 back )";
                case Unit::Plain:    return name;
            }
            return name;
        }

        void attachKnob (LabeledKnob& lk, const juce::String& caption,
                         float lo, float hi, float step, float def,
                         std::atomic<float>* target, Unit unit)
        {
            auto& s = lk.slider;
            s.setRange (lo, hi, step);
            s.setValue (def, juce::dontSendNotification);
            if (target != nullptr)
                s.setValue (target->load (std::memory_order_relaxed),
                            juce::dontSendNotification);

            lk.caption.setText (caption, juce::dontSendNotification);
            lk.value  .setText (formatValue (unit, s.getValue()), juce::dontSendNotification);
            s.setTooltip (tooltipForUnit (caption, unit));

            s.onValueChange = [&lk, target, unit]()
            {
                if (target != nullptr)
                    target->store ((float) lk.slider.getValue(), std::memory_order_relaxed);
                lk.value.setText (formatValue (unit, lk.slider.getValue()),
                                  juce::dontSendNotification);
            };
            addAndMakeVisible (lk);
        }

        void layoutKnobRow (juce::Rectangle<int>& row,
                            std::initializer_list<LabeledKnob*> knobs,
                            int height = 58)
        {
            auto r = row.removeFromTop (height);
            const int n = (int) knobs.size();
            if (n == 0) return;
            const int w = r.getWidth() / n;
            int i = 0;
            for (auto* k : knobs)
            {
                k->setBounds (juce::Rectangle<int> (r.getX() + i * w, r.getY(), w, r.getHeight())
                                  .reduced (2));
                ++i;
            }
        }

        DrumBusMixer& mixer;
        std::array<Strip, (size_t) kNumDrumBuses> strips {};

        juce::Label  masterLabel;
        LabeledKnob  masterGain, masterReverbMix, masterReverbSize, masterReverbDamp;
    };
}
