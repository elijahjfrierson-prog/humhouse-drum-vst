#include "KitEngine.h"

#if __has_include(<BundledKitData.h>)
 #include <BundledKitData.h>
 #define HHX_HAS_BUNDLED_KIT 1
#else
 #define HHX_HAS_BUNDLED_KIT 0
#endif

#include <algorithm>
#include <cmath>

namespace hhx
{
    KitEngine::KitEngine()
    {
        formats.registerBasicFormats();
    }

    void KitEngine::prepare (double sampleRate, int)
    {
        currentRate = sampleRate;
        bleedLine.fill (0.0f);
        bleedWrite = 0;
        allNotesOff();
    }

    int KitEngine::pieceNameToLane (const juce::String& piece)
    {
        const auto p = piece.toLowerCase();
        if (p.startsWith ("kick"))          return LaneKick;
        if (p.startsWith ("snare_ghost"))   return LaneSnareGhost;
        if (p.startsWith ("snare_flam"))    return LaneSnareFlam;
        if (p.startsWith ("snare_roll"))    return LaneSnareRoll;
        if (p.startsWith ("snare_rim"))     return LaneSnareRim;
        if (p.startsWith ("snare"))         return LaneSnare;
        if (p.startsWith ("rim"))           return LaneSnareRim;
        if (p.startsWith ("sidestick"))     return LaneSideStick;
        if (p.startsWith ("side_stick"))    return LaneSideStick;
        if (p.startsWith ("hat_closed"))    return LaneHatClosed;
        if (p.startsWith ("hat_tight"))     return LaneHatTight;
        if (p.startsWith ("hat_pedal"))     return LaneHatPedal;
        if (p.startsWith ("hat_splash"))    return LaneHatSplash;
        if (p.startsWith ("hat_bell"))      return LaneHatBell;
        if (p.startsWith ("hat_open_1"))    return LaneHatOpen1;
        if (p.startsWith ("hat_open_2"))    return LaneHatOpen2;
        if (p.startsWith ("hat_open_3"))    return LaneHatOpen3;
        if (p.startsWith ("hat_open_4"))    return LaneHatOpen4;
        if (p.startsWith ("hat_open"))      return LaneHatOpen2;
        if (p.startsWith ("tom_high"))      return LaneTom1;
        if (p.startsWith ("tom_1"))         return LaneTom1;
        if (p.startsWith ("tom_mid"))       return LaneTom2;
        if (p.startsWith ("tom_2"))         return LaneTom2;
        if (p.startsWith ("tom_low"))       return LaneTom3;
        if (p.startsWith ("tom_3"))         return LaneTom3;
        if (p.startsWith ("tom_4"))         return LaneTom4;
        if (p.startsWith ("floor_tom"))     return LaneTom3;
        if (p.startsWith ("left_crash"))    return LaneCrashL;
        if (p.startsWith ("right_crash"))   return LaneCrashR;
        if (p.startsWith ("crash_3"))       return LaneCrash3;
        if (p.startsWith ("crash"))         return LaneCrashR;
        if (p.startsWith ("china"))         return LaneChina;
        if (p.startsWith ("splash"))        return LaneSplash;
        if (p.startsWith ("ride_bell"))     return LaneRideBell;
        if (p.startsWith ("ride_edge"))     return LaneRideEdge;
        if (p.startsWith ("ride_crash"))    return LaneRideCrash;
        if (p.startsWith ("ride"))          return LaneRideBow;
        if (p.startsWith ("perc"))          return LanePerc;
        return -1;
    }

    int KitEngine::micNameToIndex (const juce::String& mic)
    {
        const auto m = mic.toLowerCase();
        if (m.startsWith ("oh") || m.startsWith ("over")) return MicOverhead;
        if (m.startsWith ("room") || m.startsWith ("amb")) return MicRoom;
        return MicClose;
    }

