#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>

// ============================================================================
// DrumBusMixer — per-drum mixer with independent channel strips inspired by
// MODO Drum / Logic Drum Kit Designer. Each bus has its own EQ, compressor,
// distortion, clipper, dampen filter, pan, fader, mute/solo, and reverb send.
// A dedicated Master bus applies a global reverb return + final limiter.
// ============================================================================
namespace aidrum
{
    enum class Bus : int
    {
        Kick = 0,
        Snare,
        Toms,
        ClosedHat,
        OpenHat,
        Ride,
        Crash,
        China,
        NumDrumBuses   // sentinel — 8
    };

    static constexpr int kNumDrumBuses = (int) Bus::NumDrumBuses;

    // Friendly labels for the UI (matches the Bus enum order).
    inline const char* busLabel (int i) noexcept
    {
        switch (i)
        {
            case 0: return "KICK";
            case 1: return "SNARE";
            case 2: return "TOMS";
            case 3: return "CL HAT";
            case 4: return "OP HAT";
            case 5: return "RIDE";
            case 6: return "CRASH";
            case 7: return "CHINA";
            default: return "BUS";
        }
    }

    // Per-bus parameters — all atomics so the UI can set them without locks.
    struct BusParams
    {
        std::atomic<float> gainDb      { 0.0f };      // -60..+12
        std::atomic<float> pan         { 0.0f };      // -1..+1
        std::atomic<bool>  mute        { false };
        std::atomic<bool>  solo        { false };

        // 3-band semi-parametric EQ (dB)
        std::atomic<float> eqLowDb     { 0.0f };
        std::atomic<float> eqMidDb     { 0.0f };
        std::atomic<float> eqHighDb    { 0.0f };

        // Compressor
        std::atomic<float> compAmount  { 0.0f };      // 0..1 (drives thresh/ratio/makeup)

        // Distortion drive
        std::atomic<float> drive       { 0.0f };      // 0..1

        // Clipper ceiling (0..1)  — 1 = off, 0.1 = very hard ceiling
        std::atomic<float> clipCeiling { 1.0f };

        // Dampen (0..1, 0 = bright/open, 1 = fully dampened). Internally maps
        // to a one-pole LP cutoff between 20 kHz and 500 Hz on a log curve.
        std::atomic<float> dampen      { 0.0f };

        // Reverb send (0..1)
        std::atomic<float> reverbSend  { 0.0f };

        // Depth (-1..+1). Negative = in-your-face (drier, brighter, louder).
        // Positive = further back (more reverb send, LP air-filter, quieter).
        std::atomic<float> depth       { 0.0f };
    };

    struct MasterParams
    {
        std::atomic<float> gainDb      { 0.0f };
        std::atomic<float> reverbMix   { 0.22f };     // reverb return level
        std::atomic<float> reverbSize  { 0.55f };
        std::atomic<float> reverbDamp  { 0.50f };
    };

    class DrumBusMixer
    {
    public:
        void prepare (double sampleRate, int /*samplesPerBlock*/, int numChannels)
        {
            sr = std::max (8000.0, sampleRate);
            channels = juce::jlimit (1, 2, numChannels);

            for (auto& b : buses)
                b.setSize (channels, 0, false, false, true);

            reverbBuffer.setSize (channels, 0, false, false, true);

            for (auto& s : states)
                s.reset();

            reverb.reset();
            juce::Reverb::Parameters p;
            p.roomSize = master.reverbSize.load();
            p.damping  = master.reverbDamp.load();
            p.wetLevel = 1.0f;
            p.dryLevel = 0.0f;
            p.width    = 1.0f;
            reverb.setParameters (p);
            reverb.setSampleRate (sr);
        }

        void reset()
        {
            for (auto& s : states) s.reset();
            reverb.reset();
        }

        // Called by PluginProcessor before the synth renders voices. Ensures
        // each bus buffer has the right number of samples and is silent.
        void beginBlock (int numSamples)
        {
            for (auto& b : buses)
            {
                if (b.getNumSamples() != numSamples || b.getNumChannels() != channels)
                    b.setSize (channels, numSamples, false, false, true);
                b.clear();
            }
            if (reverbBuffer.getNumSamples() != numSamples || reverbBuffer.getNumChannels() != channels)
                reverbBuffer.setSize (channels, numSamples, false, false, true);
            reverbBuffer.clear();
        }

        // Raw pointer to a bus buffer so the synth can write into it.
        juce::AudioBuffer<float>* busBuffer (int idx)
        {
            if (idx < 0 || idx >= kNumDrumBuses) return nullptr;
            return &buses[(size_t) idx];
        }

