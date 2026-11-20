#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "DspCommon.h"
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

// ============================================================================
// A loaded multisample instrument (16-bit in memory to keep big pianos manageable)
struct SampleZone
{
    std::vector<int16_t> data; // interleaved
    int channels = 1, length = 0;
    double sourceRate = 48000.0;
    int root = 60, lokey = 0, hikey = 127, lovel = 1, hivel = 127;
    float tuneCents = 0.0f, gain = 1.0f;
    float norm = 1.0f;     // data is stored peak-normalised (full 16-bit resolution); multiply by this to get the recorded level
    float loud = 0.0f;     // RMS of the first 300 ms at the recorded level - used to blend velocities between layers
    bool pedal = false;    // recorded with the sustain pedal down (used while the pedal is held)
};

struct ZoneHit { const SampleZone* zone = nullptr; float gain = 1.0f; };

struct SampleSet
{
    juce::String name, sourcePath;
    std::vector<std::unique_ptr<SampleZone>> zones;
    int numLayers = 1, lowNote = 127, highNote = 0;
    juce::String mappingInfo, mappingReport;
    size_t bytes = 0;

    bool hasPedalSet = false;

    const SampleZone* find (int note, int vel, bool pedalDown = false) const
    {
        const bool wantPedal = pedalDown && hasPedalSet;
        const SampleZone* best = nullptr; int bestDist = 1 << 30;
        for (auto& z : zones)
        {
            if (z->pedal != wantPedal) continue;
            if (note < z->lokey || note > z->hikey) continue;
            if (vel >= z->lovel && vel <= z->hivel) return z.get();
            const int d = vel < z->lovel ? z->lovel - vel : vel - z->hivel; // nearest layer if gaps
            if (d < bestDist) { bestDist = d; best = z.get(); }
        }
        if (best == nullptr && wantPedal) return find (note, vel, false); // no pedal sample for this key - use the normal one
        if (best == nullptr && ! pedalDown && hasPedalSet)                 // pedal-only set: use it with the pedal up too
            for (auto& z : zones) if (z->pedal && note >= z->lokey && note <= z->hikey && vel >= z->lovel && vel <= z->hivel) return z.get();
        return best;
    }

    // The zone to play plus a gain that moves smoothly between the recorded velocity layers,
    // so the recorded dynamics are used as they are (no second velocity curve on top).
    ZoneHit play (int note, int vel, bool pedalDown, float singleLayerDynamics) const
    {
        ZoneHit h;
        h.zone = find (note, vel, pedalDown);
        if (h.zone == nullptr) return h;
        const auto* z = h.zone;
        if (numLayers <= 1 || z->loud <= 0.0f)
        {
            h.gain = (1.0f - singleLayerDynamics) + singleLayerDynamics * (float) vel / 127.0f;
            return h;
        }
        // the next softer layer for the same key
        const SampleZone* lower = nullptr;
        for (auto& o : zones)
            if (o.get() != z && o->pedal == z->pedal && note >= o->lokey && note <= o->hikey && o->hivel < z->lovel && o->loud > 0.0f)
                if (lower == nullptr || o->hivel > lower->hivel) lower = o.get();
        const float v = (float) juce::jlimit (1, 127, vel);
        const float vHi = (float) juce::jmax (1, z->hivel);
        float db = 0.0f;
        if (v >= vHi) db = 0.0f;
        else if (lower != nullptr)
        {
            const float vLo = (float) lower->hivel;
            const float t = juce::jlimit (0.0f, 1.0f, (v - vLo) / juce::jmax (1.0f, vHi - vLo));
            const float dbLo = juce::Decibels::gainToDecibels (lower->loud), dbHi = juce::Decibels::gainToDecibels (z->loud);
            db = (1.0f - t) * (dbLo - dbHi);                     // 0 at this layer's velocity, softer towards the layer below
        }
        else db = 40.0f * std::log10 (v / vHi);                  // below the softest layer: amplitude ~ velocity squared
        h.gain = juce::Decibels::decibelsToGain (juce::jmax (-40.0f, db));
        return h;
    }
};

