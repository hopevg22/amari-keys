#include "PluginProcessor.h"
#include "PluginEditor.h"

using Params::lid;
using Params::Kind;

AmariKeysProcessor::AmariKeysProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AmariKeys", Params::createLayout())
{
    for (auto& c : ccMap) c.store (-1);
    paramList = getParameters();
    for (int i = 0; i < numPrograms; ++i) programs[(size_t) i].name = "INIT";
    startTimerHz (30);
}

AmariKeysProcessor::~AmariKeysProcessor()
{
    alive->store (false);
    stopTimer();
    loaderPool.removeAllJobs (true, 30000);
}

bool AmariKeysProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void AmariKeysProcessor::timerCallback()
{
    // free retired sample sets off the audio thread
    for (auto& L : layers)
    {
        std::unique_ptr<SampleSet> toDelete;
        const juce::SpinLock::ScopedLockType l (swapLock);
        toDelete = std::move (L.retired);
    }
    // program change requested from a MIDI pad
    const int req = programRequest.exchange (-1);
    if (req >= 0) recallProgram (req);
}

void AmariKeysProcessor::loadSamples (int layer, const juce::File& source)
{
    int expected = -1;
    if (! loadingLayer.compare_exchange_strong (expected, layer)) return;
    {
        const juce::ScopedLock l (statusLock);
        layers[layer].status = "Loading " + source.getFileName() + " ...";
    }
    loaderPool.addJob ([this, layer, source]
    {
        juce::String msg;
        auto set = SampleLoader::load (source, msg, [this, layer, source] (float p)
        {
            const juce::ScopedLock l (statusLock);
            layers[layer].status = "Loading " + source.getFileName() + "  " + juce::String (juce::roundToInt (p * 100.0f)) + "%";
        });
        const bool ok = set != nullptr;
        const juce::String name = ok ? set->name : juce::String();
        if (ok)
        {
            msg << "  |  " << Params::noteName (set->lowNote) << " to " << Params::noteName (set->highNote)
                << "  |  " << juce::roundToInt ((double) set->bytes / (1024.0 * 1024.0)) << " MB";
            const juce::SpinLock::ScopedLockType l (swapLock);
            layers[layer].pending = std::move (set);
        }
        {
            const juce::ScopedLock l (statusLock);
            layers[layer].status = ok ? msg : "Load failed: " + msg;
            if (ok) { layers[layer].name = name; layers[layer].path = source.getFullPathName(); }
        }
        if (ok)
        {
            layers[layer].hasSet.store (true);
            const auto path = source.getFullPathName();
            juce::MessageManager::callAsync ([this, path, layer, token = alive]
            {
                if (! token->load()) return;
                apvts.state.setProperty ("samplePath_l" + juce::String (layer + 1), path, nullptr);
                const auto kind = Params::kindOf (layer);
                if (kind == Kind::Keys) { if (auto* prm = apvts.getParameter (lid (layer, "model"))) prm->setValueNotifyingHost (prm->convertTo0to1 (4.0f)); }
                else if (kind == Kind::Synth) { if (auto* prm = apvts.getParameter (lid (layer, "src"))) prm->setValueNotifyingHost (1.0f); }
            });
        }
        loadingLayer.store (-1);
    });
}

//==============================================================================
void AmariKeysProcessor::storeProgram (int index)
{
    auto& p = programs[(size_t) index];
    p.values.clear();
    for (auto* prm : paramList)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            p.values.push_back ({ r->getParameterID(), r->getValue() });
    if (p.name == "INIT")
    {
        juce::StringArray on;
        for (int l = 0; l < numLayers; ++l) if (param (lid (l, "on")) > 0.5f) on.add (Params::layerShort (l));
        p.name = on.joinIntoString (" + ");
    }
    currentProgram.store (index);
}

void AmariKeysProcessor::recallProgram (int index)
{
    currentProgram.store (index);
    auto& p = programs[(size_t) index];
    for (auto& v : p.values)
        if (auto* prm = apvts.getParameter (v.first))
            prm->setValueNotifyingHost (v.second);
}

