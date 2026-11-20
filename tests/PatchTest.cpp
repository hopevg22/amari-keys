// Patches (save/recall incl. each layer's sound, rename, delete, old-program migration), sound library, wheels.
#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
using namespace juce;

static int fails = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n"; if (! ok) ++fails; }
static void setP (AmariKeysProcessor& p, const String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
static float getP (AmariKeysProcessor& p, const String& id) { return p.apvts.getRawParameterValue (id)->load(); }
static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static void waitLoads (AmariKeysProcessor& p)
{
    for (int i = 0; i < 400 && p.isLoadingSamples(); ++i) pump (20);
    pump (100);
    MidiBuffer none; AudioBuffer<float> b (2, 256); p.processBlock (b, none); // audio thread picks up new sets
}

static void writeSet (const File& dir, const String& name, int harmonic)
{
    dir.createDirectory(); String sfz;
    for (int n = 36; n <= 96; n += 12)
    {
        const double sr = 48000, f = MidiMessage::getMidiNoteInHertz (n) * harmonic;
        AudioBuffer<float> b (2, (int) sr * 2);
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.5f * (float) std::sin (6.2831853 * f * i / sr) * (float) std::exp (-i / sr * 0.3); b.setSample (0, i, v); b.setSample (1, i, v); }
        auto wf = dir.getChildFile (name + "_" + String (n) + ".wav"); wf.deleteFile();
        std::unique_ptr<OutputStream> os (wf.createOutputStream()); WavAudioFormat wav;
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        sfz << "<region> sample=" << wf.getFileName() << " lokey=" << (n - 6) << " hikey=" << (n + 5) << " pitch_keycenter=" << n << "\n";
    }
    dir.getChildFile (name + ".sfz").replaceWithText (sfz);
}

