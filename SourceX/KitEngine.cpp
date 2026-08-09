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
        if (p.startsWith ("snare"))         return LaneSnare;
        if (p.startsWith ("rim"))           return LaneSnareRim;
        if (p.startsWith ("sidestick"))     return LaneSideStick;
        if (p.startsWith ("hat_closed"))    return LaneHatClosed;
        if (p.startsWith ("hat_pedal"))     return LaneHatPedal;
        if (p.startsWith ("hat_open"))      return LaneHatOpen;
        if (p.startsWith ("tom_high"))      return LaneTomHi;
        if (p.startsWith ("tom_mid"))       return LaneTomMid;
        if (p.startsWith ("tom_low"))       return LaneTomFloor;
        if (p.startsWith ("floor_tom"))     return LaneTomFloor;
        if (p.startsWith ("left_crash"))    return LaneCrashL;
        if (p.startsWith ("right_crash"))   return LaneCrashR;
        if (p.startsWith ("crash"))         return LaneCrashR;
        if (p.startsWith ("china"))         return LaneCrashL;
        if (p.startsWith ("ride_bell"))     return LaneRideBell;
        if (p.startsWith ("ride"))          return LaneRide;
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

    void KitEngine::noteOn (int lane, float velocity01)
    {
        if (lane < 0 || lane >= NumLanes)
            return;

        auto& slot = lanes[(std::size_t) lane];
        const int numLayers = (int) slot.layers.size();
        if (numLayers == 0)
            return;

        const float vel = juce::jlimit (0.02f, 1.0f, velocity01);

        // Velocity picks the layer; the round-robin counter nudges between the
        // chosen layer and its neighbour so repeated hits differ.
        int index = (int) std::floor (vel * (float) numLayers);
        index = juce::jlimit (0, numLayers - 1, index);

        const int rr = slot.roundRobin.fetch_add (1);
        if (numLayers > 1 && (rr & 1) != 0)
            index = juce::jlimit (0, numLayers - 1, index + ((rr & 2) ? 1 : -1));
        index = juce::jlimit (0, numLayers - 1, index + slot.sampleSwitch.load());

        auto sample = slot.layers[(std::size_t) index];
        if (sample == nullptr)
            return;

        // Micro-variation: ±12 cents and ±0.6 dB per hit keeps repeated notes
        // from phase-cancelling into an obvious loop.
        const float cents = ((rr * 37) % 25 - 12) * 0.01f;
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

        // Choke: an open hat stops when a closed hat or pedal is played.
        if (lane == LaneHatClosed || lane == LaneHatPedal)
            for (auto& v : voices)
                if (v.active && v.lane == LaneHatOpen)
                    v.active = false;

        target->sample    = std::move (sample);
        target->position  = 0.0;
        target->increment = (target->sample->sourceRate / currentRate)
                          * std::pow (2.0, cents / 12.0);
        target->gainL     = gl;
        target->gainR     = gr;
        target->lane      = lane;
        target->active    = true;

        slot.activity.store (1.0f);
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

                outL[i] += l * v.gainL;
                if (outR != nullptr)
                    outR[i] += r * v.gainR;

                v.position += v.increment;
            }
        }

        const float decay = std::pow (0.5f, (float) numSamples / (float) (currentRate * 0.12));
        for (auto& l : lanes)
            l.activity.store (l.activity.load() * decay);
    }
}
