// Loads a VST2 shared library through Amari Sampler's VST2 host, records it, and loads the result back.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../AmariSampler/MultiSampleRenderer.h"
#include "../AmariSampler/Vst2Plugin.h"
#include "../Source/Sampler.h"
#include <iostream>
using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cout << "usage: Vst2RoundTrip plugin.so\n"; return 2; }
    String err;
    auto plug = Vst2Plugin::load (File (argv[1]), 48000.0, 512, err);
    if (! plug) { std::cout << "LOAD FAILED: " << err << "\n"; return 1; }
    std::cout << "Loaded: " << plug->getName() << " / " << plug->getVendor() << (plug->isInstrument() ? " (instrument)" : "") << " outs=" << plug->getTotalNumOutputChannels() << "\n";

    auto out = File::getCurrentWorkingDirectory().getChildFile ("vst2roundtrip");
    out.deleteRecursively();
    MultiSampleRenderer r;
    r.onLog = [] (const String& t) { if (! t.endsWith (" dB")) std::cout << "  " << t << "\n"; };
    MultiSampleRenderer::Settings s;
    s.name = "Vst2Test"; s.outDir = out.getChildFile ("Vst2Test");
    s.lowNote = 24; s.highNote = 96; s.step = 4; s.velocities = { 50, 120 };
    s.holdSeconds = 0.8; s.maxTailSeconds = 1.0; s.realtimePace = false;
    if (! r.run (*plug, s)) return 1;

    int fails = 0;
    String msg;
    auto set = SampleLoader::load (s.outDir.getChildFile ("Vst2Test.sfz"), msg, [] (float) {});
    if (! set) { std::cout << "SFZ LOAD FAILED " << msg << "\n"; return 1; }
    String msg2; auto set2 = SampleLoader::load (s.outDir, msg2, [] (float) {});
    std::cout << "SFZ: " << msg << "\nFolder: " << msg2 << " | " << (set2 ? set2->mappingInfo : "") << "\n";
    for (int k = 24; k <= 96; ++k)
        for (int vel : { 30, 110 })
        {
            auto* z = set->find (k, vel);
            if (! z || std::abs (z->root - k) > 2 || vel < z->lovel || vel > z->hivel) ++fails;
        }
    if (set->zones.size() != 19 * 2) { std::cout << "zones " << set->zones.size() << "\n"; ++fails; }
    plug.reset();
    std::cout << (fails ? "FAILED " + String (fails) : String ("VST2 ROUND TRIP OK")) << std::endl;
    return fails ? 1 : 0;
}
