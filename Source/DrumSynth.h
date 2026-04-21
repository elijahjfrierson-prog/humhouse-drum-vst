#pragma once

#include "DrumBusMixer.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>

// ============================================================================
// DrumSynth — acoustic physical-modelling drum engine.
//
// v1.2.0 replaced the old sine+noise toy with a per-voice modal+filtered-noise
// model tuned for acoustic punk / grunge / rock character:
//
//   * Pitched drums (kick / snare / toms) render as a stack of damped
//     sinusoidal shell modes, a fast pitch-swept body for the "thump", and
//     a short band-passed stick/beater click for the attack transient.
//   * Snares add a longer band-passed noise layer for the snare wires.
//   * Hi-hats / cymbals render as inharmonic metallic partials plus a
//     resonantly-band-passed noise bed whose decay distinguishes
//     closed / open / ride / crash / china.
//
// The engine is zero-alloc on the audio thread and still exposes the same
// public API (prepare / noteOn / renderIntoBuses) so the rest of the plugin
// didn't have to change. An atomic hit-event counter per bus lets the UI
// flash the drum that was just hit in the kit visualiser.
// ============================================================================
namespace aidrum
{
    class DrumSynth
    {
    public:
        static constexpr int kMaxVoices   = 24;
        static constexpr int kMaxModes    = 6;
        static constexpr int kNumBuses    = (int) Bus::NumDrumBuses;

        void prepare (double sampleRate)
        {
            sr = std::max (8000.0, sampleRate);
            for (auto& v : voices) v = Voice{};
        }

        void reset()
        {
            for (auto& v : voices) v.active = false;
        }

        // Velocity is 0..1. sampleOffset is the sample inside the current
        // block where the note should start.
        void noteOn (int midiNote, float velocity, int sampleOffset)
        {
            int idx = -1;
            for (int i = 0; i < kMaxVoices; ++i)
                if (! voices[i].active) { idx = i; break; }
            if (idx < 0)
            {
                int oldestIdx = 0, oldestAge = -1;
                for (int i = 0; i < kMaxVoices; ++i)
                    if (voices[i].sampleAge > oldestAge)
                        { oldestAge = voices[i].sampleAge; oldestIdx = i; }
                idx = oldestIdx;
            }

            const float v = juce::jlimit (0.05f, 1.0f, velocity);
            voices[idx].trigger (midiNote, v, sampleOffset, (float) sr);

            // UI hit indicator (atomic-safe from either thread).
            const int bus = busIndexForKind (voices[idx].kind);
            if (bus >= 0 && bus < kNumBuses)
            {
                busHitCount[(size_t) bus].fetch_add (1, std::memory_order_relaxed);
                busLastVel [(size_t) bus].store    (v,  std::memory_order_relaxed);
            }
        }

        void renderInto (juce::AudioBuffer<float>& buffer)
        {
            const int numSamples  = buffer.getNumSamples();
            const int numChannels = buffer.getNumChannels();
            if (numSamples <= 0 || numChannels <= 0) return;

            for (auto& v : voices)
            {
                if (! v.active) continue;
                const int start = juce::jlimit (0, numSamples, v.startSample);
                for (int s = start; s < numSamples; ++s)
                {
                    if (! v.active) break;
                    const float samp = v.tick() * masterGain;
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.addSample (ch, s, samp);
                }
                v.startSample = 0;
            }
        }

        // Render each active voice into its routed bus so the DrumBusMixer
        // can apply per-drum EQ / compression / reverb send.
        void renderIntoBuses (DrumBusMixer& mixer, int numSamples)
        {
            if (numSamples <= 0) return;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                auto* buf = mixer.busBuffer (busIndexForKind (v.kind));
                if (buf == nullptr) continue;
                const int chans = buf->getNumChannels();
                const int start = juce::jlimit (0, numSamples, v.startSample);
                for (int s = start; s < numSamples; ++s)
                {
                    if (! v.active) break;
                    const float samp = v.tick() * masterGain;
                    for (int ch = 0; ch < chans; ++ch)
                        buf->addSample (ch, s, samp);
                }
                v.startSample = 0;
            }
        }

        void setMasterGain (float g) { masterGain = juce::jlimit (0.0f, 1.0f, g); }

        // --- UI-facing hit indicator ---------------------------------------
        int   readAndResetHitCount (int bus)
        {
            if (bus < 0 || bus >= kNumBuses) return 0;
            return busHitCount[(size_t) bus].exchange (0, std::memory_order_relaxed);
        }
        float lastHitVelocity (int bus) const
        {
            if (bus < 0 || bus >= kNumBuses) return 0.0f;
            return busLastVel[(size_t) bus].load (std::memory_order_relaxed);
        }