    KitEngine::Placement KitEngine::placementFromFilename (const juce::String& name)
    {
        // `<piece>[_v<layer>][_rr<variant>][_<mic>]`, e.g. snare_v3_rr2_oh.
        Placement p;
        auto piece = name;

        const auto strip = [&piece] (const juce::String& token) -> juce::String
        {
            const auto lower = piece.toLowerCase();
            const int at = lower.lastIndexOf ("_" + token);
            if (at < 0)
                return {};
            const auto tail = piece.substring (at + 1 + token.length());
            if (! tail.containsOnly ("0123456789") || tail.isEmpty())
                return {};
            piece = piece.substring (0, at);
            return tail;
        };

        for (const int mic : { MicOverhead, MicRoom })
        {
            const juce::StringArray tokens = mic == MicOverhead
                ? juce::StringArray { "oh", "overhead" }
                : juce::StringArray { "room", "amb" };
            for (const auto& t : tokens)
                if (piece.toLowerCase().endsWith ("_" + t))
                {
                    p.mic = mic;
                    piece = piece.dropLastCharacters (t.length() + 1);
                }
        }

        if (const auto rr = strip ("rr"); rr.isNotEmpty())
            p.variant = std::max (0, rr.getIntValue() - 1);
        if (const auto v = strip ("v"); v.isNotEmpty())
            p.layer = std::max (0, v.getIntValue() - 1);

        p.lane = pieceNameToLane (piece);
        return p;
    }

    int KitEngine::resolveLane (int lane) const
    {
        if (lane < 0 || lane >= NumLanes)
            return -1;
        if (! lanes[(std::size_t) lane].layers.empty())
            return lane;

        // Fallback chains, nearest relative first.
        static const std::vector<std::vector<int>> chains = {
            { LaneSnareGhost,  LaneSnare },
            { LaneSnareFlam,   LaneSnare },
            { LaneSnareRoll,   LaneSnare },
            { LaneSnareRim,    LaneSnare },
            { LaneSideStick,   LaneSnareRim, LaneSnare },
            { LaneHatTight,    LaneHatClosed },
            { LaneHatOpen1,    LaneHatOpen2, LaneHatClosed },
            { LaneHatOpen2,    LaneHatOpen3, LaneHatClosed },
            { LaneHatOpen3,    LaneHatOpen2, LaneHatClosed },
            { LaneHatOpen4,    LaneHatOpen3, LaneHatOpen2, LaneHatClosed },
            { LaneHatPedal,    LaneHatClosed },
            { LaneHatSplash,   LaneHatOpen2, LaneHatClosed },
            { LaneHatBell,     LaneHatClosed },
            { LaneRideEdge,    LaneRideBow },
            { LaneRideCrash,   LaneCrashR, LaneRideBow },
            { LaneRideBell,    LaneRideBow },
            { LaneCrash3,      LaneCrashR, LaneCrashL },
            { LaneChina,       LaneCrashL, LaneCrashR },
            { LaneSplash,      LaneCrashL, LaneCrashR },
            { LaneTom4,        LaneTom3, LaneTom2 },
            { LaneTom3,        LaneTom2, LaneTom1 },
            { LaneTom2,        LaneTom1, LaneTom3 },
            { LanePerc,        LaneSideStick, LaneSnareRim },
        };

        for (const auto& chain : chains)
        {
            if (chain.front() != lane)
                continue;
            for (std::size_t i = 1; i < chain.size(); ++i)
                if (! lanes[(std::size_t) chain[i]].layers.empty())
                    return chain[i];
        }
        return -1;
    }

    void KitEngine::clearKit()
    {
        allNotesOff();
        for (auto& l : lanes)
            l.layers.clear();
    }

    void KitEngine::place (const Placement& p, std::shared_ptr<Sample> s)
    {
        if (p.lane < 0 || p.lane >= NumLanes || s == nullptr)
            return;

        auto& slot = lanes[(std::size_t) p.lane];

        // A file with no declared layer becomes its own layer; loudness sorting
        // afterwards puts the set in order.
        const int layerIndex = p.layer >= 0 ? p.layer : (int) slot.layers.size();
        if ((int) slot.layers.size() <= layerIndex)
            slot.layers.resize ((std::size_t) layerIndex + 1);

        auto& layer = slot.layers[(std::size_t) layerIndex];
        if ((int) layer.variants.size() <= p.variant)
            layer.variants.resize ((std::size_t) p.variant + 1);

        layer.variants[(std::size_t) p.variant].mics[(std::size_t) p.mic] = std::move (s);
    }

    int KitEngine::numLoadedSamples() const
    {
        int total = 0;
        for (const auto& slot : lanes)
            for (const auto& layer : slot.layers)
                for (const auto& variant : layer.variants)
                    for (const auto& sample : variant.mics)
                        if (sample != nullptr)
                            ++total;
        return total;
    }

