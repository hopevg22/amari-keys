#include "PluginProcessor.h"
#include "PluginEditor.h"

AmariKeysProcessor::AmariKeysProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AmariKeys", Params::createLayout())
{
}

bool AmariKeysProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AmariKeysProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (512, samplesPerBlock);
    organ.prepare (sr); rotary.prepare (sr);
    keys.prepare (sr); synth.prepare (sr); drums.prepare (sr);

    for (auto* b : { &orgMono }) b->setSize (1, maxBlock);
    for (auto* b : { &orgSt, &keysBuf, &synBuf, &padBuf, &fxBuf }) b->setSize (2, maxBlock);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) 512, 2 };
    chorus.prepare (spec); phaser.prepare (spec); comp.prepare (spec);
    chorus.reset(); phaser.reset(); comp.reset();
    reverb.setSampleRate (sr); reverb.reset();
    delayL.init ((int) (sr * 1.3) + 8); delayR.init ((int) (sr * 1.3) + 8);
    juce::dsp::ProcessSpec mono { sr, (juce::uint32) maxBlock, 1 };
    for (int c = 0; c < 2; ++c) { eqLow[c].prepare (mono); eqMid[c].prepare (mono); eqHigh[c].prepare (mono); }
    lastEq[0] = lastEq[1] = lastEq[2] = -99.0f;

    for (auto* s : { &masterSm, &orgSm, &keysSm, &synSm, &padSm }) s->reset (sr, 0.03);
    for (auto& ch : routed) for (auto& r : ch) r = Routed {};
}

void AmariKeysProcessor::updateSettings()
{
    auto& o = organ.s;
    const char* dbIds[9] = { "db1", "db2", "db3", "db4", "db5", "db6", "db7", "db8", "db9" };
    for (int i = 0; i < 9; ++i)
    {
        const int lvl = (int) std::round (param (dbIds[i]));
        o.drawbar[i] = lvl <= 0 ? 0.0f : std::pow (10.0f, -3.0f * (8 - lvl) / 20.0f);
    }
    o.model = (int) param ("org_model");
    o.percOn = param ("perc_on") > 0.5f; o.perc3rd = param ("perc_3rd") > 0.5f;
    o.percFast = param ("perc_fast") > 0.5f; o.percSoft = param ("perc_soft") > 0.5f;
    o.click = param ("org_click"); o.vibMode = (int) param ("vib_mode"); o.drive = param ("org_drive");
    rotary.setSpeed ((int) param ("rot_speed"));

    auto& k = keys.s;
    k.model = (int) param ("keys_model");
    const int tb = (int) param ("keys_timbre");
    k.timbre = tb == 0 ? 0.6f : tb == 1 ? 1.0f : 1.55f;
    k.dynamics = param ("keys_dyn"); k.decay = param ("keys_decay"); k.tremolo = param ("keys_trem");

    auto& y = synth.s;
    y.voiceMode = (int) param ("syn_voice");
    y.wave1 = (int) param ("syn_wave1"); y.wave2 = (int) param ("syn_wave2");
    y.oscMix = param ("syn_oscmix"); y.detune = param ("syn_detune"); y.sub = param ("syn_sub"); y.glide = param ("syn_glide");
    y.filterType = (int) param ("flt_type");
    y.cutoff = param ("flt_cutoff"); y.reso = param ("flt_reso"); y.envAmt = param ("flt_env");
    y.keyTrack = param ("flt_key"); y.drive = param ("flt_drive");
    y.fenv = { param ("fenv_a"), param ("fenv_d"), param ("fenv_s"), param ("fenv_r") };
    y.aenv = { param ("aenv_a"), param ("aenv_d"), param ("aenv_s"), param ("aenv_r") };
    y.lfoRate = param ("lfo_rate"); y.lfoAmt = param ("lfo_amt");
}

void AmariKeysProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int ch = juce::jlimit (0, 15, m.getChannel() - 1);
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        float vel = m.getFloatVelocity();
        const int curve = (int) param ("vel_curve");
        if (curve == 0) vel = std::pow (vel, 0.6f); else if (curve == 2) vel = std::pow (vel, 1.6f);

        if (ch == 9) // pads on MIDI channel 10, notes 36-51
        {
            const int pad = note - 36;
            if (pad >= 0 && pad < DrumEngine::numPads && param ("pads_on") > 0.5f) { drums.trigger (pad, vel); padHits[pad]++; }
            return;
        }
        const int n = juce::jlimit (0, 127, note + (int) param ("transpose"));
        const bool split = param ("split_on") > 0.5f;
        const int splitNote = (int) param ("split_note");
        const bool lower = ! split || n < splitNote;
        const bool upper = ! split || n >= splitNote;
        Routed r;
        if (lower && param ("org_on") > 0.5f)
        {
            const int on = juce::jlimit (0, 127, n + 12 * (int) param ("org_oct"));
            organ.noteOn (on, vel); r.org = (int8_t) on;
        }
        if (upper && param ("keys_on") > 0.5f)
        {
            const int kn = juce::jlimit (0, 127, n + 12 * (int) param ("keys_oct"));
            keys.noteOn (kn, vel); keysDown[kn] = true; heldBySustainKeys[kn] = false; r.keys = (int8_t) kn;
        }
        if (upper && param ("syn_on") > 0.5f)
        {
            const int sn = juce::jlimit (0, 127, n + 12 * (int) param ("syn_oct"));
            synth.noteOn (sn, vel); synDown[sn] = true; heldBySustainSyn[sn] = false; r.syn = (int8_t) sn;
        }
        routed[ch][note] = r;
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        if (ch == 9) return;
        auto& r = routed[ch][note];
        if (r.org >= 0) organ.noteOff (r.org);
        if (r.keys >= 0) { keysDown[r.keys] = false; if (sustain) heldBySustainKeys[r.keys] = true; else keys.noteOff (r.keys); }
        if (r.syn >= 0) { synDown[r.syn] = false; if (sustain) heldBySustainSyn[r.syn] = true; else synth.noteOff (r.syn); }
        r = Routed {};
    }
    else if (m.isSustainPedalOn()) sustain = true;
    else if (m.isSustainPedalOff())
    {
        sustain = false;
        for (int i = 0; i < 128; ++i)
        {
            if (heldBySustainKeys[i] && ! keysDown[i]) keys.noteOff (i);
            if (heldBySustainSyn[i] && ! synDown[i]) synth.noteOff (i);
            heldBySustainKeys[i] = heldBySustainSyn[i] = false;
        }
    }
    else if (m.isPitchWheel())
        synth.s.pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        panic();
}

void AmariKeysProcessor::renderSegment (int start, int num)
{
    organ.render (orgMono.getWritePointer (0) + start, num);
    keys.render (keysBuf.getWritePointer (0) + start, keysBuf.getWritePointer (1) + start, num);
    synth.render (synBuf.getWritePointer (0) + start, synBuf.getWritePointer (1) + start, num);
    drums.render (padBuf.getWritePointer (0) + start, padBuf.getWritePointer (1) + start, num);
}

void AmariKeysProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n > orgMono.getNumSamples())
    {
        orgMono.setSize (1, n, false, false, true);
        for (auto* b : { &orgSt, &keysBuf, &synBuf, &padBuf, &fxBuf }) b->setSize (2, n, false, false, true);
    }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    if (panicRequest.exchange (false))
    {
        organ.allOff(); keys.allOff(); synth.allOff(); drums.allOff();
        for (auto& c : routed) for (auto& r : c) r = Routed {};
        for (int i = 0; i < 128; ++i) keysDown[i] = synDown[i] = heldBySustainKeys[i] = heldBySustainSyn[i] = false;
        sustain = false;
        reverb.reset(); delayL.init ((int) (sr * 1.3) + 8); delayR.init ((int) (sr * 1.3) + 8);
    }
    for (int p = 0; p < DrumEngine::numPads; ++p)
    {
        const float v = padRequests[p].exchange (0.0f);
        if (v > 0.0f && param ("pads_on") > 0.5f) { drums.trigger (p, v); padHits[p]++; }
    }

    updateSettings();

    orgMono.clear(); keysBuf.clear(); synBuf.clear(); padBuf.clear(); fxBuf.clear(); orgSt.clear();

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos) { renderSegment (pos, t - pos); pos = t; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) renderSegment (pos, n - pos);
    midi.clear();

    // organ -> rotary
    if (param ("rot_on") > 0.5f)
        rotary.process (orgMono.getReadPointer (0), orgSt.getWritePointer (0), orgSt.getWritePointer (1), n);
    else { orgSt.copyFrom (0, 0, orgMono, 0, 0, n); orgSt.copyFrom (1, 0, orgMono, 0, 0, n); }

    // section gains (mixer faders + piano/synth blend)
    const float b = param ("blend");
    const float keysBlend = juce::jmin (1.0f, 2.0f * (1.0f - b));
    const float synBlend = juce::jmin (1.0f, 2.0f * b);
    orgSm.setTargetValue (param ("org_vol") * param ("mix_org") * 0.9f);
    keysSm.setTargetValue (param ("keys_vol") * param ("mix_keys") * keysBlend);
    synSm.setTargetValue (param ("syn_vol") * param ("mix_syn") * synBlend);
    padSm.setTargetValue (param ("pads_on") > 0.5f ? param ("mix_pads") : 0.0f);
    masterSm.setTargetValue (param ("master"));

    auto applySm = [n] (juce::AudioBuffer<float>& buf, juce::SmoothedValue<float>& sm)
    {
        auto* l = buf.getWritePointer (0); auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float g = sm.getNextValue(); l[i] *= g; r[i] *= g; }
    };
    applySm (orgSt, orgSm); applySm (keysBuf, keysSm); applySm (synBuf, synSm); applySm (padBuf, padSm);

    // route to FX bus or dry
    const int target = (int) param ("fx_target");
    buffer.clear();
    auto route = [&] (juce::AudioBuffer<float>& src, bool toFx)
    {
        auto& dst = toFx ? fxBuf : buffer;
        for (int c = 0; c < 2; ++c) dst.addFrom (c, 0, src, c, 0, n);
    };
    route (orgSt, target == 0 || target == 1);
    route (keysBuf, target == 0 || target == 2);
    route (synBuf, target == 0 || target == 3);
    route (padBuf, false);

    // ---------------- effects on fxBuf ----------------
    auto* L = fxBuf.getWritePointer (0);
    auto* R = fxBuf.getWritePointer (1);

    if (param ("amp_on") > 0.5f)
    {
        const float g = 1.0f + param ("amp_drive") * 24.0f;
        const float norm = 1.0f / std::sqrt (g);
        const float cutoff = 900.0f * std::pow (14.0f, param ("amp_tone"));
        const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr);
        const float lvl = param ("amp_level") * 1.6f;
        for (int i = 0; i < n; ++i)
        {
            ampToneL += a * (std::tanh (L[i] * g) * norm - ampToneL);
            ampToneR += a * (std::tanh (R[i] * g) * norm - ampToneR);
            L[i] = ampToneL * lvl; R[i] = ampToneR * lvl;
        }
    }

    if (param ("eq_on") > 0.5f)
    {
        const float lo = param ("eq_low"), mi = param ("eq_mid"), hi = param ("eq_high");
        if (std::abs (lo - lastEq[0]) > 1.0e-4f || std::abs (mi - lastEq[1]) > 1.0e-4f || std::abs (hi - lastEq[2]) > 1.0e-4f)
        {
            using C = juce::dsp::IIR::Coefficients<float>;
            auto cl = C::makeLowShelf (sr, 140.0, 0.7f, juce::Decibels::decibelsToGain (lo));
            auto cm = C::makePeakFilter (sr, 1000.0, 0.7f, juce::Decibels::decibelsToGain (mi));
            auto ch = C::makeHighShelf (sr, 6000.0, 0.7f, juce::Decibels::decibelsToGain (hi));
            for (int c = 0; c < 2; ++c) { *eqLow[c].coefficients = *cl; *eqMid[c].coefficients = *cm; *eqHigh[c].coefficients = *ch; }
            lastEq[0] = lo; lastEq[1] = mi; lastEq[2] = hi;
        }
        for (int c = 0; c < 2; ++c)
        {
            auto* d = fxBuf.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] = eqHigh[c].processSample (eqMid[c].processSample (eqLow[c].processSample (d[i])));
        }
    }

    const bool compOn = param ("comp_on") > 0.5f;
    const bool modOn = param ("mod_on") > 0.5f;
    if (compOn)
    {
        const float amt = param ("comp_amt");
        comp.setThreshold (-amt * 30.0f); comp.setRatio (2.0f + amt * 6.0f);
        comp.setAttack (param ("comp_atk")); comp.setRelease (120.0f);
    }
    const int modType = (int) param ("mod_type");
    if (modOn)
    {
        const float rate = param ("mod_rate"), depth = param ("mod_depth"), mix = param ("mod_mix");
        chorus.setRate (rate); chorus.setDepth (depth); chorus.setCentreDelay (7.0f); chorus.setFeedback (0.1f); chorus.setMix (mix);
        phaser.setRate (rate); phaser.setDepth (depth); phaser.setCentreFrequency (900.0f); phaser.setFeedback (0.45f); phaser.setMix (mix);
    }
    for (int start = 0; start < n; start += 512)
    {
        const int len = juce::jmin (512, n - start);
        juce::dsp::AudioBlock<float> block (fxBuf);
        auto sub = block.getSubBlock ((size_t) start, (size_t) len);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        if (compOn) comp.process (ctx);
        if (modOn) { if (modType == 0) chorus.process (ctx); else phaser.process (ctx); }
    }
    if (compOn) fxBuf.applyGain (0, n, 1.0f + param ("comp_gain") * 3.0f);

    if (param ("dly_on") > 0.5f)
    {
        const float d = juce::jlimit (2.0f, (float) (sr * 1.25), param ("dly_time") * 0.001f * (float) sr);
        const float fb = param ("dly_fb"), mix = param ("dly_mix");
        const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 4500.0f / (float) sr);
        for (int i = 0; i < n; ++i)
        {
            const float wl = delayL.read (d), wr = delayR.read (d);
            delayToneL += a * (wl - delayToneL); delayToneR += a * (wr - delayToneR);
            // ping-pong: feedback crosses channels
            delayL.push (L[i] + delayToneR * fb);
            delayR.push (R[i] * 0.6f + delayToneL * fb);
            L[i] += wl * mix; R[i] += wr * mix;
        }
    }

    if (param ("rev_on") > 0.5f)
    {
        juce::Reverb::Parameters rp;
        const float mix = param ("rev_mix");
        rp.roomSize = 0.3f + 0.68f * param ("rev_size");
        rp.damping = param ("rev_damp");
        rp.wetLevel = mix * 0.9f; rp.dryLevel = 1.0f - mix * 0.45f; rp.width = 1.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (L, R, n);
    }

    for (int c = 0; c < 2; ++c) buffer.addFrom (c, 0, fxBuf, c, 0, n);

    // master + safety soft clip
    auto* oL = buffer.getWritePointer (0); auto* oR = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = masterSm.getNextValue();
        float l = oL[i] * g, r = oR[i] * g;
        auto soft = [] (float x) { const float a = std::abs (x); return a <= 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f), x); };
        l = soft (l); r = soft (r);
        oL[i] = l; oR[i] = r;
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

juce::AudioProcessorEditor* AmariKeysProcessor::createEditor() { return new AmariKeysEditor (*this); }

void AmariKeysProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void AmariKeysProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmariKeysProcessor(); }
