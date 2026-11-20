// Tiny VST2 instrument used only to test Amari Sampler's VST2 host (built as a shared library).
#define __cdecl
#include "../AmariSampler/external/vst2sdk/vst.h"
#include <cmath>
#include <cstring>
#include <cstdint>

struct Synth
{
    vst_effect_t fx {};
    vst_host_callback_t host = nullptr;
    float sr = 44100.0f;
    struct V { int note = -1; double ph = 0, f = 0; float amp = 0, env = 0; bool rel = false; int start = 0; } v[16];
    bool sawTime = false;
};

// real VST 2.4 event list layout: count, pointer-sized reserved, inline pointer array
struct Events { int32_t count; intptr_t reserved; vst_event_t* events[1]; };

static intptr_t VST_FUNCTION_INTERFACE control (vst_effect_t* e, int32_t op, int32_t, intptr_t value, void* ptr, float opt)
{
    auto* s = (Synth*) e->effect_internal;
    switch (op)
    {
        case VST_EFFECT_OPCODE_SET_SAMPLE_RATE: s->sr = opt; return 0;
        case VST_EFFECT_OPCODE_EFFECT_NAME: std::strcpy ((char*) ptr, "Test VST2 Synth"); return 1;
        case VST_EFFECT_OPCODE_VENDOR_NAME: std::strcpy ((char*) ptr, "Amari Test"); return 1;
        case VST_EFFECT_OPCODE_EVENT:
        {
            auto* ev = (Events*) ptr;
            for (int i = 0; i < ev->count; ++i)
            {
                auto* m = (vst_event_midi_t*) ev->events[i];
                if (m->event.type != VST_EVENT_TYPE_MIDI) continue;
                const int st = (uint8_t) m->midi.data[0] & 0xf0, n = (uint8_t) m->midi.data[1], vel = (uint8_t) m->midi.data[2];
                if (st == 0x90 && vel > 0)
                    for (auto& x : s->v) if (x.note < 0) { x = {}; x.note = n; x.f = 440.0 * std::pow (2.0, (n - 69) / 12.0); x.amp = vel / 127.0f * 0.5f; x.env = 1; x.start = m->event.offset; break; }
                if (st == 0x80 || (st == 0x90 && vel == 0)) for (auto& x : s->v) if (x.note == n) x.rel = true;
            }
            return 1;
        }
        case VST_EFFECT_OPCODE_DESTROY: delete s; return 0;
        default: return 0;
    }
}

static void VST_FUNCTION_INTERFACE process (vst_effect_t* e, const float* const*, float** out, int32_t n)
{
    auto* s = (Synth*) e->effect_internal;
    if (! s->sawTime && s->host) s->sawTime = s->host (e, 0x07, 0, 0, nullptr, 0) != 0; // ask for transport like real synths do
    std::memset (out[0], 0, sizeof (float) * (size_t) n); std::memset (out[1], 0, sizeof (float) * (size_t) n);
    for (auto& x : s->v)
    {
        if (x.note < 0) continue;
        for (int i = x.start; i < n; ++i)
        {
            float smp = 0; for (int h = 1; h <= 5; ++h) if (h * x.f < s->sr * 0.45) smp += (float) (std::sin (x.ph * h) / h);
            smp *= x.amp * x.env; x.env *= x.rel ? 0.999f : 0.99998f;
            x.ph += 6.283185307 * x.f / s->sr;
            out[0][i] += smp; out[1][i] += smp;
        }
        x.start = 0;
        if (x.env < 1e-5f) x.note = -1;
    }
}
static void VST_FUNCTION_INTERFACE setp (vst_effect_t*, uint32_t, float) {}
static float VST_FUNCTION_INTERFACE getp (vst_effect_t*, uint32_t) { return 0; }

extern "C" __attribute__ ((visibility ("default"))) vst_effect_t* VSTPluginMain (vst_host_callback_t cb)
{
    if (cb (nullptr, VST_HOST_OPCODE_VST_VERSION, 0, 0, nullptr, 0) < 2400) return nullptr;
    auto* s = new Synth(); s->host = cb;
    auto& f = s->fx;
    f.magic_number = (int32_t) VST_MAGICNUMBER; f.control = control; f.process = process; f.process_float = process;
    f.set_parameter = setp; f.get_parameter = getp; f.num_programs = 1; f.num_outputs = 2;
    f.flags = VST_EFFECT_FLAG_INSTRUMENT | VST_EFFECT_FLAG_SUPPORTS_FLOAT; f.effect_internal = s; f.version = 1;
    return &f;
}