    public:
        enum class Kind
        {
            Kick, SideStick, Snare, Clap,
            ClosedHat, PedalHat, OpenHat,
            LowTom, MidTom, HighTom,
            Crash, Ride, RideBell, China
        };

        // Maps a voice Kind to a DrumBusMixer bus index (0..7).
        static int busIndexForKind (Kind k) noexcept
        {
            switch (k)
            {
                case Kind::Kick:                             return (int) Bus::Kick;
                case Kind::SideStick:
                case Kind::Snare:
                case Kind::Clap:                             return (int) Bus::Snare;
                case Kind::LowTom:
                case Kind::MidTom:
                case Kind::HighTom:                          return (int) Bus::Toms;
                case Kind::ClosedHat:
                case Kind::PedalHat:                         return (int) Bus::ClosedHat;
                case Kind::OpenHat:                          return (int) Bus::OpenHat;
                case Kind::Ride:
                case Kind::RideBell:                         return (int) Bus::Ride;
                case Kind::Crash:                            return (int) Bus::Crash;
                case Kind::China:                            return (int) Bus::China;
            }
            return (int) Bus::Kick;
        }

        static int busIndexForNote (int midiNote) noexcept
        {
            return busIndexForKind (Voice::kindFromNote (midiNote));
        }

        // v1.4.0 — public re-export of the voice mapping so SampleKit can
        // route incoming MIDI notes without needing friend access.
        static Kind Voice_kindFromNote (int midiNote) noexcept
        {
            return Voice::kindFromNote (midiNote);
        }

    private:

        // ---- Helpers ------------------------------------------------------
        static float decayFromT60 (float t60Seconds, float sr)
        {
            // Per-sample amplitude multiplier such that after t60 seconds the
            // signal drops by ~60 dB. decay^(sr*t60) = 1e-3  ->  decay = 10^(-3/(sr*t60)).
            if (t60Seconds <= 1.0e-5f) return 0.0f;
            return std::pow (10.0f, -3.0f / (sr * t60Seconds));
        }

        struct Biquad
        {
            float b0=0, b1=0, b2=0, a1=0, a2=0;
            float z1=0, z2=0;

            void setBandpass (float freq, float q, float sr)
            {
                const float w0 = juce::MathConstants<float>::twoPi * juce::jlimit (10.0f, sr * 0.45f, freq) / sr;
                const float cw = std::cos (w0), sw = std::sin (w0);
                const float alpha = sw / (2.0f * juce::jmax (0.05f, q));
                const float a0 = 1.0f + alpha;
                b0 =  alpha / a0;
                b1 =  0.0f;
                b2 = -alpha / a0;
                a1 = -2.0f * cw / a0;
                a2 = (1.0f - alpha) / a0;
                z1 = z2 = 0.0f;
            }

