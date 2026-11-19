#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Sampler.h"
#include <iostream>
#include <random>
using namespace juce;

static void writeTone (const File& f, int midi, float amp, double sr = 44100.0)
{
    const double f0 = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
    const int n = (int) (sr * 1.5);
    AudioBuffer<float> b (2, n); b.clear();
    std::mt19937 rng (midi); std::uniform_real_distribution<float> u (-1, 1);
    const double B = 0.0004 * std::pow (2.0, (midi - 21) / 24.0); // inharmonicity grows up the keyboard
    for (int h = 1; h <= 30; ++h)
    {
        const double fh = h * f0 * std::sqrt (1.0 + B * h * h);
        if (fh > sr * 0.45) break;
        double a = 1.0 / h; if (midi < 40 && h == 1) a *= 0.15; if (midi < 40 && h == 2) a *= 0.5; // weak fundamental in the bass
        const double dec = 1.5 + h * 0.8 + (midi - 21) * 0.05;
        const double ph = u (rng) * 3.14;
        for (int i = 0; i < n; ++i)
        {
            const float v = (float) (a * std::exp (-dec * i / sr) * std::sin (6.2831853 * fh * i / sr + ph));
            b.addSample (0, i, v); b.addSample (1, i, v * 0.9f);
        }
    }
    for (int i = 0; i < (int) (sr * 0.01); ++i) { const float v = u (rng) * 0.5f * (1.0f - i / (float) (sr * 0.01)); b.addSample (0, i, v); b.addSample (1, i, v); }
    b.applyGain (amp / jmax (0.001f, b.getMagnitude (0, n)));
    f.deleteFile();
    WavAudioFormat wav; std::unique_ptr<OutputStream> os (f.createOutputStream());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (16));
    w->writeFromAudioSampleBuffer (b, 0, n);
}

static String nm (int m) { return MidiMessage::getMidiNoteName (m, true, true, 4); }

int main()
{
    
    auto root = File::getCurrentWorkingDirectory().getChildFile ("sets");
    root.deleteRecursively();
    std::vector<int> notes; for (int m = 21; m <= 108; m += 3) notes.push_back (m);
    const int N = (int) notes.size();
    struct Case { String name; std::function<String (int idx, int midi, int layer)> fn; };
    std::vector<Case> cases = {
        { "correct",   [] (int, int m, int l) { return "Piano_" + nm (m) + "_v" + String (l + 1); } },
        { "salamander",[] (int, int m, int l) { return nm (m).replace ("#", "#") + "v" + String (l * 8 + 4); } },
        { "reversed",  [N, &notes] (int i, int, int l) { return "Grand " + nm (notes[(size_t) (N - 1 - i)]) + " " + (l ? "ff" : "pp"); } },
        { "indexed",   [] (int i, int, int l) { return "Piano " + String (i + 1).paddedLeft ('0', 2) + (l ? "_loud" : "_soft"); } },
        { "midinum",   [] (int, int m, int l) { return String (m).paddedLeft ('0', 3) + "_vel" + String (l + 1); } },
        { "nonames",   [] (int i, int, int l) { return String ("Take ") + String::charToString ((juce_wchar) ('a' + i % 26)) + String::charToString ((juce_wchar) ('a' + i / 26)) + (l ? " ff" : " pp"); } },
    };
    int fails = 0;
    for (auto& c : cases)
    {
        auto dir = root.getChildFile (c.name); dir.createDirectory();
        std::map<String, int> truth;
        for (int i = 0; i < N; ++i)
            for (int l = 0; l < 2; ++l)
            {
                auto f = dir.getChildFile (c.fn (i, notes[(size_t) i], l) + ".wav");
                writeTone (f, notes[(size_t) i], l ? 0.9f : 0.25f);
                truth[f.getFileName()] = notes[(size_t) i];
            }
        String msg;
        auto t0 = Time::getMillisecondCounterHiRes();
        auto set = SampleLoader::load (dir, msg, [] (float) {});
        auto ms = Time::getMillisecondCounterHiRes() - t0;
        int wrong = 0, total = 0;
        StringArray lines; lines.addLines (set ? set->mappingReport : String());
        for (auto& ln : lines)
        {
            if (! ln.contains ("  ->  ") || ln.startsWith ("file  ->")) continue;
            auto file = ln.upToFirstOccurrenceOf ("  ->  ", false, false);
            auto note = ln.fromFirstOccurrenceOf ("  ->  ", false, false).upToFirstOccurrenceOf ("  |", false, false);
            ++total;
            if (nm (truth[file]) != note) { ++wrong; if (wrong <= 4) std::cout << "   WRONG " << ln << "  (truth " << nm (truth[file]) << ")\n"; }
        }
        std::cout << c.name << ": " << msg << " | " << (set ? set->mappingInfo : "") << " | " << total << " zones, " << wrong << " wrong, " << (int) ms << " ms\n";
        if (wrong || total != 2 * N) ++fails;
    }
    std::cout << (fails ? "FAILED cases: " + String (fails) : String ("ALL OK")) << std::endl;
    return fails;
}
