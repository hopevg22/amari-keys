// Layers: load an SFZ on PIANO and another on E.PIANO, play both, and check each one is heard.
#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
using namespace juce;

static void writeSet (const File& dir, const String& name, int harmonic, float amp)
{
    dir.createDirectory();
    String sfz;
    for (int n = 36; n <= 96; n += 12)
    {
        const double sr = 48000, f = MidiMessage::getMidiNoteInHertz (n) * harmonic;
        AudioBuffer<float> b (2, (int) sr * 2);
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = amp * (float) std::sin (6.2831853 * f * i / sr) * std::exp (-i / sr); b.setSample (0, i, v); b.setSample (1, i, v); }
        auto f2 = dir.getChildFile (name + "_" + String (n) + ".wav"); f2.deleteFile();
        std::unique_ptr<OutputStream> os (f2.createOutputStream()); WavAudioFormat wav;
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        sfz << "<region> sample=" << f2.getFileName() << " lokey=" << (n - 6) << " hikey=" << (n + 5) << " pitch_keycenter=" << n << "\n";
    }
    dir.getChildFile (name + ".sfz").replaceWithText (sfz);
}

static void setP (AmariKeysProcessor& p, const String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// energy at the fundamental (harmonic 1 = "piano") and at 2x (harmonic 2 = "ep") for note 60
static std::pair<double, double> play (AmariKeysProcessor& p)
{
    const double sr = 48000; const int bs = 256, total = (int) sr;
    AudioBuffer<float> all (1, total);
    for (int pos = 0; pos < total; pos += bs)
    {
        MidiBuffer mb; if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        AudioBuffer<float> b (2, bs); b.clear(); p.processBlock (b, mb);
        all.copyFrom (0, pos, b, 0, 0, jmin (bs, total - pos));
    }
    MidiBuffer off; off.addEvent (MidiMessage::allNotesOff (1), 0); AudioBuffer<float> b (2, bs); p.processBlock (b, off);
    for (int i = 0; i < 400; ++i) { MidiBuffer none; p.processBlock (b, none); }
    auto goertzel = [&] (double f) { double s1 = 0, s2 = 0; const double c = 2 * std::cos (6.2831853 * f / sr);
        for (int i = 0; i < total; ++i) { const double s = all.getSample (0, i) + c * s1 - s2; s2 = s1; s1 = s; }
        return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / total; };
    const double f0 = MidiMessage::getMidiNoteInHertz (60);
    return { goertzel (f0), goertzel (2 * f0) };
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    auto dir = File::getCurrentWorkingDirectory().getChildFile ("layertest");
    writeSet (dir.getChildFile ("Piano"), "Piano", 1, 0.5f);
    writeSet (dir.getChildFile ("EP"), "EP", 2, 0.5f);

    int fails = 0;
    for (int quick = 0; quick < 2; ++quick)
    {
        AmariKeysProcessor p;
        p.setPlayConfigDetails (0, 2, 48000, 256); p.prepareToPlay (48000, 256);
        setP (p, "mod_on", 0); setP (p, "rev_on", 0); setP (p, "dly_on", 0); setP (p, "split_on", 0);
        p.loadSamples (0, dir.getChildFile ("Piano/Piano.sfz"));
        if (! quick) for (int i = 0; i < 100 && p.isLoadingSamples(); ++i) pump (20);
        p.loadSamples (1, dir.getChildFile ("EP/EP.sfz"));   // "quick": clicked while the piano is still loading
        for (int i = 0; i < 300 && (p.isLoadingSamples() || ! p.hasSamples (0) || ! p.hasSamples (1)); ++i) pump (20);
        pump (200);
        setP (p, "l1_on", 1); setP (p, "l2_on", 1); setP (p, "l1_level", 1); setP (p, "l2_level", 1);
        { MidiBuffer none; AudioBuffer<float> b (2, 256); p.processBlock (b, none); } // swap in the sets
        auto both = play (p);
        setP (p, "l2_on", 0); auto pianoOnly = play (p);
        setP (p, "l2_on", 1); setP (p, "l1_on", 0); auto epOnly = play (p);
        std::cout << (quick ? "loaded quickly one after the other" : "loaded one at a time") << ":\n"
                  << "  status piano: " << p.getSampleStatus (0).upToFirstOccurrenceOf ("|", false, false) << "\n"
                  << "  status ep:    " << p.getSampleStatus (1).upToFirstOccurrenceOf ("|", false, false) << "\n"
                  << "  both on   -> piano " << both.first << "  ep " << both.second << "\n"
                  << "  piano only-> piano " << pianoOnly.first << "  ep " << pianoOnly.second << "\n"
                  << "  ep only   -> piano " << epOnly.first << "  ep " << epOnly.second << "\n";
        const bool ok = p.hasSamples (0) && p.hasSamples (1) && both.first > 0.01 && both.second > 0.01 && pianoOnly.first > 0.01 && epOnly.second > 0.01 && std::abs (both.first - pianoOnly.first) < 0.2 * pianoOnly.first;
        std::cout << (ok ? "  OK\n" : "  PROBLEM\n");
        if (! ok) ++fails;
        p.releaseResources();
    }
    std::cout << (fails ? "LAYER TEST FAILED" : "LAYER TEST OK") << std::endl;
    return fails;
}
