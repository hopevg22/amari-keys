#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>

// ============================================================================
// Plays a hosted instrument note by note and saves each note as a WAV file,
// named the way the Amari Keys loader expects ("Name_C#4_v3.wav"), plus an SFZ.
// No GUI code in here, so it can be tested offline.
struct MultiSampleRenderer
{
    struct Settings
    {
        juce::String name = "MyPiano";
        juce::File outDir;
        int lowNote = 21, highNote = 108, step = 2;
        juce::Array<int> velocities { 30, 55, 80, 105, 127 };
        double holdSeconds = 10.0, maxTailSeconds = 6.0;
        double sampleRate = 48000.0;
        int bitDepth = 24;
        float silenceDb = -70.0f;
        bool realtimePace = true, writeSfz = true;
        int pedalMode = 0; // 0 = pedal up, 1 = sustain pedal down (string resonance), 2 = both sets

        juce::Array<int> notes() const
        {
            juce::Array<int> n;
            for (int m = lowNote; m <= highNote; m += juce::jmax (1, step)) n.add (m);
            if (! n.isEmpty() && n.getLast() != highNote) n.add (highNote); // always include the top note
            return n;
        }
    };

    std::function<void (double)> onProgress;
    std::function<void (const juce::String&)> onLog;
    std::atomic<bool>* stopFlag = nullptr;

    static juce::String noteName (int m) { return juce::MidiMessage::getMidiNoteName (m, true, true, 4); } // C4 = 60

    // Words the Amari Keys folder loader treats as release/noise samples - keep them out of file names
    static juce::String safeName (juce::String n)
    {
        n = juce::File::createLegalFileName (n.trim()).replaceCharacters (" .", "__");
        for (auto w : { "release", "rel", "harm", "pedal", "noise" })
        {
            const int i = n.toLowerCase().indexOf (w);
            if (i >= 0) n = n.substring (0, i) + n.substring (i + (int) strlen (w));
        }
        n = n.trimCharactersAtStart ("_").trimCharactersAtEnd ("_");
        return n.isEmpty() ? juce::String ("Sampled") : n;
    }

