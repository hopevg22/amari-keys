#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
using APVTS = juce::AudioProcessorValueTreeState;

// ---- layer layout: 10 instrument groups
constexpr int numLayers = 10;
enum class Kind { Keys, Organ, Synth };
inline Kind kindOf (int layer) { return layer == 2 ? Kind::Organ : (layer <= 3 ? Kind::Keys : Kind::Synth); } // 0,1,3 keys; 2 organ; 4-9 synth engine + samples
inline const char* layerShort (int l) { static const char* n[] = { "PIANO", "E.PIANO", "ORGAN", "KEYS", "STRINGS", "BRASS", "PADS", "SYNTH", "BASS", "SFX" }; return n[l]; }
inline const char* layerLong (int l)  { static const char* n[] = { "PIANO", "ELECTRIC PIANO", "ORGAN", "KEYS & MALLETS", "STRINGS", "BRASS & WINDS", "PADS & CHOIR", "SYNTH", "BASS", "SOUND FX" }; return n[l]; }
constexpr int sfxLayer = 9;   // plays loaded samples only (no built-in sound)
// Tail when the sustain pedal is lifted: strings, brass, pads and SFX keep a short adjustable tail
// (PAD TAIL), sampled keys sound like dampers, synth lead and bass stop quickly.
inline float pedalTailFor (int layer, float padTail)
{
    if (layer == 4 || layer == 5 || layer == 6 || layer == sfxLayer) return padTail;
    return kindOf (layer) == Kind::Keys ? 0.5f : 0.3f;
}
inline juce::String lid (int layer, const char* name) { return "l" + juce::String (layer + 1) + "_" + name; }

inline juce::StringArray zones()        { return { "LO", "UP", "ALL" }; }
inline juce::StringArray rotarySpeeds() { return { "SLOW", "FAST", "STOP" }; }
inline juce::StringArray vibModes()     { return { "OFF", "V1", "V2", "V3", "C1", "C2", "C3" }; }
inline juce::StringArray organModels()  { return { "B3", "VOX", "FARF", "PIPE" }; }
inline juce::StringArray keysModels()   { return { "TINE EP", "REED EP", "CLAV", "FM BELL", "SAMPLES" }; }
inline juce::StringArray timbres()      { return { "SOFT", "MID", "BRIGHT" }; }
inline juce::StringArray voiceModes()   { return { "POLY", "MONO", "LEGATO" }; }
inline juce::StringArray waves()        { return { "SAW", "SQR", "TRI", "SINE" }; }
inline juce::StringArray filterTypes()  { return { "LP24", "LP12", "HP", "BP" }; }
inline juce::StringArray sources()      { return { "SYNTH", "SAMPLES" }; }
inline juce::StringArray velCurves()    { return { "SOFT", "NORM", "HARD" }; }
inline juce::StringArray fxTargets()    { return { "ALL", "KEYS", "SYN", "NO ORG" }; }
inline juce::StringArray modTypes()     { return { "CHORUS", "PHASER" }; }
inline juce::StringArray padModes()     { return { "DRUM", "PROG", "MIX" }; }
inline juce::StringArray octaves()      { return { "-2", "-1", "0", "+1", "+2" }; }

inline juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }

struct SynthPreset { int voice, w1, w2, ftype; float mix, detune, glide, cutoff, reso, fenv, fkey, lfo, lforate, fa, fd, fs, fr, aa, ad, as, ar; };
inline SynthPreset synthPreset (int layer)
{
    switch (layer)
    {
        case 4:  return { 0, 0, 0, 0, 0.5f, 0.55f, 0.0f, 3200.0f, 0.10f, 0.15f, 0.3f, 0.06f, 0.3f, 0.3f, 1.0f, 0.8f, 1.2f, 0.35f, 1.0f, 0.9f, 1.1f };   // strings
        case 5:  return { 0, 0, 0, 0, 0.5f, 0.25f, 0.0f, 700.0f, 0.15f, 0.55f, 0.5f, 0.0f, 5.0f, 0.06f, 0.5f, 0.5f, 0.3f, 0.03f, 0.6f, 0.85f, 0.25f };   // brass
        case 6:  return { 0, 0, 2, 0, 0.4f, 0.40f, 0.0f, 1500.0f, 0.20f, 0.25f, 0.3f, 0.15f, 0.25f, 1.2f, 2.0f, 0.6f, 2.5f, 1.0f, 1.5f, 0.9f, 2.6f };   // pads
        case 8:  return { 1, 0, 1, 0, 0.4f, 0.06f, 0.0f, 650.0f, 0.25f, 0.45f, 0.3f, 0.0f, 5.0f, 0.003f, 0.35f, 0.2f, 0.2f, 0.003f, 0.6f, 0.8f, 0.15f }; // bass
        case 9:  return { 0, 2, 3, 0, 0.5f, 0.20f, 0.0f, 4000.0f, 0.10f, 0.20f, 0.3f, 0.10f, 0.4f, 0.5f, 1.0f, 0.7f, 1.0f, 0.2f, 1.0f, 0.9f, 1.0f };    // sfx (samples only)
        default: return { 2, 1, 0, 0, 0.5f, 0.15f, 0.1f, 2600.0f, 0.35f, 0.40f, 0.5f, 0.0f, 5.0f, 0.005f, 0.4f, 0.4f, 0.3f, 0.003f, 0.3f, 0.9f, 0.25f }; // lead
    }
}