// ============================================================================
// Loader: a folder of audio files named by note (e.g. "Piano_C4_v2.wav", "A0v1.flac", "060.wav")
// or an .sfz file (sample, key, lokey, hikey, pitch_keycenter, lovel, hivel, tune, volume, default_path)
struct SampleLoader
{
    static constexpr int maxSeconds = 30;

    static int parseNoteName (juce::String s) // "C#4", "db3", "a0" -> midi (C4 = 60); -1 if none
    {
        s = s.trim().toLowerCase();
        if (s.isEmpty()) return -1;
        if (s.containsOnly ("0123456789")) { const int v = s.getIntValue(); return v >= 0 && v <= 127 ? v : -1; }
        static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
        const juce::juce_wchar c = s[0];
        if (c < 'a' || c > 'g') return -1;
        int n = base[c - 'a'], i = 1;
        if (s[i] == '#' || s[i] == 's') { ++n; ++i; }
        else if (s[i] == 'b' && s.length() > 2 && (juce::CharacterFunctions::isDigit (s[2]) || s[2] == '-')) { --n; ++i; }
        auto rest = s.substring (i);
        if (rest.isEmpty() || ! (juce::CharacterFunctions::isDigit (rest[0]) || rest[0] == '-')) return -1;
        const int oct = rest.getIntValue();
        const int midi = (oct + 1) * 12 + n;
        return midi >= 0 && midi <= 127 ? midi : -1;
    }

    // find a note + optional velocity layer inside a file name
    static bool parseFileName (const juce::String& stem, int& note, int& layer)
    {
        note = -1; layer = -1;
        auto lower = stem.toLowerCase();
        // dynamics words
        const char* dyn[] = { "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff" };
        juce::StringArray tokens;
        tokens.addTokens (lower, "_- .()[]", "");
        tokens.removeEmptyStrings();
        for (auto& t : tokens)
        {
            for (int d = 0; d < 8; ++d) if (t == dyn[d]) layer = d;
            if (t == "soft" || t == "quiet") layer = 1;
            else if (t == "med" || t == "medium" || t == "mid") layer = 3;
            else if (t == "loud" || t == "hard") layer = 6;
            if (t.startsWith ("vel") && t.substring (3).containsOnly ("0123456789") && t.length() > 3) layer = t.substring (3).getIntValue();
            else if (t.startsWith ("v") && t.length() > 1 && t.substring (1).containsOnly ("0123456789")) layer = t.substring (1).getIntValue();
        }
        // pass 1: note-name tokens like "c4v12" / "a#3v2" / "c4" / "db3" (a letter note always wins over plain numbers)
        for (int ti = tokens.size(); --ti >= 0;)
        {
            auto t = tokens[ti];
            if (t.containsOnly ("0123456789")) continue;
            const int vpos = t.lastIndexOfChar ('v');
            if (vpos > 0 && t.substring (vpos + 1).containsOnly ("0123456789") && t.length() > vpos + 1)
            {
                const int nn = parseNoteName (t.substring (0, vpos));
                if (nn >= 0) { note = nn; layer = t.substring (vpos + 1).getIntValue(); return true; }
            }
            const int nn = parseNoteName (t);
            if (nn >= 0) { note = nn; return true; }
        }
        // pass 2: a plain MIDI note number ("060", "60") - only if it is the only number in the name
        int numericCount = 0, numericValue = -1;
        for (auto& t : tokens)
            if (t.containsOnly ("0123456789") && t.length() >= 2) { ++numericCount; numericValue = t.getIntValue(); }
        if (numericCount == 1 && numericValue >= 0 && numericValue <= 127) { note = numericValue; return true; }
        return false;
    }