// dominant frequency of note 69 (A4) over 0.5 s, by zero crossings
static double playA4 (AmariKeysProcessor& p, int frames = 0)
{
    const double sr = 48000; const int bs = 256, total = (int) (sr * 0.5);
    std::vector<float> x; x.reserve ((size_t) total);
    for (int pos = 0; pos < total; pos += bs)
    {
        MidiBuffer mb; if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, 69, (uint8) 100), 0);
        AudioBuffer<float> b (2, bs); b.clear(); p.processBlock (b, mb);
        for (int i = 0; i < bs; ++i) x.push_back (b.getSample (0, i));
    }
    MidiBuffer off; off.addEvent (MidiMessage::allNotesOff (1), 0); AudioBuffer<float> b (2, bs); p.processBlock (b, off);
    for (int i = 0; i < 300; ++i) { MidiBuffer none; p.processBlock (b, none); }
    int zc = 0; for (size_t i = (size_t) (sr * 0.1) + 1; i < x.size(); ++i) if (x[i - 1] <= 0 && x[i] > 0) ++zc;
    juce::ignoreUnused (frames);
    return zc / (0.4);
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    auto root = File::getCurrentWorkingDirectory().getChildFile ("patchtest-lib");
    root.deleteRecursively(); root.createDirectory();
    AmariLibrary::setRoot (root);
    writeSet (root.getChildFile ("Samples/Piano/Grand"), "Grand", 1);
    writeSet (root.getChildFile ("Samples/E.Piano/Suitcase"), "Suitcase", 1);
    writeSet (root.getChildFile ("Samples/Strings/Triton"), "Triton", 1);

    std::cout << "Sound library\n";
    {
        auto piano = AmariLibrary::soundsFor ("Piano");
        check (piano.size() == 1 && piano[0].name.contains ("Grand"), "SFZ in Samples\\Piano is listed automatically (" + String (piano.size()) + ")");
        AmariLibrary::saveSound ("My Grand", 0, root.getChildFile ("Samples/Piano/Grand/Grand.sfz").getFullPathName());
        piano = AmariLibrary::soundsFor ("Piano");
        check (piano.size() == 1 && piano[0].name == "My Grand" && piano[0].saved, "saved name replaces the file name, no duplicate");
        AmariLibrary::saveSound ("Strings from elsewhere", 4, root.getChildFile ("Samples/Strings/Triton/Triton.sfz").getFullPathName());
        check (AmariLibrary::soundsFor ("Strings").size() == 1, "saved strings sound listed under Strings");
    }

    std::cout << "Patches\n";
    {
        AmariKeysProcessor p;
        p.setPlayConfigDetails (0, 2, 48000, 256); p.prepareToPlay (48000, 256);
        check (p.getPatchCount() == 0 && p.getCurrentPatch() == -1, "starts with no patches");
        setP (p, "split_on", 0); setP (p, "rev_on", 0);
        p.loadSamples (0, root.getChildFile ("Samples/Piano/Grand/Grand.sfz"));
        p.loadSamples (1, root.getChildFile ("Samples/E.Piano/Suitcase/Suitcase.sfz"));
        waitLoads (p);
        setP (p, "l2_on", 1); setP (p, "l2_level", 0.33f);
        const int a = p.savePatchAs ("Grand + Suitcase");
        // second patch: strings only
        setP (p, "l1_on", 0); setP (p, "l2_on", 0); setP (p, "l5_on", 1);
        p.loadSamples (4, root.getChildFile ("Samples/Strings/Triton/Triton.sfz"));
        p.clearSamples (0); p.clearSamples (1);
        waitLoads (p);
        const int b = p.savePatchAs ("Triton Strings");
        for (int i = 0; i < 20; ++i) p.savePatchAs ("Extra " + String (i + 1));
        check (p.getPatchCount() == 22, "more than 16 patches (" + String (p.getPatchCount()) + ")");

        p.recallPatch (a); waitLoads (p);
        check (getP (p, "l1_on") > 0.5f && getP (p, "l2_on") > 0.5f && getP (p, "l5_on") < 0.5f, "recall restores which layers are on");
        check (std::abs (getP (p, "l2_level") - 0.33f) < 0.01f, "recall restores layer levels");
        check (p.getSamplePath (0).endsWith ("Grand.sfz") && p.getSamplePath (1).endsWith ("Suitcase.sfz"), "recall loads each layer's sound");
        check (p.getSamplePath (4).isEmpty(), "recall unloads a layer the patch doesn't use");
        p.recallPatch (b); waitLoads (p);
        check (p.getSamplePath (4).endsWith ("Triton.sfz") && p.getSamplePath (0).isEmpty(), "second patch: strings loaded, piano unloaded");

        p.renamePatch (b, "Triton Pad Strings");
        check (p.getPatchName (b) == "Triton Pad Strings", "rename");
        p.deletePatch (2);
        check (p.getPatchCount() == 21 && p.getPatchName (2) == "Extra 2", "delete");

        // the library is shared: a new instance sees the same patches
        AmariKeysProcessor q;
        check (q.getPatchCount() == 21 && q.getPatchName (0) == "Grand + Suitcase", "another plugin instance sees the same patches");

        // project state remembers the current patch
        p.recallPatch (1); waitLoads (p);
        MemoryBlock st; p.getStateInformation (st);
        AmariKeysProcessor r; r.setStateInformation (st.getData(), (int) st.getSize());
        check (r.getCurrentPatch() == 1, "project reopen shows the same patch");
        for (int i = 0; i < 200 && r.isLoadingSamples(); ++i) pump (20);
    }

    std::cout << "Old 16-program projects\n";
    {
        AmariKeysProcessor p;
        MemoryBlock st; p.getStateInformation (st);
        auto xml = AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        auto tree = ValueTree::fromXml (*xml);
        ValueTree progs ("PROGRAMS");
        ValueTree pr ("PROG"); pr.setProperty ("index", 5, nullptr); pr.setProperty ("name", "OLD ORGAN", nullptr);
        ValueTree v ("V"); v.setProperty ("id", "l3_on", nullptr); v.setProperty ("v", 1.0f, nullptr); pr.appendChild (v, nullptr);
        progs.appendChild (pr, nullptr); tree.appendChild (progs, nullptr);
        MemoryBlock old; AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
        const int before = p.getPatchCount();
        p.setStateInformation (old.getData(), (int) old.getSize());
        check (p.getPatchCount() == before + 1 && p.getPatchName (p.getPatchCount() - 1) == "OLD ORGAN", "old programs move into the library once");
        p.setStateInformation (old.getData(), (int) old.getSize());
        check (p.getPatchCount() == before + 1, "...and are not added twice");
    }

    std::cout << "Wheels\n";
    {
        AmariKeysProcessor p;
        p.setPlayConfigDetails (0, 2, 48000, 256); p.prepareToPlay (48000, 256);
        setP (p, "split_on", 0); setP (p, "rev_on", 0); setP (p, "l3_on", 0);
        p.loadSamples (0, root.getChildFile ("Samples/Piano/Grand/Grand.sfz")); waitLoads (p);
        const double f0 = playA4 (p);
        p.setPitchWheel (1.0f);
        const double fUp = playA4 (p);
        setP (p, "bend_range", 12); const double fOct = playA4 (p);
        p.setPitchWheel (0.0f);
        std::cout << "  A4 " << f0 << " Hz, wheel up (2 st) " << fUp << " Hz, range 12 " << fOct << " Hz\n";
        check (std::abs (f0 - 440) < 6, "no bend plays 440 Hz");
        check (std::abs (fUp / f0 - std::pow (2.0, 2.0 / 12)) < 0.02, "pitch wheel up bends 2 semitones");
        check (std::abs (fOct / f0 - 2.0) < 0.04, "range 12 bends an octave");
        // MIDI pitch wheel + mod wheel
        MidiBuffer mb; mb.addEvent (MidiMessage::pitchWheel (1, 0), 0); mb.addEvent (MidiMessage::controllerEvent (1, 1, 127), 0);
        AudioBuffer<float> b (2, 256); p.processBlock (b, mb);
        check (p.getPitchWheel() < -0.99f && p.getModWheel() > 0.99f, "hardware wheels move the on-screen wheels");
    }
    std::cout << (fails ? "PATCH TEST FAILED " + String (fails) : String ("PATCH TEST OK")) << std::endl;
    root.deleteRecursively();
    return fails;
}
