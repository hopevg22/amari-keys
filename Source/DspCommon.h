#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

namespace dspc
{
constexpr double twoPi = 6.283185307179586;

// 4096-point sine table with linear interpolation; phase in [0,1)
struct SineTable
{
    static constexpr int N = 4096;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = (float) std::sin (twoPi * i / N); }
    inline float operator() (double phase) const
    {
        double x = phase * N;
        int i = (int) x;
        float f = (float) (x - i);
        i &= (N - 1);
        return t[i] + f * (t[i + 1] - t[i]);
    }
    static const SineTable& get() { static SineTable s; return s; }
};

inline double wrap (double p) { return p - std::floor (p); }

inline float polyBlep (double t, double dt)
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

// band-limited basic waveforms. wave: 0 saw, 1 square, 2 triangle, 3 sine
struct Osc
{
    double phase = 0.0, tri = 0.0;
    inline float next (int wave, double freq, double sr)
    {
        const double dt = std::min (0.49, freq / sr);
        float out = 0.0f;
        switch (wave)
        {
            case 0: out = (float) (2.0 * phase - 1.0) - polyBlep (phase, dt); break;
            case 1:
            case 2:
            {
                float sq = phase < 0.5 ? 1.0f : -1.0f;
                sq += polyBlep (phase, dt);
                sq -= polyBlep (wrap (phase + 0.5), dt);
                if (wave == 1) { out = sq; break; }
                tri = (tri + 4.0 * dt * sq) * 0.9995; // leaky integrator -> triangle
                out = (float) tri;
                break;
            }
            default: out = SineTable::get() (phase); break;
        }
        phase += dt;
        if (phase >= 1.0) phase -= 1.0;
        return out;
    }
};

// TPT state-variable filter (Zavalishin). mode 0 LP, 1 HP, 2 BP
struct SVF
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float cutoff, float q, double sr)
    {
        cutoff = std::clamp (cutoff, 20.0f, (float) (sr * 0.45));
        g = (float) std::tan (3.141592653589793 * cutoff / sr);
        k = 1.0f / std::max (0.5f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline float process (float v0, int mode)
    {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        if (mode == 0) return v2;
        if (mode == 1) return v0 - k * v1 - v2;
        return v1;
    }
    void reset() { ic1 = ic2 = 0; }
};

struct Noise
{
    uint32_t s = 22222;
    inline float next() { s = s * 1664525u + 1013904223u; return (float) ((int32_t) s) * (1.0f / 2147483648.0f); }
};

inline double mtof (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

// simple fractional delay line
struct DelayLine
{
    std::vector<float> buf; int w = 0;
    void init (int size) { buf.assign ((size_t) size, 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
    inline float read (float delaySamples) const // delay >= 1
    {
        const int n = (int) buf.size();
        float rp = (float) w - delaySamples;
        while (rp < 0) rp += (float) n;
        int i0 = (int) rp; float f = rp - (float) i0;
        int i1 = i0 + 1; if (i1 >= n) i1 -= n;
        return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
};
} // namespace dspc
