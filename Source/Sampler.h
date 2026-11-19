#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "DspCommon.h"
#include <map>
#include <set>

// ============================================================================
// A loaded multisample instrument (16-bit in memory to keep big pianos manageable)
struct SampleZone
{
    std::vector<int16_t> data; // interleaved
    int channels = 1, length = 0;
    double sourceRate = 48000.0;
    int root = 60, lokey = 0, hikey = 127, lovel = 1, hivel = 127;
    float tuneCents = 0.0f, gain = 1.0f;
};

struct SampleSet
{
    juce::String name, sourcePath;
    std::vector<std::unique_ptr<SampleZone>> zones;
    int numLayers = 1, lowNote = 127, highNote = 0;
    juce::String mappingInfo;
    size_t bytes = 0;

    const SampleZone* find (int note, int vel) const
    {
        const SampleZone* best = nullptr; int bestDist = 1 << 30;
        for (auto& z : zones)
        {
            if (note < z->lokey || note > z->hikey) continue;
            if (vel >= z->lovel && vel <= z->hivel) return z.get();
            const int d = vel < z->lovel ? z->lovel - vel : vel - z->hivel; // nearest layer if gaps
            if (d < bestDist) { bestDist = d; best = z.get(); }
        }
        return best;
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
        z->data.resize ((size_t) z->length * (size_t) z->channels);
        for (int i = 0; i < z->length; ++i)
            for (int c = 0; c < z->channels; ++c)
                z->data[(size_t) i * (size_t) z->channels + (size_t) c] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, tmp.getSample (c, i) * 32767.0f);
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

    static void loadFolder (juce::AudioFormatManager& fm, const juce::File& dir, SampleSet& set, juce::String& message, std::function<void (float)> progress)
    {
        struct Item { juce::File f; int note, layer; };
        std::vector<Item> items; int skipped = 0;
        for (const auto& e : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
        {
            const auto f = e.getFile();
            if (! isAudio (f)) continue;
            const auto stem = f.getFileNameWithoutExtension().toLowerCase();
            if (stem.contains ("rel") || stem.contains ("harm") || stem.contains ("pedal") || stem.contains ("noise")) { ++skipped; continue; }
            int note, layer;
            if (parseFileName (f.getFileNameWithoutExtension(), note, layer)) items.push_back ({ f, note, layer });
            else ++skipped;
        }
        if (items.empty()) { message = "No file names with a note (e.g. C4, F#2, 060) were found"; return; }

        // velocity layers: sorted distinct layer ids, soft -> loud
        std::vector<int> layers;
        for (auto& it : items) if (std::find (layers.begin(), layers.end(), it.layer) == layers.end()) layers.push_back (it.layer);
        std::sort (layers.begin(), layers.end());
        const int L = (int) layers.size();
        set.numLayers = L;

        int done = 0;
        for (int li = 0; li < L; ++li)
        {
            std::vector<Item*> layerItems;
            for (auto& it : items) if (it.layer == layers[(size_t) li]) layerItems.push_back (&it);
            std::sort (layerItems.begin(), layerItems.end(), [] (Item* a, Item* b) { return a->note < b->note; });
            const int lovel = 1 + (127 * li) / L, hivel = (127 * (li + 1)) / L;
            for (size_t k = 0; k < layerItems.size(); ++k)
            {
                auto* it = layerItems[k];
                if (k > 0 && layerItems[k - 1]->note == it->note) continue; // duplicate (round robin) - keep first
                auto z = readZone (fm, it->f, set.bytes);
                progress ((float) ++done / (float) items.size());
                if (! z) { ++skipped; continue; }
                z->root = it->note;
                z->lokey = k == 0 ? 0 : (layerItems[k - 1]->note + it->note) / 2 + 1;
                z->hikey = k + 1 == layerItems.size() ? 127 : (it->note + layerItems[k + 1]->note) / 2;
                z->lovel = lovel; z->hivel = hivel;
                set.zones.push_back (std::move (z));
            }
        }
        message = juce::String (set.zones.size()) + " samples, " + juce::String (L) + (L == 1 ? " layer" : " layers")
                + (skipped > 0 ? " (" + juce::String (skipped) + " skipped)" : juce::String());
        // report how the lowest and highest files were read, so a naming problem is easy to spot
        auto lo = std::min_element (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.note < b.note; });
        auto hi = std::max_element (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.note < b.note; });
        set.mappingInfo = lo->f.getFileName() + " = " + juce::MidiMessage::getMidiNoteName (lo->note, true, true, 4) + ",  "
                        + hi->f.getFileName() + " = " + juce::MidiMessage::getMidiNoteName (hi->note, true, true, 4);
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
            vels.insert (z->lovel);
            set.zones.push_back (std::move (z));
        }
        set.numLayers = juce::jmax (1, (int) vels.size());
        message = juce::String (set.zones.size()) + " samples, " + juce::String (set.numLayers) + (set.numLayers == 1 ? " layer" : " layers")
                + (skipped > 0 ? " (" + juce::String (skipped) + " skipped)" : juce::String());
        juce::ignoreUnused (cache);
    }
};

// ============================================================================
struct SamplerEngine
{
    float dynamics = 0.6f, release = 0.3f; int timbre = 1;

    void prepare (double sampleRate) { sr = sampleRate; allOff(); }
    void setSet (const SampleSet* s) { set = s; allOff(); }

    void noteOn (int note, float vel)
    {
        if (! set) return;
        const auto* z = set->find (note, juce::jlimit (1, 127, (int) std::round (vel * 127.0f)));
        if (! z) return;
        for (auto& v : voices) if (v.active && v.note == note && ! v.released) { v.released = true; v.fastRelease = true; } // re-strike
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age > v->age) v = &x; }
        *v = Voice {};
        v->active = true; v->note = note; v->zone = z;
        v->inc = std::pow (2.0, (note - z->root + z->tuneCents / 100.0) / 12.0) * z->sourceRate / sr;
        v->gain = ((1.0f - dynamics) + dynamics * vel) * z->gain;
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
                const float a = v.att * v.env * v.gain * 0.4f;
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
