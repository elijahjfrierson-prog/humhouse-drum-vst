#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    constexpr const char* kParamVariation     = "variation";
    constexpr const char* kParamComplexity    = "complexity";
    constexpr const char* kParamVelocity      = "velocity";
    constexpr const char* kParamHumanize      = "humanize";
    constexpr const char* kParamPatternLength = "patternLength";
    constexpr const char* kParamGenre         = "genre";
    constexpr const char* kParamMode          = "mode"; // 0 = Groove, 1 = Fill

    const juce::StringArray kPatternLengthChoices {
        "1/16 note", "1/8 note", "1/4 note", "1/2 bar", "1 bar", "2 bars"
    };

    juce::StringArray buildGenreChoices()
    {
        juce::StringArray arr;
        for (const auto& n : aidrum::genreDisplayNames())
            arr.add (juce::String (n));
        return arr;
    }
}

double AIDrumAudioProcessor::patternLengthBeatsFromChoice (int choiceIndex)
{
    switch (choiceIndex)
    {
        case 0: return 0.25;  // 1/16 note
        case 1: return 0.5;   // 1/8 note
        case 2: return 1.0;   // 1/4 note
        case 3: return 2.0;   // 1/2 bar
        case 4: return 4.0;   // 1 bar
        case 5: return 8.0;   // 2 bars
        default: return 4.0;
    }
}

AIDrumAudioProcessor::AIDrumAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    // Seed with a simple default groove.
    aidrum::GenerationRequest req;
    req.lengthInBeats = patternLengthBeatsFromChoice (
        (int) apvts.getRawParameterValue (kParamPatternLength)->load());
    req.genre = static_cast<aidrum::Genre> (
        (int) apvts.getRawParameterValue (kParamGenre)->load());
    currentPattern = backend.generate (req);
}

AIDrumAudioProcessor::~AIDrumAudioProcessor() = default;

APVTS::ParameterLayout AIDrumAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamVariation, 1 }, "Variation",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamComplexity, 1 }, "Complexity",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamVelocity, 1 }, "Velocity",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.9f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamHumanize, 1 }, "Humanize",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamPatternLength, 1 }, "Pattern Length",
        kPatternLengthChoices, 4)); // default: 1 bar

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamGenre, 1 }, "Genre",
        buildGenreChoices(), 0)); // default: Auto

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamMode, 1 }, "Mode",
        juce::StringArray { "Groove", "Fill" }, 0));

    return { params.begin(), params.end() };
}

void AIDrumAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    playheadBeats.store (0.0, std::memory_order_relaxed);
    lastBpm.store (120.0, std::memory_order_relaxed);
    juce::ignoreUnused (sampleRate);
}

void AIDrumAudioProcessor::releaseResources() {}

bool AIDrumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AIDrumAudioProcessor::requestGeneration (aidrum::GenerationMode mode)
{
    aidrum::GenerationRequest req;
    req.mode          = mode;
    req.variation     = apvts.getRawParameterValue (kParamVariation)->load();
    req.complexity    = apvts.getRawParameterValue (kParamComplexity)->load();
    req.velocity      = apvts.getRawParameterValue (kParamVelocity)->load();
    req.humanize      = apvts.getRawParameterValue (kParamHumanize)->load();
    req.lengthInBeats = patternLengthBeatsFromChoice (
                            (int) apvts.getRawParameterValue (kParamPatternLength)->load());
    req.genre         = static_cast<aidrum::Genre> (
                            (int) apvts.getRawParameterValue (kParamGenre)->load());
    req.tempoBpm      = lastBpm.load (std::memory_order_relaxed);

    auto pattern = backend.generate (req);

    {
        std::lock_guard<std::mutex> lock (patternMutex);
        currentPattern = std::move (pattern);
    }
    playheadBeats.store (0.0, std::memory_order_release);
}

aidrum::MidiPattern AIDrumAudioProcessor::getCurrentPattern() const
{
    std::lock_guard<std::mutex> lock (patternMutex);
    return currentPattern;
}

void AIDrumAudioProcessor::renderPatternToMidiBuffer (juce::MidiBuffer& midiOut,
                                                      int               numSamples,
                                                      double            sampleRate,
                                                      double            bpm)
{
    aidrum::MidiPattern pattern;
    {
        std::lock_guard<std::mutex> lock (patternMutex);
        pattern = currentPattern;
    }

    if (pattern.notes.empty() || pattern.lengthInBeats <= 0.0)
        return;

    const double secondsPerBeat = 60.0 / std::max (1.0, bpm);
    const double blockBeats     = (static_cast<double> (numSamples) / sampleRate) / secondsPerBeat;

    const double blockStartBeat = playheadBeats.load (std::memory_order_acquire);
    const double blockEndBeat   = blockStartBeat + blockBeats;

    for (const auto& note : pattern.notes)
    {
        // Loop the pattern across blocks.
        for (double loopOffset = 0.0;
             loopOffset <= blockEndBeat + pattern.lengthInBeats;
             loopOffset += pattern.lengthInBeats)
        {
            const double onBeat  = note.startBeat + loopOffset;
            const double offBeat = onBeat + note.lengthBeat;

            if (onBeat >= blockStartBeat && onBeat < blockEndBeat)
            {
                const int sample = static_cast<int> ((onBeat - blockStartBeat) * secondsPerBeat * sampleRate);
                midiOut.addEvent (juce::MidiMessage::noteOn  (10, note.noteNumber,
                                                              static_cast<juce::uint8> (note.velocity * 127.0f)),
                                  juce::jlimit (0, numSamples - 1, sample));
            }

            if (offBeat >= blockStartBeat && offBeat < blockEndBeat)
            {
                const int sample = static_cast<int> ((offBeat - blockStartBeat) * secondsPerBeat * sampleRate);
                midiOut.addEvent (juce::MidiMessage::noteOff (10, note.noteNumber),
                                  juce::jlimit (0, numSamples - 1, sample));
            }
        }
    }

    // Wrap playhead within pattern length.
    playheadBeats.store (std::fmod (blockEndBeat, pattern.lengthInBeats),
                         std::memory_order_release);
}

void AIDrumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&         midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Don't let incoming MIDI leak into our generated pattern output.
    // acceptsMidi() is true so hosts may route a MIDI keyboard here;
    // producesMidi() is also true so whatever remains in `midi` is
    // forwarded to downstream instruments. We generate everything fresh.
    midi.clear();

    // Try to pull tempo from the host.
    double bpm = lastBpm.load (std::memory_order_relaxed);

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition(); pos.hasValue())
        {
            if (auto hostBpm = pos->getBpm(); hostBpm.hasValue())
                bpm = *hostBpm;

            if (auto ppq = pos->getPpqPosition(); ppq.hasValue())
                playheadBeats.store (*ppq, std::memory_order_release);
        }
    }

    lastBpm.store (bpm, std::memory_order_relaxed);

    renderPatternToMidiBuffer (midi,
                               buffer.getNumSamples(),
                               getSampleRate(),
                               bpm);
}

juce::AudioProcessorEditor* AIDrumAudioProcessor::createEditor()
{
    return new AIDrumAudioProcessorEditor (*this);
}

void AIDrumAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AIDrumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIDrumAudioProcessor();
}
