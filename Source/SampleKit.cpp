#include "SampleKit.h"

#if AIDRUM_HAS_BUNDLED_KIT
 #include "BundledKitData.h"
#endif

namespace aidrum
{
    namespace
    {
        // Maps a filename stem (lowercase, no extension, no velocity suffix)
        // to a voice Kind. Returns true if the name was recognised.
        static bool kindFromStem (juce::String stem, SampleKit::Kind& outKind)
        {
            stem = stem.toLowerCase().trim();

            struct Entry { const char* needle; SampleKit::Kind kind; };
            static const Entry table[] = {
                { "sidestick",   SampleKit::Kind::SideStick },
                { "snare_ghost", SampleKit::Kind::Snare     },
                { "snare_rim",   SampleKit::Kind::Snare     },
                { "snare",       SampleKit::Kind::Snare     },
                { "hat_closed",  SampleKit::Kind::ClosedHat },
                { "hat_pedal",   SampleKit::Kind::PedalHat  },
                { "hat_open",    SampleKit::Kind::OpenHat   },
                { "closedhat",   SampleKit::Kind::ClosedHat },
                { "pedalhat",    SampleKit::Kind::PedalHat  },
                { "openhat",     SampleKit::Kind::OpenHat   },
                { "hihat",       SampleKit::Kind::ClosedHat },
                { "tom_high",    SampleKit::Kind::HighTom   },
                { "tom_mid",     SampleKit::Kind::MidTom    },
                { "tom_low",     SampleKit::Kind::LowTom    },
                { "tomhigh",     SampleKit::Kind::HighTom   },
                { "tommid",      SampleKit::Kind::MidTom    },
                { "tomlow",      SampleKit::Kind::LowTom    },
                { "ride_bell",   SampleKit::Kind::RideBell  },
                { "ridebell",    SampleKit::Kind::RideBell  },
                { "ride",        SampleKit::Kind::Ride      },
                { "china",       SampleKit::Kind::China     },
                { "splash",      SampleKit::Kind::Crash     },
                { "crash",       SampleKit::Kind::Crash     },
                { "kick",        SampleKit::Kind::Kick      },
                { "bd",          SampleKit::Kind::Kick      },
                { "sd",          SampleKit::Kind::Snare     },
                { "hh",          SampleKit::Kind::ClosedHat },
            };

            for (const auto& e : table)
            {
                if (stem.startsWith (e.needle))
                {
                    outKind = e.kind;
                    return true;
                }
            }
            return false;
        }

        // Strips a trailing _<digits> velocity suffix and returns the
        // integer layer index (0 if none).
        static int stripVelocitySuffix (juce::String& stem)
        {
            const int us = stem.lastIndexOfChar ('_');
            if (us <= 0 || us >= stem.length() - 1) return 0;
            const auto tail = stem.substring (us + 1);
            if (! tail.containsOnly ("0123456789")) return 0;
            const int layer = tail.getIntValue();
            stem = stem.substring (0, us);
            return layer;
        }
    }

