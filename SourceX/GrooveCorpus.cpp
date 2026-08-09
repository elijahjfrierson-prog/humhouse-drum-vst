#include "GrooveCorpus.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace hhx
{
    namespace
    {
        constexpr char        kMagic[4]   = { 'H', 'H', 'C', 'X' };
        constexpr std::uint32_t kVersion  = 1;

        struct Reader
        {
            const std::uint8_t* p;
            const std::uint8_t* end;

            bool take (void* dest, std::size_t n)
            {
                if ((std::size_t) (end - p) < n)
                    return false;
                std::memcpy (dest, p, n);
                p += n;
                return true;
            }
        };
    }

    const char* laneName (int lane)
    {
        switch (lane)
        {
            case LaneKick:      return "Kick";
            case LaneSnare:     return "Snare";
            case LaneSnareRim:  return "Rimshot";
            case LaneSideStick: return "Sidestick";
            case LaneHatClosed: return "Hi-Hat";
            case LaneHatPedal:  return "Hat Pedal";
            case LaneHatOpen:   return "Open Hat";
            case LaneTomHi:     return "Hi Tom";
            case LaneTomMid:    return "Mid Tom";
            case LaneTomFloor:  return "Floor Tom";
            case LaneCrashL:    return "Left Crash";
            case LaneCrashR:    return "Right Crash";
            case LaneRide:      return "Ride";
            case LaneRideBell:  return "Ride Bell";
            default:            return "?";
        }
    }

    int laneToGmNote (int lane)
    {
        switch (lane)
        {
            case LaneKick:      return 36;
            case LaneSnare:     return 38;
            case LaneSnareRim:  return 40;
            case LaneSideStick: return 37;
            case LaneHatClosed: return 42;
            case LaneHatPedal:  return 44;
            case LaneHatOpen:   return 46;
            case LaneTomHi:     return 48;
            case LaneTomMid:    return 45;
            case LaneTomFloor:  return 43;
            case LaneCrashL:    return 49;
            case LaneCrashR:    return 57;
            case LaneRide:      return 51;
            case LaneRideBell:  return 53;
            default:            return 38;
        }
    }

    int gmNoteToLane (int note)
    {
        for (int lane = 0; lane < NumLanes; ++lane)
            if (laneToGmNote (lane) == note)
                return lane;
        return -1;
    }

    bool GrooveCorpus::loadFromMemory (const void* data, std::size_t numBytes)
    {
        loaded = false;
        beatPhrases.clear();
        fillPhrases.clear();

        Reader r { static_cast<const std::uint8_t*> (data),
                   static_cast<const std::uint8_t*> (data) + numBytes };

        char magic[4] {};
        std::uint32_t version = 0, count = 0;
        if (! r.take (magic, 4) || std::memcmp (magic, kMagic, 4) != 0)
            return false;
        if (! r.take (&version, 4) || version != kVersion)
            return false;
        if (! r.take (&count, 4))
            return false;

        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint8_t  kind = 0, bars = 0, cx = 0, in = 0;
            std::uint16_t bpm = 0, numHits = 0;
            if (! r.take (&kind, 1) || ! r.take (&bars, 1) || ! r.take (&cx, 1)
                || ! r.take (&in, 1) || ! r.take (&bpm, 2) || ! r.take (&numHits, 2))
                return false;

            Phrase phrase;
            phrase.isFill     = kind != 0;
            phrase.bars       = bars;
            phrase.bpm        = bpm;
            phrase.complexity = cx / 255.0f;
            phrase.intensity  = in / 255.0f;
            phrase.hits.reserve (numHits);

            for (std::uint16_t h = 0; h < numHits; ++h)
            {
                std::uint8_t lane = 0, vel = 0;
                float beat = 0.0f;
                if (! r.take (&lane, 1) || ! r.take (&vel, 1) || ! r.take (&beat, 4))
                    return false;
                if (lane < NumLanes)
                    phrase.hits.push_back ({ beat, lane, vel });
            }

            std::sort (phrase.hits.begin(), phrase.hits.end(),
                       [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });

            (phrase.isFill ? fillPhrases : beatPhrases).push_back (std::move (phrase));
        }

        loaded = ! beatPhrases.empty();
        return loaded;
    }

    std::vector<int> GrooveCorpus::rank (const std::vector<Phrase>& pool,
                                         float complexity,
                                         float intensity,
                                         int   maxResults) const
    {
        std::vector<int> idx (pool.size());
        std::iota (idx.begin(), idx.end(), 0);

        const auto distance = [&] (int i)
        {
            const auto& p = pool[(std::size_t) i];
            const float dc = p.complexity - complexity;
            const float di = p.intensity  - intensity;
            return dc * dc + di * di;
        };

        const int n = std::min<int> (maxResults, (int) idx.size());
        std::partial_sort (idx.begin(), idx.begin() + n, idx.end(),
                           [&] (int a, int b) { return distance (a) < distance (b); });
        idx.resize ((std::size_t) n);
        return idx;
    }

    int GrooveCorpus::pickBeat (float complexity, float intensity, int variation) const
    {
        if (beatPhrases.empty())
            return -1;

        // 24 nearest real takes form the neighbourhood the XY position can
        // resolve to; the variation index walks that list.
        const auto ranked = rank (beatPhrases, complexity, intensity, 24);
        if (ranked.empty())
            return -1;

        const int v = ((variation % (int) ranked.size()) + (int) ranked.size()) % (int) ranked.size();
        return ranked[(std::size_t) v];
    }

    int GrooveCorpus::pickFill (float complexity,
                                float intensity,
                                int   bars,
                                int   variation,
                                const std::vector<int>& avoid,
                                std::uint32_t laneMask) const
    {
        if (fillPhrases.empty())
            return -1;

        std::vector<Phrase> filtered;      // kept only for scoring symmetry
        std::vector<int>    candidates;
        candidates.reserve (fillPhrases.size());

        for (int i = 0; i < (int) fillPhrases.size(); ++i)
        {
            const auto& p = fillPhrases[(std::size_t) i];
            if (p.bars != bars)
                continue;

            // Skip fills that lean on lanes the user has excluded.
            int allowed = 0, total = 0;
            for (const auto& h : p.hits)
            {
                ++total;
                if ((laneMask & (1u << h.lane)) != 0)
                    ++allowed;
            }
            if (total == 0 || (float) allowed / (float) total < 0.8f)
                continue;

            candidates.push_back (i);
        }

        if (candidates.empty())
            return -1;

        std::sort (candidates.begin(), candidates.end(), [&] (int a, int b)
        {
            const auto& pa = fillPhrases[(std::size_t) a];
            const auto& pb = fillPhrases[(std::size_t) b];
            const float da = (pa.complexity - complexity) * (pa.complexity - complexity)
                           + (pa.intensity  - intensity)  * (pa.intensity  - intensity);
            const float db = (pb.complexity - complexity) * (pb.complexity - complexity)
                           + (pb.intensity  - intensity)  * (pb.intensity  - intensity);
            return da < db;
        });

        const int pool = std::min<int> (16, (int) candidates.size());
        for (int step = 0; step < pool; ++step)
        {
            const int i = candidates[(std::size_t) ((variation + step) % pool)];
            if (std::find (avoid.begin(), avoid.end(), i) == avoid.end())
                return i;
        }
        return candidates.front();
    }
}