    static std::unique_ptr<SampleZone> readZone (juce::AudioFormatManager& fm, const juce::File& f, size_t& bytes)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (! r) return nullptr;
        auto z = std::make_unique<SampleZone>();
        z->channels = (int) juce::jmin (2u, r->numChannels);
        z->sourceRate = r->sampleRate;
        z->length = (int) juce::jmin ((juce::int64) (r->sampleRate * maxSeconds), r->lengthInSamples);
        juce::AudioBuffer<float> tmp (z->channels, z->length);
        r->read (&tmp, 0, z->length, 0, true, z->channels > 1);
        // keep full 16-bit resolution even for very soft layers: store peak-normalised, remember the level
        const float rawPeak = tmp.getMagnitude (0, z->length);
        const float peak = rawPeak < 3.0e-5f ? 1.0f : rawPeak; // (near-)silent file: don't blow up its noise
        z->norm = peak;
        {
            const int n300 = juce::jmin (z->length, (int) (r->sampleRate * 0.3));
            double acc = 0.0;
            for (int c = 0; c < z->channels; ++c) { const float* d = tmp.getReadPointer (c); for (int i = 0; i < n300; ++i) acc += (double) d[i] * d[i]; }
            z->loud = n300 > 0 ? (float) std::sqrt (acc / (double) (n300 * z->channels)) : 0.0f;
        }
        const float k = 32767.0f / peak;
        juce::Random dither (12345);
        z->data.resize ((size_t) z->length * (size_t) z->channels);
        for (int i = 0; i < z->length; ++i)
            for (int c = 0; c < z->channels; ++c)
            {
                const float tpdf = dither.nextFloat() - dither.nextFloat(); // TPDF dither: smooth quiet tails instead of grainy steps
                z->data[(size_t) i * (size_t) z->channels + (size_t) c] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, std::round (tmp.getSample (c, i) * k + tpdf));
            }
        bytes += z->data.size() * sizeof (int16_t);
        return z;
    }

    static bool isAudio (const juce::File& f)
    {
        return f.hasFileExtension ("wav;aif;aiff;flac;ogg");
    }

    static std::unique_ptr<SampleSet> load (const juce::File& source, juce::String& message, std::function<void (float)> progress)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        auto set = std::make_unique<SampleSet>();
        set->sourcePath = source.getFullPathName();
        set->name = source.getFileNameWithoutExtension();

        if (source.hasFileExtension ("sfz")) loadSfz (fm, source, *set, message, progress);
        else if (source.isDirectory()) loadFolder (fm, source, *set, message, progress);
        else { message = "Choose a folder of samples or an .sfz file"; return nullptr; }

        if (set->zones.empty()) { if (message.isEmpty()) message = "No usable samples found"; return nullptr; }
        for (auto& z : set->zones) { set->lowNote = juce::jmin (set->lowNote, z->root); set->highNote = juce::jmax (set->highNote, z->root); }
        return set;
    }

    // ------------------------------------------------------------------------
    // Pitch detection (YIN) on a short window just after the attack.
    // Returns a fractional MIDI note, or -1 when the sample has no clear pitch.
    static float detectPitch (const SampleZone& z)
    {
        const double sr = z.sourceRate;
        const int ch = z.channels;
        auto mono = [&] (int i) { float v = 0; for (int c = 0; c < ch; ++c) v += (float) z.data[(size_t) i * (size_t) ch + (size_t) c]; return v / (float) ch; };

        const int scan = juce::jmin (z.length, (int) (sr * 0.5));
        int peakPos = 0; float peak = 0;
        for (int i = 0; i < scan; ++i) { const float v = std::abs (mono (i)); if (v > peak) { peak = v; peakPos = i; } }
        if (peak < 60.0f) return -1.0f; // silent

        const int tauMin = juce::jmax (2, (int) (sr / 4600.0)); // up to ~C8
        const int tauMax = (int) (sr / 25.0);                    // down to ~A0
        const int W = (tauMax * 3) / 2;
        int start = peakPos + (int) (sr * 0.03);                 // skip the hammer noise
        if (start + W + tauMax + 2 > z.length) start = z.length - (W + tauMax + 2);
        if (start < 0) return -1.0f;

        std::vector<float> x ((size_t) (W + tauMax + 2));
        for (size_t i = 0; i < x.size(); ++i) x[i] = mono (start + (int) i);

        std::vector<double> d ((size_t) tauMax + 2, 0.0), cm ((size_t) tauMax + 2, 1.0);
        auto diff = [&] (int tau) { double s = 0; for (int j = 0; j < W; ++j) { const double e = x[(size_t) j] - x[(size_t) (j + tau)]; s += e * e; } return s; };

        double running = 0; int best = -1; double bestVal = 1e9;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            d[(size_t) tau] = diff (tau);
            running += d[(size_t) tau];
            cm[(size_t) tau] = running > 0 ? d[(size_t) tau] * tau / running : 1.0;
            if (tau <= tauMin) continue;
            const int prev = tau - 1;
            if (prev > tauMin && cm[(size_t) prev] < 0.15 && cm[(size_t) tau] >= cm[(size_t) prev]) { best = prev; break; } // first dip under threshold
            if (cm[(size_t) tau] < bestVal && tau <= tauMax) { bestVal = cm[(size_t) tau]; }
        }
        if (best < 0)
        {
            // no dip under the threshold: accept the global minimum only if it is reasonably clear
            for (int tau = tauMin + 1; tau <= tauMax; ++tau) if (cm[(size_t) tau] == bestVal) { best = tau; break; }
            if (best < 0 || bestVal > 0.35) return -1.0f;
        }
        double t = best;
        if (best > 1 && best < tauMax + 1)
        {
            const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1e-12) t += 0.5 * (a - c) / den;
        }
        const double f = sr / t;
        return (float) (69.0 + 12.0 * std::log2 (f / 440.0));
    }

    static bool isPedalDownName (const juce::String& stem)
    {
        juce::StringArray t; t.addTokens (stem.toLowerCase(), "_- .()[]", ""); t.removeEmptyStrings();
        return t.contains ("sus") || t.contains ("pdown") || t.contains ("pedaldown") || t.contains ("ped");
    }

    static float wrap12 (float v) { v = std::fmod (v, 12.0f); if (v > 6.0f) v -= 12.0f; if (v < -6.0f) v += 12.0f; return v; }

    static void loadFolder (juce::AudioFormatManager& fm, const juce::File& dir, SampleSet& set, juce::String& message, std::function<void (float)> progress)
    {
        struct Item { juce::File f; int named = -1, layer = -1, note = -1; float heard = -1.0f; bool pedal = false; std::unique_ptr<SampleZone> z; };
        std::vector<juce::File> files; int skipped = 0;
        for (const auto& e : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
        {
            const auto f = e.getFile();
            if (! isAudio (f)) continue;
            const auto stem = f.getFileNameWithoutExtension().toLowerCase();
            if (stem.contains ("rel") || stem.contains ("harm") || stem.contains ("pedal") || stem.contains ("noise")) { ++skipped; continue; }
            files.push_back (f);
        }
        if (files.empty()) { message = "No audio files (wav, aif, flac, ogg) found in this folder"; return; }
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });

        // 1) read every file, parse its name and listen to its pitch
        std::vector<Item> items;
        int done = 0;
        for (auto& f : files)
        {
            Item it; it.f = f;
            parseFileName (f.getFileNameWithoutExtension(), it.named, it.layer);
            it.pedal = isPedalDownName (f.getFileNameWithoutExtension());
            it.z = readZone (fm, f, set.bytes);
            progress (0.95f * (float) ++done / (float) files.size());
            if (! it.z) { ++skipped; continue; }
            it.heard = detectPitch (*it.z);
            if (it.named < 0 && it.heard < 0) { set.bytes -= it.z->data.size() * sizeof (int16_t); ++skipped; continue; }
            items.push_back (std::move (it));
        }
        if (items.empty()) { message = "No samples with a note in the name or a clear pitch were found"; return; }

        // 2) decide how to map: trust the names, names with a fixed shift / reversed, or the heard pitch
        std::vector<std::pair<float, float>> pairs; // named, heard
        for (auto& it : items) if (it.named >= 0 && it.heard >= 0) pairs.push_back ({ (float) it.named, it.heard });
        auto fitFor = [&] (int sign, float offset)
        {
            if (pairs.empty()) return 0.0f;
            int ok = 0;
            for (auto& p : pairs) if (std::abs (wrap12 (p.second - (sign * p.first + offset))) <= 1.0f) ++ok;
            return (float) ok / (float) pairs.size();
        };
        auto medianOffset = [&] (int sign)
        {
            std::vector<float> r; for (auto& p : pairs) r.push_back (p.second - sign * p.first);
            std::nth_element (r.begin(), r.begin() + (long) r.size() / 2, r.end());
            return (float) std::round (r[r.size() / 2]);
        };
        int sign = 1; float offset = 0.0f; bool byPitch = false;
        juce::String how;
        const int namedCount = (int) std::count_if (items.begin(), items.end(), [] (const Item& i) { return i.named >= 0; });
        if (pairs.size() >= 4)
        {
            const float fitNames = fitFor (1, 0.0f);
            const float offUp = medianOffset (1), offRev = medianOffset (-1);
            const float fitUp = fitFor (1, offUp), fitRev = fitFor (-1, offRev);
            if (fitNames >= 0.6f) how = "file names (pitch-checked " + juce::String (juce::roundToInt (fitNames * 100)) + "% match)";
            else if (fitUp >= 0.6f && fitUp >= fitRev) { offset = offUp; how = "file names shifted " + juce::String (offUp > 0 ? "+" : "") + juce::String ((int) offUp) + " semitones (names did not match the audio)"; }
            else if (fitRev >= 0.6f) { sign = -1; offset = offRev; how = "file names REVERSED (names ran high-to-low against the audio)"; }
            else { byPitch = true; how = "detected pitch (file names did not match the audio)"; }
        }
        else if (namedCount == 0) { byPitch = true; how = "detected pitch (no note names in the files)"; }
        else how = "file names (too few clear pitches to check)";

        for (auto& it : items)
        {
            if (! byPitch && it.named >= 0) it.note = sign * it.named + (int) offset;
            else if (it.heard >= 0) it.note = juce::roundToInt (it.heard);
            else it.note = it.named;
            it.note = juce::jlimit (0, 127, it.note);
        }

        // 3) velocity layers (soft -> loud) and key ranges between neighbouring roots,
        //    separately for normal samples and sustain-pedal-down samples ("_sus")
        juce::String report;
        report << "Amari Keys sample mapping - " << dir.getFullPathName() << "\n"
               << "Mapped by: " << how << "\n\n"
               << "file  ->  plays as  |  name says  |  audio sounds like\n";
        int L = 0;
        set.numLayers = 1;
        for (int pg = 0; pg < 2; ++pg)
        {
        std::vector<int> layers;
        for (auto& it : items) if (it.pedal == (pg == 1) && std::find (layers.begin(), layers.end(), it.layer) == layers.end()) layers.push_back (it.layer);
        std::sort (layers.begin(), layers.end());
        const int LG = (int) layers.size();
        if (LG == 0) continue;
        if (pg == 1) { set.hasPedalSet = true; report << "\n===== sustain pedal down samples =====\n"; }
        L = juce::jmax (L, LG);
        set.numLayers = juce::jmax (set.numLayers, LG);
        for (int li = 0; li < LG; ++li)
        {
            std::vector<Item*> layerItems;
            for (auto& it : items) if (it.pedal == (pg == 1) && it.layer == layers[(size_t) li]) layerItems.push_back (&it);
            std::stable_sort (layerItems.begin(), layerItems.end(), [] (Item* a, Item* b) { return a->note < b->note; });
            std::vector<Item*> uniq;
            for (auto* it : layerItems)
            {
                if (! uniq.empty() && uniq.back()->note == it->note) { set.bytes -= it->z->data.size() * sizeof (int16_t); it->z.reset(); continue; } // round robin - keep first
                uniq.push_back (it);
            }
            const int lovel = 1 + (127 * li) / LG, hivel = (127 * (li + 1)) / LG;
            report << "\n[velocity layer " << (li + 1) << " of " << LG << ", vel " << lovel << "-" << hivel << "]\n";
            for (size_t k = 0; k < uniq.size(); ++k)
            {
                auto* it = uniq[k];
                auto z = std::move (it->z);
                z->root = it->note;
                z->lokey = k == 0 ? 0 : (uniq[k - 1]->note + it->note) / 2 + 1;
                z->hikey = k + 1 == uniq.size() ? 127 : (it->note + uniq[k + 1]->note) / 2;
                z->lovel = lovel; z->hivel = hivel; z->pedal = (pg == 1);
                report << it->f.getFileName() << "  ->  " << juce::MidiMessage::getMidiNoteName (it->note, true, true, 4)
                       << "  |  " << (it->named >= 0 ? juce::MidiMessage::getMidiNoteName (it->named, true, true, 4) : juce::String ("-"))
                       << "  |  " << (it->heard >= 0 ? juce::MidiMessage::getMidiNoteName (juce::roundToInt (it->heard), true, true, 4) : juce::String ("?")) << "\n";
                set.zones.push_back (std::move (z));
            }
        }
        }
        progress (1.0f);
        message = juce::String (set.zones.size()) + " samples, " + juce::String (L) + (L == 1 ? " layer" : " layers")
                + (set.hasPedalSet ? " + pedal-down set" : "")
                + (skipped > 0 ? " (" + juce::String (skipped) + " skipped)" : juce::String());

        auto lo = std::min_element (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.note < b.note; });
        auto hi = std::max_element (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.note < b.note; });
        set.mappingInfo = "Mapped by " + how + "  |  " + lo->f.getFileName() + " = " + juce::MidiMessage::getMidiNoteName (lo->note, true, true, 4) + ",  "
                        + hi->f.getFileName() + " = " + juce::MidiMessage::getMidiNoteName (hi->note, true, true, 4);
        set.mappingReport = report;
        if (dir.getChildFile ("AmariKeys_mapping.txt").replaceWithText (report)) set.mappingInfo << "  |  details: AmariKeys_mapping.txt";
    }

    static void loadSfz (juce::AudioFormatManager& fm, const juce::File& sfz, SampleSet& set, juce::String& message, std::function<void (float)> progress)
    {
        using Map = std::map<juce::String, juce::String>;
        Map global, group, control;
        std::vector<Map> regions;
        Map* current = nullptr; Map regionTmp; bool inRegion = false;
        auto flushRegion = [&] { if (inRegion) { Map m = global; for (auto& kv : group) m[kv.first] = kv.second; for (auto& kv : regionTmp) m[kv.first] = kv.second; regions.push_back (m); } inRegion = false; regionTmp.clear(); };

        auto text = sfz.loadFileAsString();
        // strip comments
        juce::StringArray lines; lines.addLines (text);
        juce::String all;
        for (auto l : lines) { const int c = l.indexOf ("//"); all << (c >= 0 ? l.substring (0, c) : l) << "\n"; }

        int i = 0; const int n = all.length();
        while (i < n)
        {
            if (all[i] == '<')
            {
                const int e = all.indexOfChar (i, '>'); if (e < 0) break;
                const auto header = all.substring (i + 1, e).trim().toLowerCase();
                flushRegion();
                if (header == "region") { inRegion = true; current = &regionTmp; }
                else if (header == "group") { group.clear(); current = &group; }
                else if (header == "global") { global.clear(); group.clear(); current = &global; }
                else if (header == "control") { current = &control; }
                else current = nullptr;
                i = e + 1; continue;
            }
            if (juce::CharacterFunctions::isWhitespace (all[i])) { ++i; continue; }
            // opcode=value ; value for "sample" may contain spaces, ends at next opcode or header
            const int eq = all.indexOfChar (i, '=');
            if (eq < 0) break;
            const auto key = all.substring (i, eq).trim().toLowerCase();
            int j = eq + 1, end = j;
            if (key == "sample")
            {
                end = j;
                while (end < n && all[end] != '\n' && all[end] != '<')
                {
                    // stop before " nextkey="
                    if (juce::CharacterFunctions::isWhitespace (all[end]))
                    {
                        int k = end; while (k < n && juce::CharacterFunctions::isWhitespace (all[k]) && all[k] != '\n') ++k;
                        int w = k; while (w < n && (juce::CharacterFunctions::isLetterOrDigit (all[w]) || all[w] == '_')) ++w;
                        if (w < n && all[w] == '=' && w > k) break;
                    }
                    ++end;
                }
            }
            else { while (end < n && ! juce::CharacterFunctions::isWhitespace (all[end]) && all[end] != '<') ++end; }
            const auto value = all.substring (j, end).trim();
            if (current) (*current)[key] = value;
            i = end;
        }
        flushRegion();

        auto noteVal = [] (const juce::String& v) { const int x = parseNoteName (v); return x; };
        juce::File base = sfz.getParentDirectory();
        if (control.count ("default_path")) base = base.getChildFile (control["default_path"].replaceCharacter ('\\', '/'));

        std::map<juce::String, SampleZone*> cache; // not shared: each region loads its own copy only if new path
        std::set<int> vels; int done = 0, skipped = 0;
        for (auto& r : regions)
        {
            progress ((float) ++done / (float) regions.size());
            if (! r.count ("sample")) { ++skipped; continue; }
            if (r.count ("trigger") && r["trigger"].toLowerCase() != "attack") { ++skipped; continue; }
            auto f = base.getChildFile (r["sample"].replaceCharacter ('\\', '/'));
            if (! f.existsAsFile()) { ++skipped; continue; }
            auto z = readZone (fm, f, set.bytes);
            if (! z) { ++skipped; continue; }
            int key = r.count ("key") ? noteVal (r["key"]) : -1;
            z->lokey = r.count ("lokey") ? noteVal (r["lokey"]) : (key >= 0 ? key : 0);
            z->hikey = r.count ("hikey") ? noteVal (r["hikey"]) : (key >= 0 ? key : 127);
            z->root = r.count ("pitch_keycenter") ? noteVal (r["pitch_keycenter"]) : (key >= 0 ? key : z->lokey);
            z->lovel = r.count ("lovel") ? r["lovel"].getIntValue() : 1;
            z->hivel = r.count ("hivel") ? r["hivel"].getIntValue() : 127;
            z->tuneCents = r.count ("tune") ? r["tune"].getFloatValue() : 0.0f;
            z->gain = r.count ("volume") ? juce::Decibels::decibelsToGain (r["volume"].getFloatValue()) : 1.0f;
            if (z->lokey < 0) z->lokey = 0;
            if (z->hikey < 0) z->hikey = 127;
            if (z->root < 0) z->root = 60;
            if (r.count ("locc64") && r["locc64"].getIntValue() >= 64) { z->pedal = true; set.hasPedalSet = true; } // sustain-pedal-down samples
            vels.insert (z->lovel);
            set.zones.push_back (std::move (z));
        }
        set.numLayers = juce::jmax (1, (int) vels.size());
        message = juce::String (set.zones.size()) + " samples, " + juce::String (set.numLayers) + (set.numLayers == 1 ? " layer" : " layers")
                + (set.hasPedalSet ? " + pedal-down set" : "")
                + (skipped > 0 ? " (" + juce::String (skipped) + " skipped)" : juce::String());
        juce::ignoreUnused (cache);
    }
};

