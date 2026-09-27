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

} // namespace kyk