        // Applies DSP per bus, sums into the plugin's output buffer, adds
        // master reverb return.
        void process (juce::AudioBuffer<float>& output)
        {
            const int n = output.getNumSamples();
            const int outCh = output.getNumChannels();
            if (n <= 0 || outCh <= 0) return;

            // Determine if any solo is active.
            bool anySolo = false;
            for (auto& p : params)
                if (p.solo.load (std::memory_order_relaxed)) { anySolo = true; break; }

            for (int i = 0; i < kNumDrumBuses; ++i)
            {
                auto& buf = buses[(size_t) i];
                auto& p   = params[(size_t) i];
                auto& st  = states[(size_t) i];
                if (buf.getNumSamples() != n) continue;

                const bool muted = p.mute.load (std::memory_order_relaxed)
                                || (anySolo && ! p.solo.load (std::memory_order_relaxed));
                if (muted) continue;

                // --- EQ (3-band one-pole shelves / simple peaking) ---------
                const float low  = p.eqLowDb .load (std::memory_order_relaxed);
                const float mid  = p.eqMidDb .load (std::memory_order_relaxed);
                const float high = p.eqHighDb.load (std::memory_order_relaxed);
                processEq (buf, st, low, mid, high);

                // --- Compressor -------------------------------------------
                processComp (buf, st, p.compAmount.load (std::memory_order_relaxed));

                // --- Distortion / drive -----------------------------------
                const float drv = p.drive.load (std::memory_order_relaxed);
                if (drv > 0.001f)
                {
                    const float g = 1.0f + drv * 9.0f; // 1..10x
                    for (int c = 0; c < buf.getNumChannels(); ++c)
                    {
                        auto* d = buf.getWritePointer (c);
                        for (int s = 0; s < n; ++s)
                            d[s] = std::tanh (d[s] * g) * (1.0f / std::tanh (g));
                    }
                }

                // --- Dampen (0..1, one-pole LP with log-swept cutoff) ------
                const float damp01 = juce::jlimit (0.0f, 1.0f,
                                        p.dampen.load (std::memory_order_relaxed));
                if (damp01 > 0.005f)
                {
                    const float cutoff = 20000.0f * std::pow (500.0f / 20000.0f, damp01);
                    const float a = std::exp (-juce::MathConstants<float>::twoPi
                                            * cutoff / (float) sr);
                    for (int c = 0; c < buf.getNumChannels(); ++c)
                    {
                        auto* d = buf.getWritePointer (c);
                        float& z = st.dampenZ[(size_t) c];
                        for (int s = 0; s < n; ++s)
                        {
                            z = (1.0f - a) * d[s] + a * z;
                            d[s] = z;
                        }
                    }
                }

                // --- Depth (front/back 3-D positioning) --------------------
                // depthPos > 0 = pushed back: air-LP + gain cut.
                // depthPos < 0 = in-your-face: gentle HP to clear mud.
                const float depthPos = juce::jlimit (-1.0f, 1.0f,
                                           p.depth.load (std::memory_order_relaxed));
                if (depthPos > 0.01f)
                {
                    const float airCutoff = juce::jmap (depthPos, 0.0f, 1.0f, 18000.0f, 2800.0f);
                    const float a = std::exp (-juce::MathConstants<float>::twoPi
                                            * airCutoff / (float) sr);
                    for (int c = 0; c < buf.getNumChannels(); ++c)
                    {
                        auto* d = buf.getWritePointer (c);
                        float& z = st.depthLpZ[(size_t) c];
                        for (int s = 0; s < n; ++s)
                        {
                            z = (1.0f - a) * d[s] + a * z;
                            d[s] = z;
                        }
                    }
                }
                else if (depthPos < -0.01f)
                {
                    const float hpCutoff = juce::jmap (-depthPos, 0.0f, 1.0f, 30.0f, 260.0f);
                    const float a = std::exp (-juce::MathConstants<float>::twoPi
                                            * hpCutoff / (float) sr);
                    for (int c = 0; c < buf.getNumChannels(); ++c)
                    {
                        auto* d = buf.getWritePointer (c);
                        float& z = st.depthHpZ[(size_t) c];
                        for (int s = 0; s < n; ++s)
                        {
                            z = (1.0f - a) * d[s] + a * z;
                            d[s] = d[s] - z;
                        }
                    }
                }

                // --- Hard clipper -----------------------------------------
                const float ceil = p.clipCeiling.load (std::memory_order_relaxed);
                if (ceil < 0.99f)
                {
                    for (int c = 0; c < buf.getNumChannels(); ++c)
                    {
                        auto* d = buf.getWritePointer (c);
                        for (int s = 0; s < n; ++s)
                            d[s] = juce::jlimit (-ceil, ceil, d[s]);
                    }
                }

                // --- Gain + pan (depth adds gain trim: back = -4 dB, fwd = +2 dB) -
                const float depthTrimDb = (depthPos > 0.0f ? -depthPos * 4.0f
                                                           :  -depthPos * 2.0f);
                const float lin = juce::Decibels::decibelsToGain (
                                      p.gainDb.load (std::memory_order_relaxed) + depthTrimDb,
                                      -60.0f);
                const float pan = juce::jlimit (-1.0f, 1.0f,
                                      p.pan.load (std::memory_order_relaxed));

                // Equal-power pan
                const float panRad = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                const float gL = std::cos (panRad) * lin;
                const float gR = std::sin (panRad) * lin;

                const int srcCh = buf.getNumChannels();

                if (outCh >= 2)
                {
                    auto* oL = output.getWritePointer (0);
                    auto* oR = output.getWritePointer (1);
                    const auto* sL = buf.getReadPointer (0);
                    const auto* sR = buf.getReadPointer (srcCh > 1 ? 1 : 0);
                    for (int s = 0; s < n; ++s)
                    {
                        oL[s] += sL[s] * gL;
                        oR[s] += sR[s] * gR;
                    }
                }
                else
                {
                    auto* oM = output.getWritePointer (0);
                    const auto* sM = buf.getReadPointer (0);
                    const float g  = (gL + gR) * 0.5f;
                    for (int s = 0; s < n; ++s)
                        oM[s] += sM[s] * g;
                }

                // --- Reverb send (depth augments: back = more, fwd = less) -
                float send = p.reverbSend.load (std::memory_order_relaxed);
                if (depthPos > 0.0f)      send = juce::jlimit (0.0f, 1.0f, send + depthPos * 0.35f);
                else if (depthPos < 0.0f) send = juce::jlimit (0.0f, 1.0f, send * (1.0f + depthPos * 0.7f));
                if (send > 0.001f)
                {
                    const int rCh = reverbBuffer.getNumChannels();
                    for (int c = 0; c < rCh; ++c)
                    {
                        auto* rvb = reverbBuffer.getWritePointer (c);
                        const auto* src = buf.getReadPointer (c < srcCh ? c : 0);
                        const float gPan = (c == 0 ? gL : gR);
                        const float gSend = send * gPan;
                        for (int s = 0; s < n; ++s)
                            rvb[s] += src[s] * gSend;
                    }
                }
            }

            // --- Master reverb return ----------------------------------------
            juce::Reverb::Parameters rp;
            rp.roomSize = juce::jlimit (0.0f, 1.0f, master.reverbSize.load (std::memory_order_relaxed));
            rp.damping  = juce::jlimit (0.0f, 1.0f, master.reverbDamp.load (std::memory_order_relaxed));
            rp.wetLevel = 1.0f;
            rp.dryLevel = 0.0f;
            rp.width    = 1.0f;
            rp.freezeMode = 0.0f;
            reverb.setParameters (rp);

            if (reverbBuffer.getNumChannels() >= 2)
                reverb.processStereo (reverbBuffer.getWritePointer (0),
                                      reverbBuffer.getWritePointer (1),
                                      n);
            else if (reverbBuffer.getNumChannels() == 1)
                reverb.processMono (reverbBuffer.getWritePointer (0), n);

            const float revMix = master.reverbMix.load (std::memory_order_relaxed);
            if (revMix > 0.001f)
            {
                for (int c = 0; c < outCh; ++c)
                {
                    auto* o = output.getWritePointer (c);
                    const auto* r = reverbBuffer.getReadPointer (
                                        c < reverbBuffer.getNumChannels() ? c : 0);
                    for (int s = 0; s < n; ++s)
                        o[s] += r[s] * revMix;
                }
            }

            // --- Master gain + soft clip -------------------------------------
            const float mg = juce::Decibels::decibelsToGain (
                                 master.gainDb.load (std::memory_order_relaxed), -60.0f);
            for (int c = 0; c < outCh; ++c)
            {
                auto* o = output.getWritePointer (c);
                for (int s = 0; s < n; ++s)
                    o[s] = std::tanh (o[s] * mg);
            }
        }

