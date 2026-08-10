#pragma once

#include "GrooveCorpus.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace hhx
{
    /** The microphone positions a kit can supply per stroke. Close is the only
        one a kit must have; the others are mixed in behind it and double as the
        kit's natural bleed.
    */
    enum Mic : int
    {
        MicClose = 0,
        MicOverhead,
        MicRoom,
        NumMics
    };

    /** Multisampled kit player.

        Each lane holds velocity layers, softest first, and each layer holds
        round-robin variants; each variant holds one recording per microphone.
        A hit picks the layer its velocity falls into and a variant the
        performance engine chose, so consecutive strokes are never
        bit-identical - the behaviour whose absence made the previous plugin's
        hats sound like a machine gun.
    */
    class KitEngine
    {
    public:
        KitEngine();

        void prepare (double sampleRate, int maxBlockSize);

        /** Loads the kit compiled into the binary. */
        int loadBundledKit (const juce::String& kitPrefix);

        /** Loads a kit folder. A `kit.json` describing pieces, velocity layers,
            round robins and mics is used when present; otherwise the WAV names
            (`<piece>_<n>.wav`) are parsed. */
        int loadKitFolder (const juce::File& folder);

        juce::String getKitName() const;
        juce::String getKitVersion() const;

        /** Total loaded samples across every lane, layer, round robin and mic. */
        int numLoadedSamples() const;

        int numLayersForLane (int lane) const;
        int numVariantsForLane (int lane, int layer) const;
        bool laneHasMic (int lane, int mic) const;

        /** Sample-switch: rotates which variant of the lane's set is favoured.
            Persisted with the project; takes effect on the next hit. */
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

        // --- kit mix -----------------------------------------------------
        /** Mic blend: 0 = close only, 1 = all the way back in the room. */
        void  setMicBlend (float blend01);
        float getMicBlend() const;

        /** How much of the kit leaks into the far mics. With a close-mic-only
            kit this generates the leak from a delayed mono copy instead. */
        void  setBleed (float amount01);
        float getBleed() const;

        /** Mono-crush bus: a saturated mono squash of the whole kit, blended
            back in behind the stereo mix. */
        void  setCrush (float amount01);
        float getCrush() const;

        /** `variant` is the round-robin slot the performance engine chose, so
            two consecutive strokes on a lane never fire the same sample. */
        void noteOn (int lane, float velocity01, int variant = 0);
        void allNotesOff();

        /** Which lane actually holds samples for an articulation. A 30-piece
            performance still plays on a kit that only ships one snare: the
            articulation falls back to its nearest relative rather than going
            silent. */
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

        struct Variant
        {
            std::array<std::shared_ptr<Sample>, NumMics> mics {};
        };

        struct Layer
        {
            std::vector<Variant> variants;
        };

        struct LaneSlot
        {
            std::vector<Layer> layers;               // softest first
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
            int    mic   = MicClose;
            bool   active = false;
        };

        /** Where a loaded file belongs in the kit. */
        struct Placement
        {
            int lane  = -1;
            int layer = -1;      // -1 = decide from loudness after loading
            int variant = 0;
            int mic   = MicClose;
        };

        void clearKit();
        void chokeArticulations (int articulation);
        void place (const Placement& p, std::shared_ptr<Sample> s);
        void sortLayersByLoudness();
        std::shared_ptr<Sample> readSample (juce::InputStream* stream);
        int loadFromManifest (const juce::File& folder, const juce::var& manifest);
        void startVoice (const std::shared_ptr<Sample>& sample, int lane, int articulation,
                         int mic, float gainL, float gainR, double increment,
                         float envDecay);

        static int pieceNameToLane (const juce::String& piece);
        static int micNameToIndex (const juce::String& mic);
        static Placement placementFromFilename (const juce::String& name);

        static constexpr int kMaxVoices  = 96;
        static constexpr int kBleedDelay = 512;   // samples, ~11 ms at 48 kHz

        std::array<LaneSlot, NumLanes> lanes;
        std::array<Voice, kMaxVoices>  voices;
        juce::CriticalSection          voiceLock;
        juce::AudioFormatManager       formats;
        double                         currentRate = 48000.0;

        std::atomic<float> micBlend { 0.35f };
        std::atomic<float> bleed    { 0.15f };
        std::atomic<float> crush    { 0.0f };

        std::array<float, kBleedDelay> bleedLine {};
        int                            bleedWrite = 0;

        juce::String                   kitName;
        juce::String                   kitVersion { "1" };
        mutable juce::CriticalSection  kitNameLock;
    };
}
