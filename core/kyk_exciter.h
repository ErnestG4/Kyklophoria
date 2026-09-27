/* kyk_exciter.h — exciters coupled to the modes (docs/exciters.md).
 *
 * A prototype: not wired into the engine. The first exciter is the bow,
 * because it is the one nothing feed-forward can do — its force depends on
 * the string's velocity under it, every sample. The loop:
 *
 *   contact displacement  x_c = sum_k w_k y_k        (w_k: mode k's shape at
 *   contact velocity      v_c = (x_c - x_c') sr       the contact, sin(pi p h))
 *   force                 F   = f_N mu(v_bow - v_c)
 *   into each mode        y_k += w_k F dt^2 / m       (a unit of force for a
 *                                                     sample, on modal mass m)
 *
 * The same weights read and write, which keeps the pair passive: friction
 * can only put in what the bow's own motion supplies. The bank runs
 * sample-outer here (each sample needs every mode before the force), which is
 * why only a voice being bowed would run this way (docs/exciters.md, cost).
 *
 * Header-only, no heap, Init() in place of constructors, as core/. */
#pragma once
#include <cmath>
#include "kyk_resonate.h"

namespace kyk {

/* friction on the slip velocity: static mu_s at no slip falling to dynamic
   mu_d as the slip grows (the hyperbolic curve, v0 its knee), and linear
   through zero within eps — the regularised stick an explicit loop needs */
struct Bow
{
    float v_bow;      /* the bow's velocity, m/s */
    float f_n;        /* its pressure on the string, N */
    float mu_s, mu_d, v0, eps;
    float xc_prev;    /* the contact's displacement a sample ago */

    void Init() { v_bow = 0.f; f_n = 0.f; mu_s = 0.8f; mu_d = 0.3f; v0 = 0.1f; eps = 1e-3f; xc_prev = 0.f; }
    float Mu(float v) const
    {
        const float a = v < 0.f ? -v : v;
        const float edge = mu_d + (mu_s - mu_d) / (1.f + eps / v0);
        if(a < eps) return edge * v / eps;
        const float m = mu_d + (mu_s - mu_d) / (1.f + a / v0);
        return v < 0.f ? -m : m;
    }
    float Force(float v_c) const { return f_n * Mu(v_bow - v_c); }
};

/* a bank bowed at a point: frames samples of its displacement summed into
   out (overwrite), w the modes' weights at the contact, m the modal mass */
inline void ProcessBowed(ResonatorBank& b, float* out, int frames, const float* w, Bow& bow, float sr, float m)
{
    const float kin = 1.f / (sr * sr * m);
    for(int s = 0; s < frames; s++)
    {
        float xc = 0.f;
        for(int k = 0; k < b.n; k++) xc += w[k] * b.y1[k];
        const float vc = (xc - bow.xc_prev) * sr;
        bow.xc_prev = xc;
        const float f = bow.Force(vc) * kin;
        float o = 0.f;
        for(int k = 0; k < b.n; k++)
        {
            const float y = b.c1[k] * b.y1[k] + b.c2[k] * b.y2[k] + w[k] * f;
            b.y2[k] = b.y1[k]; b.y1[k] = y;
            o += y;
        }
        out[s] = o;
    }
}

/* the hammer: a mass on a felt spring, F = K d^alpha while the felt is
   compressed (d, the hammer's position past the string's at the contact),
   the hammer slowed by it and the string pushed by it — the same loop as the
   bow, and it leaves when the felt stops pressing. Nothing is scripted: a
   harder hit is shorter (T ~ v^-(alpha-1)/(alpha+1)) and so brighter,
   because the felt stiffens as it compresses; a hit on a moving string meets
   it where it is */
struct Hammer
{
    float mass;       /* kg */
    float k, alpha;   /* felt stiffness and exponent */
    float x, v;       /* the hammer's position and velocity, toward the string (+) */
    float xc_prev;
    int   contact;    /* samples in contact so far; */
    bool  gone;       /* left the string */
    void Init() { mass = 0.008f; k = 1e8f; alpha = 2.5f; x = 0.f; v = 0.f; xc_prev = 0.f; contact = 0; gone = true; }
    void Strike(float velocity, float xc) { v = velocity; x = xc; gone = false; contact = 0; }
};

/* a bank struck by a hammer: frames samples of its displacement into out
   (overwrite); returns true while the hammer is still in contact */
inline bool ProcessStruck(ResonatorBank& b, float* out, int frames, const float* w, Hammer& h, float sr, float m)
{
    const float dt = 1.f / sr, kin = dt * dt / m;
    for(int s = 0; s < frames; s++)
    {
        float f = 0.f;
        if(!h.gone)
        {
            float xc = 0.f;
            for(int k = 0; k < b.n; k++) xc += w[k] * b.y1[k];
            const float d = h.x - xc;
            if(d > 0.f) { f = h.k * std::pow(d, h.alpha); h.contact++; }
            else if(h.contact > 0 && h.v <= 0.f) h.gone = true;          /* the felt has let go and the hammer is falling back */
            h.v -= f / h.mass * dt;
            h.x += h.v * dt;
        }
        float o = 0.f;
        for(int k = 0; k < b.n; k++)
        {
            const float y = b.c1[k] * b.y1[k] + b.c2[k] * b.y2[k] + w[k] * f * kin;
            b.y2[k] = b.y1[k]; b.y1[k] = y;
            o += y;
        }
        out[s] = o;
    }
    return !h.gone;
}

} // namespace kyk
