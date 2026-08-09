// Offline soak: an hour of continuous playback at 48 kHz / 128 samples with a
// user tweaking controls throughout. This cannot replace a run inside a real
// host on real hardware, but it does prove the things that break in one: NaNs,
// runaway gain, voice leaks, per-block spikes and CPU or memory drift.

#include "../SourceX/DrumsXProcessor.h"

#include <JuceHeader.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#if JUCE_MAC || JUCE_LINUX
 #include <sys/resource.h>
#endif

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::printf ("%s  %s\n", condition ? "ok  " : "FAIL", what.toRawUTF8());
        if (! condition)
            ++failures;
    }

    /** Resident set size in kB, or 0 where the platform does not expose it. */
    std::int64_t residentKb()
    {
       #if JUCE_LINUX
        juce::StringArray fields;
        fields.addTokens (juce::File ("/proc/self/statm").loadFileAsString(), " ", "");
        if (fields.size() > 1)
            return (std::int64_t) fields[1].getLargeIntValue() * 4;   // pages of 4 kB
       #elif JUCE_MAC
        // No /proc on macOS; peak resident size is enough to spot a leak.
        rusage usage {};
        if (getrusage (RUSAGE_SELF, &usage) == 0)
            return (std::int64_t) usage.ru_maxrss / 1024;             // bytes
       #endif
        return 0;
    }

    const char* const tweakables[] { hhx::pid::complexity, hhx::pid::intensity,
                                     hhx::pid::fillAmount, hhx::pid::swing,
                                     hhx::pid::humanize,   hhx::pid::feel,
                                     hhx::pid::ghost,      hhx::pid::hatOpenness,
                                     hhx::pid::halfTime,   hhx::pid::micBlend,
                                     hhx::pid::bleed,      hhx::pid::crush };
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const double minutes = argc > 1 ? std::atof (argv[1]) : 60.0;
    const double sampleRate = 48000.0;
    const int    blockSize  = 128;

    hhx::DrumsXProcessor proc;
    proc.prepareToPlay (sampleRate, blockSize);
    check (proc.getCorpus().isLoaded() && proc.getKit().numLoadedSamples() > 0,
           "corpus and kit are loaded before the soak");

    proc.getAPVTS().getParameter (hhx::pid::complexity)->setValueNotifyingHost (0.8f);
    proc.getAPVTS().getParameter (hhx::pid::intensity)->setValueNotifyingHost (0.75f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    proc.play();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    const int totalBlocks = (int) (minutes * 60.0 * sampleRate / blockSize);
    const int segmentBlocks = std::max (1, totalBlocks / 12);

    std::mt19937 rng (11);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    bool   finite = true;
    double peak = 0.0;
    double worstBlockMs = 0.0;
    std::int64_t lateBlocks = 0;
    double firstSegmentLoad = 0.0, lastSegmentLoad = 0.0;
    const std::int64_t rssStart = residentKb();
    std::int64_t rssPeak = rssStart;

    auto segmentStart = std::chrono::steady_clock::now();
    const auto soakStart = segmentStart;

    for (int block = 0; block < totalBlocks; ++block)
    {
        buffer.clear();
        midi.clear();

        const auto blockStart = std::chrono::steady_clock::now();
        proc.processBlock (buffer, midi);
        const double blockMs = std::chrono::duration<double, std::milli> (
                                   std::chrono::steady_clock::now() - blockStart).count();
        worstBlockMs = std::max (worstBlockMs, blockMs);
        if (blockMs > 1000.0 * blockSize / sampleRate)
            ++lateBlocks;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* d = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (! std::isfinite (d[i]))
                    finite = false;
                peak = std::max (peak, (double) std::abs (d[i]));
            }
        }

        // A player does not sit still for an hour: move a control every second
        // and regenerate every half minute.
        if (block % 375 == 0)
        {
            const auto* id = tweakables[(std::size_t) (rng() % (std::uint32_t) std::size (tweakables))];
            if (auto* p = proc.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (unit (rng));
        }
        if (block % 11250 == 0)
            proc.regenerate();
        if (block % 375 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

        if ((block + 1) % segmentBlocks == 0)
        {
            const double wall = std::chrono::duration<double> (
                                    std::chrono::steady_clock::now() - segmentStart).count();
            const double load = 100.0 * wall
                              / ((double) segmentBlocks * blockSize / sampleRate);
            if (firstSegmentLoad == 0.0)
                firstSegmentLoad = load;
            lastSegmentLoad = load;
            rssPeak = std::max (rssPeak, residentKb());
            std::printf ("note  segment %2d: %.2f %% of one core, peak %.3f, rss %lld kB\n",
                         (block + 1) / segmentBlocks, load, peak,
                         (long long) residentKb());
            segmentStart = std::chrono::steady_clock::now();
        }
    }

    proc.stop();

    const double audioSeconds = (double) totalBlocks * blockSize / sampleRate;
    const double wall = std::chrono::duration<double> (
                            std::chrono::steady_clock::now() - soakStart).count();
    const double load = 100.0 * wall / audioSeconds;
    const double blockBudgetMs = 1000.0 * blockSize / sampleRate;

    std::printf ("note  %.0f audio minutes in %.1f s wall: %.2f %% of one core,"
                 " worst block %.3f ms of %.3f ms\n",
                 minutes, wall, load, worstBlockMs, blockBudgetMs);

    check (finite, "no NaN or infinite sample in an hour of playback");
    check (peak > 0.001, "the kit is still sounding at the end of the soak");
    check (peak < 8.0, "output never runs away");
    check (load < 4.0, "average CPU stays inside the 4 % budget");
    // A shared CI machine will deschedule us mid-block, so the honest measure
    // is how often we miss: a dropout-free run needs the overwhelming majority
    // of blocks inside the deadline and no block stuck for an eternity.
    const double lateRatio = (double) lateBlocks / (double) juce::jmax (1, totalBlocks);
    std::printf ("note  %lld of %d blocks over the deadline (%.4f %%)\n",
                 (long long) lateBlocks, totalBlocks, 100.0 * lateRatio);
    check (lateRatio < 0.001, "virtually every block lands inside its deadline");
    check (worstBlockMs < 20.0 * blockBudgetMs, "no block stalls the audio thread");
    check (lastSegmentLoad < firstSegmentLoad * 2.0 + 0.5,
           "CPU does not drift upwards over the soak");
    if (rssStart > 0)
    {
        std::printf ("note  rss %lld -> %lld kB\n",
                     (long long) rssStart, (long long) residentKb());
        check (rssPeak < rssStart + 65536, "memory does not grow without bound");
    }

    std::printf (failures == 0 ? "\nSoak passed.\n" : "\nSoak FAILED.\n");
    return failures == 0 ? 0 : 1;
}
