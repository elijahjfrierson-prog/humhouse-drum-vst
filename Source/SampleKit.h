#pragma once

#include "DrumBusMixer.h"
#include "DrumSynth.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// ============================================================================
// SampleKit — v1.4.0
//
// A real-audio drum sampler that sits in front of the DrumSynth physical
// model. When a kit is loaded from disk, every noteOn plays a pre-recorded
// WAV; when no kit is loaded, the sampler is inactive and the physical-model
// synth takes over. This is the only honest path to "real acoustic drums":
// play back actual recordings of a mic'd kit.
//
// Folder layout — drop any folder containing these files on the LOAD KIT
// FOLDER picker (case-insensitive, matched by substring before the extension):
//
//   kick, snare, snare_ghost, snare_rim, sidestick
//   hat_closed, hat_pedal, hat_open
//   tom_high, tom_mid, tom_low
//   ride, ride_bell, crash, crash2, china, splash
//
// Optional velocity layers — suffix with _1 .. _8 (soft→hard); the sampler
// picks the layer whose index is closest to floor(velocity * numLayers).
//
// Thread safety: load() runs on the UI/message thread and builds a new
// KitData on the heap, then publishes it via a std::atomic<std::shared_ptr>.
// The audio thread takes the current shared_ptr by atomic_load; it never
// allocates.
// ============================================================================
namespace aidrum
{
    class SampleKit
    {
    public:
        static constexpr int kMaxVoices = 24;

        using Kind = DrumSynth::Kind;

        void prepare (double sampleRate, int /*maxBlockSize*/)
        {
            sr = std::max (8000.0, sampleRate);
            for (auto& v : voices) v = Voice{};
        }

        void reset()
        {
            for (auto& v : voices) v = Voice{};
        }

        // True if a user-loaded (or bundled) sample pack is currently active.
        bool isActive() const noexcept
        {
            auto p = std::atomic_load (&kit);
            return p != nullptr && p->anyLoaded;
        }

        // Path of the currently-loaded kit folder ("" if none).
        juce::String currentKitPath() const
        {
            auto p = std::atomic_load (&kit);
            return p != nullptr ? p->folderPath : juce::String();
        }

        // Load every recognised WAV in `folder` on the calling thread.
        // Returns the number of samples successfully loaded (0 if the folder
        // contains nothing the kit recognises).
        int load (const juce::File& folder);

        // Load the CC0 kit compiled into the plugin binary.
        // v1.6.1-rc.6 — single bundled kit ("Thrash"). The name param
        // is retained for API compatibility; empty string or anything
        // other than "Thrash" falls back to "Thrash".
        // `currentKitPath()` after a successful load returns "Built-in <KitName>".
        // Returns the number of samples loaded (0 if the binary blob is empty).
        int loadBundled (const juce::String& kitName = {});

        // Drop the current kit so the physical-model synth takes over again.
        void unload()
        {
            std::atomic_store (&kit, std::shared_ptr<KitData>());
        }

        // Audio thread. velocity 0..1, sampleOffset within the current block.
        void noteOn (int midiNote, float velocity, int sampleOffset);

        // Renders every active voice into its routed bus buffer on the mixer.
        void renderIntoBuses (DrumBusMixer& mixer, int numSamples);

    private:
        struct Layer
        {
            juce::AudioBuffer<float> buffer;  // 1 or 2 channels
        };

        struct KitSlot
        {
            std::vector<Layer> layers; // index 0..N-1 sorted soft→hard
            bool loaded = false;
        };

        struct KitData
        {
            std::array<KitSlot, (size_t) Kind::China + 1> slots {};
            juce::String folderPath;
            bool anyLoaded = false;
        };

        struct Voice
        {
            bool  active    = false;
            int   startSample = 0;
            float velocity  = 1.0f;
            Kind  kind      = Kind::Kick;
            // v1.6.1-rc.18 — fractional playback position so we can
            // micro-detune snare hits (±5¢) for R/L stick differentiation.
            // For non-snare voices playRate stays at 1.0 and the path
            // collapses to the original integer-step render.
            double playPos    = 0.0;
            double playRate   = 1.0;
            // v1.6.1-rc.18 — per-voice one-pole HF damping for the L-hand
            // snare path (left hand on a real kit reads slightly darker —
            // weaker stick angle, shorter snare-wire attack envelope).
            // 0.0 = bypass, ~0.55 = audibly damped without losing snap.
            float lpAmount    = 0.0f;
            std::array<float, 2> lpZ {};
            // Shared_ptr to the kit that owns the buffer — ensures the buffer
            // outlives the voice even if the user swaps kits mid-playback.
            std::shared_ptr<KitData> kitRef;
            const Layer* layer = nullptr;
        };

        const Layer* pickLayer (const KitSlot& slot, float vel) const;

        // kindFromNote re-uses DrumSynth's mapping.
        static Kind kindFromNote (int n) { return DrumSynth::Voice_kindFromNote (n); }

        double sr = 48000.0;
        std::array<Voice, kMaxVoices> voices {};

        // v1.6.1-rc.18 — running count of snare-like noteOns so consecutive
        // hits alternate between an R-hand voicing (idx 0,2,4… → playRate
        // 1.003 ≈ +5¢, no HF damping) and an L-hand voicing (idx 1,3,5… →
        // playRate 0.997 ≈ −5¢, lpAmount 0.55 → ~9 kHz one-pole roll-off).
        // The counter is mutated on the audio thread but only by noteOn(),
        // which is also audio-thread; no atomicity needed.
        int snareHitCounter = 0;

        // Atomic shared_ptr — load() publishes, audio thread atomic_loads.
        std::shared_ptr<KitData> kit;
    };
}
