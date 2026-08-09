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

    void KitEngine::addSample (int lane, std::shared_ptr<Sample> s)
    {
        if (lane < 0 || lane >= NumLanes || s == nullptr)
            return;
        lanes[(std::size_t) lane].layers.push_back (std::move (s));
    }

    int KitEngine::loadBundledKit (const juce::String& kitPrefix)
    {
       #if HHX_HAS_BUNDLED_KIT
        allNotesOff();
        for (auto& l : lanes)
            l.layers.clear();

        int loaded = 0;
        for (int i = 0; i < BundledKitData::namedResourceListSize; ++i)
        {
            const juce::String resName (BundledKitData::namedResourceList[i]);
            const juce::String original (BundledKitData::originalFilenames[i]);
            if (! original.startsWith (kitPrefix + "__"))
                continue;

            const auto piece = original.fromFirstOccurrenceOf ("__", false, false)
                                       .upToLastOccurrenceOf (".wav", false, false);
            const int lane = pieceNameToLane (piece);
            if (lane < 0)
                continue;

            int size = 0;
            const auto* data = BundledKitData::getNamedResource (resName.toRawUTF8(), size);
            if (data == nullptr || size <= 0)
                continue;

            std::unique_ptr<juce::AudioFormatReader> reader (
                formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, (std::size_t) size, false)));
            if (reader == nullptr || reader->lengthInSamples <= 0)
                continue;

            auto sample = std::make_shared<Sample>();
            sample->sourceRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
            sample->audio.setSize ((int) juce::jmin<juce::uint32> (2u, reader->numChannels),
                                   (int) reader->lengthInSamples);
            reader->read (&sample->audio, 0, (int) reader->lengthInSamples, 0, true,
                          reader->numChannels > 1);
            addSample (lane, std::move (sample));
            ++loaded;
        }

        for (auto& l : lanes)
            std::stable_sort (l.layers.begin(), l.layers.end(),
                              [] (const std::shared_ptr<Sample>& a, const std::shared_ptr<Sample>& b)
                              { return a->audio.getMagnitude (0, a->audio.getNumSamples())
                                     < b->audio.getMagnitude (0, b->audio.getNumSamples()); });

        {
            const juce::ScopedLock sl (kitNameLock);
            kitName = kitPrefix;
        }
        return loaded;
       #else
        juce::ignoreUnused (kitPrefix);
        return 0;
       #endif
    }

    int KitEngine::loadKitFolder (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return 0;

        allNotesOff();
        for (auto& l : lanes)
            l.layers.clear();

        int loaded = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.wav"))
        {
            const auto name  = entry.getFile().getFileNameWithoutExtension();
            const auto piece = name.contains ("__") ? name.fromFirstOccurrenceOf ("__", false, false) : name;
            const int lane = pieceNameToLane (piece);
            if (lane < 0)
                continue;

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (entry.getFile()));
            if (reader == nullptr || reader->lengthInSamples <= 0)
                continue;

            auto sample = std::make_shared<Sample>();
            sample->sourceRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
            sample->audio.setSize ((int) juce::jmin<juce::uint32> (2u, reader->numChannels),
                                   (int) reader->lengthInSamples);
            reader->read (&sample->audio, 0, (int) reader->lengthInSamples, 0, true,
                          reader->numChannels > 1);
            addSample (lane, std::move (sample));
            ++loaded;
        }

        {
            const juce::ScopedLock sl (kitNameLock);
            kitName = folder.getFileName();
        }
        return loaded;
    }

    juce::String KitEngine::getKitName() const
    {
        const juce::ScopedLock sl (kitNameLock);
        return kitName;
    }

    int KitEngine::numLayersForLane (int lane) const
    {
        if (lane < 0 || lane >= NumLanes)
            return 0;
        return (int) lanes[(std::size_t) lane].layers.size();
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

    float KitEngine::getLaneActivity (int lane) const
    {
        return (lane >= 0 && lane < NumLanes) ? lanes[(std::size_t) lane].activity.load() : 0.0f;
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

        // Velocity picks the layer; the round-robin slot the performance engine
        // chose then nudges to a neighbouring sample so repeated hits differ.
        int index = (int) std::floor (vel * (float) numLayers);
        index = juce::jlimit (0, numLayers - 1, index);

        const int rr = slot.roundRobin.fetch_add (1) + variant;
        if (numLayers > 1 && (variant & 1) != 0)
            index = juce::jlimit (0, numLayers - 1, index + ((variant & 2) ? 1 : -1));
        index = juce::jlimit (0, numLayers - 1, index + slot.sampleSwitch.load());

        auto sample = slot.layers[(std::size_t) index];
        if (sample == nullptr)
            return;

        // Micro-variation: ±12 cents and ±0.6 dB per hit keeps repeated notes
        // from phase-cancelling into an obvious loop.
        const float cents = (float) ((rr * 37) % 25 - 12) + slot.tune.load() * 100.0f;
        const float trim  = juce::Decibels::decibelsToGain (((rr * 53) % 13 - 6) * 0.1f);

        const float gain = vel * juce::Decibels::decibelsToGain (slot.gainDb.load()) * trim;
        const float pan  = slot.pan.load();
        const float gl   = gain * std::sqrt (0.5f * (1.0f - pan));
        const float gr   = gain * std::sqrt (0.5f * (1.0f + pan));

        const juce::ScopedLock sl (voiceLock);

        Voice* target = nullptr;
        for (auto& v : voices)
            if (! v.active) { target = &v; break; }

        if (target == nullptr)
        {
            // Steal the voice that is furthest through its sample.
            double best = -1.0;
            for (auto& v : voices)
            {
                const double progress = v.sample != nullptr && v.sample->audio.getNumSamples() > 0
                                      ? v.position / (double) v.sample->audio.getNumSamples() : 1.0;
                if (progress > best) { best = progress; target = &v; }
            }
        }

        if (target == nullptr)
            return;

        chokeArticulations (articulation);

        // Damping shortens the decay, the way a drummer's tape or a felt does.
        const float damp = slot.damp.load();
        const float decayTime = juce::jmap (damp, 0.0f, 1.0f, 8.0f, 0.09f);

        target->sample    = std::move (sample);
        target->position  = 0.0;
        target->increment = (target->sample->sourceRate / currentRate)
                          * std::pow (2.0, cents / 1200.0);
        target->gainL     = gl;
        target->gainR     = gr;
        target->env       = 1.0f;
        target->envDecay  = damp <= 0.001f ? 1.0f
                          : (float) std::pow (0.001, 1.0 / (decayTime * currentRate));
        target->lane      = lane;
        target->articulation = articulation;
        target->active    = true;

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

        for (auto& v : voices)
        {
            if (! v.active || v.sample == nullptr)
                continue;

            const auto& src = v.sample->audio;
            const int   len = src.getNumSamples();
            const auto* srcL = src.getReadPointer (0);
            const auto* srcR = src.getNumChannels() > 1 ? src.getReadPointer (1) : srcL;

            for (int i = 0; i < numSamples; ++i)
            {
                const int pos = (int) v.position;
                if (pos + 1 >= len)
                {
                    v.active = false;
                    v.sample.reset();
                    break;
                }

                const float frac = (float) (v.position - (double) pos);
                const float l = srcL[pos] + frac * (srcL[pos + 1] - srcL[pos]);
                const float r = srcR[pos] + frac * (srcR[pos + 1] - srcR[pos]);

                outL[i] += l * v.gainL * v.env;
                if (outR != nullptr)
                    outR[i] += r * v.gainR * v.env;

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

        const float decay = std::pow (0.5f, (float) numSamples / (float) (currentRate * 0.12));
        for (auto& l : lanes)
            l.activity.store (l.activity.load() * decay);
    }
}