void AmariKeysProcessor::padPressed (int pad, float vel)
{
    const int mode = (int) param ("pad_mode");
    if (mode == 1 || (mode == 2 && pad < 4))
    {
        recallProgram (mode == 1 ? pad : (getCurrentProgramIndex() / 4) * 4 + pad);
        padHits[pad]++;
        return;
    }
    // drum pads are played through the audio thread as MIDI channel 10, note 36 + pad
    keyboardState.noteOn (10, 36 + pad, vel);
    keyboardState.noteOff (10, 36 + pad, 0.0f);
}

void AmariKeysProcessor::triggerPadAudio (int pad, float vel)
{
    const int mode = (int) param ("pad_mode");
    if (mode == 1 || (mode == 2 && pad < 4))
    {
        programRequest.store (mode == 1 ? pad : (currentProgram.load() / 4) * 4 + pad);
        padHits[pad]++;
        return;
    }
    const int drum = mode == 0 ? pad : pad - 4;
    if (drum >= 0 && drum < DrumEngine::numPads) drums.trigger (drum, vel);
    padHits[pad]++;
}

void AmariKeysProcessor::setLearnTarget (const juce::String& paramId)
{
    for (int i = 0; i < paramList.size(); ++i)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]))
            if (r->getParameterID() == paramId) { learnTarget.store (i); return; }
}

juce::String AmariKeysProcessor::getLearnTargetName() const
{
    const int t = learnTarget.load();
    return t >= 0 && t < paramList.size() ? paramList[t]->getName (40) : juce::String();
}

//==============================================================================
void AmariKeysProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int maxBlock = juce::jmax (512, samplesPerBlock);
    organ.prepare (sr); rotary.prepare (sr); drums.prepare (sr);
    for (auto& L : layers)
    {
        L.keys.prepare (sr); L.synth.prepare (sr); L.sampler.prepare (sr); L.sampler.setSet (L.live.get());
        L.buf.setSize (2, maxBlock);
        L.gain.reset (sr, 0.03);
    }
    orgMono.setSize (1, maxBlock); padBuf.setSize (2, maxBlock); fxBuf.setSize (2, maxBlock);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) 512, 2 };
    chorus.prepare (spec); phaser.prepare (spec); comp.prepare (spec);
    chorus.reset(); phaser.reset(); comp.reset();
    reverb.setSampleRate (sr); reverb.reset();
    delayL.init ((int) (sr * 1.3) + 8); delayR.init ((int) (sr * 1.3) + 8);
    juce::dsp::ProcessSpec mono { sr, (juce::uint32) maxBlock, 1 };
    for (int c = 0; c < 2; ++c) { eqLow[c].prepare (mono); eqMid[c].prepare (mono); eqHigh[c].prepare (mono); }
    lastEq[0] = lastEq[1] = lastEq[2] = -99.0f;
    masterSm.reset (sr, 0.03); padSm.reset (sr, 0.03);
    for (auto& ch : routed) for (auto& r : ch) r = Routed {};
}

void AmariKeysProcessor::updateSettings()
{
    auto& o = organ.s;
    for (int i = 0; i < 9; ++i)
    {
        const int lvl = (int) std::round (param ("db" + juce::String (i + 1)));
        o.drawbar[i] = lvl <= 0 ? 0.0f : std::pow (10.0f, -3.0f * (8 - lvl) / 20.0f);
    }
    o.model = (int) param ("org_model");
    o.percOn = param ("perc_on") > 0.5f; o.perc3rd = param ("perc_3rd") > 0.5f;
    o.percFast = param ("perc_fast") > 0.5f; o.percSoft = param ("perc_soft") > 0.5f;
    o.click = param ("org_click"); o.vibMode = (int) param ("vib_mode"); o.drive = param ("org_drive");
    rotary.setSpeed ((int) param ("rot_speed"));

    for (int l = 0; l < numLayers; ++l)
    {
        auto& L = layers[l];
        const auto kind = Params::kindOf (l);
        if (kind == Kind::Keys)
        {
            auto& k = L.keys.s;
            k.model = juce::jmin (3, (int) param (lid (l, "model")));
            const int tb = (int) param (lid (l, "timbre"));
            k.timbre = tb == 0 ? 0.6f : tb == 1 ? 1.0f : 1.55f;
            k.dynamics = param (lid (l, "dyn")); k.decay = param (lid (l, "decay")); k.tremolo = param (lid (l, "trem"));
            L.sampler.dynamics = k.dynamics; L.sampler.release = 0.05f + k.decay * 1.5f; L.sampler.timbre = tb;
        }
        else if (kind == Kind::Synth)
        {
            auto& y = L.synth.s;
            y.voiceMode = (int) param (lid (l, "voice"));
            y.wave1 = (int) param (lid (l, "wave1")); y.wave2 = (int) param (lid (l, "wave2"));
            y.oscMix = param (lid (l, "oscmix")); y.detune = param (lid (l, "detune")); y.sub = 0.0f; y.glide = param (lid (l, "glide"));
            y.filterType = (int) param (lid (l, "ftype"));
            y.cutoff = param (lid (l, "cutoff")); y.reso = param (lid (l, "reso")); y.envAmt = param (lid (l, "fenv"));
            y.keyTrack = param (lid (l, "fkey")); y.drive = 0.1f;
            y.fenv = { param (lid (l, "fa")), param (lid (l, "fd")), param (lid (l, "fs")), param (lid (l, "fr")) };
            y.aenv = { param (lid (l, "aa")), param (lid (l, "ad")), param (lid (l, "as")), param (lid (l, "ar")) };
            y.lfoRate = param (lid (l, "lforate")); y.lfoAmt = param (lid (l, "lfo"));
            L.sampler.dynamics = 0.6f; L.sampler.release = juce::jmax (0.05f, y.aenv.release); L.sampler.timbre = 1;
        }
    }
}

