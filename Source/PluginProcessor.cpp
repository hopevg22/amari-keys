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
    patches = AmariLibrary::loadPatches();
    loadMidiMap (AmariLibrary::settings().getValue ("midimap"));
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
    if (req >= 0 && req < getPatchCount()) recallPatch (req);
    // hardware controls learned to functions
    if (midiMapDirty.exchange (false)) saveMidiMap();
    const int sel = pendingSelect.exchange (-1);
    if (sel >= 0) { setBrowseTarget (sel); selectRequest.store (sel); }
    const int acts = pendingActions.exchange (0);
    if (acts & (1 << actBrowsePatches)) setBrowseTarget (-1);
    if (acts & ((1 << actPatchPrev) | (1 << actPatchNext)))
    {
        reloadPatches();
        const int n = getPatchCount();
        if (n > 0)
        {
            const int cur = getCurrentPatch(), d = (acts & (1 << actPatchNext)) ? 1 : -1;
            recallPatch (cur < 0 ? (d > 0 ? 0 : n - 1) : (cur + d + n) % n);
        }
    }
    const int dial = pendingDial.exchange (0) / 2; // two knob values per step
    if (dial != 0) dialStep (dial);
    if (acts & (1 << actEnter)) dialEnter();
}

void AmariKeysProcessor::loadSamples (int layer, const juce::File& source)
{
    // Requests are queued on one loader thread (never dropped). A newer request for the
    // same layer replaces an older one that hasn't started yet.
    if (layer < 0 || layer >= numLayers) return;
    juce::uint32 serial;
    {
        const juce::ScopedLock l (statusLock);
        serial = ++loadSerial[layer];
        layers[layer].status = (isLoadingSamples() ? "Waiting to load " : "Loading ") + source.getFileName() + " ...";
    }
    ++queuedLoads;
    loaderPool.addJob ([this, layer, source, serial]
    {
        --queuedLoads;
        runLoad (layer, source, serial);
    });
}

void AmariKeysProcessor::clearSamples (int layer)
{
    if (layer < 0 || layer >= numLayers) return;
    {
        const juce::ScopedLock l (statusLock);
        ++loadSerial[layer];
        layers[layer].status = "No samples loaded"; layers[layer].name = {}; layers[layer].path = {};
    }
    layers[layer].clearRequest.store (true);
    layers[layer].hasSet.store (false);
    apvts.state.removeProperty ("samplePath_l" + juce::String (layer + 1), nullptr);
}

void AmariKeysProcessor::runLoad (int layer, juce::File source, juce::uint32 serial)
{
    {
        const juce::ScopedLock l (statusLock);
        if (serial != loadSerial[layer]) return; // superseded by a newer request
    }
    loadingLayer.store (layer);
    {
        const juce::ScopedLock l (statusLock);
        layers[layer].status = "Loading " + source.getFileName() + " ...";
    }
    juce::String msg;
    auto set = SampleLoader::load (source, msg, [this, layer, source] (float p)
    {
        const juce::ScopedLock l (statusLock);
        layers[layer].status = "Loading " + source.getFileName() + "  " + juce::String (juce::roundToInt (p * 100.0f)) + "%";
    });
    bool superseded;
    { const juce::ScopedLock l (statusLock); superseded = serial != loadSerial[layer]; }
    if (superseded) { loadingLayer.store (-1); return; }

    const bool ok = set != nullptr;
    const juce::String name = ok ? set->name : juce::String();
    if (ok)
    {
        msg << "  |  " << Params::noteName (set->lowNote) << " to " << Params::noteName (set->highNote)
            << "  |  " << juce::roundToInt ((double) set->bytes / (1024.0 * 1024.0)) << " MB";
        if (set->mappingInfo.isNotEmpty()) msg << "  |  " << set->mappingInfo;
        // wait (briefly) until the audio thread has taken any previous pending set
        for (int i = 0; i < 200; ++i)
        {
            { const juce::SpinLock::ScopedLockType l (swapLock); if (layers[layer].pending == nullptr) { layers[layer].pending = std::move (set); break; } }
            juce::Thread::sleep (10);
        }
        if (set != nullptr) { const juce::SpinLock::ScopedLockType l (swapLock); layers[layer].pending = std::move (set); }
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
}

//==============================================================================
static juce::ValueTree capturePatch (AmariKeysProcessor& p, const juce::Array<juce::AudioProcessorParameter*>& params, const juce::String& name)
{
    juce::ValueTree t ("PATCH");
    t.setProperty ("name", name, nullptr);
    for (auto* prm : params)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            juce::ValueTree v ("V"); v.setProperty ("id", r->getParameterID(), nullptr); v.setProperty ("v", r->getValue(), nullptr);
            t.appendChild (v, nullptr);
        }
    for (int l = 0; l < AmariKeysProcessor::numLayers; ++l)
    {
        const auto path = p.getSamplePath (l);
        if (path.isEmpty()) continue;
        juce::ValueTree snd ("SOUND");
        snd.setProperty ("layer", l, nullptr); snd.setProperty ("path", path, nullptr); snd.setProperty ("name", p.getSampleName (l), nullptr);
        t.appendChild (snd, nullptr);
    }
    return t;
}

