#pragma once

#include "GrooveCorpus.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace hhx
{
    /** Multisampled kit player.

        Each lane owns an ordered list of samples (softest → hardest). A hit
        picks the layer its velocity falls into, then alternates between the
        layer and its neighbour with small pitch/gain offsets so consecutive
        hits are never bit-identical — the round-robin behaviour whose absence
        made the previous plugin's hats sound like a machine gun.
    */
    class KitEngine
    {
    public:
        KitEngine();

        void prepare (double sampleRate, int maxBlockSize);

        /** Loads the kit compiled into the binary. */
        int loadBundledKit (const juce::String& kitPrefix);

        /** Loads a user folder of WAVs named `<piece>_<n>.wav`. */
        int loadKitFolder (const juce::File& folder);

        juce::String getKitName() const;
        int  numLayersForLane (int lane) const;

        /** Sample-switch: rotates which sample of the lane's set is favoured.
            Persisted with the project; takes effect on the next hit.
        */
        void setLaneSampleSwitch (int lane, int offset);
        int  getLaneSampleSwitch (int lane) const;

        void setLaneGainDb (int lane, float db);
        float getLaneGainDb (int lane) const;
        void setLanePan (int lane, float pan);
        float getLanePan (int lane) const;
        void setLaneTune (int lane, float semitones);
        float getLaneTune (int lane) const;
        void setLaneDamp (int lane, float amount01);
        float getLaneDamp (int lane) const;

        /** `variant` is the round-robin slot the performance engine chose, so
            two consecutive strokes on a lane never fire the same sample.
        */
        void noteOn (int lane, float velocity01, int variant = 0);
        void allNotesOff();

        /** Which lane actually holds samples for an articulation. A 30-piece
            performance still plays on a kit that only ships one snare: the
            articulation falls back to its nearest relative rather than going
            silent.
        */
        int resolveLane (int lane) const;

        void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

        /** 0..1 decaying meter per lane, for the UI's animated kit pieces. */
        float getLaneActivity (int lane) const;

    private:
        struct Sample
        {
            juce::AudioBuffer<float> audio;
            double sourceRate = 44100.0;
        };

        struct LaneSlot
        {
            std::vector<std::shared_ptr<Sample>> layers;
            std::atomic<int>   sampleSwitch { 0 };
            std::atomic<float> gainDb { 0.0f };
            std::atomic<float> pan    { 0.0f };
            std::atomic<float> tune   { 0.0f };   // semitones
            std::atomic<float> damp   { 0.0f };   // 0 = open, 1 = heavily muted
            std::atomic<float> activity { 0.0f };
            std::atomic<int>   roundRobin { 0 };
        };

        struct Voice
        {
            std::shared_ptr<Sample> sample;
            double position = 0.0;
            double increment = 1.0;
            float  gainL = 0.0f;
            float  gainR = 0.0f;
            float  env   = 1.0f;
            float  envDecay = 1.0f;
            int    lane  = -1;
            int    articulation = -1;
            bool   active = false;
        };

        void addSample (int lane, std::shared_ptr<Sample> s);
        void chokeArticulations (int lane);
        static int pieceNameToLane (const juce::String& piece);

        static constexpr int kMaxVoices = 64;

        std::array<LaneSlot, NumLanes> lanes;
        std::array<Voice, kMaxVoices>  voices;
        juce::CriticalSection          voiceLock;
        juce::AudioFormatManager       formats;
        double                         currentRate = 48000.0;
        juce::String                   kitName;
        mutable juce::CriticalSection  kitNameLock;
    };
}
