#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    constexpr const char* kParamVariation = "variation";
    constexpr const char* kParamDensity   = "density";
    constexpr const char* kParamMode      = "mode"; // 0 = Groove, 1 = Fill
}

AIDrumAudioProcessor::AIDrumAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    // Seed with a simple default groove.
    currentPattern = backend.generate ({});
}

AIDrumAudioProcessor::~AIDrumAudioProcessor() = default;

APVTS::ParameterLayout AIDrumAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamVariation, 1 }, "Variation",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamDensity, 1 }, "Density",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamMode, 1 }, "Mode",
        juce::StringArray { "Groove", "Fill" }, 0));

    return { params.begin(), params.end() };
}

void AIDrumAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    playheadBeats = 0.0;
    lastBpm       = 120.0;
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
    req.mode      = mode;
    req.variation = apvts.getRawParameterValue (kParamVariation)->load();
    req.density   = apvts.getRawParameterValue (kParamDensity)->load();
    req.tempoBpm  = lastBpm;

    auto pattern = backend.generate (req);

    std::lock_guard<std::mutex> lock (patternMutex);
    currentPattern = std::move (pattern);
    playheadBeats  = 0.0;
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

    const double blockStartBeat = playheadBeats;
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
    playheadBeats = std::fmod (blockEndBeat, pattern.lengthInBeats);
}

void AIDrumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&         midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Try to pull tempo from the host.
    double bpm = lastBpm;
    if (auto* ph = getPlayHead())
    {
        if (auto info = ph->getPosition())
        {
            if (auto hostBpm = info->getBpm())
                bpm = *hostBpm;

            // If the host is actually playing, sync playhead to its ppq position.
            if (info->getIsPlaying())
            {
                if (auto ppq = info->getPpqPosition())
                {
                    std::lock_guard<std::mutex> lock (patternMutex);
                    const double len = std::max (1e-6, currentPattern.lengthInBeats);
                    playheadBeats = std::fmod (*ppq, len);
                    if (playheadBeats < 0.0) playheadBeats += len;
                }
            }
        }
    }
    lastBpm = bpm;

    renderPatternToMidiBuffer (midi, buffer.getNumSamples(), getSampleRate(), bpm);
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

// -------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIDrumAudioProcessor();
}