void AmariKeysProcessor::reloadPatches()
{
    patches = AmariLibrary::loadPatches();
    if (currentPatch.load() >= getPatchCount()) currentPatch.store (-1);
}

void AmariKeysProcessor::recallPatch (int index)
{
    reloadPatches();
    if (index < 0 || index >= getPatchCount()) return;
    auto t = patches.getChild (index);
    currentPatch.store (index);
    apvts.state.setProperty ("patchName", t.getProperty ("name"), nullptr);
    juce::StringArray stored;
    for (auto v : t)
        if (v.hasType ("V"))
            if (auto* prm = apvts.getParameter (v.getProperty ("id").toString()))
            {
                prm->setValueNotifyingHost ((float) v.getProperty ("v"));
                stored.add (v.getProperty ("id").toString());
            }
    // settings a patch doesn't know (e.g. layers added later: BASS, SFX) go back to their defaults
    for (auto* prm : paramList)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! stored.contains (r->getParameterID()) && r->getParameterID() != "master")
                r->setValueNotifyingHost (r->getDefaultValue());
    // each layer's sound: load what the patch uses (skip if already loaded), unload layers it doesn't use
    for (int l = 0; l < numLayers; ++l)
    {
        juce::String path;
        for (auto snd : t) if (snd.hasType ("SOUND") && (int) snd.getProperty ("layer", -1) == l) path = snd.getProperty ("path").toString();
        const auto current = getSamplePath (l);
        if (path.isEmpty()) { if (current.isNotEmpty()) clearSamples (l); continue; }
        const juce::File f (path);
        if (! f.exists()) { const juce::ScopedLock lk (statusLock); layers[l].status = "Missing: " + path; continue; }
        if (f.getFullPathName() != current) loadSamples (l, f);
    }
}

bool AmariKeysProcessor::storePatch (int index)
{
    reloadPatches();
    if (index < 0 || index >= getPatchCount()) return false;
    const auto name = getPatchName (index);
    auto fresh = capturePatch (*this, paramList, name);
    patches.removeChild (index, nullptr);
    patches.addChild (fresh, index, nullptr);
    currentPatch.store (index);
    return AmariLibrary::savePatches (patches);
}

int AmariKeysProcessor::savePatchAs (const juce::String& name)
{
    reloadPatches();
    patches.appendChild (capturePatch (*this, paramList, name.trim().isEmpty() ? juce::String ("NEW PATCH") : name.trim()), nullptr);
    const int idx = getPatchCount() - 1;
    currentPatch.store (idx);
    apvts.state.setProperty ("patchName", getPatchName (idx), nullptr);
    AmariLibrary::savePatches (patches);
    return idx;
}

void AmariKeysProcessor::renamePatch (int index, const juce::String& name)
{
    reloadPatches();
    if (index < 0 || index >= getPatchCount() || name.trim().isEmpty()) return;
    patches.getChild (index).setProperty ("name", name.trim(), nullptr);
    if (index == currentPatch.load()) apvts.state.setProperty ("patchName", name.trim(), nullptr);
    AmariLibrary::savePatches (patches);
}

void AmariKeysProcessor::deletePatch (int index)
{
    reloadPatches();
    if (index < 0 || index >= getPatchCount()) return;
    patches.removeChild (index, nullptr);
    const int cur = currentPatch.load();
    if (cur == index) currentPatch.store (-1); else if (cur > index) currentPatch.store (cur - 1);
    AmariLibrary::savePatches (patches);
}

