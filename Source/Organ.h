#pragma once
#include "DspCommon.h"

// Tonewheel-style drawbar organ (no samples) + scanner vibrato/chorus + drive
struct OrganEngine
{
    struct Settings
    {
        float drawbar[9] {};     // linear gains
        int model = 0;           // 0 B3, 1 VOX, 2 FARF, 3 PIPE
        bool percOn = true, perc3rd = true, percFast = true, percSoft = false;
        float click = 0.4f;
        int vibMode = 6;         // 0 off, 1-3 V1-V3, 4-6 C1-C3
        float drive = 0.25f;
    } s;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        vib.init ((int) (sr * 0.02) + 4);
        for (auto& n : notes) n = Note {};
        held = 0;
        clickFilter.set (2800.0f, 0.9f, sr);
    }

    void noteOn (int note, float /*velocity*/)
    {
        auto& n = notes[note];
        if (! n.on) ++held;
        const bool percTrigger = s.percOn && held == 1; // single-trigger percussion (only with no other keys held)
        n.on = true;
        n.clickEnv = 1.0f;
        if (percTrigger) n.percEnv = 1.0f;
        const double f = dspc::mtof (note);
        static const double ratios[9] = { 0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };
        for (int i = 0; i < 9; ++i)
        {
            double fr = f * ratios[i];
            while (fr > 5900.0) fr *= 0.5; // tonewheel foldback at the top
            n.inc[i] = fr / sr;
        }
        double pf = f * (s.perc3rd ? 3.0 : 2.0);
        while (pf > 5900.0) pf *= 0.5;
        n.percInc = pf / sr;
    }

    void noteOff (int note)
    {
        auto& n = notes[note];
        if (n.on) { n.on = false; held = std::max (0, held - 1); }
    }

    void allOff() { for (auto& n : notes) { n.on = false; n.gate = 0.0f; } held = 0; }

    // renders mono into out (added), n samples
    void render (float* out, int num)
    {
        const auto& sine = dspc::SineTable::get();
        const bool pipe = s.model == 3;
        const float att = (float) (1.0 / (sr * (pipe ? 0.06 : 0.0015)));
        const float rel = (float) (1.0 / (sr * (pipe ? 0.15 : 0.008)));
        const float percDecay = (float) std::exp (-1.0 / (sr * (s.percFast ? 0.11 : 0.42)));
        const float clickDecay = (float) std::exp (-1.0 / (sr * 0.0025));
        const float percLevel = s.percSoft ? 0.35f : 0.75f;
        const float clickLevel = pipe ? 0.0f : s.click * 0.5f;

        for (int ni = 0; ni < 128; ++ni)
        {
            auto& n = notes[ni];
            if (! n.on && n.gate <= 0.0f) continue;
            for (int i = 0; i < num; ++i)
            {
                if (n.on) n.gate = std::min (1.0f, n.gate + att);
                else      n.gate = std::max (0.0f, n.gate - rel);

                float sum = 0.0f;
                for (int d = 0; d < 9; ++d)
                {
                    const float g = s.drawbar[d];
                    if (g > 0.0f)
                    {
                        float w = sine (n.ph[d]);
                        if (s.model == 1)      w = std::tanh (2.2f * w) * 0.8f;          // transistor, squarer
                        else if (s.model == 2) w = std::tanh (5.0f * w) * 0.62f;         // bright combo organ
                        else if (pipe)         w = w + 0.12f * sine (dspc::wrap (n.ph[d] * 2.0));
                        sum += g * w;
                    }
                    n.ph[d] += n.inc[d];
                    if (n.ph[d] >= 1.0) n.ph[d] -= 1.0;
                }
                if (n.percEnv > 0.0005f)
                {
                    sum += percLevel * n.percEnv * sine (n.percPh);
                    n.percEnv *= percDecay;
                }
                n.percPh += n.percInc; if (n.percPh >= 1.0) n.percPh -= 1.0;

                float click = 0.0f;
                if (n.clickEnv > 0.001f)
                {
                    click = clickFilter.process (noise.next(), 2) * n.clickEnv * clickLevel;
                    n.clickEnv *= clickDecay;
                }
                if (pipe) sum += noise.next() * 0.006f * n.gate; // a little air
                out[i] += (sum * n.gate + click) * 0.085f;
            }
            if (n.gate <= 0.0f) n.percEnv = 0.0f;
        }

        // scanner vibrato / chorus
        if (s.vibMode > 0)
        {
            static const float depthsMs[4] = { 0.0f, 0.22f, 0.45f, 0.85f };
            const int depthIdx = s.vibMode <= 3 ? s.vibMode : s.vibMode - 3;
            const bool chorus = s.vibMode >= 4;
            const float depth = (float) (depthsMs[depthIdx] * 0.001 * sr);
            const float base = (float) (0.0015 * sr);
            const double inc = 6.86 / sr;
            for (int i = 0; i < num; ++i)
            {
                vib.push (out[i]);
                const float d = base + depth * (float) std::sin (dspc::twoPi * vibPh);
                vibPh += inc; if (vibPh >= 1.0) vibPh -= 1.0;
                const float wet = vib.read (d);
                out[i] = chorus ? 0.62f * (out[i] + wet) : wet;
            }
        }

        if (s.drive > 0.001f)
        {
            const float g = 1.0f + s.drive * 9.0f;
            const float norm = 1.0f / std::pow (g, 0.65f);
            for (int i = 0; i < num; ++i) out[i] = std::tanh (out[i] * g) * norm;
        }
    }

