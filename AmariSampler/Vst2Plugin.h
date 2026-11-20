#pragma once
// Minimal VST 2.x instrument host for Amari Sampler, wrapped as a juce::AudioProcessor so the
// MultiSampleRenderer can drive it exactly like a VST3. Uses the clean-room, BSD-licensed
// vst2sdk header by Michael 'Xaymar' Dirks (external/vst2sdk) - no Steinberg SDK needed.
#include <juce_audio_processors/juce_audio_processors.h>
#if ! JUCE_WINDOWS && ! defined (__cdecl)
 #define __cdecl
#endif
#include "external/vst2sdk/vst.h"
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif
#include <cstring>

class Vst2Plugin : public juce::AudioProcessor
{
public:
    using Entry = vst_effect_t* (VST_FUNCTION_INTERFACE*) (vst_host_callback_t);

    // Opens a VST2 .dll (or .so). Returns nullptr and an error text on failure.
    static std::unique_ptr<Vst2Plugin> load (const juce::File& file, double sampleRate, int blockSize, juce::String& error)
    {
        auto lib = std::make_unique<juce::DynamicLibrary>();
        if (! lib->open (file.getFullPathName()))
        {
            error = "Windows could not open this file. It may be a 32-bit plugin (only 64-bit plugins work), or not a plugin.";
            return nullptr;
        }
        auto entry = (Entry) lib->getFunction ("VSTPluginMain");
        if (entry == nullptr) entry = (Entry) lib->getFunction ("main");
        if (entry == nullptr) { error = "This file is not a VST2 plugin (no VST entry point)."; return nullptr; }

        loadingSampleRate() = sampleRate; loadingBlockSize() = blockSize;
        vst_effect_t* fx = nullptr;
        try { fx = entry (&hostCallback); } catch (...) { fx = nullptr; }
        if (fx == nullptr || fx->magic_number != (int32_t) VST_MAGICNUMBER) { error = "The plugin did not start (not a VST2 instrument, or it needs a shell host)."; return nullptr; }
        if (fx->num_outputs < 1) { fx->control (fx, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0); error = "This plugin has no audio outputs."; return nullptr; }

        return std::unique_ptr<Vst2Plugin> (new Vst2Plugin (std::move (lib), fx, file, sampleRate, blockSize));
    }

    ~Vst2Plugin() override
    {
        if (fx != nullptr)
        {
            if (processing) stopProcessing();
            fx->control (fx, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0);
            fx->host_internal = nullptr;
            fx = nullptr;
        }
        library.reset();
    }

    bool isInstrument() const { return (fx->flags & VST_EFFECT_FLAG_INSTRUMENT) != 0; }
    juce::String getVendor() const { return vendor; }

