#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Sampler.h"

using namespace juce;

struct Ev { double t; int ch; int note; float vel; bool on; };

static void setP (AmariKeysProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static void render (AmariKeysProcessor& p, std::vector<Ev> evs, double seconds, const File& out, std::function<void(double)> automation = {})
{
    const double sr = 48000.0; const int bs = 256;
    p.setPlayConfigDetails (0, 2, sr, bs);
    p.prepareToPlay (sr, bs);
    const int total = (int) (seconds * sr);
    AudioBuffer<float> all (2, total); all.clear();
    std::sort (evs.begin(), evs.end(), [] (auto& a, auto& b) { return a.t < b.t; });
    size_t ei = 0; float peak = 0; bool bad = false;
    for (int pos = 0; pos < total; pos += bs)
    {
        const int n = std::min (bs, total - pos);
        if (automation) automation (pos / sr);
        MidiBuffer mb;
        while (ei < evs.size() && (int) (evs[ei].t * sr) < pos + n)
        {
            auto& e = evs[ei++];
            const int off = jlimit (0, n - 1, (int) (e.t * sr) - pos);
            mb.addEvent (e.on ? MidiMessage::noteOn (e.ch, e.note, e.vel) : MidiMessage::noteOff (e.ch, e.note), off);
        }
        AudioBuffer<float> b (2, n);
        p.processBlock (b, mb);
        for (int c = 0; c < 2; ++c)
        {
            all.copyFrom (c, pos, b, c, 0, n);
            for (int i = 0; i < n; ++i) { float v = b.getSample (c, i); if (! std::isfinite (v)) bad = true; peak = std::max (peak, std::abs (v)); }
        }
    }
    out.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (out.createOutputStream());
    auto writer = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    writer->writeFromAudioSampleBuffer (all, 0, total);
    writer.reset();
    std::cout << out.getFileName() << "  peak=" << peak << "  rms=" << all.getRMSLevel (0, 0, total) << (bad ? "  NaN!" : "") << std::endl;
}

static void chord (std::vector<Ev>& e, double t, double len, std::initializer_list<int> notes, float vel = 0.8f, int ch = 1)
{
    for (int n : notes) { e.push_back ({ t, ch, n, vel, true }); e.push_back ({ t + len, ch, n, 0, false }); }
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    File outDir (argc > 1 ? argv[1] : "/tmp/amari");
    outDir.createDirectory();

    auto fresh = [] { return std::make_unique<AmariKeysProcessor>(); };

    { // organ: gospel progression, rotary slow -> fast -> slow
        auto p = fresh(); setP (*p, "split_on", 0); setP (*p, "l1_on", 0); setP (*p, "l3_on", 1);
        std::vector<Ev> e;
        chord (e, 0.2, 2.8, { 48, 52, 55, 60, 64 }); chord (e, 3.0, 2.8, { 53, 57, 60, 65, 69 });
        chord (e, 5.9, 2.8, { 55, 59, 62, 67, 71 }); chord (e, 8.8, 3.0, { 48, 52, 55, 60, 64 });
        chord (e, 0.2, 11.6, { 36 }, 0.8f);
        render (*p, e, 13.0, outDir.getChildFile ("01_organ_B3_rotary.wav"), [&] (double t) { setP (*p, "rot_speed", (t > 4.0 && t < 9.0) ? 1.0f : 0.0f); });
    }
    { // tine EP
        auto p = fresh(); setP (*p, "split_on", 0); setP (*p, "l3_on", 0); setP (*p, "l1_model", 0); setP (*p, "l1_trem", 0.35f);
        std::vector<Ev> e;
        chord (e, 0.1, 1.9, { 50, 57, 60, 65 }, 0.55f); chord (e, 2.0, 1.9, { 55, 59, 62, 65 }, 0.75f);
        chord (e, 4.0, 3.5, { 48, 55, 59, 64 }, 0.95f);
        render (*p, e, 9.0, outDir.getChildFile ("02_keys_tine_ep.wav"));
    }
    { // reed EP + clav + bell short
        const char* names[3] = { "03_keys_reed_ep.wav", "04_keys_clav.wav", "05_keys_fm_bell.wav" };
        for (int m = 1; m <= 3; ++m)
        {
            auto p = fresh(); setP (*p, "split_on", 0); setP (*p, "l3_on", 0); setP (*p, "l1_model", (float) m);
            std::vector<Ev> e;
            const int seq[8] = { 60, 63, 65, 67, 70, 67, 65, 63 };
            for (int i = 0; i < 8; ++i) chord (e, 0.1 + i * 0.3, m == 2 ? 0.18 : 0.28, { seq[i] }, 0.5f + 0.06f * i);
            chord (e, 2.6, 2.5, { 48, 55, 60, 63, 67 }, 0.85f);
            render (*p, e, 6.5, outDir.getChildFile (names[m - 1]));
        }
    }
    { // synth pad
        auto p = fresh(); setP (*p, "split_on", 0); setP (*p, "l3_on", 0); setP (*p, "l1_on", 0); setP (*p, "l7_on", 1);
        std::vector<Ev> e;
        chord (e, 0.1, 3.5, { 57, 60, 64, 67 }, 0.7f); chord (e, 3.8, 3.5, { 53, 57, 60, 64 }, 0.7f);
        render (*p, e, 10.0, outDir.getChildFile ("06_synth_pad.wav"));
    }
    { // drums beat on ch 10
        auto p = fresh(); setP (*p, "pad_mode", 0);
        std::vector<Ev> e;
        const double step = 60.0 / 96.0 / 4.0;
        for (int bar = 0; bar < 4; ++bar)
            for (int s = 0; s < 16; ++s)
            {
                const double t = 0.1 + (bar * 16 + s) * step;
                if (s == 0 || s == 7 || s == 10) chord (e, t, step, { 36 }, 0.95f, 10);
                if (s == 4 || s == 12) chord (e, t, step, { 37 }, 0.9f, 10);
                if (s % 2 == 0) chord (e, t, step, { s == 14 ? 41 : 40 }, s % 4 == 0 ? 0.7f : 0.45f, 10);
                if (bar == 3 && s >= 12) chord (e, t, step, { s < 14 ? 42 : 43 }, 0.8f, 10);
            }
        chord (e, 0.1 + 64 * step, 1.0, { 44, 36 }, 0.9f, 10);
        render (*p, e, 0.1 + 64 * step + 2.5, outDir.getChildFile ("07_pads_drum_kit.wav"));
    }
    { // split: organ left hand, EP + pad right hand
        auto p = fresh(); setP (*p, "l1_model", 0); setP (*p, "l5_on", 1); setP (*p, "l5_level", 0.4f);
        std::vector<Ev> e;
        chord (e, 0.1, 3.8, { 36, 43, 46 }, 0.8f); chord (e, 0.1, 1.8, { 60, 64, 67, 70 }, 0.8f); chord (e, 2.0, 1.9, { 62, 65, 69, 72 }, 0.7f);
        chord (e, 4.0, 3.8, { 41, 45, 48 - 1 }, 0.8f); chord (e, 4.0, 3.8, { 60, 65, 69, 72 }, 0.85f);
        render (*p, e, 10.0, outDir.getChildFile ("08_split_organ_plus_keys_synth.wav"));
    }

    // ---- sampler: name parsing
    {
        const char* names[] = { "Piano C4 001", "001 Piano C4", "Grand_A0_064", "Piano 088", "Piano_01_C1", "Piano_C4_v2", "A0v1", "C#4v16", "Db3_mf", "060", "Grand-F#2-ff", "Rhodes C3", "release_C4", "noise" };
        for (auto* nm : names)
        {
            int note, layer; const bool ok = SampleLoader::parseFileName (nm, note, layer);
            std::cout << "parse " << nm << " -> " << (ok ? MidiMessage::getMidiNoteName (note, true, true, 4) : String ("none")) << " layer " << layer << std::endl;
        }
    }
    // ---- sampler: synthetic folder (every 3 semitones, 2 layers) + sfz
    {
        File dir = outDir.getChildFile ("testsamples"); dir.deleteRecursively(); dir.createDirectory();
        WavAudioFormat wav;
        String sfz = "<control> default_path=./\n";
        for (int note = 21; note <= 108; note += 3)
            for (int layer = 1; layer <= 2; ++layer)
            {
                const double sr = 44100.0; const int len = (int) (sr * 2.0);
                AudioBuffer<float> b (2, len);
                const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                for (int i = 0; i < len; ++i)
                {
                    const double t = i / sr; const float env = (float) std::exp (-t / 0.8);
                    const float v = (float) (std::sin (2 * MathConstants<double>::pi * f * t) + (layer == 2 ? 0.4 * std::sin (6 * MathConstants<double>::pi * f * t) : 0.0)) * env * (layer == 2 ? 0.7f : 0.35f);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
                auto name = "TestPiano_" + MidiMessage::getMidiNoteName (note, true, true, 4) + "_v" + String (layer) + ".wav";
                File wf = dir.getChildFile (name);
                std::unique_ptr<OutputStream> os (wf.createOutputStream());
                auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (16));
                w->writeFromAudioSampleBuffer (b, 0, len);
                sfz << "<region> sample=" << name << " lokey=" << note - 1 << " hikey=" << note + 1 << " pitch_keycenter=" << note
                    << " lovel=" << (layer == 1 ? 1 : 65) << " hivel=" << (layer == 1 ? 64 : 127) << "\n";
            }
        dir.getChildFile ("test.sfz").replaceWithText (sfz);

        for (int mode = 0; mode < 2; ++mode)
        {
            auto p = fresh(); setP (*p, "split_on", 0); setP (*p, "l3_on", 0);
            p->setPlayConfigDetails (0, 2, 48000.0, 256); p->prepareToPlay (48000.0, 256);
            p->loadSamples (0, mode == 0 ? dir : dir.getChildFile ("test.sfz"));
            while (p->isLoadingSamples()) Thread::sleep (20);
            std::cout << (mode == 0 ? "folder: " : "sfz: ") << p->getSampleName (0) << " | " << p->getSampleStatus (0) << std::endl;
            setP (*p, "l1_model", 4.0f);
            { MidiBuffer mb; AudioBuffer<float> b (2, 256); p->processBlock (b, mb); } // pick up the pending set
            std::vector<Ev> e;
            chord (e, 0.1, 1.5, { 48, 55, 60, 64 }, 0.4f); chord (e, 1.8, 1.5, { 50, 57, 62, 65 }, 0.95f);
            render (*p, e, 4.0, outDir.getChildFile (mode == 0 ? "09_sampler_folder.wav" : "10_sampler_sfz.wav"));
        }
    }

    // UI snapshot with samples loaded (for the guide)
    {
        File src = outDir.getChildFile ("testsamples");
        File dir = outDir.getChildFile ("MyGrandPiano"); dir.deleteRecursively(); src.copyDirectoryTo (dir);
        auto p = fresh();
        p->setPlayConfigDetails (0, 2, 48000.0, 256); p->prepareToPlay (48000.0, 256);
        p->loadSamples (0, dir);
        while (p->isLoadingSamples()) Thread::sleep (20);
        { MidiBuffer mb; AudioBuffer<float> b (2, 256); p->processBlock (b, mb); }
        std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (AmariKeysEditor::baseW, AmariKeysEditor::baseH);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        File f = outDir.getChildFile ("ui_loaded.png"); f.deleteFile();
        FileOutputStream fos (f); PNGImageFormat png; png.writeImageToStream (img, fos);
        ed.reset();
    }
    // UI snapshot
    {
        auto p = fresh();
        p->setPlayConfigDetails (0, 2, 48000.0, 256); p->prepareToPlay (48000.0, 256);
        std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (AmariKeysEditor::baseW, AmariKeysEditor::baseH);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        File f = outDir.getChildFile ("ui.png"); f.deleteFile();
        FileOutputStream fos (f); PNGImageFormat png; png.writeImageToStream (img, fos);
        std::cout << "ui.png written" << std::endl;
        ed.reset();
    }
    return 0;
}