    int SampleKit::load (const juce::File& folder)
    {
        if (! folder.isDirectory()) return 0;

        auto data = std::make_shared<KitData>();
        data->folderPath = folder.getFullPathName();

        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();

        juce::Array<juce::File> files;
        folder.findChildFiles (files, juce::File::findFiles, false,
                               "*.wav;*.aif;*.aiff;*.flac;*.ogg");

        // Track per-slot (kind, velLayer) so we can sort layers after reading.
        struct Pending { Kind kind; int layer; juce::File file; };
        std::vector<Pending> pending;

        for (auto& f : files)
        {
            auto stem = f.getFileNameWithoutExtension();
            const int vel = stripVelocitySuffix (stem);
            Kind k;
            if (kindFromStem (stem, k))
                pending.push_back ({ k, vel, f });
        }

        int loaded = 0;
        for (auto& p : pending)
        {
            std::unique_ptr<juce::AudioFormatReader> reader
                (fmt.createReaderFor (p.file));
            if (reader == nullptr) continue;

            const int len = (int) reader->lengthInSamples;
            if (len <= 0 || len > (int) (sr * 30.0)) continue;

            const int ch = juce::jmin (2, (int) reader->numChannels);
            Layer layer;
            layer.buffer.setSize (ch, len);
            if (! reader->read (&layer.buffer, 0, len, 0, true, ch >= 2)) continue;

            // Sample-rate convert crude-ily (linear) if needed.
            if (std::abs (reader->sampleRate - sr) > 1.0)
            {
                const double ratio = reader->sampleRate / sr;
                const int newLen = juce::jmax (1, (int) (len / ratio));
                juce::AudioBuffer<float> out (ch, newLen);
                out.clear();
                for (int c = 0; c < ch; ++c)
                {
                    const float* in = layer.buffer.getReadPointer (c);
                    float* dst = out.getWritePointer (c);
                    for (int i = 0; i < newLen; ++i)
                    {
                        const double pos = i * ratio;
                        const int i0 = (int) pos;
                        const int i1 = juce::jmin (i0 + 1, len - 1);
                        const float frac = (float) (pos - i0);
                        dst[i] = in[i0] + frac * (in[i1] - in[i0]);
                    }
                }
                layer.buffer = std::move (out);
            }

            auto& slot = data->slots[(size_t) p.kind];
            slot.layers.push_back (std::move (layer));
            slot.loaded = true;
            data->anyLoaded = true;
            ++loaded;
        }

        if (loaded == 0) return 0;
        std::atomic_store (&kit, data);
        return loaded;
    }

    int SampleKit::loadBundled (const juce::String& kitNameIn)
    {
       #if AIDRUM_HAS_BUNDLED_KIT
        // v1.6.1-rc.6 — single bundled kit. Anything other than the
        // one we ship falls back to "Thrash" so legacy save files that
        // still reference PopRock/NuRock/AltRock/IndieLofi/HardRock
        // don't come up empty.
        juce::String kitName = kitNameIn.isEmpty() ? juce::String ("Thrash") : kitNameIn;
        if (kitName != "Thrash")
            kitName = "Thrash";
        const juce::String prefix  = kitName + "__";

        auto data = std::make_shared<KitData>();
        data->folderPath = "Built-in " + kitName;

        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();

        int loaded = 0;
        for (int i = 0; i < BundledKitData::namedResourceListSize; ++i)
        {
            const char* resName = BundledKitData::namedResourceList[i];
            int dataSize = 0;
            const char* bytes = BundledKitData::getNamedResource (resName, dataSize);
            if (bytes == nullptr || dataSize <= 0) continue;

            juce::String origName = BundledKitData::getNamedResourceOriginalFilename (resName);
            if (origName.isEmpty()) origName = resName;

            // v1.5.0+: all bundled kits share one binary blob; filenames are prefixed
            // with "<KitName>__" (e.g. "PopRock__kick.wav"). Only load entries
            // belonging to the requested kit.
            if (! origName.startsWith (prefix)) continue;
            auto remainder = origName.substring (prefix.length());
            auto stem = juce::File (remainder).getFileNameWithoutExtension();
            (void) stripVelocitySuffix (stem);
            Kind k;
            if (! kindFromStem (stem, k)) continue;

            std::unique_ptr<juce::InputStream> mem
                (new juce::MemoryInputStream (bytes, (size_t) dataSize, false));
            std::unique_ptr<juce::AudioFormatReader> reader
                (fmt.createReaderFor (std::move (mem)));
            if (reader == nullptr) continue;

            const int len = (int) reader->lengthInSamples;
            if (len <= 0 || len > (int) (sr * 30.0)) continue;

            const int ch = juce::jmin (2, (int) reader->numChannels);
            Layer layer;
            layer.buffer.setSize (ch, len);
            if (! reader->read (&layer.buffer, 0, len, 0, true, ch >= 2)) continue;

            if (std::abs (reader->sampleRate - sr) > 1.0)
            {
                const double ratio = reader->sampleRate / sr;
                const int newLen = juce::jmax (1, (int) (len / ratio));
                juce::AudioBuffer<float> out (ch, newLen);
                out.clear();
                for (int c = 0; c < ch; ++c)
                {
                    const float* in = layer.buffer.getReadPointer (c);
                    float* dst = out.getWritePointer (c);
                    for (int ii = 0; ii < newLen; ++ii)
                    {
                        const double pos = ii * ratio;
                        const int i0 = (int) pos;
                        const int i1 = juce::jmin (i0 + 1, len - 1);
                        const float frac = (float) (pos - i0);
                        dst[ii] = in[i0] + frac * (in[i1] - in[i0]);
                    }
                }
                layer.buffer = std::move (out);
            }

            auto& slot = data->slots[(size_t) k];
            slot.layers.push_back (std::move (layer));
            slot.loaded = true;
            data->anyLoaded = true;
            ++loaded;
        }

        if (loaded == 0) return 0;
        std::atomic_store (&kit, data);
        return loaded;
       #else
        return 0;
       #endif
    }

