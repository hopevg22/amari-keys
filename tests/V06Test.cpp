// 0.6: pedal tails, sample release time, BASS + SFX layers, data dial + ENTER, MIDI learn of functions.
#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
using namespace juce;

static int fails = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n"; if (! ok) ++fails; }
static void setP (AmariKeysProcessor& p, const String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static void block (AmariKeysProcessor& p, MidiBuffer& mb, AudioBuffer<float>& b) { b.clear(); p.processBlock (b, mb); }
static void waitLoads (AmariKeysProcessor& p) { for (int i = 0; i < 400 && p.isLoadingSamples(); ++i) pump (20); pump (60); MidiBuffer n; AudioBuffer<float> b (2, 256); block (p, n, b); }

static void writeSet (const File& dir, const String& name)
{
    dir.createDirectory(); String sfz;
    for (int n = 36; n <= 96; n += 12)
    {
        const double sr = 48000, f = MidiMessage::getMidiNoteInHertz (n);
        AudioBuffer<float> b (2, (int) sr * 6);
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.5f * (float) std::sin (6.2831853 * f * i / sr); b.setSample (0, i, v); b.setSample (1, i, v); }
        auto wf = dir.getChildFile (name + "_" + String (n) + ".wav"); wf.deleteFile();
        std::unique_ptr<OutputStream> os (wf.createOutputStream()); WavAudioFormat wav;
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        sfz << "<region> sample=" << wf.getFileName() << " lokey=" << (n - 6) << " hikey=" << (n + 5) << " pitch_keycenter=" << n << "\n";
    }
    dir.getChildFile (name + ".sfz").replaceWithText (sfz);
}

static AmariKeysProcessor* make()
{
    auto* p = new AmariKeysProcessor();
    p->setPlayConfigDetails (0, 2, 48000, 256); p->prepareToPlay (48000, 256);
    setP (*p, "split_on", 0); setP (*p, "rev_on", 0); setP (*p, "dly_on", 0); setP (*p, "mod_on", 0);
    for (int l = 0; l < AmariKeysProcessor::numLayers; ++l) setP (*p, Params::lid (l, "on"), 0);
    return p;
}