private:
    struct Note
    {
        bool on = false;
        float gate = 0.0f, percEnv = 0.0f, clickEnv = 0.0f;
        double ph[9] {}, inc[9] {}, percPh = 0.0, percInc = 0.0;
    };
    Note notes[128];
    int held = 0;
    double sr = 48000.0, vibPh = 0.0;
    dspc::DelayLine vib;
    dspc::SVF clickFilter;
    dspc::Noise noise;
};

// Rotary speaker: horn (treble) + drum (bass) with inertia, doppler and AM, stereo mics
struct RotarySpeaker
{
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        hornL.init ((int) (sr * 0.01) + 4);
        hornR.init ((int) (sr * 0.01) + 4);
        lp.set (800.0f, 0.707f, sr);
        hp.set (800.0f, 0.707f, sr);
        lp.reset(); hp.reset();
    }
    void setSpeed (int mode) // 0 slow 1 fast 2 stop
    {
        hornTarget = mode == 0 ? 0.83 : mode == 1 ? 6.7 : 0.0;
        drumTarget = mode == 0 ? 0.67 : mode == 1 ? 5.8 : 0.0;
    }
    void process (const float* in, float* outL, float* outR, int num)
    {
        const double hornCoef = 1.0 - std::exp (-1.0 / (sr * 0.7));
        const double drumCoef = 1.0 - std::exp (-1.0 / (sr * 3.8));
        const float base = (float) (0.0028 * sr), depth = (float) (0.0011 * sr);
        for (int i = 0; i < num; ++i)
        {
            hornHz += (hornTarget - hornHz) * hornCoef;
            drumHz += (drumTarget - drumHz) * drumCoef;
            hornPh = dspc::wrap (hornPh + hornHz / sr);
            drumPh = dspc::wrap (drumPh + drumHz / sr);

            const float x = in[i];
            const float lo = lp.process (x, 0);
            const float hi = hp.process (x, 1);

            hornL.push (hi); hornR.push (hi);
            const float sL = (float) std::sin (dspc::twoPi * hornPh);
            const float sR = (float) std::sin (dspc::twoPi * (hornPh + 0.3));
            const float hL = hornL.read (base + depth * sL) * (0.72f + 0.28f * sL);
            const float hR = hornR.read (base + depth * sR) * (0.72f + 0.28f * sR);

            const float dL = (float) std::sin (dspc::twoPi * drumPh);
            const float dR = (float) std::sin (dspc::twoPi * (drumPh + 0.3));
            outL[i] = hL + lo * (0.85f + 0.15f * dL);
            outR[i] = hR + lo * (0.85f + 0.15f * dR);
        }
    }
private:
    double sr = 48000.0, hornHz = 0.83, drumHz = 0.67, hornTarget = 0.83, drumTarget = 0.67, hornPh = 0.0, drumPh = 0.25;
    dspc::DelayLine hornL, hornR;
    dspc::SVF lp, hp;
};