            inline float process (float x)
            {
                const float y = b0 * x + z1;
                z1 = b1 * x + z2 - a1 * y;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        struct Mode
        {
            float amp      = 0.0f;
            float omega    = 0.0f;   // 2*pi*f / sr
            float phase    = 0.0f;
            float decayMul = 0.0f;   // per-sample amp multiplier

            inline float tick()
            {
                if (amp < 1.0e-5f) { amp = 0.0f; return 0.0f; }
                const float s = amp * std::sin (phase);
                phase += omega;
                if (phase > juce::MathConstants<float>::twoPi)
                    phase -= juce::MathConstants<float>::twoPi;
                amp *= decayMul;
                return s;
            }

            void setup (float freq, float amp0, float t60Sec, float sr, float phase0 = 0.0f)
            {
                omega    = juce::MathConstants<float>::twoPi * freq / sr;
                amp      = amp0;
                phase    = phase0;
                decayMul = decayFromT60 (t60Sec, sr);
            }
        };

        struct NoiseLayer
        {
            bool   active   = false;
            float  amp      = 0.0f;
            float  decayMul = 0.0f;
            Biquad bp;

            inline float tick (juce::Random& rng)
            {
                if (! active || amp < 1.0e-5f) { active = false; amp = 0.0f; return 0.0f; }
                const float n = (rng.nextFloat() * 2.0f - 1.0f);
                const float s = bp.process (n) * amp;
                amp *= decayMul;
                return s;
            }

            void setup (float freq, float q, float amp0, float t60Sec, float sr)
            {
                bp.setBandpass (freq, q, sr);
                amp      = amp0;
                decayMul = decayFromT60 (t60Sec, sr);
                active   = true;
            }
        };

        struct Voice
        {
            bool  active      = false;
            int   startSample = 0;
            int   sampleAge   = 0;
            float velocity    = 1.0f;
            Kind  kind        = Kind::Kick;
            float sr          = 48000.0f;
            float outGain     = 1.0f;
            float drive       = 1.0f;
            juce::Random rng  { (juce::int64) 0x5EEDDEAD };

            std::array<Mode, kMaxModes> modes {};
            int numModes = 0;

            // Pitch-swept body (kick / tom thump).
            bool  hasSweep = false;
            float sweepAmp = 0.0f, sweepDecayMul = 0.0f;
            float sweepFreq = 0.0f, sweepFreqEnd = 0.0f, sweepPitchMul = 1.0f;
            float sweepPhase = 0.0f;

            // Noise layers: short click + longer body/wire/shimmer.
            NoiseLayer click, body;

            // Rough cut-off once everything has decayed below audibility.
            int maxLifeSamples = 0;

            void trigger (int midiNote, float vel, int offset, float sampleRate)
            {
                active        = true;
                startSample   = offset;
                sampleAge     = 0;
                velocity      = vel;
                sr            = sampleRate;
                rng.setSeedRandomly();

                // Reset modes / noise.
                for (auto& m : modes) m = {};
                numModes = 0;
                hasSweep = false;
                click    = {};
                body     = {};
                drive    = 1.0f;
                outGain  = 1.0f;

                kind = kindFromNote (midiNote);

                switch (kind)
                {
                    case Kind::Kick:           setupKick (midiNote); break;
                    case Kind::SideStick:      setupSideStick();     break;
                    case Kind::Snare:          setupSnare (midiNote); break;
                    case Kind::Clap:           setupClap();          break;
                    case Kind::ClosedHat:      setupHat (false, 0.08f);  break;
                    case Kind::PedalHat:       setupHat (false, 0.14f);  break;
                    case Kind::OpenHat:        setupHat (true,  0.45f);  break;
                    case Kind::LowTom:         setupTom (110.0f, 0.55f); break;
                    case Kind::MidTom:         setupTom (160.0f, 0.45f); break;
                    case Kind::HighTom:        setupTom (220.0f, 0.38f); break;
                    case Kind::Crash:          setupCrash();         break;
                    case Kind::Ride:           setupRide();          break;
                    case Kind::RideBell:       setupRideBell();      break;
                    case Kind::China:          setupChina();         break;
                }

                // Safety cut-off so a huge-Q voice can't run forever.
                maxLifeSamples = (int) (sr * 4.0f);
            }

            // ---- Per-kind setups -----------------------------------------
            // All amplitudes are relative; final output is multiplied by
            // velocity * outGain at the end of tick().

            void setupKick (int midi)
            {
                // Acoustic rock / grunge punch — beater click + pitch-swept
                // body + two shell modes. midi 35 = acoustic (boomier), 36 = tighter.
                const bool boomy = (midi == 35);

                // Shell modes (body resonances).
                modes[0].setup (boomy ? 62.0f  : 74.0f, 0.75f, boomy ? 0.90f : 0.65f, sr);
                modes[1].setup (boomy ? 128.0f : 148.0f, 0.22f, boomy ? 0.35f : 0.25f, sr);
                modes[2].setup (boomy ? 210.0f : 240.0f, 0.10f, boomy ? 0.18f : 0.14f, sr);
                numModes = 3;

                // Pitch sweep simulates the head tensioning in the first ~30ms.
                hasSweep      = true;
                sweepFreq     = boomy ? 185.0f : 210.0f;
                sweepFreqEnd  = boomy ?  52.0f :  62.0f;
                sweepPitchMul = decayFromT60 (0.022f, sr);
                sweepAmp      = 0.85f;
                sweepDecayMul = decayFromT60 (boomy ? 0.32f : 0.22f, sr);
                sweepPhase    = 0.0f;

                // Beater click — short, punchy, body-mic frequency.
                click.setup (2200.0f, 1.1f, 0.35f, 0.012f, sr);

                // Low shell air thump as a second noise layer.
                body.setup (140.0f, 0.9f, 0.20f, 0.08f, sr);

                drive   = 1.25f;
                outGain = 1.35f;
            }

            void setupSideStick()
            {
                // Rim click — sharp wooden "tock".
                modes[0].setup (900.0f, 0.6f, 0.04f, sr);
                modes[1].setup (1600.0f, 0.3f, 0.03f, sr);
                numModes = 2;
                click.setup (1600.0f, 1.8f, 0.9f, 0.010f, sr);
                outGain = 0.95f;
            }

            void setupSnare (int midi)
            {
                // Acoustic-cracking rock / grunge snare: shell tone + snare wires +
                // stick-head slap transient.
                const bool tight = (midi == 40);  // GM electric snare = tighter/brighter

                // Shell modes (fundamental + 1.62x overtone + sub thump).
                modes[0].setup (tight ? 260.0f : 210.0f, 0.55f, 0.18f, sr);
                modes[1].setup (tight ? 420.0f : 340.0f, 0.22f, 0.10f, sr);
                modes[2].setup (tight ? 180.0f : 150.0f, 0.35f, 0.14f, sr);
                numModes = 3;

                // Stick-head slap click.
                click.setup (tight ? 4200.0f : 3200.0f, 0.9f, 0.45f, 0.006f, sr);

                // Snare wires — the signature buzz.
                body.setup (tight ? 7200.0f : 6200.0f, 0.55f, 0.95f,
                            tight ? 0.14f   : 0.22f, sr);

                drive   = 1.15f;
                outGain = 1.10f;
            }

            void setupClap()
            {
                // Single-layer hand-clap — bright band-passed noise burst.
                click.setup (1100.0f, 1.2f, 0.9f, 0.035f, sr);
                body .setup (2200.0f, 0.6f, 0.4f, 0.12f,  sr);
                outGain = 0.95f;
            }

            void setupHat (bool openHat, float bodyT60)
            {
                // Hi-hat: stick chick + multi-peak inharmonic shimmer.
                // Six inharmonic metallic sines give the "squirty" metal colour
                // without a dedicated HPF — the high freqs already dominate.
                static constexpr float kRatios[6] = { 2.00f, 2.87f, 3.47f, 4.83f, 6.12f, 7.45f };
                const float base = openHat ? 1400.0f : 1800.0f;
                for (int i = 0; i < 6; ++i)
                    modes[(size_t) i].setup (base * kRatios[i],
                                             openHat ? 0.18f : 0.10f,
                                             openHat ? bodyT60 * 0.55f : bodyT60 * 0.35f,
                                             sr, (float) i * 0.37f);
                numModes = 6;

                // Bright noise bed — the "sss".
                click.setup (openHat ? 7500.0f : 9000.0f, 0.8f, openHat ? 0.55f : 0.65f,
                             openHat ? 0.035f : 0.020f, sr);
                body .setup (openHat ? 6000.0f : 8000.0f, 0.5f, openHat ? 0.45f : 0.25f,
                             bodyT60, sr);

                outGain = openHat ? 0.55f : 0.50f;
            }

            void setupTom (float fundamental, float t60Sec)
            {
                // Two-mode shell + a subtle pitch bend and a stick click.
                modes[0].setup (fundamental,          0.85f, t60Sec,        sr);
                modes[1].setup (fundamental * 1.62f,  0.25f, t60Sec * 0.4f, sr);
                modes[2].setup (fundamental * 0.62f,  0.18f, t60Sec * 0.7f, sr);
                numModes = 3;

                hasSweep      = true;
                sweepFreq     = fundamental * 1.22f;
                sweepFreqEnd  = fundamental;
                sweepPitchMul = decayFromT60 (0.055f, sr);
                sweepAmp      = 0.35f;
                sweepDecayMul = decayFromT60 (t60Sec * 0.6f, sr);

                click.setup (3000.0f, 1.0f, 0.28f, 0.005f, sr);
                outGain = 1.05f;
            }

            void setupCrash()
            {
                // Dense inharmonic metallic modes over ~6 partials.
                static constexpr float kPart[6] = { 780.0f, 1120.0f, 1680.0f, 2430.0f, 3310.0f, 4200.0f };
                for (int i = 0; i < 6; ++i)
                    modes[(size_t) i].setup (kPart[i], 0.22f,
                                             2.0f - (float) i * 0.20f,
                                             sr, (float) i * 0.29f);
                numModes = 6;
                click.setup (4500.0f, 0.9f, 0.45f, 0.018f, sr);
                body .setup (5500.0f, 0.55f, 0.70f, 1.60f, sr);
                outGain = 0.55f;
            }

            void setupRide()
            {
                // Ride: strong bell mode + shimmer noise, long decay.
                static constexpr float kPart[5] = { 2400.0f, 3200.0f, 4100.0f, 5300.0f, 6600.0f };
                modes[0].setup (2400.0f, 0.45f, 1.20f, sr);
                for (int i = 1; i < 5; ++i)
                    modes[(size_t) i].setup (kPart[i], 0.15f,
                                             1.10f - (float) i * 0.10f,
                                             sr, (float) i * 0.41f);
                numModes = 5;
                click.setup (5500.0f, 1.1f, 0.35f, 0.006f, sr);
                body .setup (4800.0f, 0.7f, 0.35f, 0.90f, sr);
                outGain = 0.55f;
            }

            void setupRideBell()
            {
                // Pingy bell tone.
                modes[0].setup (2400.0f, 0.85f, 0.85f, sr);
                modes[1].setup (3800.0f, 0.35f, 0.45f, sr);
                modes[2].setup (5600.0f, 0.22f, 0.35f, sr);
                numModes = 3;
                click.setup (5200.0f, 1.3f, 0.30f, 0.004f, sr);
                outGain = 0.70f;
            }

            void setupChina()
            {
                // Trashy, lower-pitched splashy cymbal.
                static constexpr float kPart[6] = { 620.0f, 940.0f, 1380.0f, 1900.0f, 2660.0f, 3450.0f };
                for (int i = 0; i < 6; ++i)
                    modes[(size_t) i].setup (kPart[i], 0.26f,
                                             1.40f - (float) i * 0.12f,
                                             sr, (float) i * 0.33f);
                numModes = 6;
                click.setup (3600.0f, 0.75f, 0.50f, 0.020f, sr);
                body .setup (3800.0f, 0.35f, 0.85f, 1.10f, sr);
                outGain = 0.55f;
            }

            // ---- Audio thread tick ---------------------------------------
            inline float tick()
            {
                if (! active) return 0.0f;
                if (sampleAge >= maxLifeSamples) { active = false; return 0.0f; }

                float s = 0.0f;

                // Modal body
                float modalSum = 0.0f;
                for (int i = 0; i < numModes; ++i)
                    modalSum += modes[(size_t) i].tick();
                s += modalSum;

                // Pitch-swept tone (kick thump / tom bend)
                if (hasSweep && sweepAmp > 1.0e-5f)
                {
                    const float omega = juce::MathConstants<float>::twoPi * sweepFreq / sr;
                    sweepPhase += omega;
                    if (sweepPhase > juce::MathConstants<float>::twoPi)
                        sweepPhase -= juce::MathConstants<float>::twoPi;
                    const float sw = std::sin (sweepPhase) * sweepAmp;
                    s += sw;
                    sweepAmp  *= sweepDecayMul;
                    sweepFreq  = sweepFreqEnd + (sweepFreq - sweepFreqEnd) * sweepPitchMul;
                }
                else
                {
                    hasSweep = false;
                }

                // Noise layers
                s += click.tick (rng);
                s += body .tick (rng);

                // Per-voice drive adds a touch of harmonic saturation for the kick/snare.
                if (drive > 1.0f) s = std::tanh (s * drive) / drive;

                ++sampleAge;
                const float out = s * velocity * outGain;

                // Auto-retire when everything faded.
                if (sampleAge > (int) (sr * 0.05f)
                    && std::abs (out) < 1.0e-5f
                    && ! hasSweep
                    && ! click.active
                    && ! body.active)
                {
                    bool anyMode = false;
                    for (int i = 0; i < numModes; ++i)
                        if (modes[(size_t) i].amp > 1.0e-5f) { anyMode = true; break; }
                    if (! anyMode) active = false;
                }

                return out;
            }

            static Kind kindFromNote (int n)
            {
                switch (n)
                {
                    case 35: case 36: return Kind::Kick;
                    case 37:          return Kind::SideStick;
                    case 38: case 40: return Kind::Snare;
                    case 39:          return Kind::Clap;
                    case 42:          return Kind::ClosedHat;
                    case 44:          return Kind::PedalHat;
                    case 46:          return Kind::OpenHat;
                    case 41: case 43: return Kind::LowTom;
                    case 45: case 47: return Kind::MidTom;
                    case 48: case 50: return Kind::HighTom;
                    case 49: case 57: case 55: return Kind::Crash;
                    case 51: case 59:          return Kind::Ride;
                    case 53:                   return Kind::RideBell;
                    case 52:                   return Kind::China;
                    default:
                        if (n < 45)        return Kind::LowTom;
                        if (n < 50)        return Kind::MidTom;
                        return Kind::HighTom;
                }
            }
        };

        double sr         = 48000.0;
        float  masterGain = 0.85f;
        std::array<Voice, kMaxVoices> voices {};

        // UI hit indicator (per drum bus).
        std::array<std::atomic<int>,   (size_t) kNumBuses> busHitCount {};
        std::array<std::atomic<float>, (size_t) kNumBuses> busLastVel  {};
    };
}
