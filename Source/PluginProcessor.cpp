#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <numeric>
#include <random>

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

    // v0.6.0 Logic-Drummer controls
    constexpr const char* kParamSwing         = "swing";
    constexpr const char* kParamFillsProb     = "fillsProb";
    constexpr const char* kParamHalfTime      = "halfTime";
    constexpr const char* kParamHiHat         = "hiHat";

    // v0.7.0 DrumKit voicing
    constexpr const char* kParamDrumKit       = "drumKit";

    const juce::StringArray kPatternLengthChoices {
        "1/16 note", "1/8 note", "1/4 note", "1/2 bar", "1 bar", "2 bars"
    };

    const juce::StringArray kHiHatChoices {
        "Dynamic", "Closed", "Open", "Ride"
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
    // Seed arrangement with a single initial groove so the visualizer has
    // something to draw on first launch.
    arrangement.push_back (backend.generate (buildRequestForMode (aidrum::GenerationMode::Groove)));

    // Manual pattern starts empty; 16 bars of 4/4 = 64 beats.
    manualPattern.lengthInBeats = static_cast<double> (manualNumBars * 4);
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

    // v0.6.0 Logic-Drummer controls
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamSwing, 1 }, "Swing",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamFillsProb, 1 }, "Fills",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kParamHalfTime, 1 }, "Half-Time", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kParamHiHat, 1 }, "Hi-Hat",
        kHiHatChoices, 0));

    // v0.7.0 DrumKit — 20 labeled kits, each with unique GM voicing
    // + velocity / ghost / accent profile.
    {
        juce::StringArray kitNames;
        for (const auto& n : aidrum::drumKitDisplayNames())
            kitNames.add (juce::String (n));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { kParamDrumKit, 1 }, "Drum Kit",
            kitNames,
            (int) aidrum::DrumKit::LudwigSupraphonicClassicRock));
    }

    return { params.begin(), params.end() };
}

void AIDrumAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    playheadBeats.store (0.0, std::memory_order_relaxed);
    lastBpm.store (120.0, std::memory_order_relaxed);
    drumSynth.prepare (sampleRate);
    drumSynth.reset();
    busMixer.prepare (sampleRate, 0, 2);
    busMixer.reset();
    hostTransportSeen.store (false, std::memory_order_relaxed);
}

void AIDrumAudioProcessor::releaseResources() {}

bool AIDrumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

aidrum::GenerationRequest
AIDrumAudioProcessor::buildRequestForMode (aidrum::GenerationMode mode) const
{
    aidrum::GenerationRequest req;
    req.mode          = mode;
    req.variation     = apvts.getRawParameterValue (kParamVariation )->load();
    req.complexity    = apvts.getRawParameterValue (kParamComplexity)->load();
    req.velocity      = apvts.getRawParameterValue (kParamVelocity  )->load();
    req.humanize      = apvts.getRawParameterValue (kParamHumanize  )->load();
    req.lengthInBeats = patternLengthBeatsFromChoice (
                            (int) apvts.getRawParameterValue (kParamPatternLength)->load());
    req.genre         = static_cast<aidrum::Genre> (
                            (int) apvts.getRawParameterValue (kParamGenre)->load());
    req.tempoBpm      = lastBpm.load (std::memory_order_relaxed);

    req.swing         = apvts.getRawParameterValue (kParamSwing)    ->load();
    req.fillsProb     = apvts.getRawParameterValue (kParamFillsProb)->load();
    req.halfTime      = apvts.getRawParameterValue (kParamHalfTime) ->load() > 0.5f;
    req.hiHatMode     = static_cast<aidrum::HiHatMode> (
                            (int) apvts.getRawParameterValue (kParamHiHat)->load());
    req.kit           = static_cast<aidrum::DrumKit> (
                            (int) apvts.getRawParameterValue (kParamDrumKit)->load());
    return req;
}

void AIDrumAudioProcessor::appendRegion (aidrum::GenerationMode requestedMode)
{
    auto req = buildRequestForMode (requestedMode);

    // Fills-probability: chance to reshape a Groove call into a Fill, so
    // chained groove chains occasionally inject a fill like Logic's Drummer.
    if (requestedMode == aidrum::GenerationMode::Groove && req.fillsProb > 0.0f)
    {
        std::mt19937_64 rng (static_cast<std::uint64_t> (std::random_device{}()));
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        if (unit (rng) < req.fillsProb)
            req.mode = aidrum::GenerationMode::Fill;
    }

    auto pattern = backend.generate (req);

    std::lock_guard<std::mutex> lock (arrangementMutex);
    arrangement.push_back (std::move (pattern));
    // Note: deliberately do NOT reset the playhead here — we want the
    // sequencer to keep rolling into the newly-appended region.
}

