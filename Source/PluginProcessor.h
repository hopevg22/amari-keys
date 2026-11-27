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
#include "Library.h"

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
    bool isLoadingSamples() const { return loadingLayer.load() >= 0 || queuedLoads.load() > 0; }
    void clearSamples (int layer); // unload a layer's sample set

    // ---- programs (message thread)
    // ---- patches: unlimited, named, stored in the library (Library\Patches.xml) ----
    // A patch keeps every setting of every layer plus each layer's sound (SFZ / sample folder).
    int getPatchCount() const { return patches.getNumChildren(); }
    juce::String getPatchName (int i) const { return patches.getChild (i).getProperty ("name").toString(); }
    int getCurrentPatch() const { return currentPatch.load(); }
    void recallPatch (int index);                    // message thread
    bool storePatch (int index);                     // overwrite with the current sound
    int savePatchAs (const juce::String& name);      // new patch at the end, returns its index
    void renamePatch (int index, const juce::String& name);
    void deletePatch (int index);
    void reloadPatches();                            // re-read the library (another instance may have changed it)
    juce::String getSamplePath (int layer) const { const juce::ScopedLock l (statusLock); return layers[layer].path; }

    // ---- wheels (UI and MIDI) ----
    void setPitchWheel (float v) { wheelBend.store (juce::jlimit (-1.0f, 1.0f, v)); }  // -1..1
    void setModWheel (float v)   { wheelMod.store (juce::jlimit (0.0f, 1.0f, v)); }    // 0..1
    float getPitchWheel() const { return wheelBend.load(); }
    float getModWheel() const { return wheelMod.load(); }

    // ---- pads
    void padPressed (int pad, float vel); // message thread (UI)
    std::atomic<int> padHits[DrumEngine::numPads] {};

    // ---- MIDI learn
    void setLearnMode (bool on) { learnMode.store (on); if (! on) learnTarget.store (-1); }
    bool isLearnMode() const { return learnMode.load(); }
    void setLearnTarget (const juce::String& paramId);
    juce::String getLearnTargetName() const;
    // learnable functions that are not parameters
    enum Action { actPatchPrev = 0, actPatchNext, actDial, actEnter, actPanic, actBrowsePatches, actSelectLayer = 10 };
    static constexpr int actionBase = 100000;
    void setLearnAction (int action) { learnTarget.store (actionBase + action); }
    void startLearn (int code) { learnTarget.store (code); learnMode.store (true); }  // waits for the next knob/button
    int paramIndex (const juce::String& paramId) const
    {
        for (int i = 0; i < paramList.size(); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (paramList[i])) if (r->getParameterID() == paramId) return i;
        return -1;
    }
    int getLastLearnedCode() const { return lastLearned.load(); }
    std::atomic<int> ccSeen { 0 }, notesSeen { 0 };   // MIDI activity, so learn can tell what is (not) arriving
    static juce::String actionName (int action);
    juce::String targetName (int code) const;
    struct Mapping { int cc; juce::String name; };
    juce::Array<Mapping> getMappings() const;
    void removeMapping (int cc) { ccMap[cc].store (-1); saveMidiMap(); }
    void clearMappings() { for (auto& c : ccMap) c.store (-1); saveMidiMap(); }

    // ---- data dial: scroll the selected layer's sounds (or patches), ENTER loads (message thread)
    void setBrowseTarget (int layer);                 // -1 or ORGAN = patches
    int getBrowseTarget() const { return browseTarget; }
    void dialStep (int steps);
    void dialEnter();
    bool isBrowsePending() const { return browsePending; }
    std::pair<juce::String, juce::String> getBrowseText() const;
    int takeSelectRequest() { return selectRequest.exchange (-1); } // hardware "select layer" for the editor
    std::atomic<int> learnHits { 0 };

    void panic() { panicRequest.store (true); }

private:
    struct Layer
    {
        KeysEngine keys; SamplerEngine sampler; SynthEngine synth;
        std::unique_ptr<SampleSet> live, pending, retired;
        std::atomic<bool> hasSet { false }, clearRequest { false };
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
    void noteOffLayer (int l, int note, bool pedalLifted = false);
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    Layer layers[numLayers];
    OrganEngine organ; RotarySpeaker rotary; DrumEngine drums;
    juce::AudioBuffer<float> orgMono, padBuf, fxBuf;

    struct Routed { int8_t note[numLayers]; Routed() { std::fill (note, note + numLayers, (int8_t) -1); } };
    Routed routed[16][128];
    bool sustain = false;

    juce::ValueTree patches { "PATCHES" };          // message thread only
    std::atomic<int> currentPatch { -1 };
    std::atomic<int> programRequest { -1 };
    void migrateOldPrograms (const juce::ValueTree& progs);
    std::atomic<float> wheelBend { 0.0f }, wheelMod { 0.0f };
    double vibPhase = 0.0;

    juce::SpinLock swapLock;
    juce::ThreadPool loaderPool { 1 };
    std::atomic<int> loadingLayer { -1 }, queuedLoads { 0 };
    juce::uint32 loadSerial[numLayers] {};          // newest request per layer wins
    void runLoad (int layer, juce::File source, juce::uint32 serial);
    juce::CriticalSection statusLock;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    std::atomic<bool> panicRequest { false }, learnMode { false };
    std::atomic<int> learnTarget { -1 };
    std::atomic<int> ccMap[128];
    int ccLast[128] {};                                  // audio thread: last value per CC (buttons, dial)
    std::atomic<int> pendingDial { 0 }, pendingActions { 0 }, pendingSelect { -1 }, selectRequest { -1 };
    int browseTarget = -1, browseIndex = -1; bool browsePending = false;
    juce::Array<AmariLibrary::Sound> browseList;
    void refreshBrowseList();
    void saveMidiMap();
    void loadMidiMap (const juce::String& map);
    std::atomic<bool> midiMapDirty { false };
    std::atomic<int> lastLearned { -1 };
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
