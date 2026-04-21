#pragma once

#include "DrumBusMixer.h"
#include "GothicLookAndFeel.h"

#include <JuceHeader.h>

#include <array>
#include <functional>

// ============================================================================
// MixerPanel — MODO-Drum / Logic-Kit-Designer-style channel-strip view.
// Eight drum buses (Kick / Snare / Toms / Cl Hat / Op Hat / Ride / Crash /
// China) plus a Master strip. Each strip exposes: 3-band EQ, one-knob
// Compressor, Drive, Clip ceiling, Dampen low-pass, Reverb send, Pan, Gain,
// Mute, Solo. Master strip exposes: Gain, Reverb Mix, Reverb Size, Damping.
// ============================================================================
namespace aidrum
{
    class MixerPanel : public juce::Component
    {
    public:
        explicit MixerPanel (DrumBusMixer& mixerIn) : mixer (mixerIn)
        {
            for (int i = 0; i < kNumDrumBuses; ++i)
            {
                auto& s = strips[(size_t) i];
                s.label.setText (busLabel (i), juce::dontSendNotification);
                s.label.setJustificationType (juce::Justification::centred);
                s.label.setColour (juce::Label::textColourId,
                                   juce::Colour (aidrum::GothicPalette::kBone));
                s.label.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
                addAndMakeVisible (s.label);

                auto& p = mixer.params_ref (i);
                attachKnob (s.gain,       -60.0f, 12.0f, 0.1f,  0.0f,     "GAIN",   &p.gainDb);
                attachKnob (s.pan,        -1.0f,  1.0f,  0.01f, 0.0f,     "PAN",    &p.pan);
                attachKnob (s.eqLow,      -12.0f, 12.0f, 0.1f,  0.0f,     "LOW",    &p.eqLowDb);
                attachKnob (s.eqMid,      -12.0f, 12.0f, 0.1f,  0.0f,     "MID",    &p.eqMidDb);
                attachKnob (s.eqHigh,     -12.0f, 12.0f, 0.1f,  0.0f,     "HIGH",   &p.eqHighDb);
                attachKnob (s.comp,        0.0f,  1.0f,  0.01f, 0.0f,     "COMP",   &p.compAmount);
                attachKnob (s.drive,       0.0f,  1.0f,  0.01f, 0.0f,     "DRIVE",  &p.drive);
                attachKnob (s.clip,        0.05f, 1.0f,  0.01f, 1.0f,     "CLIP",   &p.clipCeiling);
                attachKnob (s.dampen,    500.0f, 20000.0f, 10.0f, 20000.0f, "DAMP", &p.dampenHz);
                attachKnob (s.reverb,      0.0f,  1.0f,  0.01f, 0.0f,     "REV",    &p.reverbSend);

                s.mute.setButtonText ("M");
                s.mute.setClickingTogglesState (true);
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
                                   juce::Colour (aidrum::GothicPalette::kBone));
            masterLabel.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
            addAndMakeVisible (masterLabel);

            auto& m = mixer.master_ref();
            attachKnob (masterGain,      -24.0f, 12.0f, 0.1f,  0.0f,  "GAIN",       &m.gainDb);
            attachKnob (masterReverbMix,   0.0f,  1.0f, 0.01f, 0.22f, "REV MIX",    &m.reverbMix);
            attachKnob (masterReverbSize,  0.0f,  1.0f, 0.01f, 0.55f, "REV SIZE",   &m.reverbSize);
            attachKnob (masterReverbDamp,  0.0f,  1.0f, 0.01f, 0.50f, "REV DAMP",   &m.reverbDamp);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (aidrum::GothicPalette::kInk));
            g.fillRect (b);

            auto header = b.removeFromTop (30.0f);
            g.setColour (juce::Colour (aidrum::GothicPalette::kPanel));
            g.fillRect (header);
            g.setColour (juce::Colour (aidrum::GothicPalette::kAccent));
            g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
            g.drawText (juce::String::fromUTF8 ("\u2020 MIXER \u2020  \u2014  PER-DRUM EQ / COMP / DRIVE / CLIP / DAMPEN / REVERB"),
                        header, juce::Justification::centred);

            // Draw strip backgrounds
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
                    // Accent master strip
                    g.setColour (juce::Colour (aidrum::GothicPalette::kAccent).withAlpha (0.25f));
                    g.drawRoundedRectangle (sb, 10.0f, 2.0f);
                }
            }
        }

        void resized() override
        {
            auto b = getLocalBounds();
            b.removeFromTop (30); // header

            const int totalStrips = kNumDrumBuses + 1;
            const int stripW = b.getWidth() / totalStrips;

            for (int i = 0; i < kNumDrumBuses; ++i)
            {
                auto& s = strips[(size_t) i];
                auto r = juce::Rectangle<int> (b.getX() + i * stripW, b.getY(),
                                               stripW - 2, b.getHeight()).reduced (6);

                s.label.setBounds (r.removeFromTop (22));

                // Knob rows: EQ (L M H), COMP/DRIVE/CLIP/DAMP, REV, PAN, M/S, GAIN
                layoutKnobRow (r, { &s.eqLow, &s.eqMid, &s.eqHigh });
                layoutKnobRow (r, { &s.comp, &s.drive });
                layoutKnobRow (r, { &s.clip, &s.dampen });
                layoutKnobRow (r, { &s.reverb, &s.pan });

                auto ms = r.removeFromTop (22);
                s.mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (3, 0));
                s.solo.setBounds (ms.reduced (3, 0));

                s.gain.setBounds (r.reduced (4, 4));
            }

            // Master strip
            auto r = juce::Rectangle<int> (b.getX() + kNumDrumBuses * stripW, b.getY(),
                                           stripW - 2, b.getHeight()).reduced (6);
            masterLabel.setBounds (r.removeFromTop (22));
            layoutKnobRow (r, { &masterReverbMix, &masterReverbSize });
            layoutKnobRow (r, { &masterReverbDamp });
            masterGain.setBounds (r.reduced (4, 4));
        }

    private:
        struct Strip
        {
            juce::Label       label;
            juce::Slider      gain, pan;
            juce::Slider      eqLow, eqMid, eqHigh;
            juce::Slider      comp, drive, clip, dampen, reverb;
            juce::TextButton  mute, solo;
        };

        void attachKnob (juce::Slider& s, float lo, float hi, float step, float def,
                         const juce::String& tip,
                         std::atomic<float>* target)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            s.setRange (lo, hi, step);
            s.setValue (def, juce::dontSendNotification);
            if (target != nullptr)
                s.setValue (target->load (std::memory_order_relaxed),
                            juce::dontSendNotification);
            s.setTooltip (tip);
            s.setColour (juce::Slider::rotarySliderFillColourId,
                         juce::Colour (aidrum::GothicPalette::kAccent));
            s.setColour (juce::Slider::rotarySliderOutlineColourId,
                         juce::Colour (aidrum::GothicPalette::kPanelEdge));
            s.onValueChange = [&s, target]()
            {
                if (target != nullptr)
                    target->store ((float) s.getValue(), std::memory_order_relaxed);
            };
            addAndMakeVisible (s);
        }

        void layoutKnobRow (juce::Rectangle<int>& row,
                            std::initializer_list<juce::Slider*> knobs)
        {
            auto r = row.removeFromTop (44);
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
        juce::Slider masterGain, masterReverbMix, masterReverbSize, masterReverbDamp;
    };
}