void AIDrumAudioProcessor::undoLastRegion()
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    if (arrangement.size() > 1)
        arrangement.pop_back();
}

void AIDrumAudioProcessor::clearArrangement()
{
    auto fresh = backend.generate (buildRequestForMode (aidrum::GenerationMode::Groove));

    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        arrangement.clear();
        arrangement.push_back (std::move (fresh));
    }

    playheadBeats.store (0.0, std::memory_order_release);
}

std::vector<aidrum::MidiPattern> AIDrumAudioProcessor::getArrangement() const
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    return arrangement;
}

double AIDrumAudioProcessor::getArrangementTotalBeats() const
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    double total = 0.0;
    for (const auto& p : arrangement)
        total += std::max (0.001, p.lengthInBeats);
    return total;
}

double AIDrumAudioProcessor::getPlayheadBeats() const
{
    return playheadBeats.load (std::memory_order_acquire);
}

aidrum::MidiPattern AIDrumAudioProcessor::getCurrentPattern() const
{
    std::lock_guard<std::mutex> lock (arrangementMutex);
    return arrangement.empty() ? aidrum::MidiPattern{} : arrangement.back();
}

// ============================================================================
// v0.8.0 Manual Mode
// ============================================================================

bool AIDrumAudioProcessor::isManualMode() const
{
    return manualModeActive.load (std::memory_order_acquire);
}

void AIDrumAudioProcessor::setManualMode (bool shouldBeOn)
{
    manualModeActive.store (shouldBeOn, std::memory_order_release);
    // Restart playhead when flipping modes so the user hears the
    // new source from beat 1.
    playheadBeats.store (0.0, std::memory_order_release);
}

namespace
{
    // 16th note = 0.25 beats.
    constexpr double kStepBeats = 0.25;
}

void AIDrumAudioProcessor::setManualCell (int midiNote, int stepIndex, float velocity)
{
    if (stepIndex < 0) return;
    std::lock_guard<std::mutex> lock (manualMutex);

    const double startBeat = stepIndex * kStepBeats;
    if (startBeat >= manualPattern.lengthInBeats) return;

    // Replace any existing note on this cell so repeat-clicks update velocity
    // rather than stacking duplicates.
    for (auto& n : manualPattern.notes)
    {
        if (n.noteNumber == midiNote
            && std::abs (n.startBeat - startBeat) < 1.0e-4)
        {
            n.velocity = juce::jlimit (0.05f, 1.0f, velocity);
            return;
        }
    }

    aidrum::MidiNote note;
    note.noteNumber = midiNote;
    note.startBeat  = startBeat;
    note.lengthBeat = kStepBeats * 0.9; // slightly shorter so cells read distinct
    note.velocity   = juce::jlimit (0.05f, 1.0f, velocity);
    manualPattern.notes.push_back (note);
}

void AIDrumAudioProcessor::clearManualCell (int midiNote, int stepIndex)
{
    if (stepIndex < 0) return;
    std::lock_guard<std::mutex> lock (manualMutex);
    const double startBeat = stepIndex * kStepBeats;
    manualPattern.notes.erase (
        std::remove_if (manualPattern.notes.begin(), manualPattern.notes.end(),
                        [&] (const aidrum::MidiNote& n)
                        {
                            return n.noteNumber == midiNote
                                && std::abs (n.startBeat - startBeat) < 1.0e-4;
                        }),
        manualPattern.notes.end());
}

void AIDrumAudioProcessor::clearManualPattern()
{
    std::lock_guard<std::mutex> lock (manualMutex);
    manualPattern.notes.clear();
}

aidrum::MidiPattern AIDrumAudioProcessor::getManualPattern() const
{
    std::lock_guard<std::mutex> lock (manualMutex);
    return manualPattern;
}

int AIDrumAudioProcessor::getManualNumBars() const
{
    std::lock_guard<std::mutex> lock (manualMutex);
    return manualNumBars;
}