// Play note 60; release the key (with or without pedal); return seconds from the release moment until -60 dB below the held level.
static double tailSeconds (AmariKeysProcessor& p, bool pedal)
{
    AudioBuffer<float> b (2, 256); MidiBuffer mb;
    if (pedal) mb.addEvent (MidiMessage::controllerEvent (1, 64, 127), 0);
    mb.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
    block (p, mb, b);
    float held = 0;
    for (int i = 0; i < 400; ++i) { MidiBuffer n; block (p, n, b); held = jmax (held, b.getMagnitude (0, 0, 256)); } // ~2 s held
    MidiBuffer off; off.addEvent (MidiMessage::noteOff (1, 60), 0); block (p, off, b);
    if (pedal) { for (int i = 0; i < 40; ++i) { MidiBuffer n; block (p, n, b); } MidiBuffer up; up.addEvent (MidiMessage::controllerEvent (1, 64, 0), 0); block (p, up, b); }
    int blocks = 0;
    for (; blocks < 2000; ++blocks) { MidiBuffer n; block (p, n, b); if (b.getMagnitude (0, 0, 256) < held * 0.001f) break; }
    for (int i = 0; i < 200; ++i) { MidiBuffer n; block (p, n, b); }
    return blocks * 256 / 48000.0;
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    auto root = File::getCurrentWorkingDirectory().getChildFile ("v06-lib");
    root.deleteRecursively(); root.createDirectory(); AmariLibrary::setRoot (root);
    AmariLibrary::settings().setValue ("midimap", String());
    writeSet (root.getChildFile ("Samples/Piano/GrandA"), "GrandA");
    writeSet (root.getChildFile ("Samples/Piano/GrandB"), "GrandB");
    writeSet (root.getChildFile ("Samples/Pads/WarmPad"), "WarmPad");
    writeSet (root.getChildFile ("Samples/SFX/Riser"), "Riser");

    std::cout << "Sustain pedal tails\n";
    {
        std::unique_ptr<AmariKeysProcessor> p (make());
        setP (*p, "l7_on", 1);                                        // PADS, built-in synth, release 2.6 s
        const double keyUp = tailSeconds (*p, false), pedalUp = tailSeconds (*p, true);
        std::cout << "  pads synth: key up " << keyUp << " s, pedal up " << pedalUp << " s\n";
        check (pedalUp < 1.0 && pedalUp > 0.5, "pads (synth) fade in about 0.8 s when the pedal is lifted");
        check (keyUp > 2.0, "lifting a key still uses the pad's own long release");
        setP (*p, "pedal_tail", 0.3f); const double shorter = tailSeconds (*p, true);
        check (shorter < 0.45, "PAD TAIL setting shortens it (0.3 s -> " + String (shorter, 2) + " s)");
        setP (*p, "pedal_tail", 0.8f);
        // sampled pads
        p->loadSamples (6, root.getChildFile ("Samples/Pads/WarmPad/WarmPad.sfz")); waitLoads (*p);
        const double sKey = tailSeconds (*p, false), sPedal = tailSeconds (*p, true);
        std::cout << "  pads samples: key up " << sKey << " s, pedal up " << sPedal << " s\n";
        check (sPedal < 1.0, "pads (samples) fade in about 0.8 s when the pedal is lifted");
        check (sKey < 3.2 && sKey > 2.0, "sample release ends at its set time (2.6 s), not many seconds later");
        // piano samples: damper-like
        setP (*p, "l7_on", 0); setP (*p, "l1_on", 1);
        p->loadSamples (0, root.getChildFile ("Samples/Piano/GrandA/GrandA.sfz")); waitLoads (*p);
        const double pKey = tailSeconds (*p, false), pPedal = tailSeconds (*p, true);
        std::cout << "  piano samples: key up " << pKey << " s, pedal up " << pPedal << " s\n";
        check (pPedal <= 0.6 && pKey <= 0.9, "piano stops like a damper (key up and pedal up)");
    }

    std::cout << "BASS and SFX layers\n";
    {
        std::unique_ptr<AmariKeysProcessor> p (make());
        auto level = [&] (int note)
        {
            AudioBuffer<float> b (2, 256); MidiBuffer mb; mb.addEvent (MidiMessage::noteOn (1, note, (uint8) 100), 0); block (*p, mb, b);
            float pk = 0; for (int i = 0; i < 40; ++i) { MidiBuffer n; block (*p, n, b); pk = jmax (pk, b.getMagnitude (0, 0, 256)); }
            MidiBuffer off; off.addEvent (MidiMessage::allNotesOff (1), 0); block (*p, off, b);
            for (int i = 0; i < 300; ++i) { MidiBuffer n; block (*p, n, b); }
            return pk;
        };
        check (AmariKeysProcessor::numLayers == 10 && String (Params::layerShort (8)) == "BASS" && String (Params::layerShort (9)) == "SFX", "10 layers incl. BASS and SFX");
        setP (*p, "l9_on", 1); check (level (40) > 0.01f, "BASS plays its built-in synth bass");
        setP (*p, "l9_on", 0); setP (*p, "l10_on", 1); check (level (60) < 1e-4f, "SFX is silent until samples are loaded");
        p->loadSamples (9, root.getChildFile ("Samples/SFX/Riser/Riser.sfz")); waitLoads (*p);
        check (level (60) > 0.01f, "SFX plays its loaded samples");
        check (AmariLibrary::soundsFor ("SFX").size() == 1, "Samples\\SFX folder listed under SFX sounds");
    }

    std::cout << "Data dial + ENTER\n";
    {
        std::unique_ptr<AmariKeysProcessor> p (make());
        p->setBrowseTarget (0);
        p->dialStep (1);
        auto t = p->getBrowseText();
        check (p->isBrowsePending() && t.first.contains ("GRANDA"), "dial on PIANO shows the first piano sound: " + t.first);
        p->dialStep (1); t = p->getBrowseText();
        check (t.first.contains ("GRANDB") && t.second.contains ("2 OF 2"), "turning on shows the next one: " + t.first);
        check (p->getSamplePath (0).isEmpty(), "nothing loads until ENTER");
        p->dialEnter(); waitLoads (*p);
        check (p->getSamplePath (0).endsWith ("GrandB.sfz") && ! p->isBrowsePending(), "ENTER loads it");
        check (p->apvts.getRawParameterValue ("l1_on")->load() > 0.5f, "ENTER switches the layer on");
        p->dialStep (-1); check (p->getBrowseText().first.contains ("GRANDA"), "next turn starts from the loaded sound");
        p->savePatchAs ("P one"); p->savePatchAs ("P two");
        p->setBrowseTarget (-1); p->dialStep (-1); p->dialEnter();
        check (p->getCurrentPatch() == 0, "dial on patches: turn + ENTER recalls patch 1");
        p->setBrowseTarget (2); check (p->getBrowseTarget() == -1, "ORGAN selected: dial browses patches");
    }

    std::cout << "MIDI learn: functions\n";
    {
        std::unique_ptr<AmariKeysProcessor> p (make());
        AudioBuffer<float> b (2, 256);
        auto cc = [&] (int n, int v) { MidiBuffer mb; mb.addEvent (MidiMessage::controllerEvent (1, n, v), 0); block (*p, mb, b); pump (60); };
        // right-click > MIDI Learn, then move one control: learning ends by itself
        p->startLearn (AmariKeysProcessor::actionBase + AmariKeysProcessor::actPatchNext); cc (20, 127);
        check (! p->isLearnMode(), "learning stops after one control");
        p->startLearn (AmariKeysProcessor::actionBase + AmariKeysProcessor::actDial); cc (21, 64);
        p->startLearn (AmariKeysProcessor::actionBase + AmariKeysProcessor::actEnter); cc (22, 127);
        p->startLearn (AmariKeysProcessor::actionBase + AmariKeysProcessor::actSelectLayer + 0); cc (23, 127);
        p->startLearn (p->paramIndex ("l1_level")); cc (24, 10);
        check (p->apvts.getRawParameterValue ("l1_level")->load() > 0.5f, "the knob move that teaches doesn't change the setting");
        const int notesBefore = p->notesSeen.load(), ccBefore = p->ccSeen.load();
        { MidiBuffer mb; mb.addEvent (MidiMessage::noteOn (1, 60, (uint8) 90), 0); block (*p, mb, b); }
        check (p->notesSeen.load() == notesBefore + 1 && p->ccSeen.load() == ccBefore, "MIDI activity counters (used for the 'only notes arriving' hint)");
        { MidiBuffer mb; mb.addEvent (MidiMessage::allNotesOff (1), 0); block (*p, mb, b); }
        auto maps = p->getMappings();
        check (maps.size() == 5, "five controls learned (" + String (maps.size()) + ")");
        for (auto& m : maps) std::cout << "        CC " << m.cc << " -> " << m.name << "\n";
        cc (20, 0); cc (20, 127);
        check (p->getCurrentPatch() == 0 || p->getCurrentPatch() == 1, "PATCH > button on CC 20 changes patch (now " + String (p->getCurrentPatch() + 1) + ")");
        const int before = p->getCurrentPatch(); cc (20, 0); cc (20, 127);
        check (p->getCurrentPatch() != before, "pressing it again steps again");
        cc (23, 0); cc (23, 127); check (p->getBrowseTarget() == 0, "select-layer button sets the dial to PIANO");
        cc (21, 66); cc (21, 68); check (p->isBrowsePending(), "turning the learned knob scrolls the sounds");
        cc (22, 0); cc (22, 127); waitLoads (*p); check (! p->isBrowsePending() && p->getSamplePath (0).isNotEmpty(), "learned ENTER loads the sound");
        cc (24, 127); check (p->apvts.getRawParameterValue ("l1_level")->load() > 0.99f, "learned fader still works");
        std::unique_ptr<AmariKeysProcessor> q (make());
        check (q->getMappings().size() == 5, "a new instance / project has the same MIDI map");
        q->removeMapping (24); check (q->getMappings().size() == 4, "remove one mapping");
        q->clearMappings(); check (q->getMappings().isEmpty(), "clear all");
    }
    std::cout << (fails ? "V06 TEST FAILED " + String (fails) : String ("V06 TEST OK")) << std::endl;
    root.deleteRecursively();
    return fails;
}
