#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Organ.h"
#include "Keys.h"
#include "Synth.h"
#include "Drums.h"
#include "Sampler.h"

class AmariKeysProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int numLayers = Params::numLayers;
    static constexpr int numPrograms = 16; // banks 1-4 x A-D
    static constexpr int numPads = DrumEngine::numPads;

    AmariKeysProcessor();
    ~AmariKeysProcessor() override;

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

    // ---- samples per layer
    void loadSamples (int layer, const juce::File& folderOrSfz);
    juce::String getSampleStatus (int layer) const { const juce::ScopedLock l (statusLock); return layers[layer].status; }
    juce::String getSampleName (int layer) const { const juce::ScopedLock l (statusLock); return layers[layer].name; }
    bool hasSamples (int layer) const { return layers[layer].hasSet.load(); }
    bool isLoadingSamples() const { return loadingLayer.load() >= 0; }

    // ---- programs (message thread)
    void storeProgram (int index);
    void recallProgram (int index);
    void selectProgramSlot (int index) { currentProgram.store (index); }
    int getCurrentProgramIndex() const { return currentProgram.load(); }
    static juce::String getProgramLabel (int index) { return juce::String (index / 4 + 1) + juce::String::charToString ((juce::juce_wchar) ('A' + index % 4)); }
    juce::String getProgramNameAt (int index) const { return programs[(size_t) index].name; }
    void setProgramNameAt (int index, const juce::String& n) { programs[(size_t) index].name = n; }
    bool programHasData (int index) const { return ! programs[(size_t) index].values.empty(); }

    // ---- pads
    void padPressed (int pad, float vel); // message thread (UI)
    std::atomic<int> padHits[DrumEngine::numPads] {};

    // ---- MIDI learn
    void setLearnMode (bool on) { learnMode.store (on); if (! on) learnTarget.store (-1); }
    bool isLearnMode() const { return learnMode.load(); }
    void setLearnTarget (const juce::String& paramId);
    juce::String getLearnTargetName() const;
    std::atomic<int> learnHits { 0 };

    void panic() { panicRequest.store (true); }

private:
    struct Layer
    {
        KeysEngine keys; SamplerEngine sampler; SynthEngine synth;
        std::unique_ptr<SampleSet> live, pending, retired;
        std::atomic<bool> hasSet { false };
        juce::String status { "No samples loaded" }, name, path;
        juce::AudioBuffer<float> buf;
        juce::SmoothedValue<float> gain;
        bool down[128] {}, heldSus[128] {};
    };
    struct Program { juce::String name; std::vector<std::pair<juce::String, float>> values; };

    void timerCallback() override;
    void handleMidi (const juce::MidiMessage& m);
    void updateSettings();
    void renderSegment (int start, int num);
    void triggerPadAudio (int pad, float vel); // audio thread
    void noteOffLayer (int l, int note);
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    Layer layers[numLayers];
    OrganEngine organ; RotarySpeaker rotary; DrumEngine drums;
    juce::AudioBuffer<float> orgMono, padBuf, fxBuf;

    struct Routed { int8_t note[numLayers]; Routed() { std::fill (note, note + numLayers, (int8_t) -1); } };
    Routed routed[16][128];
    bool sustain = false;

    std::array<Program, numPrograms> programs;
    std::atomic<int> currentProgram { 0 };
    std::atomic<int> programRequest { -1 };

    juce::SpinLock swapLock;
    juce::ThreadPool loaderPool { 1 };
    std::atomic<int> loadingLayer { -1 };
    juce::CriticalSection statusLock;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    std::atomic<bool> panicRequest { false }, learnMode { false };
    std::atomic<int> learnTarget { -1 };
    std::atomic<int> ccMap[128];
    juce::Array<juce::AudioProcessorParameter*> paramList;

    // effects
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    juce::dsp::Compressor<float> comp;
    juce::Reverb reverb;
    dspc::DelayLine delayL, delayR;
    float delayToneL = 0, delayToneR = 0, ampToneL = 0, ampToneR = 0;
    juce::dsp::IIR::Filter<float> eqLow[2], eqMid[2], eqHigh[2];
    float lastEq[3] { -99, -99, -99 };
    juce::SmoothedValue<float> masterSm, padSm;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmariKeysProcessor)
};
