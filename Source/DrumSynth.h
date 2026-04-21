#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>

// ============================================================================
// DrumSynth — lightweight analog-style polyphonic drum synthesizer.
// Renders audible kick / snare / hat / tom / cymbal / clap / sidestick voices
// from GM note numbers so HumHouse Drums' Standalone makes sound on its own
// without needing a sampler. Timbre per kit comes naturally because the kit
// profile already remaps GM notes (e.g. TR-808 routes kick to GM 35 = deep
// sub) and each GM number maps to a voice tuning below.
// ============================================================================
namespace aidrum
{
    class DrumSynth
    {
    public:
        static constexpr int kMaxVoices = 24;

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
                // Steal the voice that has been running longest.
                int oldestIdx = 0;
                int oldestAge = -1;
                for (int i = 0; i < kMaxVoices; ++i)
                    if (voices[i].sampleAge > oldestAge)
                        { oldestAge = voices[i].sampleAge; oldestIdx = i; }
                idx = oldestIdx;
            }
            voices[idx].trigger (midiNote, juce::jlimit (0.05f, 1.0f, velocity), sampleOffset);
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
                    const float samp = v.tick ((float) sr) * masterGain;
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.addSample (ch, s, samp);
                }
                v.startSample = 0;
            }
        }

        void setMasterGain (float g) { masterGain = juce::jlimit (0.0f, 1.0f, g); }

    private:
        enum class Kind
        {
            Kick, SideStick, Snare, Clap,
            ClosedHat, PedalHat, OpenHat,
            LowTom, MidTom, HighTom,
            Crash, Ride, RideBell, China
        };

        struct Voice
        {
            bool  active      = false;
            int   startSample = 0;
            int   sampleAge   = 0;
            float velocity    = 1.0f;
            Kind  kind        = Kind::Kick;
            float phase       = 0.0f;
            float noiseHp     = 0.0f;
            float noiseLp     = 0.0f;
            juce::Random rng  { (juce::int64) 0x5EEDDEAD };

            // Per-voice tuning derived from GM note + Kind.
            float freqStart   = 120.0f;
            float freqEnd     =  50.0f;
            float pitchTau    = 0.03f;   // seconds
            float ampTau      = 0.20f;   // seconds
            float totalDur    = 0.60f;   // seconds
            float toneMix     = 0.7f;    // sine vs noise
            float hpAmount    = 0.0f;    // how much we highpass the noise
            float drive       = 1.0f;    // soft distortion on tone

            void trigger (int midiNote, float vel, int offset)
            {
                active      = true;
                startSample = offset;
                sampleAge   = 0;
                velocity    = vel;
                phase       = 0.0f;
                noiseHp     = 0.0f;
                noiseLp     = 0.0f;
                rng.setSeedRandomly();

                kind = kindFromNote (midiNote);

                switch (kind)
                {
                    case Kind::Kick:
                        // GM 35 = Acoustic Bass Drum (boomy),
                        // GM 36 = Bass Drum 1 (tighter).
                        freqStart = (midiNote == 35) ? 155.0f : 130.0f;
                        freqEnd   = (midiNote == 35) ?  40.0f :  48.0f;
                        pitchTau  = 0.05f;
                        ampTau    = (midiNote == 35) ? 0.30f : 0.20f;
                        totalDur  = ampTau * 5.0f;
                        toneMix   = 0.88f;
                        hpAmount  = 0.0f;
                        drive     = 2.0f;
                        break;

                    case Kind::SideStick:
                        freqStart = 900.0f; freqEnd = 700.0f;
                        pitchTau  = 0.004f;
                        ampTau    = 0.015f; totalDur = 0.08f;
                        toneMix   = 0.35f; hpAmount = 0.9f; drive = 1.0f;
                        break;

                    case Kind::Snare:
                        // GM 38 = Acoustic (fat wood), GM 40 = Electric (tight/bright).
                        freqStart = (midiNote == 40) ? 260.0f : 210.0f;
                        freqEnd   = (midiNote == 40) ? 220.0f : 170.0f;
                        pitchTau  = 0.02f;
                        ampTau    = (midiNote == 40) ? 0.10f : 0.14f;
                        totalDur  = ampTau * 5.0f;
                        toneMix   = 0.35f; hpAmount = 0.6f; drive = 1.2f;
                        break;

                    case Kind::Clap:
                        freqStart = 1200.0f; freqEnd = 1200.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.07f; totalDur = 0.25f;
                        toneMix   = 0.05f; hpAmount = 0.8f; drive = 1.0f;
                        break;

                    case Kind::ClosedHat:
                        freqStart = 6000.0f; freqEnd = 6000.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.035f; totalDur = 0.12f;
                        toneMix   = 0.0f; hpAmount = 0.95f; drive = 1.0f;
                        break;

                    case Kind::PedalHat:
                        freqStart = 5000.0f; freqEnd = 5000.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.05f; totalDur = 0.18f;
                        toneMix   = 0.0f; hpAmount = 0.9f; drive = 1.0f;
                        break;

                    case Kind::OpenHat:
                        freqStart = 7000.0f; freqEnd = 7000.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.20f; totalDur = 0.55f;
                        toneMix   = 0.0f; hpAmount = 0.95f; drive = 1.0f;
                        break;

                    case Kind::LowTom:
                        freqStart = 130.0f; freqEnd = 90.0f;
                        pitchTau  = 0.08f;
                        ampTau    = 0.35f; totalDur = 1.2f;
                        toneMix   = 0.8f;  hpAmount = 0.0f; drive = 1.4f;
                        break;

                    case Kind::MidTom:
                        freqStart = 180.0f; freqEnd = 135.0f;
                        pitchTau  = 0.07f;
                        ampTau    = 0.28f; totalDur = 1.0f;
                        toneMix   = 0.8f;  hpAmount = 0.0f; drive = 1.4f;
                        break;

                    case Kind::HighTom:
                        freqStart = 240.0f; freqEnd = 180.0f;
                        pitchTau  = 0.06f;
                        ampTau    = 0.22f; totalDur = 0.9f;
                        toneMix   = 0.8f;  hpAmount = 0.0f; drive = 1.4f;
                        break;

                    case Kind::Crash:
                        freqStart = 9000.0f; freqEnd = 9000.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.9f; totalDur = 2.8f;
                        toneMix   = 0.0f; hpAmount = 0.97f; drive = 1.0f;
                        break;

                    case Kind::Ride:
                        freqStart = 5500.0f; freqEnd = 5500.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.35f; totalDur = 1.2f;
                        toneMix   = 0.08f; hpAmount = 0.92f; drive = 1.0f;
                        break;

                    case Kind::RideBell:
                        freqStart = 2200.0f; freqEnd = 2200.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.30f; totalDur = 1.0f;
                        toneMix   = 0.5f;  hpAmount = 0.5f; drive = 1.2f;
                        break;

                    case Kind::China:
                        freqStart = 8000.0f; freqEnd = 8000.0f;
                        pitchTau  = 0.001f;
                        ampTau    = 0.55f; totalDur = 1.8f;
                        toneMix   = 0.0f; hpAmount = 0.85f; drive = 1.0f;
                        break;
                }
            }

            float tick (float sampleRateHz)
            {
                const float t = (float) sampleAge / sampleRateHz;
                if (t >= totalDur) { active = false; return 0.0f; }

                // Amplitude envelope (exponential).
                const float amp = std::exp (-t / std::max (0.001f, ampTau));

                // Pitch envelope.
                const float freq = freqEnd + (freqStart - freqEnd)
                                      * std::exp (-t / std::max (0.001f, pitchTau));

                // Tone component (soft-clipped sine for a bit of body).
                const float twoPi = juce::MathConstants<float>::twoPi;
                phase += twoPi * freq / sampleRateHz;
                if (phase > twoPi) phase -= twoPi;
                float tone = std::sin (phase) * drive;
                tone = std::tanh (tone);

                // Noise component, optionally high-passed (single-pole).
                float noise = rng.nextFloat() * 2.0f - 1.0f;
                // One-pole smoother (low-pass state) then subtract for high-pass.
                noiseLp += 0.15f * (noise - noiseLp);
                const float hpNoise = noise - noiseLp;
                const float noiseOut = juce::jmap (hpAmount, 0.0f, 1.0f, noise, hpNoise);

                const float mix = toneMix * tone + (1.0f - toneMix) * noiseOut;
                const float outSample = mix * amp * velocity;

                ++sampleAge;
                return outSample;
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
                        // Anything else treated as a tom in the middle octave.
                        if (n < 45)        return Kind::LowTom;
                        if (n < 50)        return Kind::MidTom;
                        return Kind::HighTom;
                }
            }
        };

        double sr         = 48000.0;
        float  masterGain = 0.85f;
        std::array<Voice, kMaxVoices> voices {};
    };
}