    bool run (juce::AudioProcessor& p, const Settings& s)
    {
        const int bs = 512;
        const double sr = s.sampleRate;
        const auto notes = s.notes();
        auto vels = s.velocities; vels.sort();
        if (notes.isEmpty() || vels.isEmpty()) { log ("Nothing to record: check the note range and velocities."); return false; }

        const auto name = safeName (s.name);
        if (name != s.name) log ("File name used: " + name);
        auto dir = s.outDir;
        if (! dir.createDirectory()) { log ("Could not create folder " + dir.getFullPathName()); return false; }

        p.setNonRealtime (! s.realtimePace);
        p.setRateAndBufferSizeDetails (sr, bs);
        p.prepareToPlay (sr, bs);
        const int numCh = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        const int outCh = juce::jmin (2, p.getTotalNumOutputChannels());
        if (outCh < 1) { log ("This plugin has no audio outputs."); p.releaseResources(); return false; }

        juce::AudioBuffer<float> block (numCh, bs);
        const float silence = juce::Decibels::decibelsToGain (s.silenceDb);
        auto paceStart = juce::Time::getMillisecondCounterHiRes();
        juce::int64 renderedTotal = 0;

        auto process = [&] (juce::MidiBuffer& midi)
        {
            block.clear();
            {
                const juce::ScopedLock sl (p.getCallbackLock());
                p.processBlock (block, midi);
            }
            renderedTotal += bs;
            if (s.realtimePace) // give disk-streaming instruments real time to load their samples
            {
                const double due = paceStart + 1000.0 * (double) renderedTotal / sr;
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (due > now) juce::Thread::sleep ((int) (due - now));
            }
            float pk = 0;
            for (int c = 0; c < outCh; ++c) pk = juce::jmax (pk, block.getMagnitude (c, 0, bs));
            return pk;
        };

        auto flush = [&] (double maxSeconds) // render until quiet
        {
            juce::MidiBuffer none;
            for (int i = 0; i < (int) (maxSeconds * sr / bs); ++i)
                if (process (none) < silence * 0.5f && i > 2) break;
        };

        { // reset: all notes off, sustain pedal up
            juce::MidiBuffer m;
            for (int ch = 1; ch <= 16; ++ch) { m.addEvent (juce::MidiMessage::controllerEvent (ch, 64, 0), 0); m.addEvent (juce::MidiMessage::allNotesOff (ch), 0); }
            process (m);
            flush (3.0);
        }

        juce::Array<bool> passes;
        if (s.pedalMode == 0 || s.pedalMode == 2) passes.add (false);
        if (s.pedalMode == 1 || s.pedalMode == 2) passes.add (true);
        const int total = notes.size() * vels.size() * passes.size();
        int done = 0, clipped = 0, silentCount = 0;
        struct Made { int note, vel; bool pedal; juce::String file; };
        juce::Array<Made> made;
        log ("Recording " + juce::String (notes.size()) + " notes x " + juce::String (vels.size()) + " velocities"
             + (passes.size() > 1 ? " x 2 (pedal up + pedal down)" : (passes[0] ? " with the sustain pedal down" : juce::String()))
             + " = " + juce::String (total) + " samples");

        for (const bool pedal : passes)
        {
        if (pedal) log ("--- sustain pedal down set ---");
        for (int vi = 0; vi < vels.size(); ++vi)
        {
            for (int ni = 0; ni < notes.size(); ++ni)
            {
                if (stopFlag != nullptr && stopFlag->load()) { log ("Stopped."); p.releaseResources(); return false; }
                const int note = notes[ni], vel = juce::jlimit (1, 127, vels[vi]);

                std::vector<float> L, R;
                auto pedalTo = [&] (int value)
                {
                    juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, value), 0);
                    process (m);
                };
                auto record = [&] (int attempt)
                {
                    if (pedal) { pedalTo (127); flush (0.3); } // pedal down first, let its noise pass
                    L.clear(); R.clear();
                    const int hold = (int) (s.holdSeconds * sr);
                    const int maxLen = hold + (int) (s.maxTailSeconds * sr);
                    const int quietWindow = (int) (0.15 * sr);
                    int pos = 0, quietRun = 0;
                    while (pos < maxLen)
                    {
                        juce::MidiBuffer m;
                        if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), 0);
                        if (hold >= pos && hold < pos + bs) m.addEvent (juce::MidiMessage::noteOff (1, note), hold - pos);
                        const float pk = process (m);
                        for (int i = 0; i < bs; ++i)
                        {
                            L.push_back (block.getSample (0, i));
                            R.push_back (block.getSample (outCh > 1 ? 1 : 0, i));
                        }
                        pos += bs;
                        if (pos > hold) { quietRun = pk < silence ? quietRun + bs : 0; if (quietRun >= quietWindow) break; }
                    }
                    juce::ignoreUnused (attempt);
                };

                record (0);
                if (pedal) pedalTo (0);
                float peak = 0; for (size_t i = 0; i < L.size(); ++i) peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
                if (peak < silence * 4.0f) // nothing came out - the instrument may still be loading
                {
                    log (noteName (note) + " v" + juce::String (vi + 1) + ": silent, waiting and trying again...");
                    flush (2.0); juce::Thread::sleep (2000);
                    record (1);
                    if (pedal) pedalTo (0);
                    peak = 0; for (size_t i = 0; i < L.size(); ++i) peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
                }
                flush (3.0);
                ++done;
                if (onProgress) onProgress ((double) done / (double) total);

                if (peak < silence * 4.0f) { ++silentCount; log (noteName (note) + " v" + juce::String (vi + 1) + ": still silent - skipped"); continue; }
                if (peak >= 0.999f) ++clipped;