    const SampleKit::Layer* SampleKit::pickLayer (const KitSlot& slot, float vel) const
    {
        if (! slot.loaded || slot.layers.empty()) return nullptr;
        if (slot.layers.size() == 1) return &slot.layers[0];
        const float v = juce::jlimit (0.0f, 1.0f, vel);
        int idx = (int) (v * (float) slot.layers.size());
        idx = juce::jlimit (0, (int) slot.layers.size() - 1, idx);
        return &slot.layers[(size_t) idx];
    }

    void SampleKit::noteOn (int midiNote, float velocity, int sampleOffset)
    {
        auto data = std::atomic_load (&kit);
        if (data == nullptr || ! data->anyLoaded) return;

        const auto k = kindFromNote (midiNote);
        const auto* layer = pickLayer (data->slots[(size_t) k], velocity);
        if (layer == nullptr)
        {
            // Snare fallback for sidestick/clap if only plain snare was provided.
            if (k == Kind::SideStick || k == Kind::Clap)
                layer = pickLayer (data->slots[(size_t) Kind::Snare], velocity);
            if (layer == nullptr) return;
        }

        int idx = -1;
        for (int i = 0; i < kMaxVoices; ++i)
            if (! voices[i].active) { idx = i; break; }
        if (idx < 0)
        {
            // Steal the voice with the largest playPos (nearest to end of sample).
            int bestI = 0, bestPos = -1;
            for (int i = 0; i < kMaxVoices; ++i)
                if (voices[i].playPos > bestPos) { bestPos = voices[i].playPos; bestI = i; }
            idx = bestI;
        }

        auto& v = voices[idx];
        v.active      = true;
        v.startSample = sampleOffset;
        v.velocity    = juce::jlimit (0.05f, 1.2f, velocity);
        v.kind        = k;
        v.playPos     = 0;
        v.kitRef      = data;
        v.layer       = layer;
    }

    void SampleKit::renderIntoBuses (DrumBusMixer& mixer, int numSamples)
    {
        if (numSamples <= 0) return;
        for (auto& v : voices)
        {
            if (! v.active || v.layer == nullptr) continue;

            auto* busBuf = mixer.busBuffer (DrumSynth::busIndexForKind (v.kind));
            if (busBuf == nullptr) { v.active = false; continue; }

            const auto& src = v.layer->buffer;
            const int srcLen = src.getNumSamples();
            const int srcChans = src.getNumChannels();
            const int busChans = busBuf->getNumChannels();
            const int start = juce::jlimit (0, numSamples, v.startSample);
            v.startSample = 0;

            const float gain = v.velocity;
            int i = start;
            int pos = v.playPos;
            for (; i < numSamples && pos < srcLen; ++i, ++pos)
            {
                for (int c = 0; c < busChans; ++c)
                {
                    const int sc = juce::jmin (c, srcChans - 1);
                    busBuf->addSample (c, i, src.getReadPointer (sc)[pos] * gain);
                }
            }
            v.playPos = pos;
            if (pos >= srcLen)
            {
                v.active = false;
                v.kitRef.reset();
                v.layer = nullptr;
            }
        }
    }
}