void AIDrumAudioProcessor::setManualNumBars (int bars)
{
    bars = juce::jlimit (1, 32, bars);
    std::lock_guard<std::mutex> lock (manualMutex);
    manualNumBars = bars;
    manualPattern.lengthInBeats = static_cast<double> (bars * 4);
    // Drop notes that no longer fit.
    const double maxBeat = manualPattern.lengthInBeats;
    manualPattern.notes.erase (
        std::remove_if (manualPattern.notes.begin(), manualPattern.notes.end(),
                        [&] (const aidrum::MidiNote& n) { return n.startBeat >= maxBeat; }),
        manualPattern.notes.end());
}

aidrum::MidiPattern AIDrumAudioProcessor::withActiveKitApplied (aidrum::MidiPattern p) const
{
    const auto kit = static_cast<aidrum::DrumKit> (
        (int) apvts.getRawParameterValue (kParamDrumKit)->load());
    const auto& prof = aidrum::drumKitProfile (kit);

    // GM drum constants (mirror AIBackend's internal ones).
    constexpr int kKickGM = 36, kSnareGM = 38, kSideStickGM = 37, kClapGM = 39;
    constexpr int kClosedHatGM = 42, kPedalHatGM = 44, kOpenHatGM = 46;
    constexpr int kRideGM = 51, kRideBellGM = 53, kCrashGM = 49, kCrashAltGM = 57, kChinaGM = 52;
    constexpr int kLowTomGM = 41, kMidTomGM = 45, kHighTomGM = 48;

    for (auto& n : p.notes)
    {
        if      (n.noteNumber == kKickGM)      n.noteNumber = prof.kick;
        else if (n.noteNumber == kSnareGM)
            n.noteNumber = (n.velocity <= prof.ghostThreshold && prof.ghostSnare != prof.snare)
                             ? prof.ghostSnare : prof.snare;
        else if (n.noteNumber == kSideStickGM) n.noteNumber = prof.sideStick;
        else if (n.noteNumber == kClapGM)      n.noteNumber = prof.clap;
        else if (n.noteNumber == kClosedHatGM) n.noteNumber = prof.closedHat;
        else if (n.noteNumber == kPedalHatGM)  n.noteNumber = prof.pedalHat;
        else if (n.noteNumber == kOpenHatGM)   n.noteNumber = prof.openHat;
        else if (n.noteNumber == kRideGM)      n.noteNumber = prof.ride;
        else if (n.noteNumber == kRideBellGM)  n.noteNumber = prof.rideBell;
        else if (n.noteNumber == kCrashGM)     n.noteNumber = prof.crash;
        else if (n.noteNumber == kCrashAltGM)  n.noteNumber = prof.crashAlt;
        else if (n.noteNumber == kChinaGM)     n.noteNumber = prof.china;
        else if (n.noteNumber == kLowTomGM)    n.noteNumber = prof.lowTom;
        else if (n.noteNumber == kMidTomGM)    n.noteNumber = prof.midTom;
        else if (n.noteNumber == kHighTomGM)   n.noteNumber = prof.highTom;

        n.velocity = juce::jlimit (0.01f, 1.0f, n.velocity * prof.velocityScale);
    }
    return p;
}

void AIDrumAudioProcessor::commitManualPatternAsRegion()
{
    aidrum::MidiPattern remapped;
    {
        std::lock_guard<std::mutex> lock (manualMutex);
        remapped = manualPattern;
    }
    remapped = withActiveKitApplied (std::move (remapped));

    std::lock_guard<std::mutex> lock (arrangementMutex);
    arrangement.push_back (std::move (remapped));
}

