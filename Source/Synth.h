#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "DspCommon.h"

// Virtual-analog subtractive synth: 2 osc + sub, multimode filter, 2 ADSRs, LFO, poly/mono/legato + glide
struct SynthEngine
{
    struct Settings
    {
        int voiceMode = 0, wave1 = 0, wave2 = 0, filterType = 0;
        float oscMix = 0.5f, detune = 0.3f, sub = 0.0f, glide = 0.0f;
        float cutoff = 2500.0f, reso = 0.25f, envAmt = 0.35f, keyTrack = 0.4f, drive = 0.1f;
        juce::ADSR::Parameters fenv { 0.4f, 1.2f, 0.4f, 1.5f }, aenv { 0.6f, 1.0f, 0.85f, 1.8f };
        float lfoRate = 0.4f, lfoAmt = 0.15f;
        float pitchBend = 0.0f; // semitones
    } s;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) { v = Voice {}; v.amp.setSampleRate (sr); v.filt.setSampleRate (sr); }
        stackSize = 0;
    }

    void noteOn (int note, float vel)
    {
        if (s.voiceMode == 0)
        {
            Voice* v = nullptr;
            for (auto& x : voices) if (x.note == note && x.amp.isActive()) { v = &x; break; }
            if (! v) for (auto& x : voices) if (! x.amp.isActive()) { v = &x; break; }
            if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age > v->age) v = &x; }
            start (*v, note, vel, true, false);
            return;
        }
        // mono / legato: voice 0, note stack
        removeFromStack (note);
        if (stackSize < 32) stack[stackSize++] = note;
        auto& v = voices[0];
        const bool legato = s.voiceMode == 2 && v.gateOn;
        start (v, note, vel, ! legato, true);
    }

    // maxRelease > 0 shortens the amp release for this note (sustain pedal lifted)
    void noteOff (int note, float maxRelease = -1.0f)
    {
        if (s.voiceMode == 0)
        {
            for (auto& v : voices) if (v.note == note && v.gateOn) { v.relCap = maxRelease; release (v); }
            return;
        }
        removeFromStack (note);
        auto& v = voices[0];
        if (v.note != note) return;
        if (stackSize > 0) start (v, stack[stackSize - 1], v.vel, s.voiceMode == 1, true);
        else release (v);
    }

    void allOff() { for (auto& v : voices) { v.amp.reset(); v.filt.reset(); v.gateOn = false; } stackSize = 0; }

    void render (float* outL, float* outR, int num)
    {
        for (auto& v : voices)
        {
            auto a = s.aenv;
            if (v.relCap > 0.0f && ! v.gateOn) a.release = juce::jmin (a.release, v.relCap);
            v.amp.setParameters (a); v.filt.setParameters (s.fenv);
        }
        const float q = 0.55f + s.reso * 9.0f;
        const double glideCoef = s.glide < 0.001f ? 1.0 : 1.0 - std::exp (-1.0 / (sr * (0.005 + s.glide * 0.6)));
        const double detuneRatio = std::pow (2.0, (s.detune * 25.0) / 1200.0);
        const double bend = std::pow (2.0, s.pitchBend / 12.0);
        const float driveGain = 1.0f + s.drive * 5.0f;

        // LFO is global
        float lfoBuf[512];
        for (int start = 0; start < num; start += 512)
        {
            const int n = std::min (512, num - start);
            for (int i = 0; i < n; ++i)
            {
                lfoBuf[i] = (float) std::sin (dspc::twoPi * lfoPh);
                lfoPh += s.lfoRate / sr; if (lfoPh >= 1.0) lfoPh -= 1.0;
            }
            for (auto& v : voices)
            {
                if (! v.amp.isActive()) continue;
                for (int i = 0; i < n; ++i)
                {
                    v.freq += (v.targetFreq - v.freq) * glideCoef;
                    const double f = v.freq * bend;
                    const float o1 = v.o1.next (s.wave1, f / std::sqrt (detuneRatio), sr);
                    const float o2 = v.o2.next (s.wave2, f * std::sqrt (detuneRatio), sr);
                    const float sub = s.sub > 0.0f ? v.sub.next (1, f * 0.5, sr) * s.sub : 0.0f;
                    float x = (1.0f - s.oscMix) * o1 + s.oscMix * o2 + sub;
                    x = std::tanh (x * driveGain * 0.7f);

                    const float fe = v.filt.getNextSample();
                    if ((v.counter++ & 7) == 0)
                    {
                        const float oct = s.envAmt * fe * 6.0f + s.lfoAmt * lfoBuf[i] * 2.0f
                                        + s.keyTrack * (float) (v.note - 60) / 12.0f;
                        const float cut = s.cutoff * std::pow (2.0f, oct);
                        if (s.filterType == 0) { v.f1.set (cut, 0.54f, sr); v.f2.set (cut, q, sr); }
                        else v.f1.set (cut, q, sr);
                    }
                    float y;
                    switch (s.filterType)
                    {
                        case 0:  y = v.f2.process (v.f1.process (x, 0), 0); break;
                        case 1:  y = v.f1.process (x, 0); break;
                        case 2:  y = v.f1.process (x, 1); break;
                        default: y = v.f1.process (x, 2); break;
                    }
                    const float a = v.amp.getNextSample() * (0.35f + 0.65f * v.vel) * 0.38f;
                    outL[start + i] += y * a;
                    outR[start + i] += y * a;
                }
                ++v.age;
            }
        }
    }

private:
    struct Voice
    {
        int note = -1; float vel = 0.0f; bool gateOn = false; float relCap = -1.0f;
        double freq = 440.0, targetFreq = 440.0;
        dspc::Osc o1, o2, sub;
        dspc::SVF f1, f2;
        juce::ADSR amp, filt;
        uint32_t counter = 0, age = 0;
    };

    void start (Voice& v, int note, float vel, bool retrigger, bool monoGlide)
    {
        const double f = dspc::mtof (note);
        const bool wasActive = v.amp.isActive();
        v.note = note; v.vel = vel; v.targetFreq = f; v.gateOn = true; v.age = 0; v.relCap = -1.0f;
        if (! monoGlide || ! wasActive || s.glide < 0.001f) v.freq = f;
        if (retrigger || ! wasActive)
        {
            if (! wasActive) { v.f1.reset(); v.f2.reset(); }
            v.amp.noteOn(); v.filt.noteOn();
        }
    }
    void release (Voice& v)
    {
        v.gateOn = false;
        if (v.relCap > 0.0f) { auto a = s.aenv; a.release = juce::jmin (a.release, v.relCap); v.amp.setParameters (a); }
        v.amp.noteOff(); v.filt.noteOff();
    }
    void removeFromStack (int note)
    {
        int w = 0;
        for (int i = 0; i < stackSize; ++i) if (stack[i] != note) stack[w++] = stack[i];
        stackSize = w;
    }

    Voice voices[12];
    int stack[32] {}; int stackSize = 0;
    double sr = 48000.0, lfoPh = 0.0;
};