    // ---- AudioProcessor ------------------------------------------------------
    const juce::String getName() const override { return name; }
    void prepareToPlay (double sr, int bs) override
    {
        if (processing) stopProcessing();
        rate = sr; block = juce::jmax (16, bs);
        fx->control (fx, VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, nullptr, (float) rate);
        fx->control (fx, VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, (intptr_t) block, nullptr, 0);
        ins.setSize (juce::jmax (1, (int) fx->num_inputs), block); ins.clear();
        outs.setSize ((int) fx->num_outputs, block);
        fx->control (fx, VST_EFFECT_OPCODE_SUSPEND_RESUME, 0, 1, nullptr, 0);
        fx->control (fx, VST_EFFECT_OPCODE_PROCESS_BEGIN, 0, 0, nullptr, 0);
        processing = true;
    }
    void releaseResources() override { if (processing) stopProcessing(); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int n = buffer.getNumSamples();
        if (! processing || n > block) { buffer.clear(); return; }

        // MIDI -> VST events (kept alive until the next call, as the format requires)
        midiEvents.clearQuick(); eventPtrs.clearQuick();
        for (const auto meta : midi)
        {
            const auto& m = meta.getMessage();
            if (m.getRawDataSize() > 3 || m.isSysEx()) continue;
            vst_event_midi_t e; std::memset (&e, 0, sizeof (e));
            e.event.type = VST_EVENT_TYPE_MIDI;
            e.event.size = (int32_t) sizeof (vst_event_midi_t);
            e.event.offset = juce::jlimit (0, n - 1, meta.samplePosition);
            std::memcpy (e.midi.data, m.getRawData(), (size_t) m.getRawDataSize());
            midiEvents.add (e);
        }
        for (auto& e : midiEvents) eventPtrs.add (&e.event);
        if (! midiEvents.isEmpty())
        {
            // VST 2.4 event list ABI: int32 count, pointer-sized reserved, then the pointer array inline (offset 16 on 64-bit)
            struct { int32_t count; intptr_t reserved; vst_event_t* events[1024]; } list {};
            list.count = juce::jmin (1024, eventPtrs.size());
            for (int i = 0; i < list.count; ++i) list.events[i] = eventPtrs[i];
            fx->control (fx, VST_EFFECT_OPCODE_EVENT, 0, 0, &list, 0);
        }

        ins.clear(); outs.clear();
        const bool replacing = (fx->flags & VST_EFFECT_FLAG_SUPPORTS_FLOAT) != 0 && fx->process_float != nullptr;
        if (replacing) fx->process_float (fx, ins.getArrayOfReadPointers(), const_cast<float**> (outs.getArrayOfWritePointers()), n);
        else if (fx->process != nullptr) fx->process (fx, ins.getArrayOfReadPointers(), const_cast<float**> (outs.getArrayOfWritePointers()), n); // old accumulating process
        samplePos += n;

        buffer.clear();
        for (int c = 0; c < juce::jmin (buffer.getNumChannels(), outs.getNumChannels()); ++c)
            buffer.copyFrom (c, 0, outs, c, 0, n);
    }

