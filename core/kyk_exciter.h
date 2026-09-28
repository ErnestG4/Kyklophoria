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

/* the noise of the contact itself — felt on steel, a plectrum's scrape, a
   bow's hair, breath through a reed — as loud as the contact is, sample by
   sample: the exciter hands it how hard it is touching (the hammer's force,
   the plectrum's, the bow's friction while it slips, the reed's flow) and it
   is noise band-passed to the contact's brightness, followed within a
   millisecond. It lives exactly while the contact does, so it cannot stack:
   the wash's twelve bands each fell on their own and at four notes a second
   summed into a "snare chain" (Combust, 23 September; docs/exciters.md).
   drive is in the exciter's own units; gain scales it to the output's */
struct ContactNoise
{
    uint32_t rng;
    float env, att, rel;       /* the follower and its one-pole coefficients */
    float a1, a2, b0;          /* the band-pass: b0 (x - x2) - a1 y1 - a2 y2 */
    float x1, x2, y1, y2;
    float gain;
    void Init(float sr, float fc = 3000.f, float q = 0.8f, float g = 1.f)
    {
        rng = 0x2545F491u; env = 0.f; gain = g;
        att = 1.f - std::exp(-1.f / (0.0001f * sr));      /* 0.1 ms up */
        rel = 1.f - std::exp(-1.f / (0.001f * sr));       /* 1 ms down */
        const float w0 = 6.2831853f * (fc < 0.45f * sr ? fc : 0.45f * sr) / sr;
        const float al = std::sin(w0) / (2.f * q), a0 = 1.f + al;
        b0 = al / a0; a1 = -2.f * std::cos(w0) / a0; a2 = (1.f - al) / a0;
        x1 = x2 = y1 = y2 = 0.f;
    }
    float Next(float drive)
    {
        const float d = drive < 0.f ? -drive : drive;
        env += (d > env ? att : rel) * (d - env);
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float x = (float)(int32_t)rng * (1.f / 2147483648.f);
        const float y = b0 * (x - x2) - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return gain * env * y;
    }
};

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
inline void ProcessBowed(ResonatorBank& b, float* out, int frames, const float* w, Bow& bow, float sr, float m, ContactNoise* cn = nullptr)
{
    const float kin = 1.f / (sr * sr * m);
    for(int s = 0; s < frames; s++)
    {
        float xc = 0.f;
        for(int k = 0; k < b.n; k++) xc += w[k] * b.y1[k];
        const float vc = (xc - bow.xc_prev) * sr;
        bow.xc_prev = xc;
        const float fr = bow.Force(vc);
        const float f = fr * kin;
        /* the hair's noise while it slips; sticking, the hair moves with the
           string and says nothing */
        const float slip = bow.v_bow - vc;
        const float cnoise = cn ? cn->Next((slip > bow.eps || slip < -bow.eps) ? fr : 0.f) : 0.f;
        float o = 0.f;
        for(int k = 0; k < b.n; k++)
        {
            const float y = b.c1[k] * b.y1[k] + b.c2[k] * b.y2[k] + w[k] * f;
            b.y2[k] = b.y1[k]; b.y1[k] = y;
            o += y;
        }
        out[s] = o + cnoise;
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
inline bool ProcessStruck(ResonatorBank& b, float* out, int frames, const float* w, Hammer& h, float sr, float m, ContactNoise* cn = nullptr)
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
        out[s] = cn ? o + cn->Next(f) : o;                  /* the felt's noise: as loud as it presses */
    }
    return !h.gone;
}

/* the reed: a clarinet's, non-dimensional (Kergomard's form). gamma is the
   mouth pressure over the pressure that shuts the reed, zeta the embouchure
   (how much the lip lets through); p the pressure at the mouthpiece, what
   the bore sends back. The reed is a spring with no mass: its opening
   1 - gamma + p, shut at zero (it beats against the lay); the flow through
   it zeta x opening x sqrt|gamma - p|, signed as the difference is. The
   flow's change drives the bore's modes (a bore passes no DC) and the modes'
   sum at the mouthpiece is p: the loop. Below about a third of the closing
   pressure it is silent, above it plays the bore's fundamental */
struct Reed
{
    float gamma, zeta;
    float u_prev;
    void Init() { gamma = 0.f; zeta = 0.3f; u_prev = 0.f; }
    float Flow(float p) const
    {
        const float open = 1.f - gamma + p;
        if(open <= 0.f) return 0.f;
        const float d = gamma - p;
        /* sqrt|d| smoothed near zero, sqrt(d^2 + e^2)^(1/2): its slope there
           is infinite, and an explicit loop through it blew up to NaN at any
           coupling past a fifth */
        const float r = std::sqrt(std::sqrt(d * d + 1e-4f));
        return d < 0.f ? -zeta * open * r : zeta * open * r;
    }
};

/* the pluck: a finger or plectrum, a stiff spring from the finger to the
   string at the contact. The finger moves at speed v into the string's
   plane and the string is drawn with it; when the spring's force passes
   the threshold (the plectrum slips, the finger lets go) it is gone and the
   string rings from where it was drawn to. A stiffer plectrum lets go of a
   string with less time for the contact to smooth it: brighter */
struct Pluck
{
    float k;          /* the plectrum's stiffness, N/m */
    float v;          /* the finger's speed, m/s */
    float release;    /* the force it lets go at, N */
    float xf;         /* the finger's position */
    bool  gone;
    float work;       /* what the finger has put in so far, J */
    void Init() { k = 2000.f; v = 0.2f; release = 2.f; xf = 0.f; gone = true; work = 0.f; }
    void Start(float xc) { xf = xc; gone = false; work = 0.f; }
};

