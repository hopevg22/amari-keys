#pragma once
#include "DspCommon.h"

// Sample-free electric keys: FM tine EP, reed EP, clav, FM bell
struct KeysEngine
{
    struct Settings
    {
        int model = 0;       // 0 tine, 1 reed, 2 clav, 3 bell
        float timbre = 1.0f; // 0.6 soft, 1 mid, 1.5 bright
        float dynamics = 0.6f, decay = 0.5f, tremolo = 0.0f;
        double pitchMul = 1.0; // pitch bend + vibrato
    } s;

    void prepare (double sampleRate) { sr = sampleRate; for (auto& v : voices) v.active = false; dcL = dcR = 0; }

    void noteOn (int note, float vel)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (x.active && x.note == note) { v = &x; break; }
        if (! v) for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age > v->age) v = &x; }
        *v = Voice {};
        v->active = true; v->note = note; v->vel = vel; v->model = s.model;
        v->freq = dspc::mtof (note);
        const double keyScale = std::clamp (std::pow (2.0, (60.0 - note) / 24.0), 0.35, 2.5);
        double baseDecay = 0.8 + 3.4 * s.decay;
        if (s.model == 1) baseDecay *= 0.6;
        if (s.model == 2) baseDecay *= 0.45;
        if (s.model == 3) baseDecay *= 1.2;
        v->decayCoef = (float) std::exp (-1.0 / (sr * baseDecay * keyScale));
        v->pan = (float) std::clamp ((note - 64) / 60.0, -0.6, 0.6);
        v->clavFilter.reset();
    }

    void noteOff (int note)
    {
        for (auto& v : voices) if (v.active && v.note == note && ! v.released) v.released = true;
    }

    void allOff() { for (auto& v : voices) v.active = false; }

    void render (float* outL, float* outR, int num)
    {
        const auto& sine = dspc::SineTable::get();
        const float dyn = s.dynamics;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const float velAmp = (1.0f - dyn) + dyn * v.vel;
            const float velIdx = 0.25f + 0.75f * v.vel;
            const float relCoef = (float) std::exp (-1.0 / (sr * (v.model == 2 ? 0.035 : v.model == 3 ? 0.4 : 0.11)));
            const double f = v.freq * s.pitchMul, dt = 1.0 / sr;
            const float gl = std::sqrt (0.5f - 0.5f * v.pan), gr = std::sqrt (0.5f + 0.5f * v.pan);
            for (int i = 0; i < num; ++i)
            {
                const float t = v.t;
                float y = 0.0f;
                switch (v.model)
                {
                    case 0: // tine EP
                    {
                        const float idx = (0.25f + 2.1f * velIdx * s.timbre) * (0.3f + 0.7f * std::exp (-t / 0.45f));
                        y = sine (dspc::wrap (v.cp + idx * sine (v.mp) * 0.159f));
                        const float tineIdx = 1.4f * velIdx * s.timbre * std::exp (-t / 0.03f);
                        y += 0.32f * velIdx * std::exp (-t / 0.14f) * sine (dspc::wrap (v.bp + tineIdx * sine (v.bmp) * 0.159f));
                        v.cp = dspc::wrap (v.cp + f * dt);  v.mp = dspc::wrap (v.mp + f * dt);
                        v.bp = dspc::wrap (v.bp + f * dt * 1.003); v.bmp = dspc::wrap (v.bmp + f * 14.0 * dt);
                        break;
                    }
                    case 1: // reed EP
                    {
                        const float idx = (0.2f + 1.5f * velIdx * s.timbre) * (0.45f + 0.55f * std::exp (-t / 0.3f));
                        float x = sine (dspc::wrap (v.cp + idx * sine (v.mp) * 0.159f));
                        y = std::tanh (1.7f * x + 0.55f * velIdx * s.timbre * x * x) * 0.85f;
                        v.cp = dspc::wrap (v.cp + f * dt); v.mp = dspc::wrap (v.mp + f * dt);
                        break;
                    }
                    case 2: // clav: narrow pulse into a plucked resonant lowpass
                    {
                        const double dtp = std::min (0.49, f * dt);
                        const double pw = 0.22;
                        float p = v.cp < pw ? 1.0f : -1.0f;
                        p += dspc::polyBlep (v.cp, dtp);
                        p -= dspc::polyBlep (dspc::wrap (v.cp + 1.0 - pw), dtp);
                        p -= (float) (2.0 * pw - 1.0);
                        if ((v.sampleCount & 15) == 0)
                        {
                            const float cut = (float) (f * (1.5 + 16.0 * velIdx * s.timbre * std::exp (-t / 0.16f)) + 300.0);
                            v.clavFilter.set (cut, 2.2f, sr);
                        }
                        y = v.clavFilter.process (p, 0) * 0.55f;
                        v.cp = dspc::wrap (v.cp + f * dt);
                        break;
                    }
                    default: // FM bell
                    {
                        const float idx = 3.2f * velIdx * s.timbre * std::exp (-t / 0.9f);
                        y = sine (dspc::wrap (v.cp + idx * sine (v.mp) * 0.159f));
                        y += 0.25f * std::exp (-t / 0.4f) * sine (dspc::wrap (v.bp));
                        v.cp = dspc::wrap (v.cp + f * dt); v.mp = dspc::wrap (v.mp + f * 3.5 * dt);
                        v.bp = dspc::wrap (v.bp + f * 4.02 * dt);
                        break;
                    }
                }
                // amp envelope: 2 ms attack, exponential decay, damper on release
                v.att = std::min (1.0f, v.att + (float) (1.0 / (sr * 0.002)));
                v.env *= v.decayCoef;
                if (v.released) v.rel *= relCoef;
                const float a = v.att * v.env * v.rel * velAmp * 0.28f;
                outL[i] += y * a * gl;
                outR[i] += y * a * gr;
                v.t += (float) dt;
                ++v.sampleCount;
            }
            if (v.rel < 0.0003f || v.env < 0.0003f) v.active = false;
            ++v.age;
        }

        // DC block + stereo tremolo (auto-pan)
        const double tInc = 4.6 / sr;
        for (int i = 0; i < num; ++i)
        {
            float l = outL[i], r = outR[i];
            const float yl = l - dcLx + 0.995f * dcL; dcLx = l; dcL = yl;
            const float yr = r - dcRx + 0.995f * dcR; dcRx = r; dcR = yr;
            l = yl; r = yr;
            if (s.tremolo > 0.001f)
            {
                const float m = (float) std::sin (dspc::twoPi * tremPh);
                l *= 1.0f - s.tremolo * (0.5f + 0.5f * m);
                r *= 1.0f - s.tremolo * (0.5f - 0.5f * m);
            }
            tremPh += tInc; if (tremPh >= 1.0) tremPh -= 1.0;
            outL[i] = l; outR[i] = r;
        }
    }

private:
    struct Voice
    {
        bool active = false, released = false;
        int note = 0, model = 0; float vel = 0.0f, pan = 0.0f;
        double freq = 440.0, cp = 0, mp = 0, bp = 0, bmp = 0;
        float t = 0.0f, att = 0.0f, env = 1.0f, rel = 1.0f, decayCoef = 0.9999f;
        uint32_t sampleCount = 0, age = 0;
        dspc::SVF clavFilter;
    };
    Voice voices[24];
    double sr = 48000.0, tremPh = 0.0;
    float dcL = 0, dcR = 0, dcLx = 0, dcRx = 0;
};
