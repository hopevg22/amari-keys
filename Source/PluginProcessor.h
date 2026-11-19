#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Organ.h"
#include "Keys.h"
#include "Synth.h"
#include "Drums.h"

class AmariKeysProcessor : public juce::AudioProcessor
{
public:
    AmariKeysProcessor();
    ~AmariKeysProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Amari Keys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    // UI -> audio
    void triggerPad (int pad, float vel) { if (pad >= 0 && pad < DrumEngine::numPads) padRequests[pad].store (vel); }
    void panic() { panicRequest.store (true); }
    // audio -> UI (pad flash counters)
    std::atomic<int> padHits[DrumEngine::numPads] {};

private:
    void handleMidi (const juce::MidiMessage& m);
    void updateSettings();
    void renderSegment (int start, int num);

    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    OrganEngine organ; RotarySpeaker rotary;
    KeysEngine keys; SynthEngine synth; DrumEngine drums;

    std::atomic<float> padRequests[DrumEngine::numPads] {};
    std::atomic<bool> panicRequest { false };

    // per incoming (channel, note): which sections were triggered and with which shifted note
    struct Routed { uint8_t mask = 0; int8_t org = -1, keys = -1, syn = -1; };
    Routed routed[16][128];
    bool sustain = false;
    bool heldBySustainKeys[128] {}, heldBySustainSyn[128] {};
    bool keysDown[128] {}, synDown[128] {};

    juce::AudioBuffer<float> orgMono, orgSt, keysBuf, synBuf, padBuf, fxBuf;

    // effects
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    juce::dsp::Compressor<float> comp;
    juce::Reverb reverb;
    dspc::DelayLine delayL, delayR;
    float delayToneL = 0, delayToneR = 0;
    juce::dsp::IIR::Filter<float> eqLow[2], eqMid[2], eqHigh[2];
    float lastEq[3] { -99, -99, -99 };
    float ampToneL = 0, ampToneR = 0;
    juce::SmoothedValue<float> masterSm, orgSm, keysSm, synSm, padSm;
    double sr = 48000.0;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmariKeysProcessor)
};