/* a bank plucked: frames samples of its displacement into out (overwrite);
   true while the finger still holds the string */
inline bool ProcessPlucked(ResonatorBank& b, float* out, int frames, const float* w, Pluck& pk, float sr, float m, ContactNoise* cn = nullptr)
{
    const float dt = 1.f / sr, kin = dt * dt / m;
    for(int s = 0; s < frames; s++)
    {
        float f = 0.f;
        if(!pk.gone)
        {
            float xc = 0.f;
            for(int k = 0; k < b.n; k++) xc += w[k] * b.y1[k];
            pk.xf += pk.v * dt;
            f = pk.k * (pk.xf - xc);
            if(f >= pk.release) { pk.gone = true; f = 0.f; }
            else pk.work += f * pk.v * dt;
        }
        float o = 0.f;
        for(int k = 0; k < b.n; k++)
        {
            const float y = b.c1[k] * b.y1[k] + b.c2[k] * b.y2[k] + w[k] * f * kin;
            b.y2[k] = b.y1[k]; b.y1[k] = y;
            o += y;
        }
        out[s] = cn ? o + cn->Next(f) : o;                  /* the plectrum's scrape: as hard as it pulls */
    }
    return !pk.gone;
}

/* the lips: a brass player's, a mass on a spring with a frequency of its
   own (f_lip, quality q) swung open by the pressure across it (outward
   striking), the opening its displacement past closed; the flow and the
   bore as the reed's. A massless reed plays whatever the bore's lowest
   strong resonance says; a lip with a frequency selects the resonance
   nearest it, which is how a bugle has notes */
struct Lips
{
    float gamma, zeta;       /* mouth pressure over the closing pressure, the lip's opening scale */
    float f_lip, q;          /* the lip's own frequency, Hz, and quality */
    float h0;                /* its opening at rest */
    float h, hv;             /* its opening and the opening's velocity */
    float u_prev;
    void Init() { gamma = 0.f; zeta = 0.3f; f_lip = 200.f; q = 3.f; h0 = 0.1f; h = h0; hv = 0.f; u_prev = 0.f; }
    /* one sample: the lip moved by the pressure across it, then the flow */
    float Step(float p, float sr)
    {
        const float w = 6.2831853f * f_lip, dt = 1.f / sr;
        const float dp = gamma - p;
        const float acc = -w * w * (h - h0) - (w / q) * hv + w * w * dp;    /* pushed open by the pressure, in units of the closing pressure */
        hv += acc * dt; h += hv * dt;                                        /* semi-implicit: stable for a lip well under the rate */
        const float open = h > 0.f ? h : 0.f;
        const float r = std::sqrt(std::sqrt(dp * dp + 1e-4f));
        return dp < 0.f ? -zeta * open * r : zeta * open * r;
    }
};

/* a bank blown through the lips, as ProcessBlown through a reed */
inline void ProcessLipped(ResonatorBank& b, float* out, int frames, const float* w, Lips& l, float z, float sr)
{
    float bk[ResonatorBank::kMax];
    for(int k = 0; k < b.n; k++)
    {
        const float wk = b.wq[k], rk = b.rq[k], sh = std::sin(0.5f * wk);
        bk[k] = sh > 1e-6f ? z * (1.f - rk) * std::sin(wk) / sh : 0.f;
    }
    for(int s = 0; s < frames; s++)
    {
        float p = 0.f;
        for(int k = 0; k < b.n; k++) p += w[k] * b.y1[k];
        const float u = l.Step(p, sr);
        const float du = u - l.u_prev;
        l.u_prev = u;
        float o = 0.f;
        for(int k = 0; k < b.n; k++)
        {
            const float y = b.c1[k] * b.y1[k] + b.c2[k] * b.y2[k] + w[k] * bk[k] * du;
            b.y2[k] = b.y1[k]; b.y1[k] = y;
            o += w[k] * y;
        }
        out[s] = o;
    }
}

/* a bank blown through a reed: frames samples of the mouthpiece pressure
   into out (overwrite); w the modes' weights at the mouthpiece, z the bore's
   impedance at a resonance (pressure over flow at a peak, in the reed's
   units: a clarinet's is some tens). Each mode is driven so that its own
   peak is z: driven raw, a lightly damped mode's peak was 1 / (1 - r), five
   thousand at 147 Hz, and the loop ran away at any pressure */
inline void ProcessBlown(ResonatorBank& b, float* out, int frames, const float* w, Reed& r, float z, ContactNoise* cn = nullptr)
{
    float bk[ResonatorBank::kMax];
    for(int k = 0; k < b.n; k++)
    {
        /* the recursion's gain at its own resonance, for an input that is
           the flow's first difference: (1 - r) 2 sin w over 2 sin(w/2) */
        const float wk = b.wq[k], rk = b.rq[k];
        const float sh = std::sin(0.5f * wk);
        bk[k] = sh > 1e-6f ? z * (1.f - rk) * std::sin(wk) / sh : 0.f;
    }
    for(int s = 0; s < frames; s++)
    {
        float p = 0.f;
        for(int k = 0; k < b.n; k++) p += w[k] * b.y1[k];
        const float u = r.Flow(p);
        const float du = u - r.u_prev;
        r.u_prev = u;
        float o = 0.f;
        for(int k = 0; k < b.n; k++)
        {
            const float y = b.c1[k] * b.y1[k] + b.c2[k] * b.y2[k] + w[k] * bk[k] * du;
            b.y2[k] = b.y1[k]; b.y1[k] = y;
            o += w[k] * y;
        }
        out[s] = cn ? o + cn->Next(u) : o;                  /* breath through the reed: as much as flows */
    }
}

} // namespace kyk