        BusParams&    params_ref (int i) { return params[(size_t) juce::jlimit (0, kNumDrumBuses - 1, i)]; }
        MasterParams& master_ref()       { return master; }

    private:
        struct BusState
        {
            // One-pole shelf / peaking memories (simple, non-resonant).
            std::array<float, 2> eqLowZ   {};
            std::array<float, 2> eqMidZ1  {};
            std::array<float, 2> eqMidZ2  {};
            std::array<float, 2> eqHighZ  {};
            std::array<float, 2> dampenZ  {};
            std::array<float, 2> depthLpZ {};
            std::array<float, 2> depthHpZ {};
            std::array<float, 2> compEnv  { { 0.0f, 0.0f } };

            void reset()
            {
                eqLowZ.fill (0.0f);
                eqMidZ1.fill (0.0f);
                eqMidZ2.fill (0.0f);
                eqHighZ.fill (0.0f);
                dampenZ.fill (0.0f);
                depthLpZ.fill (0.0f);
                depthHpZ.fill (0.0f);
                compEnv.fill (0.0f);
            }
        };

        void processEq (juce::AudioBuffer<float>& buf, BusState& st,
                        float lowDb, float midDb, float highDb)
        {
            const int n = buf.getNumSamples();
            const float lowGain  = juce::Decibels::decibelsToGain (lowDb,  -24.0f);
            const float midGain  = juce::Decibels::decibelsToGain (midDb,  -24.0f);
            const float highGain = juce::Decibels::decibelsToGain (highDb, -24.0f);

            // Cheap split: low band = LP @ 220Hz, high band = HP @ 4kHz,
            // mid = remainder.
            const float aLow  = std::exp (-juce::MathConstants<float>::twoPi * 220.0f  / (float) sr);
            const float aHigh = std::exp (-juce::MathConstants<float>::twoPi * 4000.0f / (float) sr);

            for (int c = 0; c < buf.getNumChannels(); ++c)
            {
                auto* d = buf.getWritePointer (c);
                float& zL = st.eqLowZ[(size_t) c];
                float& zH = st.eqHighZ[(size_t) c];
                for (int s = 0; s < n; ++s)
                {
                    const float in = d[s];
                    zL = (1.0f - aLow)  * in + aLow  * zL;          // low-passed signal
                    // Highpass = signal - low-passed (via aHigh smoother)
                    zH = (1.0f - aHigh) * in + aHigh * zH;          // low-passed @ 4k
                    const float highBand = in - zH;                 // above 4k
                    const float lowBand  = zL;
                    const float midBand  = in - zL - highBand;      // rest
                    d[s] = lowBand * lowGain + midBand * midGain + highBand * highGain;
                }
            }
        }

