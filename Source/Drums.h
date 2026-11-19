#pragma once
#include "DspCommon.h"

// 16 synthesised drum sounds for the pads (no samples)
struct DrumEngine
{
    static constexpr int numPads = 16;
    static const char* name (int i)
    {
        static const char* n[numPads] = { "KICK", "SNARE", "CLAP", "RIM", "HH CL", "HH OP", "TOM HI", "TOM LO",
                                          "CRASH", "RIDE", "SHAKER", "COWBELL", "808 KICK", "SNARE 2", "CONGA", "ZAP" };
        return n[i];
    }

    void prepare (double sampleRate) { sr = sampleRate; for (auto& v : v_) v.active = false; }

    void trigger (int pad, float vel)
    {
        if (pad < 0 || pad >= numPads) return;
        if (pad == 4) v_[5].active = false; // closed hat chokes open hat
        auto& v = v_[pad];
        v = Voice {};
        v.active = true; v.vel = vel;
        static const float pans[numPads] = { 0, 0.05f, -0.1f, 0.15f, 0.3f, 0.3f, -0.25f, 0.25f,
                                             -0.35f, 0.35f, 0.4f, -0.3f, 0, -0.05f, 0.2f, 0 };
        v.pan = pans[pad];
        switch (pad)
        {
            case 1: case 13: v.f1.set (pad == 1 ? 3200.0f : 2400.0f, 0.8f, sr); break;
            case 2: v.f1.set (1200.0f, 1.6f, sr); break;
            case 3: v.f1.set (2000.0f, 2.0f, sr); break;
            case 4: case 5: v.f1.set (7500.0f, 0.7f, sr); break;
            case 8: v.f1.set (5000.0f, 0.6f, sr); break;
            case 9: v.f1.set (6200.0f, 1.2f, sr); break;
            case 10: v.f1.set (6500.0f, 0.8f, sr); break;
            case 11: v.f1.set (1000.0f, 2.5f, sr); break;
            default: break;
        }
    }

    void allOff() { for (auto& v : v_) v.active = false; }

    void render (float* outL, float* outR, int num)
    {
        const double dt = 1.0 / sr;
        for (int pad = 0; pad < numPads; ++pad)
        {
            auto& v = v_[pad];
            if (! v.active) continue;
            const float gl = std::sqrt (0.5f - 0.5f * v.pan), gr = std::sqrt (0.5f + 0.5f * v.pan);
            for (int i = 0; i < num; ++i)
            {
                const float t = v.t;
                float y = 0.0f, env = 0.0f;
                switch (pad)
                {
                    case 0: { const double f = 48.0 + 130.0 * std::exp (-t / 0.03f); y = osc (v, f) + noise.next() * 0.35f * std::exp (-t / 0.002f); env = std::exp (-t / 0.32f); break; }
                    case 1: { y = 0.45f * (osc (v, 185.0) + 0.6f * osc2 (v, 330.0)) * std::exp (-t / 0.07f) + 0.9f * v.f1.process (noise.next(), 2) * 2.2f * std::exp (-t / 0.15f); env = 1.0f; if (t > 1.2f) v.active = false; break; }
                    case 2:
                    {
                        float e = 0.0f;
                        for (int b = 0; b < 3; ++b) { const float tb = t - b * 0.011f; if (tb >= 0.0f) e = std::max (e, std::exp (-tb / 0.006f)); }
                        if (t > 0.022f) e = std::max (e, 0.7f * std::exp (-(t - 0.022f) / 0.13f));
                        y = v.f1.process (noise.next(), 2) * 3.0f; env = e; break;
                    }
                    case 3: { y = osc (v, 1700.0) * 0.6f + v.f1.process (noise.next(), 2) * 1.5f; env = std::exp (-t / 0.018f); break; }
                    case 4: case 5: { y = v.f1.process (metal (v), 1) * 0.9f; env = std::exp (-t / (pad == 4 ? 0.045f : 0.38f)); break; }
                    case 6: { y = osc (v, 210.0 + 90.0 * std::exp (-t / 0.05f)); env = std::exp (-t / 0.24f); break; }
                    case 7: { y = osc (v, 120.0 + 60.0 * std::exp (-t / 0.06f)); env = std::exp (-t / 0.34f); break; }
                    case 8: { y = v.f1.process (metal (v) * 0.6f + noise.next() * 0.7f, 1); env = std::exp (-t / 1.3f) * (0.6f + 0.4f * std::exp (-t / 0.05f)); break; }
                    case 9: { y = v.f1.process (metal (v), 2) * 1.6f + 0.2f * osc (v, 2950.0) ; env = std::exp (-t / 1.1f); break; }
                    case 10: { const float a = std::min (1.0f, t / 0.015f); y = v.f1.process (noise.next(), 1) * 0.8f; env = a * std::exp (-std::max (0.0f, t - 0.015f) / 0.06f); break; }
                    case 11: { float sq = (v.ph < 0.5 ? 1.0f : -1.0f) + (v.ph2 < 0.5 ? 1.0f : -1.0f); v.ph = dspc::wrap (v.ph + 540.0 * dt); v.ph2 = dspc::wrap (v.ph2 + 800.0 * dt); y = v.f1.process (sq, 2) * 0.9f; env = std::exp (-t / 0.11f); break; }
                    case 12: { const double f = 44.0 + 55.0 * std::exp (-t / 0.05f); y = std::tanh (osc (v, f) * 1.6f); env = std::exp (-t / 0.95f); break; }
                    case 13: { y = v.f1.process (noise.next(), 2) * 2.4f * std::exp (-t / 0.22f) + 0.3f * osc (v, 200.0) * std::exp (-t / 0.05f); env = 1.0f; if (t > 1.5f) v.active = false; break; }
                    case 14: { y = osc (v, 330.0 + 70.0 * std::exp (-t / 0.012f)); env = std::exp (-t / 0.17f); break; }
                    default: { y = osc (v, 80.0 + 2000.0 * std::exp (-t / 0.035f)); env = std::exp (-t / 0.22f); break; }
                }
                const float out = y * env * v.vel * 0.55f;
                outL[i] += out * gl; outR[i] += out * gr;
                v.t += (float) dt;
                if (env < 0.0002f && t > 0.03f) { v.active = false; break; }
            }
        }
    }

private:
    struct Voice
    {
        bool active = false; float vel = 1.0f, t = 0.0f, pan = 0.0f;
        double ph = 0.0, ph2 = 0.0, mph[6] {};
        dspc::SVF f1;
    };
    inline float osc (Voice& v, double f) { const float y = dspc::SineTable::get() (v.ph); v.ph = dspc::wrap (v.ph + f / sr); return y; }
    inline float osc2 (Voice& v, double f) { const float y = dspc::SineTable::get() (v.ph2); v.ph2 = dspc::wrap (v.ph2 + f / sr); return y; }
    inline float metal (Voice& v) // 808-style six detuned square oscillators
    {
        static const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        float s = 0.0f;
        for (int k = 0; k < 6; ++k) { s += v.mph[k] < 0.5 ? 1.0f : -1.0f; v.mph[k] = dspc::wrap (v.mph[k] + fr[k] / sr); }
        return s * 0.18f;
    }
    Voice v_[numPads];
    dspc::Noise noise;
    double sr = 48000.0;
};