void AmariKeysProcessor::noteOffLayer (int l, int note)
{
    auto& L = layers[l];
    switch (Params::kindOf (l))
    {
        case Kind::Organ: organ.noteOff (note); break;
        case Kind::Keys:  L.keys.noteOff (note); L.sampler.noteOff (note); break;
        case Kind::Synth: L.synth.noteOff (note); L.sampler.noteOff (note); break;
    }
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

        if (ch == 9) // pads: MIDI channel 10, notes 36-51 = pads 1A..4D
        {
            const int pad = note - 36;
            if (pad >= 0 && pad < numPads) triggerPadAudio (pad, vel);
            return;
        }
        const int n = juce::jlimit (0, 127, note + (int) param ("transpose"));
        const bool split = param ("split_on") > 0.5f;
        const int splitNote = (int) param ("split_note");
        Routed r;
        for (int l = 0; l < numLayers; ++l)
        {
            if (param (lid (l, "on")) < 0.5f) continue;
            const int zone = (int) param (lid (l, "zone")); // 0 LO, 1 UP, 2 ALL
            if (split && zone == 0 && n >= splitNote) continue;
            if (split && zone == 1 && n < splitNote) continue;
            const int ln = juce::jlimit (0, 127, n + 12 * (int) param (lid (l, "oct")));
            auto& L = layers[l];
            switch (Params::kindOf (l))
            {
                case Kind::Organ: organ.noteOn (ln, vel); break;
                case Kind::Keys:
                    if ((int) param (lid (l, "model")) == 4 && L.live != nullptr) L.sampler.noteOn (ln, vel);
                    else
                    {
                        if ((int) param (lid (l, "model")) == 4) L.keys.s.model = 0; // no samples yet -> tine EP fallback
                        L.keys.noteOn (ln, vel);
                    }
                    break;
                case Kind::Synth:
                    if ((int) param (lid (l, "src")) == 1 && L.live != nullptr) L.sampler.noteOn (ln, vel);
                    else L.synth.noteOn (ln, vel);
                    break;
            }
            L.down[ln] = true; L.heldSus[ln] = false;
            r.note[l] = (int8_t) ln;
        }
        routed[ch][note] = r;
    }
    else if (m.isNoteOff())
    {
        if (ch == 9) return;
        auto& r = routed[ch][m.getNoteNumber()];
        for (int l = 0; l < numLayers; ++l)
        {
            const int ln = r.note[l];
            if (ln < 0) continue;
            auto& L = layers[l];
            L.down[ln] = false;
            if (sustain && Params::kindOf (l) != Kind::Organ) L.heldSus[ln] = true;
            else noteOffLayer (l, ln);
        }
        r = Routed {};
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 64)
        {
            sustain = val >= 64;
            if (! sustain)
                for (int l = 0; l < numLayers; ++l)
                    for (int i = 0; i < 128; ++i)
                        if (layers[l].heldSus[i]) { layers[l].heldSus[i] = false; if (! layers[l].down[i]) noteOffLayer (l, i); }
            return;
        }
        if (m.isAllNotesOff() || m.isAllSoundOff()) { panic(); return; }
        if (learnMode.load() && learnTarget.load() >= 0)
        {
            for (auto& c : ccMap) if (c.load() == learnTarget.load()) c.store (-1);
            ccMap[cc].store (learnTarget.load());
            learnTarget.store (-1);
            learnHits++;
        }
        const int target = ccMap[cc].load();
        if (target >= 0 && target < paramList.size())
            paramList[target]->setValueNotifyingHost ((float) val / 127.0f);
    }
    else if (m.isPitchWheel())
    {
        const float bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        for (auto& L : layers) L.synth.s.pitchBend = bend;
    }
}