// Programs from builds up to 0.4 (16 slots inside the project) move into the library once.
void AmariKeysProcessor::migrateOldPrograms (const juce::ValueTree& progs)
{
    if (! progs.isValid()) return;
    reloadPatches();
    bool changed = false;
    for (auto p : progs)
    {
        if (p.getNumChildren() == 0) continue;
        const int i = (int) p.getProperty ("index", 0);
        auto name = p.getProperty ("name", "INIT").toString();
        if (name == "INIT") name = "PROGRAM " + juce::String (i / 4 + 1) + juce::String::charToString ((juce::juce_wchar) ('A' + i % 4));
        bool exists = false;
        for (auto q : patches) if (q.getProperty ("name").toString() == name) exists = true;
        if (exists) continue;
        juce::ValueTree t ("PATCH"); t.setProperty ("name", name, nullptr);
        for (auto v : p) { juce::ValueTree c ("V"); c.setProperty ("id", v.getProperty ("id"), nullptr); c.setProperty ("v", v.getProperty ("v"), nullptr); t.appendChild (c, nullptr); }
        patches.appendChild (t, nullptr);
        changed = true;
    }
    if (changed) AmariLibrary::savePatches (patches);
}

void AmariKeysProcessor::padPressed (int pad, float vel)
{
    const int mode = (int) param ("pad_mode");
    if (mode == 1 || (mode == 2 && pad < 4))
    {
        recallPatch (mode == 1 ? pad : (juce::jmax (0, getCurrentPatch()) / 4) * 4 + pad);
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
        programRequest.store (mode == 1 ? pad : (juce::jmax (0, currentPatch.load()) / 4) * 4 + pad);
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

juce::String AmariKeysProcessor::actionName (int a)
{
    switch (a)
    {
        case actPatchPrev: return "PATCH <";
        case actPatchNext: return "PATCH >";
        case actDial: return "DATA DIAL";
        case actEnter: return "ENTER";
        case actPanic: return "PANIC";
        case actBrowsePatches: return "DIAL: BROWSE PATCHES";
        default: break;
    }
    if (a >= actSelectLayer && a < actSelectLayer + numLayers) return juce::String ("SELECT ") + Params::layerShort (a - actSelectLayer);
    return "?";
}

juce::String AmariKeysProcessor::targetName (int t) const
{
    if (t >= actionBase) return actionName (t - actionBase);
    return t >= 0 && t < paramList.size() ? paramList[t]->getName (40) : juce::String();
}

juce::String AmariKeysProcessor::getLearnTargetName() const { return targetName (learnTarget.load()); }

juce::Array<AmariKeysProcessor::Mapping> AmariKeysProcessor::getMappings() const
{
    juce::Array<Mapping> out;
    for (int c = 0; c < 128; ++c) if (ccMap[c].load() >= 0) out.add ({ c, targetName (ccMap[c].load()) });
    return out;
}

// The MIDI map belongs to your controller, not to one project: kept in the Amari Keys settings.
void AmariKeysProcessor::saveMidiMap()
{
    juce::String map;
    for (int c = 0; c < 128; ++c)
    {
        const int t = ccMap[c].load();
        if (t >= actionBase) map << c << "=@" << (t - actionBase) << ";";
        else if (t >= 0 && t < paramList.size())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (paramList[t])) map << c << "=" << r->getParameterID() << ";";
    }
    AmariLibrary::settings().setValue ("midimap", map);
    AmariLibrary::settings().saveIfNeeded();
}

void AmariKeysProcessor::loadMidiMap (const juce::String& mapText)
{
    for (auto& c : ccMap) c.store (-1);
    juce::StringArray pairs; pairs.addTokens (mapText, ";", "");
    for (auto& pr : pairs)
    {
        const int cc = pr.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const auto id = pr.fromFirstOccurrenceOf ("=", false, false);
        if (id.isEmpty() || cc < 0 || cc > 127) continue;
        if (id.startsWith ("@")) { ccMap[cc].store (actionBase + id.substring (1).getIntValue()); continue; }
        for (int i = 0; i < paramList.size(); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]))
                if (r->getParameterID() == id) ccMap[cc].store (i);
    }
}

// ---- data dial ----------------------------------------------------------------
void AmariKeysProcessor::refreshBrowseList()
{
    browseList.clear();
    if (browseTarget >= 0) browseList = AmariLibrary::soundsFor (AmariLibrary::groupFolder (browseTarget));
}

void AmariKeysProcessor::setBrowseTarget (int layer)
{
    browseTarget = (layer < 0 || Params::kindOf (layer) == Kind::Organ) ? -1 : layer;
    browsePending = false; browseIndex = -1;
    refreshBrowseList();
}

