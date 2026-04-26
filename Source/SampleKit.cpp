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
            // v1.6.1-rc.8 — Left vs Right crash now route to different
            // physical Kinds (Crash and China). The order matters here:
            // longer / more-specific needles must come first so
            // "right_crash" is matched before the bare "crash" entry.
            // Floor / Small tom and the L/R crash split mirror the new
            // 8-lane arrangement strip, and the install_rc8_samples.py
            // installer writes filenames like
            //   NuRockYamaha__rcrash_1.wav  /  NuRockYamaha__lcrash_1.wav
            //   NuRockYamaha__floortom_1.wav / NuRockYamaha__smalltom_1.wav
            // so the routing is deterministic regardless of host.
            static const Entry table[] = {
                { "sidestick",    SampleKit::Kind::SideStick },
                { "snare_ghost",  SampleKit::Kind::Snare     },
                { "snare_rim",    SampleKit::Kind::Snare     },
                { "snare",        SampleKit::Kind::Snare     },
                { "hat_closed",   SampleKit::Kind::ClosedHat },
                { "hat_pedal",    SampleKit::Kind::PedalHat  },
                { "hat_open",     SampleKit::Kind::OpenHat   },
                { "closedhat",    SampleKit::Kind::ClosedHat },
                { "pedalhat",     SampleKit::Kind::PedalHat  },
                { "openhat",      SampleKit::Kind::OpenHat   },
                { "hihat",        SampleKit::Kind::ClosedHat },
                { "floor_tom",    SampleKit::Kind::LowTom    },
                { "floortom",     SampleKit::Kind::LowTom    },
                { "small_tom",    SampleKit::Kind::HighTom   },
                { "smalltom",     SampleKit::Kind::HighTom   },
                { "tom_high",     SampleKit::Kind::HighTom   },
                { "tom_mid",      SampleKit::Kind::MidTom    },
                { "tom_low",      SampleKit::Kind::LowTom    },
                { "tomhigh",      SampleKit::Kind::HighTom   },
                { "tommid",       SampleKit::Kind::MidTom    },
                { "tomlow",       SampleKit::Kind::LowTom    },
                { "ride_bell",    SampleKit::Kind::RideBell  },
                { "ridebell",     SampleKit::Kind::RideBell  },
                { "ride",         SampleKit::Kind::Ride      },
                { "china",        SampleKit::Kind::China     },
                { "right_crash",  SampleKit::Kind::China     },
                { "rightcrash",   SampleKit::Kind::China     },
                { "rcrash",       SampleKit::Kind::China     },
                { "left_crash",   SampleKit::Kind::Crash     },
                { "leftcrash",    SampleKit::Kind::Crash     },
                { "lcrash",       SampleKit::Kind::Crash     },
                { "splash",       SampleKit::Kind::Crash     },
                { "crash",        SampleKit::Kind::Crash     },
                { "kick",         SampleKit::Kind::Kick      },
                { "bd",           SampleKit::Kind::Kick      },
                { "sd",           SampleKit::Kind::Snare     },
                { "hh",           SampleKit::Kind::ClosedHat },
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

        // v1.6.1-rc.11 — per-Kind DSP applied at WAV load time. The user
        // asked for "less reverb baked into the drums" + "fine-tune all
        // timber for all instruments" + "make it real, not tacky/wet".
        // Each Kind gets:
        //   * a one-pole high-pass at the Kind's natural fundamental
        //     to strip rumble / mud
        //   * a one-pole high-shelf cut for cymbals / hats so the
        //     baked-in brittleness goes away
        //   * a tail-gate envelope that linearly fades the sample to
        //     silence over a Kind-specific window so no reverb wash
        //     hangs past the natural transient
        // Result: the bundled WAVs play bone-dry and punchy. The Room
        // Amount knob is the *only* reverb in the chain.
        static void bakeRealnessDSP (juce::AudioBuffer<float>& buf,
                                     SampleKit::Kind kind,
                                     double sampleRate)
        {
            using K = SampleKit::Kind;
            float hpHz = 0.0f, hsHz = 0.0f, hsCut = 0.0f;
            float gateMs = 0.0f, gateFadeMs = 30.0f;
            switch (kind)
            {
                case K::Kick:      hpHz =  30.0f;                    gateMs = 400.0f; break;
                case K::Snare:     hpHz = 120.0f;                    gateMs = 250.0f; break;
                case K::SideStick: hpHz = 200.0f;                    gateMs = 120.0f; break;
                case K::HighTom:
                case K::MidTom:
                case K::LowTom:    hpHz =  60.0f;                    gateMs = 600.0f; break;
                case K::ClosedHat: hpHz = 200.0f; hsHz = 14000.0f; hsCut = 0.45f; gateMs = 200.0f; break;
                case K::PedalHat:  hpHz = 200.0f; hsHz = 14000.0f; hsCut = 0.45f; gateMs = 220.0f; break;
                case K::OpenHat:   hpHz = 200.0f; hsHz = 14000.0f; hsCut = 0.45f; gateMs = 350.0f; break;
                case K::Ride:      hpHz = 150.0f; hsHz = 13000.0f; hsCut = 0.50f; gateMs = 700.0f; break;
                case K::RideBell:  hpHz = 150.0f; hsHz = 13000.0f; hsCut = 0.50f; gateMs = 500.0f; break;
                case K::Crash:
                case K::China:     hpHz = 120.0f; hsHz = 12000.0f; hsCut = 0.55f; gateMs = 600.0f; break;
                default: break;
            }

            const int   ch  = buf.getNumChannels();
            const int   len = buf.getNumSamples();
            if (ch <= 0 || len <= 0) return;

            // One-pole high-pass: y[n] = a*(y[n-1] + x[n] - x[n-1]).
            if (hpHz > 0.0f)
            {
                const float dt = 1.0f / (float) sampleRate;
                const float rc = 1.0f / (2.0f * juce::MathConstants<float>::pi * hpHz);
                const float a  = rc / (rc + dt);
                for (int c = 0; c < ch; ++c)
                {
                    float* d = buf.getWritePointer (c);
                    float prevX = d[0], prevY = d[0];
                    for (int i = 1; i < len; ++i)
                    {
                        const float y = a * (prevY + d[i] - prevX);
                        prevX = d[i];
                        d[i]  = y;
                        prevY = y;
                    }
                }
            }

            // One-pole high-shelf cut for cymbals / hats (cut by `hsCut`
            // above hsHz). Implemented as: y = x - cut * (x - lp(x))
            // i.e. subtract the high-frequency component scaled by cut.
            if (hsHz > 0.0f && hsCut > 0.0f)
            {
                const float dt = 1.0f / (float) sampleRate;
                const float rc = 1.0f / (2.0f * juce::MathConstants<float>::pi * hsHz);
                const float a  = dt / (rc + dt); // LP coeff
                for (int c = 0; c < ch; ++c)
                {
                    float* d = buf.getWritePointer (c);
                    float lp = d[0];
                    for (int i = 0; i < len; ++i)
                    {
                        lp += a * (d[i] - lp);
                        const float hi = d[i] - lp;
                        d[i] = lp + (1.0f - hsCut) * hi;
                    }
                }
            }

            // Tail-gate: linear ramp from full to silence between
            // gateMs and (gateMs + gateFadeMs). Anything past the fade
            // window is muted. Skips gating if the WAV is shorter than
            // gateMs (oneshot already short enough).
            if (gateMs > 0.0f)
            {
                const int gateStart = (int) (gateMs       * 1e-3 * sampleRate);
                const int gateEnd   = (int) ((gateMs + gateFadeMs) * 1e-3 * sampleRate);
                if (len > gateStart)
                {
                    for (int c = 0; c < ch; ++c)
                    {
                        float* d = buf.getWritePointer (c);
                        for (int i = gateStart; i < len; ++i)
                        {
                            float gain = 1.0f;
                            if (i >= gateEnd) gain = 0.0f;
                            else if (i > gateStart)
                                gain = 1.0f - (float) (i - gateStart) / (float) (gateEnd - gateStart);
                            d[i] *= gain;
                        }
                    }
                }
            }
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

            // v1.6.1-rc.11 — bake realness DSP (HP, hi-shelf cut, tail
            // gate) per Kind so loaded oneshots play dry / punchy and
            // the Room Amount knob is the sole reverb source.
            bakeRealnessDSP (layer.buffer, p.kind, sr);

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
        // v1.6.1-rc.8 — active bundled kit is now "NuRockYamaha". The
        // legacy "Thrash" name from rc.6/rc.7 is still accepted (older
        // session state asks for it by name when restoring), but it
        // remaps to NuRockYamaha so existing user projects still load
        // *something* instead of going silent.
        // v1.6.1-rc.11 — two bundled kits ship now: "NuRockYamaha"
        // (Nu Rock 70's Yamaha — rc.8 oneshots) and "BayGrungeMaple"
        // (Bay Grunge Yamaha Maple — rc.11 oneshots). Anything else
        // remaps to NuRockYamaha so legacy save files don't go silent.
        juce::String kitName = kitNameIn.isEmpty()
                                 ? juce::String ("NuRockYamaha")
                                 : kitNameIn;
        if (kitName != "NuRockYamaha"
         && kitName != "BayGrungeMaple"
         && kitName != "Thrash")
            kitName = "NuRockYamaha";
        if (kitName == "Thrash")
            kitName = "NuRockYamaha";
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

            // v1.6.1-rc.11 — bake realness DSP per Kind (see helper).
            bakeRealnessDSP (layer.buffer, k, sr);

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