void AmariKeysProcessor::renderSegment (int start, int num)
{
    organ.render (orgMono.getWritePointer (0) + start, num);
    for (int l = 0; l < numLayers; ++l)
    {
        auto& L = layers[l];
        auto* lp = L.buf.getWritePointer (0) + start; auto* rp = L.buf.getWritePointer (1) + start;
        const auto kind = Params::kindOf (l);
        if (kind == Kind::Organ) continue;
        L.sampler.render (lp, rp, num);
        if (kind == Kind::Keys) L.keys.render (lp, rp, num);
        else L.synth.render (lp, rp, num);
    }
    drums.render (padBuf.getWritePointer (0) + start, padBuf.getWritePointer (1) + start, num);
}

void AmariKeysProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n > orgMono.getNumSamples())
    {
        orgMono.setSize (1, n, false, false, true);
        padBuf.setSize (2, n, false, false, true); fxBuf.setSize (2, n, false, false, true);
        for (auto& L : layers) L.buf.setSize (2, n, false, false, true);
    }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    {   // pick up freshly loaded sample sets without blocking
        const juce::SpinLock::ScopedTryLockType l (swapLock);
        if (l.isLocked())
            for (auto& L : layers)
                if (L.pending != nullptr && L.retired == nullptr)
                {
                    L.retired = std::move (L.live);
                    L.live = std::move (L.pending);
                    L.sampler.setSet (L.live.get());
                }
    }

    if (panicRequest.exchange (false))
    {
        organ.allOff(); drums.allOff();
        for (auto& L : layers)
        {
            L.keys.allOff(); L.synth.allOff(); L.sampler.allOff();
            std::fill (std::begin (L.down), std::end (L.down), false);
            std::fill (std::begin (L.heldSus), std::end (L.heldSus), false);
        }
        for (auto& c : routed) for (auto& r : c) r = Routed {};
        sustain = false;
        reverb.reset(); delayL.init ((int) (sr * 1.3) + 8); delayR.init ((int) (sr * 1.3) + 8);
    }

    updateSettings();

    orgMono.clear(); padBuf.clear(); fxBuf.clear();
    for (auto& L : layers) L.buf.clear();

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos) { renderSegment (pos, t - pos); pos = t; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) renderSegment (pos, n - pos);
    midi.clear();

    // organ -> rotary into its layer buffer
    auto& orgL = layers[2];
    if (param ("rot_on") > 0.5f)
        rotary.process (orgMono.getReadPointer (0), orgL.buf.getWritePointer (0), orgL.buf.getWritePointer (1), n);
    else { orgL.buf.copyFrom (0, 0, orgMono, 0, 0, n); orgL.buf.copyFrom (1, 0, orgMono, 0, 0, n); }

    // layer mixer
    const int target = (int) param ("fx_target"); // ALL, KEYS, SYN, NO ORG
    buffer.clear();
    for (int l = 0; l < numLayers; ++l)
    {
        auto& L = layers[l];
        const float base = l == 2 ? 0.6f : 0.65f;
        L.gain.setTargetValue (param (lid (l, "on")) > 0.5f ? param (lid (l, "level")) * base : 0.0f);
        auto* lp = L.buf.getWritePointer (0); auto* rp = L.buf.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float g = L.gain.getNextValue(); lp[i] *= g; rp[i] *= g; }
        const auto kind = Params::kindOf (l);
        const bool toFx = target == 0 || (target == 1 && kind == Kind::Keys) || (target == 2 && kind == Kind::Synth) || (target == 3 && kind != Kind::Organ);
        auto& dst = toFx ? fxBuf : buffer;
        for (int c = 0; c < 2; ++c) dst.addFrom (c, 0, L.buf, c, 0, n);
    }
    padSm.setTargetValue (param ("pads_level"));
    {
        auto* l = padBuf.getWritePointer (0); auto* r = padBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float g = padSm.getNextValue(); l[i] *= g; r[i] *= g; }
        for (int c = 0; c < 2; ++c) buffer.addFrom (c, 0, padBuf, c, 0, n);
    }

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
            auto chh = C::makeHighShelf (sr, 6000.0, 0.7f, juce::Decibels::decibelsToGain (hi));
            for (int c = 0; c < 2; ++c) { *eqLow[c].coefficients = *cl; *eqMid[c].coefficients = *cm; *eqHigh[c].coefficients = *chh; }
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

    masterSm.setTargetValue (param ("master"));
    auto* oL = buffer.getWritePointer (0); auto* oR = buffer.getWritePointer (1);
    auto soft = [] (float x) { const float a = std::abs (x); return a <= 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f), x); };
    for (int i = 0; i < n; ++i)
    {
        const float g = masterSm.getNextValue();
        oL[i] = soft (oL[i] * g); oR[i] = soft (oR[i] * g);
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

juce::AudioProcessorEditor* AmariKeysProcessor::createEditor() { return new AmariKeysEditor (*this); }

//==============================================================================
void AmariKeysProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("PROGRAMS"), nullptr);
    juce::ValueTree progs ("PROGRAMS");
    progs.setProperty ("current", currentProgram.load(), nullptr);
    for (int i = 0; i < numPrograms; ++i)
    {
        juce::ValueTree p ("PROG");
        p.setProperty ("index", i, nullptr);
        p.setProperty ("name", programs[(size_t) i].name, nullptr);
        for (auto& v : programs[(size_t) i].values)
        {
            juce::ValueTree pv ("V"); pv.setProperty ("id", v.first, nullptr); pv.setProperty ("v", v.second, nullptr);
            p.appendChild (pv, nullptr);
        }
        progs.appendChild (p, nullptr);
    }
    state.appendChild (progs, nullptr);
    juce::String map;
    for (int c = 0; c < 128; ++c)
        if (ccMap[c].load() >= 0)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (paramList[ccMap[c].load()]))
                map << c << "=" << r->getParameterID() << ";";
    state.setProperty ("ccmap", map, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AmariKeysProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    auto progs = tree.getChildWithName ("PROGRAMS");
    tree.removeChild (progs, nullptr);
    apvts.replaceState (tree);

    if (progs.isValid())
    {
        currentProgram.store ((int) progs.getProperty ("current", 0));
        for (auto p : progs)
        {
            const int i = (int) p.getProperty ("index", -1);
            if (i < 0 || i >= numPrograms) continue;
            auto& prog = programs[(size_t) i];
            prog.name = p.getProperty ("name", "INIT").toString();
            prog.values.clear();
            for (auto v : p) prog.values.push_back ({ v.getProperty ("id").toString(), (float) v.getProperty ("v") });
        }
    }
    for (auto& c : ccMap) c.store (-1);
    juce::StringArray pairs; pairs.addTokens (apvts.state.getProperty ("ccmap").toString(), ";", "");
    for (auto& pr : pairs)
    {
        const int cc = pr.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const auto id = pr.fromFirstOccurrenceOf ("=", false, false);
        if (id.isEmpty() || cc < 0 || cc > 127) continue;
        for (int i = 0; i < paramList.size(); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]))
                if (r->getParameterID() == id) ccMap[cc].store (i);
    }
    // reload sample sets (one at a time, queued)
    for (int l = 0; l < numLayers; ++l)
    {
        const juce::File f (apvts.state.getProperty ("samplePath_l" + juce::String (l + 1)).toString());
        if (f.exists() && layers[l].path != f.getFullPathName())
        {
            const int layer = l;
            juce::Timer::callAfterDelay (200 + 300 * l, [this, layer, f, token = alive]
            {
                if (! token->load()) return;
                if (isLoadingSamples()) { juce::Timer::callAfterDelay (1500, [this, layer, f, token] { if (token->load()) loadSamples (layer, f); }); return; }
                loadSamples (layer, f);
            });
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmariKeysProcessor(); }
