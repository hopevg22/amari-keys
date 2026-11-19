// Renders a simple test instrument with MultiSampleRenderer, then loads the result
// with the Amari Keys loader (folder and SFZ) and checks every note and velocity layer.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../AmariSampler/MultiSampleRenderer.h"
#include "../Source/Sampler.h"
#include <iostream>
using namespace juce;

struct TestPiano : AudioProcessor
{
    TestPiano() : AudioProcessor (BusesProperties().withOutput ("Out", AudioChannelSet::stereo(), true)) {}
    struct V { int note = -1; double ph = 0, f = 0; float amp = 0, env = 0; bool rel = false; int delay = 0; };
    V voices[8];
    double sr = 48000;
    void prepareToPlay (double s, int) override { sr = s; for (auto& v : voices) v.note = -1; }
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>& b, MidiBuffer& m) override
    {
        b.clear();
        for (const auto meta : m)
        {
            auto msg = meta.getMessage();
            if (msg.isNoteOn())
                for (auto& v : voices) if (v.note < 0) { v = {}; v.note = msg.getNoteNumber(); v.f = MidiMessage::getMidiNoteInHertz (v.note); v.amp = msg.getFloatVelocity() * 0.7f; v.env = 1; v.delay = meta.samplePosition + 300; break; }
            if (msg.isNoteOff()) for (auto& v : voices) if (v.note == msg.getNoteNumber()) v.rel = true;
        }
        for (auto& v : voices)
        {
            if (v.note < 0) continue;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                if (v.delay > 0) { --v.delay; continue; } // fake plugin latency
                float s = 0; for (int h = 1; h <= 6; ++h) if (h * v.f < sr * 0.45) s += (float) (std::sin (v.ph * h) / h);
                s *= v.amp * v.env;
                v.env *= v.rel ? 0.9992f : 0.99998f;
                v.ph += 6.283185307 * v.f / sr;
                b.addSample (0, i, s); b.addSample (1, i, s * 0.95f);
            }
            if (v.env < 1e-5f) v.note = -1;
        }
    }
    const String getName() const override { return "TestPiano"; }
    bool acceptsMidi() const override { return true; } bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    AudioProcessorEditor* createEditor() override { return nullptr; } bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; } void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; } void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {} void setStateInformation (const void*, int) override {}
};

int main()
{
    ScopedJuceInitialiser_GUI init;
    auto out = File::getCurrentWorkingDirectory().getChildFile ("roundtrip");
    out.deleteRecursively();
    TestPiano piano;
    MultiSampleRenderer r;
    r.onLog = [] (const String& t) { if (! t.endsWith (" dB")) std::cout << "  " << t << "\n"; };
    MultiSampleRenderer::Settings s;
    s.name = "Pedal Test Piano"; // checks that loader-confusing words are removed
    s.outDir = out.getChildFile ("TestPiano");
    s.lowNote = 21; s.highNote = 108; s.step = 3; s.velocities = { 40, 90, 127 };
    s.holdSeconds = 1.0; s.maxTailSeconds = 1.0; s.realtimePace = false;
    if (! r.run (piano, s)) return 1;

    int fails = 0;
    for (auto src : { s.outDir, s.outDir.getChildFile ("Test_Piano.sfz") })
    {
        String msg;
        auto set = SampleLoader::load (src, msg, [] (float) {});
        if (! set) { std::cout << "LOAD FAILED " << msg << "\n"; ++fails; continue; }
        std::cout << src.getFileName() << ": " << msg << " | " << set->mappingInfo << "\n";
        // every key 21..108 at three velocities must hit a zone whose root is within 1 semitone, with the right layer
        for (int k = 21; k <= 108; ++k)
            for (int vel : { 20, 70, 120 })
            {
                auto* z = set->find (k, vel);
                if (! z || std::abs (z->root - k) > 1 || vel < z->lovel || vel > z->hivel) { if (fails < 5) std::cout << "  bad key " << k << " vel " << vel << "\n"; ++fails; }
            }
        if (set->zones.size() != 30 * 3) { std::cout << "  zone count " << set->zones.size() << "\n"; ++fails; }
    }
    std::cout << (fails ? "FAILED " + String (fails) : String ("ROUND TRIP OK")) << std::endl;
    return fails ? 1 : 0;
}