void AmariKeysProcessor::dialStep (int steps)
{
    if (steps == 0) return;
    if (browseTarget < 0)
    {
        reloadPatches();
        const int n = getPatchCount(); if (n == 0) return;
        if (browseIndex < 0) browseIndex = juce::jmax (0, getCurrentPatch());
        browseIndex = juce::jlimit (0, n - 1, browseIndex + steps);
    }
    else
    {
        if (browseList.isEmpty() || ! browsePending) refreshBrowseList();
        const int n = browseList.size(); if (n == 0) return;
        if (browseIndex < 0)
        {   // start from the sound that is loaded now
            browseIndex = 0;
            const juce::File cur (getSamplePath (browseTarget));
            for (int i = 0; i < n; ++i) if (juce::File (browseList[i].path) == cur) { browseIndex = i; break; }
            if (cur == juce::File() || juce::File (browseList[browseIndex].path) != cur) steps = steps > 0 ? steps - 1 : steps; // first step lands on the first sound
        }
        browseIndex = juce::jlimit (0, n - 1, browseIndex + steps);
    }
    browsePending = true;
}

void AmariKeysProcessor::dialEnter()
{
    if (! browsePending || browseIndex < 0) return;
    if (browseTarget < 0) { if (browseIndex < getPatchCount()) recallPatch (browseIndex); }
    else if (browseIndex < browseList.size())
    {
        const juce::File f (browseList[browseIndex].path);
        if (f.exists()) loadSamples (browseTarget, f);
        // make sure the layer plays its samples and can be heard
        if (auto* on = apvts.getParameter (lid (browseTarget, "on"))) on->setValueNotifyingHost (1.0f);
    }
    browsePending = false; browseIndex = -1;
}