    void setNonRealtime (bool isOffline) noexcept override { offline = isOffline; AudioProcessor::setNonRealtime (isOffline); }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    bool hasEditor() const override
    {
       #if JUCE_WINDOWS
        return (fx->flags & VST_EFFECT_FLAG_EDITOR) != 0;
       #else
        return false;
       #endif
    }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override { return juce::jmax (1, (int) fx->num_programs); }
    int getCurrentProgram() override { return (int) fx->control (fx, VST_EFFECT_OPCODE_GET_PROGRAM, 0, 0, nullptr, 0); }
    void setCurrentProgram (int i) override { fx->control (fx, VST_EFFECT_OPCODE_SET_PROGRAM, 0, (intptr_t) i, nullptr, 0); }
    const juce::String getProgramName (int) override { char b[256] = {}; fx->control (fx, VST_EFFECT_OPCODE_GET_PROGRAM_NAME, 0, 0, b, 0); return juce::String::fromUTF8 (b); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    vst_effect_t* getEffect() const { return fx; }
    std::function<void (int, int)> onEditorResize; // set by the editor

private:
    Vst2Plugin (std::unique_ptr<juce::DynamicLibrary> lib, vst_effect_t* effect, const juce::File& file, double sr, int bs)
        : AudioProcessor (makeBuses (effect)), library (std::move (lib)), fx (effect), rate (sr), block (bs)
    {
        fx->host_internal = this;
        effectDirectory = file.getParentDirectory().getFullPathName();
        fx->control (fx, VST_EFFECT_OPCODE_INITIALIZE, 0, 0, nullptr, 0);
        fx->control (fx, VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, nullptr, (float) sr);
        fx->control (fx, VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, (intptr_t) bs, nullptr, 0);
        char b[256] = {};
        fx->control (fx, VST_EFFECT_OPCODE_EFFECT_NAME, 0, 0, b, 0);
        name = juce::String::fromUTF8 (b).trim();
        if (name.isEmpty()) { std::memset (b, 0, sizeof (b)); fx->control (fx, VST_EFFECT_OPCODE_PRODUCT_NAME, 0, 0, b, 0); name = juce::String::fromUTF8 (b).trim(); }
        if (name.isEmpty()) name = file.getFileNameWithoutExtension();
        std::memset (b, 0, sizeof (b));
        fx->control (fx, VST_EFFECT_OPCODE_VENDOR_NAME, 0, 0, b, 0);
        vendor = juce::String::fromUTF8 (b).trim();
        if (vendor.isEmpty()) vendor = "VST2";
    }

    static BusesProperties makeBuses (vst_effect_t* fx)
    {
        auto set = [] (int n) { return n == 1 ? juce::AudioChannelSet::mono() : n == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::discreteChannels (n); };
        BusesProperties b;
        if (fx->num_inputs > 0) b = b.withInput ("In", set (fx->num_inputs), true);
        b = b.withOutput ("Out", set (fx->num_outputs), true);
        return b;
    }

    void stopProcessing()
    {
        fx->control (fx, VST_EFFECT_OPCODE_PROCESS_END, 0, 0, nullptr, 0);
        fx->control (fx, VST_EFFECT_OPCODE_SUSPEND_RESUME, 0, 0, nullptr, 0);
        processing = false;
    }

    static double& loadingSampleRate() { static double r = 48000.0; return r; }
    static int& loadingBlockSize() { static int b = 512; return b; }

    // Transport info some plugins ask for (layout of the VST 2.x time info block)
    struct TimeInfo
    {
        double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
        int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
    };

    static intptr_t VST_FUNCTION_INTERFACE hostCallback (vst_effect_t* effect, int32_t opcode, int32_t index, int64_t value, const char* ptr, float opt)
    {
        juce::ignoreUnused (index, opt);
        auto* self = effect != nullptr ? static_cast<Vst2Plugin*> (effect->host_internal) : nullptr;
        switch (opcode)
        {
            case VST_HOST_OPCODE_VST_VERSION: return 2400;
            case VST_HOST_OPCODE_CURRENT_EFFECT_ID: return 0;
            case VST_HOST_OPCODE_KEEPALIVE_OR_IDLE: return 0;
            case 0x06: return 1; // wants MIDI
            case 0x07: // transport / time info
            {
                if (self == nullptr) return 0;
                auto& t = self->timeInfo;
                std::memset (&t, 0, sizeof (t));
                t.samplePos = (double) self->samplePos; t.sampleRate = self->rate; t.tempo = 120.0;
                t.ppqPos = t.samplePos / self->rate * 2.0;
                t.timeSigNumerator = 4; t.timeSigDenominator = 4;
                t.flags = (1 << 9) | (1 << 10) | (1 << 13); // ppq, tempo and time signature valid
                return (intptr_t) &t;
            }
            case VST_HOST_OPCODE_IO_MODIFIED: return 1;
            case VST_HOST_OPCODE_EDITOR_RESIZE:
                if (self != nullptr && self->onEditorResize) { const int w = index, h = (int) value; juce::MessageManager::callAsync ([self, w, h] { if (self->onEditorResize) self->onEditorResize (w, h); }); }
                return 1;
            case VST_HOST_OPCODE_GET_SAMPLE_RATE: return (intptr_t) (self != nullptr ? self->rate : loadingSampleRate());
            case VST_HOST_OPCODE_GET_BLOCK_SIZE: return (intptr_t) (self != nullptr ? self->block : loadingBlockSize());
            case VST_HOST_OPCODE_GET_ACTIVE_THREAD: // 1 = GUI, 2 = realtime audio, 4 = offline render
                if (juce::MessageManager::existsAndIsCurrentThread()) return 1;
                return self != nullptr && self->offline ? 4 : 2;
            case VST_HOST_OPCODE_VENDOR_NAME: if (ptr) std::strcpy ((char*) ptr, "Amari Labs"); return 1;
            case VST_HOST_OPCODE_PRODUCT_NAME: if (ptr) std::strcpy ((char*) ptr, "Amari Sampler"); return 1;
            case VST_HOST_OPCODE_VENDOR_VERSION: return 100;
            case VST_HOST_OPCODE_SUPPORTS:
            {
                if (ptr == nullptr) return 0;
                const juce::String s (ptr);
                for (auto* yes : { "sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "sizeWindow", "startStopProcess", "supplyIdle" })
                    if (s == yes) return 1;
                return 0;
            }
            case VST_HOST_OPCODE_LANGUAGE: return 1; // English
            case VST_HOST_OPCODE_GET_EFFECT_DIRECTORY: return self != nullptr ? (intptr_t) self->effectDirectory.toRawUTF8() : 0;
            default: return 0;
        }
    }

    std::unique_ptr<juce::DynamicLibrary> library;
    vst_effect_t* fx = nullptr;
    juce::String name, vendor, effectDirectory;
    double rate = 48000.0; int block = 512;
    bool processing = false, offline = false;
    juce::int64 samplePos = 0;
    TimeInfo timeInfo {};
    juce::AudioBuffer<float> ins, outs;
    juce::Array<vst_event_midi_t> midiEvents;
    juce::Array<vst_event_t*> eventPtrs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Vst2Plugin)
};

// ----------------------------------------------------------------------------
#if JUCE_WINDOWS
// Hosts the plugin's own window. The plugin draws into a child of this component's native window.
class Vst2Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Vst2Editor (Vst2Plugin& p) : AudioProcessorEditor (p), plugin (p)
    {
        setOpaque (true);
        setSize (640, 400);
        if (auto* r = getRect()) setPhysicalSize (r->right - r->left, r->bottom - r->top);
        plugin.onEditorResize = [this] (int w, int h) { setPhysicalSize (w, h); };
    }
    ~Vst2Editor() override
    {
        stopTimer();
        plugin.onEditorResize = nullptr;
        if (opened) plugin.getEffect()->control (plugin.getEffect(), VST_EFFECT_OPCODE_EDITOR_CLOSE, 0, 0, nullptr, 0);
        if (holder) DestroyWindow ((HWND) holder);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    void resized() override
    {
        if (holder) SetWindowPos ((HWND) holder, nullptr, 0, 0, juce::roundToInt (getWidth() * scale()), juce::roundToInt (getHeight() * scale()), SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }
    void parentHierarchyChanged() override { openIfReady(); }
    void visibilityChanged() override { openIfReady(); }

private:
    // plugin windows work in physical pixels, JUCE components in scaled (logical) pixels
    double scale() const
    {
        if (auto* p = getPeer()) return p->getPlatformScaleFactor();
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) return d->scale;
        return 1.0;
    }
    void setPhysicalSize (int w, int h) { const double s = scale(); setSize (juce::jmax (100, juce::roundToInt (w / s)), juce::jmax (60, juce::roundToInt (h / s))); }

    vst_rect_t* getRect()
    {
        vst_rect_t* r = nullptr;
        plugin.getEffect()->control (plugin.getEffect(), VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &r, 0);
        return r != nullptr && r->right > r->left && r->bottom > r->top ? r : nullptr;
    }

    void openIfReady()
    {
        if (opened || ! isShowing() || getPeer() == nullptr) return;
        auto parent = (HWND) getPeer()->getNativeHandle();
        const auto pos = getPeer()->getComponent().getLocalPoint (this, juce::Point<int>()) * getPeer()->getPlatformScaleFactor();
        holder = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                  pos.x, pos.y, juce::roundToInt (getWidth() * scale()), juce::roundToInt (getHeight() * scale()), parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (holder == nullptr) return;
        opened = true;
        plugin.getEffect()->control (plugin.getEffect(), VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, holder, 0);
        if (auto* r = getRect()) setPhysicalSize (r->right - r->left, r->bottom - r->top);
        resized();
        startTimerHz (30);
    }

    void timerCallback() override { plugin.getEffect()->control (plugin.getEffect(), VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, nullptr, 0); }

    Vst2Plugin& plugin;
    void* holder = nullptr;
    bool opened = false;
};
inline juce::AudioProcessorEditor* Vst2Plugin::createEditor() { return new Vst2Editor (*this); }
#else
inline juce::AudioProcessorEditor* Vst2Plugin::createEditor() { return nullptr; }
#endif