// ============================================================================
struct SamplerEngine
{
    float dynamics = 0.6f, release = 0.3f; int timbre = 1;
    static constexpr float outputGain = 2.0f; // with default layer level + master, a sample plays close to its recorded level

    void prepare (double sampleRate) { sr = sampleRate; allOff(); }
    void setSet (const SampleSet* s) { set = s; allOff(); }

    void noteOn (int note, float vel, bool pedalDown = false)
    {
        if (! set) return;
        const int v127 = juce::jlimit (1, 127, (int) std::round (vel * 127.0f));
        const auto hit = set->play (note, v127, pedalDown, dynamics);
        const auto* z = hit.zone;
        if (! z) return;
        for (auto& v : voices) if (v.active && v.note == note && ! v.released) { v.released = true; v.fastRelease = true; } // re-strike
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age > v->age) v = &x; }
        *v = Voice {};
        v->active = true; v->note = note; v->zone = z;
        v->inc = std::pow (2.0, (note - z->root + z->tuneCents / 100.0) / 12.0) * z->sourceRate / sr;
        v->gain = hit.gain * z->gain * z->norm * outputGain; // plays each sample at its recorded level
    }
    void noteOff (int note) { for (auto& v : voices) if (v.active && v.note == note) v.released = true; }
    void allOff() { for (auto& v : voices) v.active = false; }

    void render (float* outL, float* outR, int num)
    {
        if (! set) return;
        const float relCoef = (float) std::exp (-1.0 / (sr * juce::jmax (0.02f, release)));
        const float fastCoef = (float) std::exp (-1.0 / (sr * 0.03));
        const float k = 1.0f / 32767.0f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto* z = v.zone;
            const int ch = z->channels; const int16_t* d = z->data.data();
            for (int i = 0; i < num; ++i)
            {
                const int p = (int) v.pos;
                if (p + 2 >= z->length) { v.active = false; break; }
                const float f = (float) (v.pos - p);
                float l, r;
                // cubic Hermite interpolation
                auto sample = [&] (int idx, int c) { return (float) d[(size_t) juce::jmax (0, idx) * (size_t) ch + (size_t) c] * k; };
                auto herm = [&] (int c)
                {
                    const float xm1 = sample (p - 1, c), x0 = sample (p, c), x1 = sample (p + 1, c), x2 = sample (p + 2, c);
                    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                    return ((c3 * f + c2) * f + c1) * f + x0;
                };
                l = herm (0); r = ch > 1 ? herm (1) : l;
                v.att = juce::jmin (1.0f, v.att + 0.02f);
                if (v.released) v.env *= v.fastRelease ? fastCoef : relCoef;
                const float a = v.att * v.env * v.gain;
                outL[i] += l * a; outR[i] += r * a;
                v.pos += v.inc;
            }
            if (v.env < 0.0002f) v.active = false;
            ++v.age;
        }
        // timbre: soft = gentle low-pass, bright = presence lift
        if (timbre != 1)
        {
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (timbre == 0 ? 3200.0f : 2500.0f) / (float) sr);
            for (int i = 0; i < num; ++i)
            {
                lpL += a * (outL[i] - lpL); lpR += a * (outR[i] - lpR);
                if (timbre == 0) { outL[i] = lpL; outR[i] = lpR; }
                else { outL[i] += 0.6f * (outL[i] - lpL); outR[i] += 0.6f * (outR[i] - lpR); }
            }
        }
    }

private:
    struct Voice
    {
        bool active = false, released = false, fastRelease = false;
        int note = 0; const SampleZone* zone = nullptr;
        double pos = 0.0, inc = 1.0; float gain = 1.0f, env = 1.0f, att = 0.0f; uint32_t age = 0;
    };
    Voice voices[64];
    const SampleSet* set = nullptr;
    double sr = 48000.0;
    float lpL = 0, lpR = 0;
};