        void processComp (juce::AudioBuffer<float>& buf, BusState& st, float amount01)
        {
            if (amount01 < 0.001f) return;
            const int n = buf.getNumSamples();

            // Map one knob to threshold / ratio / makeup.
            const float threshDb = juce::jmap (amount01, 0.0f, 1.0f, -6.0f, -30.0f);
            const float ratio    = juce::jmap (amount01, 0.0f, 1.0f, 1.5f, 8.0f);
            const float makeupDb = amount01 * 9.0f;
            const float thresh   = juce::Decibels::decibelsToGain (threshDb, -60.0f);
            const float makeup   = juce::Decibels::decibelsToGain (makeupDb, -60.0f);

            const float attack  = std::exp (-1.0f / (0.005f * (float) sr));
            const float release = std::exp (-1.0f / (0.15f  * (float) sr));

            for (int c = 0; c < buf.getNumChannels(); ++c)
            {
                auto* d = buf.getWritePointer (c);
                float& env = st.compEnv[(size_t) c];
                for (int s = 0; s < n; ++s)
                {
                    const float absX = std::abs (d[s]);
                    const float coef = (absX > env) ? attack : release;
                    env = coef * env + (1.0f - coef) * absX;

                    float g = 1.0f;
                    if (env > thresh)
                    {
                        const float overDb = juce::Decibels::gainToDecibels (env / thresh, -60.0f);
                        const float compDb = overDb * (1.0f - 1.0f / ratio);
                        g = juce::Decibels::decibelsToGain (-compDb, -60.0f);
                    }
                    d[s] = d[s] * g * makeup;
                }
            }
        }

        double sr       = 48000.0;
        int    channels = 2;
        std::array<juce::AudioBuffer<float>, (size_t) kNumDrumBuses> buses;
        std::array<BusParams,                 (size_t) kNumDrumBuses> params {};
        std::array<BusState,                  (size_t) kNumDrumBuses> states {};

        juce::AudioBuffer<float> reverbBuffer;
        juce::Reverb             reverb;
        MasterParams             master;
    };
}