bool AIDrumAudioProcessor::writeArrangementAsMidiFile (const juce::File& dest) const
{
    std::vector<aidrum::MidiPattern> snapshot;

    if (manualModeActive.load (std::memory_order_acquire))
    {
        // Manual mode: export just the manual pattern, with active kit remap.
        aidrum::MidiPattern manual;
        {
            std::lock_guard<std::mutex> lock (manualMutex);
            manual = manualPattern;
        }
        snapshot.push_back (withActiveKitApplied (std::move (manual)));
    }
    else
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        snapshot = arrangement;
    }

    double total = 0.0;
    for (const auto& p : snapshot)
        total += std::max (0.0, p.lengthInBeats);

    if (snapshot.empty() || total <= 0.0)
        return false;

    constexpr short kPPQ = 960;

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (500000)); // 120 BPM

    double regionOffset = 0.0;
    for (const auto& region : snapshot)
    {
        for (const auto& note : region.notes)
        {
            const double onTicks  = (regionOffset + note.startBeat) * kPPQ;
            const double offTicks = (regionOffset + note.startBeat
                                     + std::max (0.01, note.lengthBeat)) * kPPQ;
            const auto vel = static_cast<juce::uint8> (
                juce::jlimit (1.0f, 127.0f, note.velocity * 127.0f));

            auto on  = juce::MidiMessage::noteOn  (10, note.noteNumber, vel);
            auto off = juce::MidiMessage::noteOff (10, note.noteNumber);
            on .setTimeStamp (onTicks);
            off.setTimeStamp (offTicks);
            seq.addEvent (on);
            seq.addEvent (off);
        }
        regionOffset += std::max (0.0, region.lengthInBeats);
    }

    auto eot = juce::MidiMessage::endOfTrack();
    eot.setTimeStamp (regionOffset * kPPQ);
    seq.addEvent (eot);

    seq.updateMatchedPairs();
    seq.sort();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kPPQ);
    file.addTrack (seq);

    dest.deleteFile();
    if (auto stream = dest.createOutputStream())
    {
        file.writeTo (*stream);
        stream->flush();
        return true;
    }
    return false;
}

void AIDrumAudioProcessor::renderArrangementToMidiBuffer (juce::MidiBuffer& midiOut,
                                                          int               numSamples,
                                                          double            sampleRate,
                                                          double            bpm)
{
    std::vector<aidrum::MidiPattern> snapshot;

    if (manualModeActive.load (std::memory_order_acquire))
    {
        aidrum::MidiPattern manual;
        {
            std::lock_guard<std::mutex> lock (manualMutex);
            manual = manualPattern;
        }
        snapshot.push_back (withActiveKitApplied (std::move (manual)));
    }
    else
    {
        std::lock_guard<std::mutex> lock (arrangementMutex);
        snapshot = arrangement;
    }

    if (snapshot.empty())
        return;

    double total = 0.0;
    for (const auto& p : snapshot)
        total += std::max (0.001, p.lengthInBeats);
    if (total <= 0.0)
        return;

    const double secondsPerBeat = 60.0 / std::max (1.0, bpm);
    const double blockBeats     = (static_cast<double> (numSamples) / sampleRate) / secondsPerBeat;

    const double blockStartBeat = playheadBeats.load (std::memory_order_acquire);
    const double blockEndBeat   = blockStartBeat + blockBeats;
    const bool   looping        = loopingEnabled.load (std::memory_order_acquire);

    // If looping is OFF, clamp the emission window at the arrangement end
    // so the song plays once and stops.
    const double emissionEnd = looping ? blockEndBeat : std::min (blockEndBeat, total);

    // Loop arrangement: for each pass, emit notes that fall within [blockStart, blockEnd).
    double loopOffset = 0.0;
    // Walk enough loop copies to cover this block.
    while (loopOffset <= emissionEnd + total)
    {
        double regionOffset = loopOffset;
        for (const auto& region : snapshot)
        {
            const double regionLen = std::max (0.001, region.lengthInBeats);
            for (const auto& note : region.notes)
            {
                const double onBeat  = regionOffset + note.startBeat;
                const double offBeat = onBeat + std::max (0.01, note.lengthBeat);

                if (onBeat >= blockStartBeat && onBeat < emissionEnd)
                {
                    const int sample = static_cast<int> (
                        (onBeat - blockStartBeat) * secondsPerBeat * sampleRate);
                    midiOut.addEvent (
                        juce::MidiMessage::noteOn (10, note.noteNumber,
                            static_cast<juce::uint8> (
                                juce::jlimit (1.0f, 127.0f, note.velocity * 127.0f))),
                        juce::jlimit (0, numSamples - 1, sample));
                }

                if (offBeat >= blockStartBeat && offBeat < emissionEnd)
                {
                    const int sample = static_cast<int> (
                        (offBeat - blockStartBeat) * secondsPerBeat * sampleRate);
                    midiOut.addEvent (juce::MidiMessage::noteOff (10, note.noteNumber),
                                      juce::jlimit (0, numSamples - 1, sample));
                }
            }
            regionOffset += regionLen;
        }
        loopOffset += total;
    }

    // Advance (and optionally wrap) the playhead.
    if (looping)
    {
        playheadBeats.store (std::fmod (blockEndBeat, total),
                             std::memory_order_release);
    }
    else
    {
        const double next = std::min (blockEndBeat, total);
        playheadBeats.store (next, std::memory_order_release);

        // When the arrangement ends and looping is off, auto-stop so the
        // Play button becomes 'start from 0' again next press.
        if (next >= total - 1.0e-6)
        {
            transportState.store ((int) TransportState::Stopped,
                                  std::memory_order_release);
            playheadBeats.store (0.0, std::memory_order_release);
        }
    }
}

void AIDrumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&         midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Don't let incoming MIDI leak into our generated pattern output.
    midi.clear();

    // Try to pull tempo + host transport state.
    double bpm = lastBpm.load (std::memory_order_relaxed);
    bool   hostIsPlaying = false;
    bool   hostDrivesPlayhead = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition(); pos.hasValue())
        {
            if (auto hostBpm = pos->getBpm(); hostBpm.hasValue())
                bpm = *hostBpm;

            hostIsPlaying = pos->getIsPlaying();

            // Only let the host own the transport when it exposes a musical
            // position. Standalone commonly has a playhead object too, but no
            // meaningful PPQ timeline; in that case we keep using our internal
            // clock and the UI transport buttons.
            if (auto ppq = pos->getPpqPosition(); ppq.hasValue())
            {
                hostDrivesPlayhead = true;

                if (hostIsPlaying)
                {
                    // Wrap host ppq into our arrangement length so looping still works.
                    const double total = getArrangementTotalBeats();
                    if (total > 0.0)
                        playheadBeats.store (std::fmod (std::max (0.0, *ppq), total),
                                             std::memory_order_release);
                }
            }
        }
    }

    hostTransportSeen.store (hostDrivesPlayhead, std::memory_order_relaxed);
    lastBpm.store (bpm, std::memory_order_relaxed);

    // Decide whether to advance / emit this block.
    const auto state = (TransportState) transportState.load (std::memory_order_acquire);
    const bool shouldPlay = hostDrivesPlayhead ? hostIsPlaying
                                               : (state == TransportState::Playing);

    if (! shouldPlay)
    {
        // Silent block. Still render existing synth tails through the mixer
        // so held voices decay naturally when the user hits pause.
        busMixer.beginBlock (buffer.getNumSamples());
        drumSynth.renderIntoBuses (busMixer, buffer.getNumSamples());
        busMixer.process (buffer);
        return;
    }

    renderArrangementToMidiBuffer (midi,
                                   buffer.getNumSamples(),
                                   getSampleRate(),
                                   bpm);

    // Feed the freshly-generated MIDI notes into the drum synth so the
    // Standalone app (and any DAW listening to audio output) makes sound.
    drumSynth.setMasterGain (outputLevel.load (std::memory_order_relaxed));
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            drumSynth.noteOn (msg.getNoteNumber(),
                              msg.getFloatVelocity(),
                              meta.samplePosition);
    }

    // v1.1.0 — render each voice into its own bus, then mixer sums to output.
    busMixer.beginBlock (buffer.getNumSamples());
    drumSynth.renderIntoBuses (busMixer, buffer.getNumSamples());
    busMixer.process (buffer);
}

// ============================================================================
// v1.0.0 — Transport API
// ============================================================================
void AIDrumAudioProcessor::play()
{
    transportState.store ((int) TransportState::Playing, std::memory_order_release);
}

void AIDrumAudioProcessor::pause()
{
    // Freeze the playhead where it is; voices decay naturally.
    transportState.store ((int) TransportState::Paused, std::memory_order_release);
}

void AIDrumAudioProcessor::stop()
{
    transportState.store ((int) TransportState::Stopped, std::memory_order_release);
    playheadBeats.store (0.0, std::memory_order_release);
    drumSynth.reset();
}

void AIDrumAudioProcessor::setLooping (bool shouldLoop)
{
    loopingEnabled.store (shouldLoop, std::memory_order_release);
}

bool AIDrumAudioProcessor::isLooping() const
{
    return loopingEnabled.load (std::memory_order_acquire);
}

AIDrumAudioProcessor::TransportState AIDrumAudioProcessor::getTransportState() const
{
    return (TransportState) transportState.load (std::memory_order_acquire);
}

void AIDrumAudioProcessor::setOutputLevel (float level01)
{
    outputLevel.store (juce::jlimit (0.0f, 1.0f, level01), std::memory_order_release);
}

float AIDrumAudioProcessor::getOutputLevel() const
{
    return outputLevel.load (std::memory_order_acquire);
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