    void KitEngine::finaliseKit (bool sortByLoudness)
    {
        const auto loudness = [] (const Layer& l) -> float
        {
            float peak = 0.0f;
            for (const auto& v : l.variants)
                if (const auto& s = v.mics[MicClose])
                    peak = std::max (peak, s->peak);
            return peak;
        };

        for (auto& slot : lanes)
        {
            slot.layers.erase (std::remove_if (slot.layers.begin(), slot.layers.end(),
                                               [] (const Layer& l)
                                               {
                                                   for (const auto& v : l.variants)
                                                       if (v.mics[MicClose] != nullptr)
                                                           return false;
                                                   return true;
                                               }),
                               slot.layers.end());

            if (sortByLoudness)
                std::stable_sort (slot.layers.begin(), slot.layers.end(),
                                  [&loudness] (const Layer& a, const Layer& b)
                                  { return loudness (a) < loudness (b); });

            for (auto& layer : slot.layers)
                layer.variants.erase (std::remove_if (layer.variants.begin(), layer.variants.end(),
                                                      [] (const Variant& v)
                                                      { return v.mics[MicClose] == nullptr; }),
                                      layer.variants.end());
        }
    }

    std::shared_ptr<KitEngine::Sample> KitEngine::readSample (juce::InputStream* stream)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (
            std::unique_ptr<juce::InputStream> (stream)));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return nullptr;

        const int channels = (int) juce::jmin<juce::uint32> (2u, reader->numChannels);
        const int frames   = (int) reader->lengthInSamples;

        juce::AudioBuffer<float> scratch (channels, frames);
        reader->read (&scratch, 0, frames, 0, true, reader->numChannels > 1);

        auto sample = std::make_shared<Sample>();
        sample->sourceRate  = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        sample->numChannels = channels;
        sample->numFrames   = frames;
        sample->peak        = scratch.getMagnitude (0, frames);
        sample->data.resize ((std::size_t) frames * (std::size_t) channels);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* src = scratch.getReadPointer (ch);
            for (int i = 0; i < frames; ++i)
                sample->data[(std::size_t) i * (std::size_t) channels + (std::size_t) ch]
                    = (std::int16_t) juce::jlimit (-32768, 32767,
                                                   (int) std::lround (src[i] * 32767.0f));
        }
        return sample;
    }

    int KitEngine::loadBundledKit (const juce::String& kitPrefix)
    {
       #if HHX_HAS_BUNDLED_KIT
        clearKit();

        int loaded = 0;
        for (int i = 0; i < BundledKitData::namedResourceListSize; ++i)
        {
            const juce::String resName (BundledKitData::namedResourceList[i]);
            const juce::String original (BundledKitData::originalFilenames[i]);
            if (! original.startsWith (kitPrefix + "__"))
                continue;

            const auto piece = original.fromFirstOccurrenceOf ("__", false, false)
                                       .upToLastOccurrenceOf (".wav", false, false);
            const auto placement = placementFromFilename (piece);
            if (placement.lane < 0)
                continue;

            int size = 0;
            const auto* data = BundledKitData::getNamedResource (resName.toRawUTF8(), size);
            if (data == nullptr || size <= 0)
                continue;

            if (auto sample = readSample (new juce::MemoryInputStream (data, (std::size_t) size, false)))
            {
                place (placement, std::move (sample));
                ++loaded;
            }
        }

        finaliseKit (true);
        {
            const juce::ScopedLock sl (kitNameLock);
            kitName = kitPrefix;
            kitVersion = "1";
        }
        return loaded;
       #else
        juce::ignoreUnused (kitPrefix);
        return 0;
       #endif
    }

    int KitEngine::loadFromManifest (const juce::File& folder, const juce::var& manifest)
    {
        if (auto* obj = manifest.getDynamicObject())
        {
            const juce::ScopedLock sl (kitNameLock);
            kitName    = obj->getProperty ("name").toString();
            kitVersion = obj->getProperty ("version").toString();
            if (kitName.isEmpty())
                kitName = folder.getFileName();
            if (kitVersion.isEmpty())
                kitVersion = "1";
        }

        const auto* pieces = manifest["pieces"].getArray();
        if (pieces == nullptr)
            return 0;

        int loaded = 0;
        for (const auto& entry : *pieces)
        {
            Placement p;
            p.lane = pieceNameToLane (entry["piece"].toString());
            if (p.lane < 0)
                continue;

            p.layer   = entry.hasProperty ("layer")   ? std::max (0, (int) entry["layer"] - 1) : -1;
            p.variant = entry.hasProperty ("variant") ? std::max (0, (int) entry["variant"] - 1) : 0;
            p.mic     = micNameToIndex (entry["mic"].toString());

            const auto file = folder.getChildFile (entry["file"].toString());
            if (! file.existsAsFile())
                continue;
            if (auto sample = readSample (new juce::FileInputStream (file)))
            {
                place (p, std::move (sample));
                ++loaded;
            }
        }
        return loaded;
    }

    int KitEngine::loadKitFolder (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return 0;

        clearKit();

        int loaded = 0;
        bool declaredLayers = false;
        const auto manifestFile = folder.getChildFile ("kit.json");
        if (manifestFile.existsAsFile())
        {
            const auto manifest = juce::JSON::parse (manifestFile);
            loaded = loadFromManifest (folder, manifest);
            declaredLayers = loaded > 0;
        }

        if (loaded == 0)
        {
            for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.wav;*.flac"))
            {
                const auto name  = entry.getFile().getFileNameWithoutExtension();
                const auto piece = name.contains ("__") ? name.fromFirstOccurrenceOf ("__", false, false) : name;
                const auto placement = placementFromFilename (piece);
                if (placement.lane < 0)
                    continue;

                if (auto sample = readSample (new juce::FileInputStream (entry.getFile())))
                {
                    place (placement, std::move (sample));
                    ++loaded;
                }
            }

            const juce::ScopedLock sl (kitNameLock);
            kitName    = folder.getFileName();
            kitVersion = "1";
        }

        finaliseKit (! declaredLayers);
        return loaded;
    }

    juce::String KitEngine::getKitName() const
    {
        const juce::ScopedLock sl (kitNameLock);
        return kitName;
    }

    juce::String KitEngine::getKitVersion() const
    {
        const juce::ScopedLock sl (kitNameLock);
        return kitVersion;
    }

    int KitEngine::numLayersForLane (int lane) const
    {
        if (lane < 0 || lane >= NumLanes)
            return 0;
        return (int) lanes[(std::size_t) lane].layers.size();
    }

    int KitEngine::numVariantsForLane (int lane, int layer) const
    {
        if (lane < 0 || lane >= NumLanes)
            return 0;
        const auto& slot = lanes[(std::size_t) lane];
        if (layer < 0 || layer >= (int) slot.layers.size())
            return 0;
        return (int) slot.layers[(std::size_t) layer].variants.size();
    }

    bool KitEngine::laneHasMic (int lane, int mic) const
    {
        if (lane < 0 || lane >= NumLanes || mic < 0 || mic >= NumMics)
            return false;
        for (const auto& layer : lanes[(std::size_t) lane].layers)
            for (const auto& v : layer.variants)
                if (v.mics[(std::size_t) mic] != nullptr)
                    return true;
        return false;
    }

    void KitEngine::setLaneSampleSwitch (int lane, int offset)
    {
        if (lane >= 0 && lane < NumLanes)
            lanes[(std::size_t) lane].sampleSwitch.store (offset);
    }

    int KitEngine::getLaneSampleSwitch (int lane) const
    {
        return (lane >= 0 && lane < NumLanes) ? lanes[(std::size_t) lane].sampleSwitch.load() : 0;
    }

    void KitEngine::setLaneGainDb (int lane, float db)
    {
        if (lane >= 0 && lane < NumLanes)
            lanes[(std::size_t) lane].gainDb.store (db);
    }

    float KitEngine::getLaneGainDb (int lane) const
    {
        return (lane >= 0 && lane < NumLanes) ? lanes[(std::size_t) lane].gainDb.load() : 0.0f;
    }

    void KitEngine::setLaneTune (int lane, float semitones)
    {
        if (lane >= 0 && lane < NumLanes)
            lanes[(std::size_t) lane].tune.store (juce::jlimit (-12.0f, 12.0f, semitones));
    }

    float KitEngine::getLaneTune (int lane) const
    {
        return (lane >= 0 && lane < NumLanes) ? lanes[(std::size_t) lane].tune.load() : 0.0f;
    }

    void KitEngine::setLaneDamp (int lane, float amount01)
    {
        if (lane >= 0 && lane < NumLanes)
            lanes[(std::size_t) lane].damp.store (juce::jlimit (0.0f, 1.0f, amount01));
    }

    float KitEngine::getLaneDamp (int lane) const
    {
        return (lane >= 0 && lane < NumLanes) ? lanes[(std::size_t) lane].damp.load() : 0.0f;
    }

    void KitEngine::setLanePan (int lane, float pan)
    {
        if (lane >= 0 && lane < NumLanes)
            lanes[(std::size_t) lane].pan.store (juce::jlimit (-1.0f, 1.0f, pan));
    }

    float KitEngine::getLanePan (int lane) const
    {
        return (lane >= 0 && lane < NumLanes) ? lanes[(std::size_t) lane].pan.load() : 0.0f;
    }

    void KitEngine::setMicBlend (float blend01) { micBlend.store (juce::jlimit (0.0f, 1.0f, blend01)); }
    float KitEngine::getMicBlend() const       { return micBlend.load(); }
    void KitEngine::setBleed (float amount01)  { bleed.store (juce::jlimit (0.0f, 1.0f, amount01)); }
    float KitEngine::getBleed() const          { return bleed.load(); }
    void KitEngine::setCrush (float amount01)  { crush.store (juce::jlimit (0.0f, 1.0f, amount01)); }
    float KitEngine::getCrush() const          { return crush.load(); }

    float KitEngine::getLaneActivity (int lane) const
    {
        return (lane >= 0 && lane < NumLanes) ? lanes[(std::size_t) lane].activity.load() : 0.0f;
    }

    void KitEngine::startVoice (const std::shared_ptr<Sample>& sample, int lane, int articulation,
                                int mic, float gainL, float gainR, double increment, float envDecay,
                                float toneCoeff)
    {
        Voice* target = nullptr;
        for (auto& v : voices)
            if (! v.active) { target = &v; break; }

        if (target == nullptr)
        {
            // Steal the voice that is furthest through its sample.
            double best = -1.0;
            for (auto& v : voices)
            {
                const double progress = v.sample != nullptr && v.sample->numFrames > 0
                                      ? v.position / (double) v.sample->numFrames : 1.0;
                if (progress > best) { best = progress; target = &v; }
            }
        }

        if (target == nullptr)
            return;

        target->sample       = sample;
        target->position     = 0.0;
        target->increment    = increment;
        target->gainL        = gainL;
        target->gainR        = gainR;
        target->env          = 1.0f;
        target->envDecay     = envDecay;
        target->toneCoeff    = toneCoeff;
        target->toneL        = 0.0f;
        target->toneR        = 0.0f;
        target->lane         = lane;
        target->articulation = articulation;
        target->mic          = mic;
        target->active       = true;
    }

    void KitEngine::noteOn (int lane, float velocity01, int variant)
    {
        const int articulation = lane;
        lane = resolveLane (lane);
        if (lane < 0)
            return;

        auto& slot = lanes[(std::size_t) lane];
        const int numLayers = (int) slot.layers.size();
        if (numLayers == 0)
            return;

        const float vel = juce::jlimit (0.02f, 1.0f, velocity01);
        const int rr = slot.roundRobin.fetch_add (1) + variant;

        // A hash of the round-robin counter, used to dither the choices below
        // so that they stay deterministic without ever needing rand().
        const auto dither = [rr] (int salt)
        {
            const auto h = (juce::uint32) rr * 2654435761u + (juce::uint32) salt * 40503u;
            return (float) ((h >> 9) & 0xffffu) / 65535.0f;
        };

        // Velocity picks a point on the layer ladder rather than a bucket, and
        // the boundary is dithered: a stroke sitting between two layers takes
        // one or the other. Hard buckets are what made repeated hats step.
        const float ladder = vel * (float) numLayers - 0.5f;
        const int   below  = (int) std::floor (ladder);
        int layerIndex = below + (dither (1) < (ladder - (float) below) ? 1 : 0);
        layerIndex = juce::jlimit (0, numLayers - 1, layerIndex);

        const auto& layer = slot.layers[(std::size_t) layerIndex];
        const int numVariants = (int) layer.variants.size();
        if (numVariants == 0)
            return;

        // The round-robin slot rotates within the layer when the kit supplies
        // several takes of it; otherwise it nudges to a neighbouring layer so
        // repeated strokes still differ.
        int variantIndex = ((rr % numVariants) + numVariants) % numVariants;
        variantIndex = (variantIndex + slot.sampleSwitch.load()) % numVariants;
        if (variantIndex < 0)
            variantIndex += numVariants;

        if (numVariants == 1 && numLayers > 1 && (variant & 1) != 0)
            layerIndex = juce::jlimit (0, numLayers - 1, layerIndex + ((variant & 2) ? 1 : -1));

        const auto& chosen = slot.layers[(std::size_t) layerIndex]
                                 .variants[(std::size_t) juce::jlimit (
                                     0, (int) slot.layers[(std::size_t) layerIndex].variants.size() - 1,
                                     variantIndex)];
        if (chosen.mics[MicClose] == nullptr)
            return;

        // Micro-variation: a few cents and a fraction of a dB per hit keeps
        // repeated notes from phase-cancelling into an obvious loop.
        const float cents = (dither (2) - 0.5f) * 22.0f + slot.tune.load() * 100.0f;
        const float trim  = juce::Decibels::decibelsToGain ((dither (3) - 0.5f) * 1.3f);

        // How much of the dynamic range the recordings already carry. On a deep
        // kit the layers do the work and the fader only tops it up; on a
        // one-layer kit the fader is all there is.
        const float depth   = juce::jmin (1.0f, (float) numLayers / 5.0f);
        const float curve   = std::pow (vel, 0.75f);
        const float velGain = juce::jmap (depth, curve, 0.45f + 0.55f * curve);

        const float gain = velGain * juce::Decibels::decibelsToGain (slot.gainDb.load()) * trim;
        const float pan  = juce::jlimit (-1.0f, 1.0f,
                                         slot.pan.load() + (dither (4) - 0.5f) * 0.05f);
        const float gl   = gain * std::sqrt (0.5f * (1.0f - pan));
        const float gr   = gain * std::sqrt (0.5f * (1.0f + pan));

        // Damping shortens the decay, the way a drummer's tape or a felt does.
        const float damp = slot.damp.load();
        const float decayTime = juce::jmap (damp, 0.0f, 1.0f, 8.0f, 0.09f);
        const float envDecay = damp <= 0.001f ? 1.0f
                             : (float) std::pow (0.001, 1.0 / (decayTime * currentRate));

        // Stick hardness: a soft stroke is darker as well as quieter, because a
        // lightly struck cymbal never excites its top octave. Without this a
        // ladder of level-matched multisamples reads as one flat, boxy hit.
        const float reach = juce::jmap (depth, 0.85f, 0.4f);
        const float dark  = (1.0f - std::pow (vel, 0.8f)) * reach;
        const float cutoff = 20000.0f * std::exp (-3.1f * dark);
        const float tone = dark < 0.02f
                         ? 1.0f
                         : juce::jlimit (0.02f, 1.0f,
                                         1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                                          * cutoff / (float) currentRate));

        const juce::ScopedLock sl (voiceLock);
        chokeArticulations (articulation);

        for (int mic = 0; mic < NumMics; ++mic)
        {
            const auto& sample = chosen.mics[(std::size_t) mic];
            if (sample == nullptr)
                continue;
            const double increment = (sample->sourceRate / currentRate)
                                   * std::pow (2.0, cents / 1200.0);
            startVoice (sample, lane, articulation, mic, gl, gr, increment, envDecay, tone);
        }

        slot.activity.store (1.0f);
    }

    void KitEngine::chokeArticulations (int articulation)
    {
        // Hat pedal state machine: closing the hats kills anything ringing on
        // the openness ladder. Cymbal chokes stop their own cymbal.
        const auto killIf = [this] (auto&& predicate)
        {
            for (auto& v : voices)
                if (v.active && predicate (v.articulation))
                    v.active = false;
        };

        if (articulation == LaneHatClosed || articulation == LaneHatTight
            || articulation == LaneHatPedal)
        {
            killIf ([] (int a) { return a >= LaneHatOpen1 && a <= LaneHatOpen4; });
        }
        else if (articulation >= LaneHatOpen1 && articulation <= LaneHatOpen4)
        {
            // A wider hat replaces a narrower one rather than stacking.
            killIf ([articulation] (int a)
                    { return a >= LaneHatOpen1 && a <= LaneHatOpen4 && a != articulation; });
        }
        else if (articulation == LaneChina || articulation == LaneSplash)
        {
            killIf ([articulation] (int a) { return a == articulation; });
        }
    }

    void KitEngine::allNotesOff()
    {
        const juce::ScopedLock sl (voiceLock);
        for (auto& v : voices)
        {
            v.active = false;
            v.sample.reset();
        }
    }

    void KitEngine::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        const juce::ScopedLock sl (voiceLock);

        const int numOut = buffer.getNumChannels();
        auto* outL = buffer.getWritePointer (0, startSample);
        auto* outR = numOut > 1 ? buffer.getWritePointer (1, startSample) : nullptr;

        const float blend     = micBlend.load();
        const float bleedAmt  = bleed.load();
        const float crushAmt  = crush.load();

        // Mic blend: close dominates until the blend is turned up, and the far
        // mics carry the bleed.
        std::array<float, NumMics> micGain { 1.0f - 0.45f * blend,
                                             blend * (0.4f + 0.6f * bleedAmt),
                                             blend * blend * (0.3f + 0.7f * bleedAmt) };

        for (auto& v : voices)
        {
            if (! v.active || v.sample == nullptr)
                continue;

            const auto* src  = v.sample->data.data();
            const int   len  = v.sample->numFrames;
            const int   ch   = v.sample->numChannels;
            const float mg   = micGain[(std::size_t) v.mic];
            constexpr float toFloat = 1.0f / 32768.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const int pos = (int) v.position;
                if (pos + 1 >= len)
                {
                    v.active = false;
                    v.sample.reset();
                    break;
                }

                const auto* a = src + (std::size_t) pos * (std::size_t) ch;
                const auto* b = a + ch;
                const float frac = (float) (v.position - (double) pos);
                float l = (a[0] + frac * (b[0] - a[0])) * toFloat;
                float r = ch > 1 ? (a[1] + frac * (b[1] - a[1])) * toFloat : l;

                v.toneL += v.toneCoeff * (l - v.toneL);
                v.toneR += v.toneCoeff * (r - v.toneR);
                l = v.toneL;
                r = v.toneR;

                outL[i] += l * v.gainL * v.env * mg;
                if (outR != nullptr)
                    outR[i] += r * v.gainR * v.env * mg;

                v.position += v.increment;
                v.env *= v.envDecay;
                if (v.env < 1.0e-4f)
                {
                    v.active = false;
                    v.sample.reset();
                    break;
                }
            }
        }

        // A close-mic-only kit has no recorded leak, so the bleed control feeds
        // a delayed mono copy of the kit into the room instead.
        const bool haveFarMics = laneHasMic (LaneKick, MicRoom) || laneHasMic (LaneKick, MicOverhead);
        if (bleedAmt > 0.001f && ! haveFarMics)
        {
            const float send = 0.35f * bleedAmt * (0.4f + 0.6f * blend);
            for (int i = 0; i < numSamples; ++i)
            {
                const float mono = 0.5f * (outL[i] + (outR != nullptr ? outR[i] : outL[i]));
                const float delayed = bleedLine[(std::size_t) bleedWrite];
                bleedLine[(std::size_t) bleedWrite] = mono;
                bleedWrite = (bleedWrite + 1) % kBleedDelay;

                outL[i] += delayed * send;
                if (outR != nullptr)
                    outR[i] += delayed * send * 0.85f;
            }
        }

        if (crushAmt > 0.001f)
        {
            const float drive = 1.0f + 6.0f * crushAmt;
            for (int i = 0; i < numSamples; ++i)
            {
                const float mono = 0.5f * (outL[i] + (outR != nullptr ? outR[i] : outL[i]));
                const float crushed = std::tanh (mono * drive) * (0.7f / drive) * drive * 0.5f;
                outL[i] = outL[i] * (1.0f - 0.5f * crushAmt) + crushed * crushAmt;
                if (outR != nullptr)
                    outR[i] = outR[i] * (1.0f - 0.5f * crushAmt) + crushed * crushAmt;
            }
        }

        const float decay = std::pow (0.5f, (float) numSamples / (float) (currentRate * 0.12));
        for (auto& l : lanes)
            l.activity.store (l.activity.load() * decay);
    }
}