std::pair<juce::String, juce::String> AmariKeysProcessor::getBrowseText() const
{
    if (! browsePending) return {};
    if (browseTarget < 0)
    {
        const int n = getPatchCount();
        return { juce::String (browseIndex + 1).paddedLeft ('0', 3) + "  " + getPatchName (browseIndex).toUpperCase(),
                 "PATCH " + juce::String (browseIndex + 1) + " OF " + juce::String (n) + "   |   ENTER TO LOAD" };
    }
    const int n = browseList.size();
    const auto name = browseIndex >= 0 && browseIndex < n ? browseList[browseIndex].name : juce::String ("-");
    return { juce::String (Params::layerShort (browseTarget)) + "  " + name.toUpperCase(),
             "SOUND " + juce::String (browseIndex + 1) + " OF " + juce::String (n) + "   |   ENTER TO LOAD" };
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
    rotary.setSpeed (wheelMod.load() > 0.5f ? 1 : (int) param ("rot_speed")); // mod wheel up = rotary FAST

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

// pedalLifted: the note was only held by the sustain pedal - end it with a short, natural tail
void AmariKeysProcessor::noteOffLayer (int l, int note, bool pedalLifted)
{
    auto& L = layers[l];
    const float cap = pedalLifted ? Params::pedalTailFor (l, param ("pedal_tail")) : -1.0f;
    switch (Params::kindOf (l))
    {
        case Kind::Organ: organ.noteOff (note); break;
        case Kind::Keys:  L.keys.noteOff (note); L.sampler.noteOff (note, cap); break;
        case Kind::Synth: L.synth.noteOff (note, cap); L.sampler.noteOff (note, cap); break;
    }
}

void AmariKeysProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int ch = juce::jlimit (0, 15, m.getChannel() - 1);
    if (m.isNoteOn()) ++notesSeen;
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
                    if ((int) param (lid (l, "model")) == 4 && L.live != nullptr) L.sampler.noteOn (ln, vel, sustain);
                    else
                    {
                        if ((int) param (lid (l, "model")) == 4) L.keys.s.model = 0; // no samples yet -> tine EP fallback
                        L.keys.noteOn (ln, vel);
                    }
                    break;
                case Kind::Synth:
                    if ((int) param (lid (l, "src")) == 1 && L.live != nullptr) L.sampler.noteOn (ln, vel, sustain);
                    else if (l != Params::sfxLayer) L.synth.noteOn (ln, vel); // SFX: samples only
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
        ++ccSeen;
        if (cc == 64)
        {
            sustain = val >= 64;
            if (! sustain)
                for (int l = 0; l < numLayers; ++l)
                    for (int i = 0; i < 128; ++i)
                        if (layers[l].heldSus[i]) { layers[l].heldSus[i] = false; if (! layers[l].down[i]) noteOffLayer (l, i, true); }
            return;
        }
        if (m.isAllNotesOff() || m.isAllSoundOff()) { panic(); return; }
        if (cc == 1) wheelMod.store ((float) val / 127.0f); // mod wheel (can also be MIDI-learned below)
        const int prevVal = ccLast[cc]; ccLast[cc] = val;
        if (learnMode.load() && learnTarget.load() >= 0)
        {
            for (auto& c : ccMap) if (c.load() == learnTarget.load()) c.store (-1);
            ccMap[cc].store (learnTarget.load());
            lastLearned.store (learnTarget.load());
            learnTarget.store (-1);
            learnMode.store (false);   // one control per learn: done
            learnHits++;
            midiMapDirty.store (true);
            return; // the move that taught it doesn't also trigger it
        }
        const int target = ccMap[cc].load();
        if (target >= actionBase)
        {
            const int a = target - actionBase;
            if (a == actDial)
            {
                // absolute knob: step with the movement; endless encoders (1/127 or 63/65 repeats) step by one
                int delta = val - prevVal;
                if (delta == 0) delta = val < 64 ? 1 : -1;
                else if (std::abs (delta) > 32) delta = val < 64 ? 1 : -1;
                pendingDial += delta;
            }
            else if (val >= 64 && prevVal < 64) // buttons: act on press
            {
                if (a == actPanic) panic();
                else if (a >= actSelectLayer) pendingSelect.store (a - actSelectLayer);
                else pendingActions.fetch_or (1 << a);
            }
        }
        else if (target >= 0 && target < paramList.size())
            paramList[target]->setValueNotifyingHost ((float) val / 127.0f);
    }
    else if (m.isPitchWheel())
    {
        wheelBend.store (juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f));
    }
    else if (m.isProgramChange())
    {
        programRequest.store (m.getProgramChangeNumber());
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
            {
                if (L.clearRequest.load() && L.retired == nullptr)
                {
                    L.clearRequest.store (false);
                    L.sampler.allOff();
                    L.retired = std::move (L.live);
                    L.sampler.setSet (nullptr);
                }
                if (L.pending != nullptr && L.retired == nullptr)
                {
                    L.retired = std::move (L.live);
                    L.live = std::move (L.pending);
                    L.sampler.setSet (L.live.get());
                }
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

    {   // pitch bend + mod wheel vibrato for every sound except the tonewheel organ
        const float mod = wheelMod.load();
        const double vibDepth = 0.45 * mod * mod;                    // up to +/- 0.45 semitone
        const double semis = wheelBend.load() * param ("bend_range") + vibDepth * std::sin (vibPhase);
        vibPhase = std::fmod (vibPhase + juce::MathConstants<double>::twoPi * 5.6 * n / sr, juce::MathConstants<double>::twoPi);
        const double mul = std::pow (2.0, semis / 12.0);
        for (auto& L : layers) { L.sampler.pitchMul = mul; L.keys.s.pitchMul = mul; L.synth.s.pitchBend = (float) semis; }
    }

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
    auto soft = [] (float x) { const float a = std::abs (x); return a <= 0.9f ? x : std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x); };
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
    state.setProperty ("patchIndex", currentPatch.load(), nullptr);
    state.removeProperty ("ccmap", nullptr); // the MIDI map is global now (Amari Keys settings)
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

    migrateOldPrograms (progs);
    reloadPatches();
    {
        int idx = (int) apvts.state.getProperty ("patchIndex", -1);
        const auto nm = apvts.state.getProperty ("patchName").toString();
        if (idx >= getPatchCount() || (idx >= 0 && nm.isNotEmpty() && getPatchName (idx) != nm))
        {   // the list changed since this project was saved: find the patch by name
            idx = -1;
            for (int i = 0; i < getPatchCount(); ++i) if (getPatchName (i) == nm) { idx = i; break; }
        }
        currentPatch.store (idx);
    }
    {   // older projects kept their own MIDI map: adopt it once if no global map exists yet
        const auto old = apvts.state.getProperty ("ccmap").toString();
        if (old.isNotEmpty() && AmariLibrary::settings().getValue ("midimap").isEmpty()) { loadMidiMap (old); saveMidiMap(); }
        apvts.state.removeProperty ("ccmap", nullptr);
    }
    // reload sample sets (one at a time, queued)
    for (int l = 0; l < numLayers; ++l)
    {
        const juce::File f (apvts.state.getProperty ("samplePath_l" + juce::String (l + 1)).toString());
        if (f.exists() && layers[l].path != f.getFullPathName())
        {
            const int layer = l;
            loadSamples (layer, f); // queued - loads one after the other
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmariKeysProcessor(); }