                // trim: start just before the attack, end when it falls below the silence level, short fade out
                const float onsetLevel = juce::jmax (silence, peak * 0.001f);
                size_t start = 0; while (start < L.size() && std::abs (L[start]) < onsetLevel && std::abs (R[start]) < onsetLevel) ++start;
                start = start > 32 ? start - 32 : 0;
                size_t end = L.size(); while (end > start + 1 && std::abs (L[end - 1]) < silence && std::abs (R[end - 1]) < silence) --end;
                end = juce::jmin (L.size(), end + (size_t) (0.01 * sr));
                const int len = (int) (end - start);
                const int fade = juce::jmin (len / 4, (int) (0.02 * sr));
                juce::AudioBuffer<float> out (outCh, len);
                for (int i = 0; i < len; ++i)
                {
                    const float g = i >= len - fade ? (float) (len - i) / (float) fade : 1.0f;
                    out.setSample (0, i, L[start + (size_t) i] * g);
                    if (outCh > 1) out.setSample (1, i, R[start + (size_t) i] * g);
                }

                const auto fileName = name + "_" + noteName (note) + "_v" + juce::String (vi + 1) + (pedal ? "_sus" : "") + ".wav";
                auto f = dir.getChildFile (fileName);
                if (! writeWav (f, out, sr, s.bitDepth)) { log ("Could not write " + fileName); continue; }
                made.add ({ note, vi, pedal, fileName });
                log (fileName + "  " + juce::String (len / sr, 1) + " s, peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB"
                     + (peak >= 0.999f ? "  CLIPPED" : ""));
            }
        }
        }
        p.releaseResources();
        p.setNonRealtime (false);

        if (s.writeSfz && ! made.isEmpty()) writeSfz (dir.getChildFile (name + ".sfz"), made, notes, vels, name);

        log ("Done: " + juce::String (made.size()) + " samples in " + dir.getFullPathName());
        if (clipped > 0) log ("WARNING: " + juce::String (clipped) + " samples clipped. Turn the instrument's output down and record again.");
        if (silentCount > 0) log ("WARNING: " + juce::String (silentCount) + " notes were silent and skipped.");
        if (s.writeSfz) log ("In Amari Keys use LOAD SFZ and pick " + name + ".sfz (exact mapping), or LOAD FOLDER.");
        return true;
    }

private:
    void log (const juce::String& t) { if (onLog) onLog (t); }

    static bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr, int bits)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        if (! os) return false;
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (bits));
        if (! w) return false;
        return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    template <typename MadeArray>
    static void writeSfz (const juce::File& f, const MadeArray& made, const juce::Array<int>& notes, const juce::Array<int>& vels, const juce::String& name)
    {
        juce::String t;
        t << "// " << name << " - recorded with Amari Sampler " << juce::Time::getCurrentTime().toString (true, true) << "\n";
        bool anyPedal = false, anyNormal = false;
        for (auto& m : made) (m.pedal ? anyPedal : anyNormal) = true;
        for (int pg = 0; pg < 2; ++pg)
        {
            const bool pedal = pg == 1;
            if ((pedal && ! anyPedal) || (! pedal && ! anyNormal)) continue;
            const juce::String cc = ! (anyPedal && anyNormal) ? juce::String() : pedal ? " locc64=64 hicc64=127" : " locc64=0 hicc64=63";
            if (pedal) t << "\n// ---- sustain pedal down ----\n";
            for (int vi = 0; vi < vels.size(); ++vi)
            {
                const int lovel = vi == 0 ? 1 : vels[vi - 1] + 1;
                const int hivel = vi == vels.size() - 1 ? 127 : vels[vi];
                t << "\n<group> lovel=" << lovel << " hivel=" << hivel << cc << "\n";
                juce::Array<int> have;
                for (auto& m : made) if (m.vel == vi && m.pedal == pedal) have.addIfNotAlreadyThere (m.note);
                have.sort();
                for (int k = 0; k < have.size(); ++k)
                {
                    const int n = have[k];
                    const int lo = k == 0 ? 0 : (have[k - 1] + n) / 2 + 1;
                    const int hi = k == have.size() - 1 ? 127 : (n + have[k + 1]) / 2;
                    for (auto& m : made)
                        if (m.vel == vi && m.pedal == pedal && m.note == n)
                            t << "<region> sample=" << m.file << " lokey=" << lo << " hikey=" << hi << " pitch_keycenter=" << n << "\n";
                }
            }
        }
        juce::ignoreUnused (notes);
        f.replaceWithText (t);
    }
};
