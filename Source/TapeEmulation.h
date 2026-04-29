#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Analog tape emulation: wow/flutter (modulated delay), head-bump
// (60–100 Hz shelf boost), high-end roll-off, and subtle saturation.
// IPS control: 15 IPS = warmer / more flutter, 30 IPS = cleaner.
class TapeEmulation
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };

        headBumpFilter.prepare(spec);
        rolloffFilter.prepare(spec);

        // Head bump: low shelf boost at 80 Hz
        *headBumpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
            sr, 80.0f, 0.707f, juce::Decibels::decibelsToGain(3.0f));

        // High-end roll-off at 16 kHz
        *rolloffFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 16000.0f, 0.707f);

        // Flutter LFO
        flutterPhase = 0.0;

        // Delay buffer for wow/flutter
        delayBuffer.resize(static_cast<size_t>(static_cast<int>(sr)), 0.0f);
        delayBufferR.resize(static_cast<size_t>(static_cast<int>(sr)), 0.0f);
        writePos = 0;
    }

    void setSpeed (float ips) { tapeSpeed = juce::jlimit(15.0f, 30.0f, ips); }
    void setFlutter (float amt) { flutterAmount = juce::jlimit(0.0f, 1.0f, amt); }
    void setDrive (float d) { tapeDrive = juce::jlimit(0.0f, 1.0f, d); }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const int bufSize = static_cast<int>(delayBuffer.size());

        // IPS factor: 15 IPS = more character, 30 IPS = cleaner
        float ipsNorm = (30.0f - tapeSpeed) / 15.0f; // 1 at 15 IPS, 0 at 30 IPS

        // Wow/flutter
        float flutterDepthSamples = flutterAmount * ipsNorm * 0.001f * static_cast<float>(sr);
        float flutterRate = 3.5f + ipsNorm * 2.0f; // Hz

        for (int i = 0; i < numSamples; ++i)
        {
            // LFO for flutter
            float lfo = std::sin(2.0f * juce::MathConstants<float>::pi * static_cast<float>(flutterPhase));
            flutterPhase += flutterRate / sr;
            if (flutterPhase >= 1.0) flutterPhase -= 1.0;

            float delaySamples = 1.0f + flutterDepthSamples * lfo;

            for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
            {
                auto& dBuf = (ch == 0) ? delayBuffer : delayBufferR;
                float sample = buffer.getSample(ch, i);

                // Write to delay
                dBuf[static_cast<size_t>(writePos)] = sample;

                // Read with fractional delay (linear interpolation)
                float readPosF = static_cast<float>(writePos) - delaySamples;
                if (readPosF < 0.0f) readPosF += static_cast<float>(bufSize);
                int readIdx = static_cast<int>(readPosF);
                float frac = readPosF - static_cast<float>(readIdx);
                int nextIdx = (readIdx + 1) % bufSize;

                float delayed = dBuf[static_cast<size_t>(readIdx)] * (1.0f - frac) +
                                dBuf[static_cast<size_t>(nextIdx)] * frac;

                // Tape saturation (mild)
                if (tapeDrive > 0.01f)
                    delayed = std::tanh(delayed * (1.0f + tapeDrive * 3.0f * ipsNorm));

                buffer.setSample(ch, i, delayed);
            }

            writePos = (writePos + 1) % bufSize;
        }

        // Head bump + roll-off
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        if (ipsNorm > 0.1f)
            headBumpFilter.process(ctx);
        rolloffFilter.process(ctx);
    }

private:
    double sr = 44100.0;
    bool active = true;
    float tapeSpeed = 30.0f;
    float flutterAmount = 0.3f;
    float tapeDrive = 0.2f;
    double flutterPhase = 0.0;
    int writePos = 0;

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    IIRFilter headBumpFilter;
    IIRFilter rolloffFilter;

    std::vector<float> delayBuffer;
    std::vector<float> delayBufferR;
};

} // namespace humvocal
