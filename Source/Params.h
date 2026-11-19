#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
using APVTS = juce::AudioProcessorValueTreeState;

inline juce::StringArray rotarySpeeds() { return { "SLOW", "FAST", "STOP" }; }
inline juce::StringArray vibModes()     { return { "OFF", "V1", "V2", "V3", "C1", "C2", "C3" }; }
inline juce::StringArray organModels()  { return { "B3", "VOX", "FARF", "PIPE" }; }
inline juce::StringArray keysModels()   { return { "TINE EP", "REED EP", "CLAV", "FM BELL", "MY SAMPLES" }; }
inline juce::StringArray timbres()      { return { "SOFT", "MID", "BRIGHT" }; }
inline juce::StringArray voiceModes()   { return { "POLY", "MONO", "LEGATO" }; }
inline juce::StringArray waves()        { return { "SAW", "SQR", "TRI", "SINE" }; }
inline juce::StringArray filterTypes()  { return { "LP24", "LP12", "HP", "BP" }; }
inline juce::StringArray velCurves()    { return { "SOFT", "NORM", "HARD" }; }
inline juce::StringArray fxTargets()    { return { "ALL", "ORG", "KEYS", "SYN" }; }
inline juce::StringArray modTypes()     { return { "CHORUS", "PHASER" }; }

inline juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }

inline APVTS::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto fl = [&] (const char* id, const char* name, float lo, float hi, float def, float skewCentre = -1.0f)
    {
        NormalisableRange<float> r (lo, hi);
        if (skewCentre > 0) r.setSkewForCentre (skewCentre);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def));
    };
    auto bo = [&] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto ch = [&] (const char* id, const char* name, const StringArray& opts, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, opts, def)); };
    auto in = [&] (const char* id, const char* name, int lo, int hi, int def)
    { p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };

    // ---- global
    fl ("master", "Master", 0.0f, 1.0f, 0.75f);
    bo ("split_on", "Split On", true);
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "split_note", 1 }, "Split Note", 21, 108, 48,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return noteName (v); })));
    in ("transpose", "Transpose", -12, 12, 0);
    ch ("vel_curve", "Velocity Curve", velCurves(), 1);

    // ---- mix
    fl ("mix_org", "Organ Level", 0.0f, 1.0f, 0.75f);
    fl ("mix_keys", "Keys Level", 0.0f, 1.0f, 0.8f);
    fl ("mix_syn", "Synth Level", 0.0f, 1.0f, 0.6f);
    fl ("mix_pads", "Pads Level", 0.0f, 1.0f, 0.7f);
    fl ("blend", "Keys/Synth Blend", 0.0f, 1.0f, 0.5f);

    // ---- organ
    bo ("org_on", "Organ On", true);
    ch ("org_model", "Organ Model", organModels(), 0);
    const int dbDefaults[9] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    const char* dbIds[9] = { "db1", "db2", "db3", "db4", "db5", "db6", "db7", "db8", "db9" };
    const char* dbNames[9] = { "Drawbar 16'", "Drawbar 5 1/3'", "Drawbar 8'", "Drawbar 4'", "Drawbar 2 2/3'",
                               "Drawbar 2'", "Drawbar 1 3/5'", "Drawbar 1 1/3'", "Drawbar 1'" };
    for (int i = 0; i < 9; ++i) in (dbIds[i], dbNames[i], 0, 8, dbDefaults[i]);
    bo ("perc_on", "Percussion", true);
    bo ("perc_3rd", "Perc 3rd", true);
    bo ("perc_fast", "Perc Fast", true);
    bo ("perc_soft", "Perc Soft", false);
    ch ("vib_mode", "Vibrato/Chorus", vibModes(), 6);
    fl ("org_click", "Key Click", 0.0f, 1.0f, 0.4f);
    fl ("org_drive", "Organ Drive", 0.0f, 1.0f, 0.25f);
    fl ("org_vol", "Organ Volume", 0.0f, 1.0f, 0.75f);
    bo ("rot_on", "Rotary On", true);
    ch ("rot_speed", "Rotary Speed", rotarySpeeds(), 0);
    in ("org_oct", "Organ Octave", -2, 2, 0);

    // ---- keys (sample-free electric keys)
    bo ("keys_on", "Keys On", true);
    ch ("keys_model", "Keys Model", keysModels(), 0);
    ch ("keys_timbre", "Keys Timbre", timbres(), 1);
    fl ("keys_dyn", "Keys Dynamics", 0.0f, 1.0f, 0.6f);
    fl ("keys_decay", "Keys Decay", 0.0f, 1.0f, 0.5f);
    fl ("keys_trem", "Keys Tremolo", 0.0f, 1.0f, 0.0f);
    fl ("keys_vol", "Keys Volume", 0.0f, 1.0f, 0.8f);
    in ("keys_oct", "Keys Octave", -2, 2, 0);

    // ---- synth
    bo ("syn_on", "Synth On", true);
    ch ("syn_voice", "Synth Voice Mode", voiceModes(), 0);
    ch ("syn_wave1", "Osc 1 Wave", waves(), 0);
    ch ("syn_wave2", "Osc 2 Wave", waves(), 0);
    fl ("syn_oscmix", "Osc Mix", 0.0f, 1.0f, 0.5f);
    fl ("syn_detune", "Detune", 0.0f, 1.0f, 0.3f);
    fl ("syn_sub", "Sub Osc", 0.0f, 1.0f, 0.0f);
    fl ("syn_glide", "Glide", 0.0f, 1.0f, 0.0f);
    ch ("flt_type", "Filter Type", filterTypes(), 0);
    fl ("flt_cutoff", "Cutoff", 30.0f, 18000.0f, 2500.0f, 1200.0f);
    fl ("flt_reso", "Resonance", 0.0f, 1.0f, 0.25f);
    fl ("flt_env", "Filter Env Amount", -1.0f, 1.0f, 0.35f);
    fl ("flt_key", "Key Track", 0.0f, 1.0f, 0.4f);
    fl ("flt_drive", "Filter Drive", 0.0f, 1.0f, 0.1f);
    fl ("fenv_a", "Filter Attack", 0.001f, 5.0f, 0.4f, 0.5f);
    fl ("fenv_d", "Filter Decay", 0.001f, 5.0f, 1.2f, 0.5f);
    fl ("fenv_s", "Filter Sustain", 0.0f, 1.0f, 0.4f);
    fl ("fenv_r", "Filter Release", 0.001f, 8.0f, 1.5f, 0.6f);
    fl ("aenv_a", "Amp Attack", 0.001f, 5.0f, 0.6f, 0.5f);
    fl ("aenv_d", "Amp Decay", 0.001f, 5.0f, 1.0f, 0.5f);
    fl ("aenv_s", "Amp Sustain", 0.0f, 1.0f, 0.85f);
    fl ("aenv_r", "Amp Release", 0.001f, 8.0f, 1.8f, 0.6f);
    fl ("lfo_rate", "LFO Rate", 0.05f, 20.0f, 0.4f, 2.0f);
    fl ("lfo_amt", "LFO Amount", 0.0f, 1.0f, 0.15f);
    fl ("syn_vol", "Synth Volume", 0.0f, 1.0f, 0.7f);
    in ("syn_oct", "Synth Octave", -2, 2, 0);

    // ---- pads (synthesised drum kit)
    bo ("pads_on", "Pads On", true);

    // ---- effects
    ch ("fx_target", "FX Target", fxTargets(), 0);
    bo ("mod_on", "Mod On", true);
    ch ("mod_type", "Mod Type", modTypes(), 0);
    fl ("mod_rate", "Mod Rate", 0.05f, 5.0f, 0.6f, 1.0f);
    fl ("mod_depth", "Mod Depth", 0.0f, 1.0f, 0.4f);
    fl ("mod_mix", "Mod Mix", 0.0f, 1.0f, 0.35f);
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