inline APVTS::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto fl = [&] (const String& id, const String& name, float lo, float hi, float def, float skewCentre = -1.0f)
    {
        NormalisableRange<float> r (lo, hi);
        if (skewCentre > 0) r.setSkewForCentre (skewCentre);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def));
    };
    auto bo = [&] (const String& id, const String& name, bool def) { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto ch = [&] (const String& id, const String& name, const StringArray& opts, int def) { p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, opts, def)); };
    auto in = [&] (const String& id, const String& name, int lo, int hi, int def) { p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };

    // ---- global
    fl ("master", "Master", 0.0f, 1.0f, 0.75f);
    bo ("split_on", "Split On", true);
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "split_note", 1 }, "Split Note", 21, 108, 48,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return noteName (v); })));
    in ("transpose", "Transpose", -12, 12, 0);
    ch ("vel_curve", "Velocity Curve", velCurves(), 1);
    in ("bend_range", "Pitch Bend Range", 1, 12, 2);
    fl ("pedal_tail", "Pad Tail (Pedal Up)", 0.1f, 3.0f, 0.8f, 0.8f);

    // ---- layers
    for (int l = 0; l < numLayers; ++l)
    {
        const String nm = layerShort (l);
        const bool defOn = (l == 0 || l == 2);
        bo (lid (l, "on"), nm + " On", defOn);
        fl (lid (l, "level"), nm + " Level", 0.0f, 1.0f, l == 0 ? 0.8f : 0.7f);
        ch (lid (l, "zone"), nm + " Zone", zones(), l == 2 ? 0 : 1);
        in (lid (l, "oct"), nm + " Octave", -2, 2, 0);
        const auto kind = kindOf (l);
        if (kind == Kind::Keys)
        {
            ch (lid (l, "model"), nm + " Model", keysModels(), l == 0 ? 4 : l == 1 ? 0 : 2);
            ch (lid (l, "timbre"), nm + " Timbre", timbres(), 1);
            fl (lid (l, "dyn"), nm + " Dynamics", 0.0f, 1.0f, 0.6f);
            fl (lid (l, "decay"), nm + " Decay", 0.0f, 1.0f, 0.5f);
            fl (lid (l, "trem"), nm + " Tremolo", 0.0f, 1.0f, 0.0f);
        }
        else if (kind == Kind::Synth)
        {
            const auto s = synthPreset (l);
            ch (lid (l, "src"), nm + " Source", sources(), l == sfxLayer ? 1 : 0);
            ch (lid (l, "voice"), nm + " Voice Mode", voiceModes(), s.voice);
            ch (lid (l, "wave1"), nm + " Osc 1", waves(), s.w1);
            ch (lid (l, "wave2"), nm + " Osc 2", waves(), s.w2);
            fl (lid (l, "oscmix"), nm + " Osc Mix", 0.0f, 1.0f, s.mix);
            fl (lid (l, "detune"), nm + " Detune", 0.0f, 1.0f, s.detune);
            fl (lid (l, "glide"), nm + " Glide", 0.0f, 1.0f, s.glide);
            ch (lid (l, "ftype"), nm + " Filter Type", filterTypes(), s.ftype);
            fl (lid (l, "cutoff"), nm + " Cutoff", 30.0f, 18000.0f, s.cutoff, 1200.0f);
            fl (lid (l, "reso"), nm + " Resonance", 0.0f, 1.0f, s.reso);
            fl (lid (l, "fenv"), nm + " Filter Env", -1.0f, 1.0f, s.fenv);
            fl (lid (l, "fkey"), nm + " Key Track", 0.0f, 1.0f, s.fkey);
            fl (lid (l, "lfo"), nm + " LFO Amount", 0.0f, 1.0f, s.lfo);
            fl (lid (l, "lforate"), nm + " LFO Rate", 0.05f, 20.0f, s.lforate, 2.0f);
            fl (lid (l, "fa"), nm + " Filter Attack", 0.001f, 5.0f, s.fa, 0.5f);
            fl (lid (l, "fd"), nm + " Filter Decay", 0.001f, 5.0f, s.fd, 0.5f);
            fl (lid (l, "fs"), nm + " Filter Sustain", 0.0f, 1.0f, s.fs);
            fl (lid (l, "fr"), nm + " Filter Release", 0.001f, 8.0f, s.fr, 0.6f);
            fl (lid (l, "aa"), nm + " Amp Attack", 0.001f, 5.0f, s.aa, 0.5f);
            fl (lid (l, "ad"), nm + " Amp Decay", 0.001f, 5.0f, s.ad, 0.5f);
            fl (lid (l, "as"), nm + " Amp Sustain", 0.0f, 1.0f, s.as);
            fl (lid (l, "ar"), nm + " Amp Release", 0.001f, 8.0f, s.ar, 0.6f);
        }
    }

    // ---- organ (layer 3)
    ch ("org_model", "Organ Model", organModels(), 0);
    const int dbDefaults[9] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    const char* dbNames[9] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
    for (int i = 0; i < 9; ++i) in ("db" + String (i + 1), String ("Drawbar ") + dbNames[i], 0, 8, dbDefaults[i]);
    bo ("perc_on", "Percussion", true);
    bo ("perc_3rd", "Perc 3rd", true);
    bo ("perc_fast", "Perc Fast", true);
    bo ("perc_soft", "Perc Soft", false);
    ch ("vib_mode", "Vibrato/Chorus", vibModes(), 6);
    fl ("org_click", "Key Click", 0.0f, 1.0f, 0.4f);
    fl ("org_drive", "Organ Drive", 0.0f, 1.0f, 0.25f);
    bo ("rot_on", "Rotary On", true);
    ch ("rot_speed", "Rotary Speed", rotarySpeeds(), 0);

    // ---- drum pads
    fl ("pads_level", "Drum Pads Level", 0.0f, 1.0f, 0.7f);
    ch ("pad_mode", "Pad Mode", padModes(), 2);

    // ---- effects
    ch ("fx_target", "FX Target", fxTargets(), 0);
    bo ("mod_on", "Mod On", false);
    ch ("mod_type", "Mod Type", modTypes(), 0);
    fl ("mod_rate", "Mod Rate", 0.05f, 5.0f, 0.6f, 1.0f);
    fl ("mod_depth", "Mod Depth", 0.0f, 1.0f, 0.4f);
    fl ("mod_mix", "Mod Mix", 0.0f, 1.0f, 0.3f);
    bo ("dly_on", "Delay On", false);
    fl ("dly_time", "Delay Time", 20.0f, 1200.0f, 375.0f, 300.0f);
    fl ("dly_fb", "Delay Feedback", 0.0f, 0.9f, 0.3f);
    fl ("dly_mix", "Delay Mix", 0.0f, 1.0f, 0.2f);
    bo ("rev_on", "Reverb On", true);
    fl ("rev_size", "Reverb Size", 0.0f, 1.0f, 0.6f);
    fl ("rev_damp", "Reverb Damping", 0.0f, 1.0f, 0.45f);
    fl ("rev_mix", "Reverb Mix", 0.0f, 1.0f, 0.22f);
    bo ("amp_on", "Amp On", false);
    fl ("amp_drive", "Amp Drive", 0.0f, 1.0f, 0.3f);
    fl ("amp_tone", "Amp Tone", 0.0f, 1.0f, 0.5f);
    fl ("amp_level", "Amp Level", 0.0f, 1.0f, 0.7f);
    bo ("comp_on", "Comp On", false);
    fl ("comp_amt", "Comp Amount", 0.0f, 1.0f, 0.4f);
    fl ("comp_atk", "Comp Attack", 0.1f, 80.0f, 10.0f, 10.0f);
    fl ("comp_gain", "Comp Makeup", 0.0f, 1.0f, 0.3f);
    bo ("eq_on", "EQ On", true);
    fl ("eq_low", "EQ Low", -12.0f, 12.0f, 0.0f);
    fl ("eq_mid", "EQ Mid", -12.0f, 12.0f, 0.0f);
    fl ("eq_high", "EQ High", -12.0f, 12.0f, 0.0f);

    return { p.begin(), p.end() };
}
} // namespace Params
