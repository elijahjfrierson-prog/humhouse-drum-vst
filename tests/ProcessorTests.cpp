// Plugin-level tests: the guarantees that need the real processor, its
// parameter tree and the sampled kit. Runs headless, with no audio device.

#include "../SourceX/DrumsXProcessor.h"

#include <JuceHeader.h>

#include <chrono>
#include <cstdio>
#include <random>

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::printf ("%s  %s\n", condition ? "ok  " : "FAIL", what.toRawUTF8());
        if (! condition)
            ++failures;
    }

    /** Every automatable control that can plausibly re-render the performance. */
    juce::StringArray performanceParams()
    {
        juce::StringArray ids { hhx::pid::complexity, hhx::pid::intensity,
                                hhx::pid::fillAmount, hhx::pid::fillComplexity,
                                hhx::pid::fillBars, hhx::pid::fillStyle,
                                hhx::pid::fillVelVar, hhx::pid::kickVariation,
                                hhx::pid::swing, hhx::pid::swingGrid,
                                hhx::pid::humanize, hhx::pid::feel, hhx::pid::ghost,
                                hhx::pid::hatOpenness, hhx::pid::rideMode,
                                hhx::pid::halfTime, hhx::pid::phraseBars,
                                hhx::pid::timeSigNum, hhx::pid::timeSigDen,
                                hhx::pid::variationRhythm, hhx::pid::variationCymbal,
                                hhx::pid::followSections };
        return ids;
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    hhx::DrumsXProcessor proc;
    proc.prepareToPlay (48000.0, 128);

    check (proc.getCorpus().isLoaded(), "the bundled corpus loads inside the plugin");

    // 1. Non-destructive editing: 100 randomised control moves must not touch a
    //    single user-entered note.
    {
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        std::array<std::pair<int, int>, 24> edits {};
        for (std::size_t i = 0; i < edits.size(); ++i)
        {
            const int lane = (int) (rng() % (std::uint32_t) hhx::NumLanes);
            const int step = (int) (rng() % (std::uint32_t) hhx::DrumsXProcessor::kManualSteps);
            edits[i] = { lane, step };
            proc.setManualStep (lane, step, 0.25f + 0.5f * unit (rng));
        }

        std::array<float, 24> before {};
        for (std::size_t i = 0; i < edits.size(); ++i)
            before[i] = proc.getManualStep (edits[i].first, edits[i].second);

        const auto ids = performanceParams();
        bool intact = true;
        for (int move = 0; move < 100; ++move)
        {
            const auto& id = ids[move % ids.size()];
            if (auto* p = proc.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (unit (rng));
            if (move % 7 == 0)
                proc.regenerate();

            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

            for (std::size_t i = 0; i < edits.size(); ++i)
                if (! juce::exactlyEqual (proc.getManualStep (edits[i].first, edits[i].second),
                                          before[i]))
                    intact = false;
        }
        check (intact, "100 randomised control moves destroy no user edits");
    }

    // 2. The kit plays: a busy 16th-note groove renders audio, and the whole
    //    signal path costs well under the 4 % CPU budget at 48 kHz / 128.
    {
        proc.getAPVTS().getParameter (hhx::pid::complexity)->setValueNotifyingHost (0.95f);
        proc.getAPVTS().getParameter (hhx::pid::intensity)->setValueNotifyingHost (0.9f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        proc.play();

        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;

        const int    blocks  = 48000 * 60 / 128;           // one minute of audio
        const double audioSeconds = (double) blocks * 128.0 / 48000.0;
        double peak = 0.0;

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            peak = std::max (peak, (double) buffer.getMagnitude (0, buffer.getNumSamples()));
        }
        const double wall = std::chrono::duration<double> (
                                std::chrono::steady_clock::now() - start).count();
        const double load = 100.0 * wall / audioSeconds;

        std::printf ("note  one minute of busy playback: %.2f %% of one core, peak %.3f\n",
                     load, peak);
        check (peak > 0.001, "a busy groove actually makes sound");
        check (load < 4.0, "CPU stays inside the 4 % budget at 48 kHz / 128 samples");
        proc.stop();
    }

    // 3. State round-trip: UI scale, seed and user edits all survive a reload,
    //    which is what a saved project depends on.
    {
        proc.setUiScale (1.25f);
        proc.setManualStep (hhx::LaneSnare, 3, 0.8f);
        const auto seed = proc.getSeed();

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        hhx::DrumsXProcessor reloaded;
        reloaded.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (reloaded.getUiScale() - 1.25f) < 0.001f, "UI scale is saved with the project");
        check (reloaded.getSeed() == seed, "the performance seed is saved with the project");
        check (std::abs (reloaded.getManualStep (hhx::LaneSnare, 3) - 0.8f) < 0.001f,
               "user edits are saved with the project");
    }

    // 4. Kit format: a folder of 8 velocity layers x 4 round robins x 3 mics
    //    loads at full depth, through kit.json and through the filenames alone.
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("hhdx_kit_test");
        folder.deleteRecursively();
        folder.createDirectory();

        juce::WavAudioFormat wav;
        juce::var pieces (juce::Array<juce::var> {});
        juce::Random rng (11);

        for (int layer = 1; layer <= 8; ++layer)
            for (int variant = 1; variant <= 4; ++variant)
                for (const auto* mic : { "close", "oh", "room" })
                {
                    const juce::String name = juce::String ("snare_v") + juce::String (layer)
                                            + "_rr" + juce::String (variant)
                                            + (juce::String (mic) == "close" ? juce::String()
                                                                             : "_" + juce::String (mic))
                                            + ".wav";
                    const auto file = folder.getChildFile (name);

                    juce::AudioBuffer<float> buffer (1, 2048);
                    const float level = 0.05f * (float) layer;
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (0, i, level * rng.nextFloat() * std::exp (-4.0f * (float) i / 2048.0f));

                    if (auto* writer = wav.createWriterFor (new juce::FileOutputStream (file),
                                                            48000.0, 1, 16, {}, 0))
                    {
                        std::unique_ptr<juce::AudioFormatWriter> owner (writer);
                        owner->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                    }

                    auto* piece = new juce::DynamicObject();
                    piece->setProperty ("piece", "snare");
                    piece->setProperty ("layer", layer);
                    piece->setProperty ("variant", variant);
                    piece->setProperty ("mic", mic);
                    piece->setProperty ("file", name);
                    pieces.getArray()->add (juce::var (piece));
                }

        auto* root = new juce::DynamicObject();
        root->setProperty ("name", "Test Kit");
        root->setProperty ("version", "3");
        root->setProperty ("pieces", pieces);
        folder.getChildFile ("kit.json").replaceWithText (juce::JSON::toString (juce::var (root)));

        hhx::KitEngine kit;
        kit.prepare (48000.0, 128);
        const int loadedManifest = kit.loadKitFolder (folder);
        check (loadedManifest == 8 * 4 * 3, "kit.json loads every layer, round robin and mic");
        check (kit.getKitName() == "Test Kit" && kit.getKitVersion() == "3",
               "kit.json supplies the kit name and content version");
        check (kit.numLayersForLane (hhx::LaneSnare) == 8, "the kit exposes 8 velocity layers");
        check (kit.numVariantsForLane (hhx::LaneSnare, 3) == 4, "each layer exposes 4 round robins");
        check (kit.laneHasMic (hhx::LaneSnare, hhx::MicOverhead)
               && kit.laneHasMic (hhx::LaneSnare, hhx::MicRoom),
               "overhead and room mics are loaded alongside the close mic");

        // Loudest layer only for hard hits: the softest sample must not be what
        // a full-velocity stroke plays.
        juce::AudioBuffer<float> soft (2, 4096), hard (2, 4096);
        const auto renderOne = [&kit] (juce::AudioBuffer<float>& buffer, float vel)
        {
            buffer.clear();
            kit.allNotesOff();
            kit.noteOn (hhx::LaneSnare, vel, 0);
            kit.renderNextBlock (buffer, 0, buffer.getNumSamples());
            return buffer.getMagnitude (0, buffer.getNumSamples());
        };
        const float softPeak = renderOne (soft, 0.15f);
        const float hardPeak = renderOne (hard, 1.0f);
        check (hardPeak > softPeak * 1.5f, "velocity selects louder layers, not just louder gain");

        juce::AudioBuffer<float> first (2, 4096), second (2, 4096);
        kit.allNotesOff();
        first.clear();
        kit.noteOn (hhx::LaneSnare, 0.7f, 0);
        kit.renderNextBlock (first, 0, first.getNumSamples());
        kit.allNotesOff();
        second.clear();
        kit.noteOn (hhx::LaneSnare, 0.7f, 1);
        kit.renderNextBlock (second, 0, second.getNumSamples());
        bool identical = true;
        for (int i = 0; i < first.getNumSamples() && identical; ++i)
            if (std::abs (first.getSample (0, i) - second.getSample (0, i)) > 1.0e-6f)
                identical = false;
        check (! identical, "consecutive strokes at the same velocity play different samples");

        folder.getChildFile ("kit.json").deleteFile();
        hhx::KitEngine fromNames;
        fromNames.prepare (48000.0, 128);
        check (fromNames.loadKitFolder (folder) == 8 * 4 * 3
               && fromNames.numLayersForLane (hhx::LaneSnare) == 8
               && fromNames.numVariantsForLane (hhx::LaneSnare, 0) == 4,
               "the filename convention alone reproduces the same kit");

        folder.deleteRecursively();
    }

    std::printf ("\n%s\n", failures == 0 ? "All processor tests passed."
                                         : "Processor tests FAILED.");
    return failures == 0 ? 0 : 1;
}
