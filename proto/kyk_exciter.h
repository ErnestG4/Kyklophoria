/* kyk_exciter.h — a prototype, desktop only: the attack synthesised instead
 * of played back, and a waveguide above the modes.
 *
 * Not wired into Kyklophoria. It sits beside the runtime (ModalBake's
 * verbatim copy of core/kyk_resonate.h) and uses it as it is: the modes are
 * kyk::ResonatorBank, struck through its own Strike(); nothing here edits
 * the core. Conventions as core/: header-only, float, no heap, Init()
 * where a constructor would be, nothing with a default member initialiser.
 *
 * Three parts, each what a point of a world would carry in place of its
 * burst (proto/README.md has the numbers):
 *
 *   Contact     the force the strike puts in. A hammer is a mass on a
 *               nonlinear felt spring, F = K d^p, integrated at strike time,
 *               so its length follows the velocity the way felt does (a
 *               harder hit is shorter: T ~ v^-(p-1)/(p+1)). A pluck is the
 *               release of a displaced string, a raised-cosine pulse whose
 *               width sets the brightness. What the rest hears of it is its
 *               arrival (the impulse so far, which ramps the modes and the
 *               noise in) and a felt low-pass, 1 / (1 + (f T)^2) — the
 *               smooth pulse's own spectrum has nulls and falls 60-100 dB
 *               by 2-8 kHz, which no recording's attack does.
 *   NoiseBands  the unpitched attack: white noise through twelve
 *               log-spaced band-passes, 60 Hz to 16 kHz, each under a level
 *               and two decays. The level arrives as the contact's impulse
 *               arrives, so a restrike adds to what is still falling.
 *   Waveguide   one delay line tuned to the note, a first-order Thiran for
 *               the fraction, M first-order allpasses for the stiffness, a
 *               one-pole loss from the measured decay law (a power law in
 *               frequency, from the recording's own bands above the bank),
 *               and a crossover:
 *               a high-pass at the input, one inside the loop (so nothing
 *               below the crossover rings) and one at the output. It plays
 *               the partials above the bank's top mode and none of the ones
 *               the bank has.
 *
 * The modes keep their fitted gains and phases: the strike bank is set as
 * today and ramped in from t = 0 over the contact (never under the fit's
 * own 3 ms), each mode's gain scaled by the force's spectrum at its
 * frequency against the reference strike's — which is 1 at the swing the
 * fit saw, so the sustain is the sustain that sounds good today.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <complex>
#include <algorithm>
#include "../runtime/kyk_resonate.h"

namespace kykp {

/* ── the parameter block a point carries (a flat float array, so Python
      and C++ read the same layout) ──────────────────────────────────── */
enum : int {
    P_KIND = 0,        /* 0 hammer, 1 pluck */
    P_TC,              /* contact length at unit swing, s (hammer); release width (pluck) */
    P_EXP,             /* hammer: felt exponent p; pluck: width ~ swing^-EXP */
    P_FELT,            /* the felt low-pass corner, x 1/T (1: the pulse's own -6 dB point) */
    P_RAMP,            /* the modes' ramp floor, s (the fit's own 3 ms) */
    P_NL,              /* 12 band levels, RMS at unit swing */
    P_NF = P_NL + 12,  /* 12 fast decay time constants, s */
    P_NS = P_NF + 12,  /* 12 slow decay time constants, s */
    P_NW = P_NS + 12,  /* 12 slow weights, 0..1 */
    P_WG = P_NW + 12,  /* waveguide on: 0/1 */
    P_F0,              /* the point's fundamental, Hz (the loop's pitch at the point) */
    P_FC,              /* crossover, Hz, at the point */
    P_FREF,            /* a partial tuned exactly, Hz, at the point */
    P_KREF,            /* the loop's resonance number there (not the string's partial
                          number: the two may differ by a whole number, which is what
                          lets a short allpass bend a stretched series over a band) */
    P_M,               /* dispersion sections */
    P_AD,              /* their coefficient */
    P_S1, P_S3,        /* decay law: sigma(f) = S1 (f / 1 kHz)^S3 (1/s, amplitude) */
    P_GIN,             /* input gain */
    P_TILT,            /* input low-pass corner, Hz, two poles (0 none) */
    P_HPK,             /* the in-loop high-pass at HPK x FC */
    P_ORDER,           /* input/output high-pass order: 2 or 4 */
    P_M2, P_AD2,       /* a second group of dispersion sections, its coefficient */
    P_NRISE,           /* the noise's arrival: 0 with the contact's impulse, else a raised
                          cosine over this many seconds (the modes' ramp, where the
                          recording builds slowly) */
    P_COUNT
};

static constexpr int kBands = 12;
inline float BandEdge(int k) { return 60.f * std::pow(16000.f / 60.f, (float)k / (float)kBands); }

/* ── small parts ───────────────────────────────────────────────────── */
struct Biquad
{
    float b0, b1, b2, a1, a2, z1, z2;
    void Init() { b0 = 1.f; b1 = b2 = a1 = a2 = z1 = z2 = 0.f; }
    void Clear() { z1 = z2 = 0.f; }
    /* RBJ high-pass */
    void HP(float fc, float q, float sr)
    {
        const float w = 6.2831853f * fc / sr, c = std::cos(w), al = std::sin(w) / (2.f * q), a0 = 1.f + al;
        b0 = (1.f + c) * 0.5f / a0; b1 = -(1.f + c) / a0; b2 = b0; a1 = -2.f * c / a0; a2 = (1.f - al) / a0;
    }
    float Run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    std::complex<float> H(float w) const
    {
        const std::complex<float> z1_ = std::polar(1.f, -w), z2_ = std::polar(1.f, -2.f * w);
        return (b0 + b1 * z1_ + b2 * z2_) / (1.f + a1 * z1_ + a2 * z2_);
    }
};

/* ── the contact ───────────────────────────────────────────────────── */
struct Contact
{
    static constexpr int kMax = 2048;             /* 42 ms: a slow pluck's release */
    float f[kMax];                                /* the force, per sample; its sum is the impulse */
    float cum[kMax];                              /* the impulse so far over the whole: the arrival */
    int   len, pos, peak;
    float T, impulse;                             /* the contact's length, s, and its impulse */

    void Init() { len = pos = peak = 0; T = 0.f; impulse = 0.f; }

    /* the hammer: m = 1, F = K d^p (d the felt's compression) against a
       string held still — its motion is the modes' business. RK4, eight
       steps a sample; ends when the felt lets go. (A string that gives,
       d' = v - gamma F, was tried: stiff, and with any gamma the rebound
       never comes and the contact never ends — a real string's reflection
       ends it, and that is the waveguide's time scale, not the felt's.) */
    static int Hammer(float K, float p, float v, float sr, float* out, int max)
    {
        const int sub = 8;
        const float h = 1.f / (sr * (float)sub);
        float d = 0.f, vh = v;                    /* compression, hammer velocity */
        auto F = [&](float dd) { return dd > 0.f ? K * std::pow(dd, p) : 0.f; };
        const float gamma = 0.f;
        float peak = 0.f; int n = 0;
        for(; n < max; n++)
        {
            float acc = 0.f;
            for(int s = 0; s < sub; s++)
            {
                /* d' = vh - gamma F(d),  vh' = -F(d) */
                const float k1d = vh - gamma * F(d),               k1v = -F(d);
                const float k2d = (vh + 0.5f * h * k1v) - gamma * F(d + 0.5f * h * k1d), k2v = -F(d + 0.5f * h * k1d);
                const float k3d = (vh + 0.5f * h * k2v) - gamma * F(d + 0.5f * h * k2d), k3v = -F(d + 0.5f * h * k2d);
                const float k4d = (vh + h * k3v) - gamma * F(d + h * k3d),               k4v = -F(d + h * k3d);
                d  += h / 6.f * (k1d + 2.f * k2d + 2.f * k3d + k4d);
                vh += h / 6.f * (k1v + 2.f * k2v + 2.f * k3v + k4v);
                acc += F(d) * h;
            }
            out[n] = acc;                         /* the impulse delivered in this sample */
            const float fn = acc * sr;
            if(fn > peak) peak = fn;
            if(n > 2 && d <= 0.f) { n++; break; }
            (void)peak;
        }
        return n;
    }

    /* K for a contact of tc seconds at unit velocity: bisection in log K */
    static float HammerK(float tc, float p, float sr)
    {
        static float buf[kMax];
        float lo = -20.f, hi = 60.f;
        for(int it = 0; it < 60; it++)
        {
            const float mid = 0.5f * (lo + hi);
            const int n = Hammer(std::exp(mid), p, 1.f, sr, buf, kMax);
            if((float)n / sr > tc) lo = mid; else hi = mid;    /* stiffer is shorter */
        }
        return std::exp(0.5f * (lo + hi));
    }

    /* make the pulse for a strike at swing s; K cached by the caller */
    void Make(const float* P, float K, float s, float sr)
    {
        pos = 0;
        const float v = s > 1e-4f ? s : 1e-4f;
        if((int)P[P_KIND] == 0)
            len = Hammer(K, P[P_EXP], v, sr, f, kMax);
        else
        {
            /* the release: a raised-cosine pulse, its width tc s^-EXP, its
               area the swing (the displacement the finger let go of) */
            float w = P[P_TC] * std::pow(v, -P[P_EXP]) * sr;
            if(w < 2.f) w = 2.f;
            len = (int)std::ceil(w); if(len > kMax) len = kMax;
            float sum = 0.f;
            for(int n = 0; n < len; n++) { f[n] = 0.5f - 0.5f * std::cos(6.2831853f * ((float)n + 0.5f) / w); sum += f[n]; }
            for(int n = 0; n < len; n++) f[n] *= v / sum;
        }
        float c = 0.f, tot = 0.f;
        peak = 0;
        for(int n = 0; n < len; n++) { tot += f[n]; if(f[n] > f[peak]) peak = n; }
        for(int n = 0; n < len; n++) { c += f[n]; cum[n] = tot > 0.f ? c / tot : 1.f; }
        T = (float)len / sr;
        impulse = tot;
    }
    /* what a partial at f hears of this strike: the impulse through the
       felt, 1 / (1 + (f T / felt)^2) */
    float Felt(float f, float felt) const { const float x = f * T / (felt > 0.f ? felt : 1.f); return impulse / (1.f + x * x); }
    /* the pulse's spectrum at w (rad/sample): what a mode at w receives */
    std::complex<float> X(float w) const
    {
        std::complex<float> acc(0.f, 0.f);
        const std::complex<float> step = std::polar(1.f, -w);
        std::complex<float> e(1.f, 0.f);
        for(int n = 0; n < len; n++) { acc += f[n] * e; e *= step; }
        return acc;
    }
    bool Active() const { return pos < len; }
};

/* ── the unpitched attack ──────────────────────────────────────────── */
struct NoiseBands
{
    float b0[kBands], a1[kBands], a2[kBands];     /* band-pass: b0 (x - x2) - a1 y1 - a2 y2 */
    float norm[kBands];                           /* unit-variance noise to unit band RMS */
    float y1[kBands], y2[kBands], x1, x2;
    float ef[kBands], es[kBands];                 /* the fast and slow parts of each band's envelope */
    float df[kBands], ds[kBands];                 /* their per-sample multipliers */
    float pf[kBands], ps[kBands];                 /* what is still to arrive from the strike in flight */
    uint32_t rng;
    bool on;
    Biquad lp;                                    /* 16 kHz: the top band's skirt ran to Nyquist, where a 44.1 kHz recording has nothing */

    void Init(float sr)
    {
        rng = 0x2545F491u; x1 = x2 = 0.f; on = false;
        {
            const float w = 6.2831853f * 16000.f / sr, c = std::cos(w), al = std::sin(w) / (2.f * 0.70710678f), a0 = 1.f + al;
            lp.b0 = (1.f - c) * 0.5f / a0; lp.b1 = (1.f - c) / a0; lp.b2 = lp.b0; lp.a1 = -2.f * c / a0; lp.a2 = (1.f - al) / a0; lp.z1 = lp.z2 = 0.f;
        }
        for(int k = 0; k < kBands; k++)
        {
            const float lo = BandEdge(k), hi = BandEdge(k + 1), fc = std::sqrt(lo * hi), q = fc / (hi - lo);
            const float w = 6.2831853f * fc / sr, al = std::sin(w) / (2.f * q), a0 = 1.f + al;
            b0[k] = al / a0; a1[k] = -2.f * std::cos(w) / a0; a2[k] = (1.f - al) / a0;
            float g = 0.f, u1 = 0.f, u2 = 0.f, v1 = 0.f, v2 = 0.f;
            for(int i = 0; i < 8192; i++)
            {
                const float u = i == 0 ? 1.f : 0.f;
                const float v = b0[k] * (u - u2) - a1[k] * v1 - a2[k] * v2;
                u2 = u1; u1 = u; v2 = v1; v1 = v; g += v * v;
            }
            norm[k] = 1.f / std::sqrt(g);
            y1[k] = y2[k] = ef[k] = es[k] = pf[k] = ps[k] = 0.f; df[k] = ds[k] = 0.f;
        }
    }
    /* a strike: this point's levels and decays, each band's level scaled by
       the force's spectrum there against the reference strike's (vg) */
    void Strike(const float* P, const float* vg, float sr)
    {
        for(int k = 0; k < kBands; k++)
        {
            const float L = P[P_NL + k] * vg[k], w = P[P_NW + k];
            /* what is still falling falls at the new note's rates */
            df[k] = P[P_NF + k] > 0.f ? std::exp(-1.f / (P[P_NF + k] * sr)) : 0.f;
            ds[k] = P[P_NS + k] > 0.f ? std::exp(-1.f / (P[P_NS + k] * sr)) : 0.f;
            pf[k] += L * (1.f - w); ps[k] += L * w;
            if(L > 0.f) on = true;
        }
    }
    /* dc: the share of the strike's impulse that arrived this sample */
    float Run(float dc)
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float x = (float)(int32_t)rng * (1.7320508f / 2147483648.f);
        float acc = 0.f;
        for(int k = 0; k < kBands; k++)
        {
            const float y = b0[k] * (x - x2) - a1[k] * y1[k] - a2[k] * y2[k];
            y2[k] = y1[k]; y1[k] = y;
            if(dc > 0.f) { ef[k] += pf[k] * dc; es[k] += ps[k] * dc; }
            acc += (ef[k] + es[k]) * norm[k] * y;
            ef[k] *= df[k]; es[k] *= ds[k];
        }
        x2 = x1; x1 = x;
        return lp.Run(acc);
    }
    void Arrived() { for(int k = 0; k < kBands; k++) pf[k] = ps[k] = 0.f; }
    bool Active() const { for(int k = 0; k < kBands; k++) if(ef[k] + es[k] > 1e-6f || pf[k] + ps[k] > 0.f) return true; return false; }
};

/* ── the waveguide above the modes ─────────────────────────────────── */
struct Waveguide
{
    static constexpr int kLen = 4096, kMask = kLen - 1, kAP = 48;
    float buf[kLen];
    int   w, D, M, M2;
    float fa, fx1, fy1;                  /* Thiran, first order */
    float ad, ad2, ax1[kAP], ay1[kAP];   /* dispersion: M sections at ad, then M2 at ad2 */
    float lb, la, ly1;                   /* loss: y = lb x - la y1 (lb = g (1 + la)) */
    Biquad hpl[2], hpi[2], hpo[2];       /* the crossover: inside the loop (fourth order), at the input, at the output */
    int   order;
    float ta, tz, tz2;                   /* input low-pass, two poles */
    float gin, gout, dout;               /* input gain; output gain and its per-sample fade (a choke) */
    bool  on;
    float f0, fc;                        /* as tuned, for a readout */

    void Init()
    {
        std::memset(buf, 0, sizeof(buf));
        w = 0; D = 16; M = M2 = 0; fa = fx1 = fy1 = 0.f; ad = ad2 = 0.f; lb = la = ly1 = 0.f;
        for(int m = 0; m < kAP; m++) ax1[m] = ay1[m] = 0.f;
        for(int i = 0; i < 2; i++) { hpl[i].Init(); hpi[i].Init(); hpo[i].Init(); }
        order = 2; ta = 0.f; tz = tz2 = 0.f; gin = 0.f; gout = 1.f; dout = 1.f; on = false; f0 = fc = 0.f;
    }
    void Clear()
    {
        std::memset(buf, 0, sizeof(buf)); fx1 = fy1 = ly1 = tz = tz2 = 0.f;
        for(int m = 0; m < kAP; m++) ax1[m] = ay1[m] = 0.f;
        for(int i = 0; i < 2; i++) { hpl[i].Clear(); hpi[i].Clear(); hpo[i].Clear(); }
    }

    /* the loop's filters' phase lag at w, everything but the integer delay
       and the Thiran (which the tuning places) */
    float FilterLag(float w) const
    {
        std::complex<float> h(1.f, 0.f);
        const std::complex<float> z = std::polar(1.f, -w);
        const std::complex<float> ap = (ad + z) / (1.f + ad * z), ap2 = (ad2 + z) / (1.f + ad2 * z);
        /* the allpasses' lag is continuous: M times each section's phase,
           taken per section so M x pi does not wrap */
        const float lap = -(float)M * std::arg(ap) - (float)M2 * std::arg(ap2);
        /* the in-loop high-pass's lead runs to 2 pi at DC: its two
           sections taken apart so neither wraps */
        const float lrest = -std::arg(lb / (1.f + la * z)) - std::arg(hpl[0].H(w)) - std::arg(hpl[1].H(w));
        return lap + lrest;
    }
    std::complex<float> HPL(float w) const { return hpl[0].H(w) * hpl[1].H(w); }
    float ThiranLag(float w) const
    {
        const std::complex<float> z = std::polar(1.f, -w);
        return -std::arg((fa + z) / (1.f + fa * z));
    }
    float LoopLag(float w) const { return (float)D * w + ThiranLag(w) + FilterLag(w); }
    float LoopMag(float w) const
    {
        const std::complex<float> z = std::polar(1.f, -w);
        return std::abs(lb / (1.f + la * z)) * std::abs(HPL(w));
    }

    /* place the delay so the loop's phase at wr is 2 pi k: the integer part
       and a Thiran for the fraction (its delay in 0.5 .. 1.5) */
    void Tune(float wr, int k)
    {
        const float target = 6.2831853f * (float)k - FilterLag(wr);
        float dt = target / wr;                       /* the delay the rest must supply */
        for(int it = 0; it < 4; it++)
        {
            int Di = (int)std::floor(dt - 0.5f); if(Di < 1) Di = 1; if(Di > kLen - 2) Di = kLen - 2;
            float d = dt - (float)Di; if(d < 0.5f) d = 0.5f; if(d > 1.5f) d = 1.5f;
            D = Di; fa = (1.f - d) / (1.f + d);
            /* the Thiran's lag at wr is not d wr exactly: correct the ask by the difference */
            const float err = (float)D * wr + ThiranLag(wr) - target;
            dt -= err / wr;
        }
    }
    /* the loop's resonance near w0 for partial k: Newton on LoopLag = 2 pi k */
    float Partial(float w0, int k) const
    {
        float w = w0;
        for(int it = 0; it < 12; it++)
        {
            const float e = LoopLag(w) - 6.2831853f * (float)k;
            const float dw = 1e-4f;
            const float g = (LoopLag(w + dw) - LoopLag(w - dw)) / (2.f * dw);
            if(!(g > 0.f)) break;
            w -= e / g;
            if(w < 1e-4f) w = 1e-4f; if(w > 3.14f) w = 3.14f;
            if(std::fabs(e) < 1e-6f) break;
        }
        return w;
    }
    /* group delay, samples */
    float Group(float w) const { const float dw = 1e-4f; return (LoopLag(w + dw) - LoopLag(w - dw)) / (2.f * dw); }

    /* the whole design at a strike: P the point's parameters, ratio the
       played note over the point's, sr */
    void Set(const float* P, float ratio, float sr)
    {
        on = false;
        if(P[P_WG] < 0.5f) return;
        f0 = P[P_F0] * ratio; fc = P[P_FC] * ratio;
        if(!(fc < 0.42f * sr) || !(f0 > 10.f)) return;
        order = (int)P[P_ORDER] >= 4 ? 4 : 2;
        M = (int)P[P_M]; if(M > kAP) M = kAP; if(M < 0) M = 0;
        M2 = (int)P[P_M2]; if(M + M2 > kAP) M2 = kAP - M; if(M2 < 0) M2 = 0;
        ad = P[P_AD]; ad2 = P[P_AD2];
        /* inside the loop, fourth order at HPK x fc (0.8): what is below
           it cannot ring, and a partial a fifth over fc sees 0.98 of
           itself a round trip — a second order at fc took the partials
           just over the bank to 0.7-0.9 a trip, dead in 100 ms */
        const float hk = P[P_HPK] > 0.f ? P[P_HPK] : 0.8f;
        hpl[0].HP(std::fmin(fc * hk, 0.45f * sr), 0.5411961f, sr); hpl[1].HP(std::fmin(fc * hk, 0.45f * sr), 1.3065630f, sr);
        /* Butterworth fourth order as two sections: Q 0.5412 and 1.3066 */
        if(order == 4) { hpi[0].HP(fc, 0.5411961f, sr); hpi[1].HP(fc, 1.3065630f, sr); hpo[0].HP(fc, 0.5411961f, sr); hpo[1].HP(fc, 1.3065630f, sr); }
        else { hpi[0].HP(fc, 0.70710678f, sr); hpo[0].HP(fc, 0.70710678f, sr); }
        ta = P[P_TILT] > 0.f ? 1.f - std::exp(-6.2831853f * std::fmin(P[P_TILT], 0.45f * sr) / sr) : 0.f;
        gin = P[P_GIN];
        /* a first loss guess so the tuning sees roughly the right phase, then
           the loss from the decay law at two partials, then the tuning again */
        lb = 0.999f; la = 0.f;
        const float wr = 6.2831853f * P[P_FREF] * ratio / sr;
        const int kr = (int)P[P_KREF];
        Tune(wr, kr);
        for(int pass = 0; pass < 2; pass++)
        {
            const float f1 = std::fmin(1.3f * fc, 0.3f * sr), f2 = std::fmin(4.f * fc, 0.4f * sr);
            float m[2]; float wv[2] = { 6.2831853f * f1 / sr, 6.2831853f * f2 / sr };
            for(int i = 0; i < 2; i++)
            {
                const float f = wv[i] * sr / 6.2831853f;
                const float sig = P[P_S1] * std::pow(f * 0.001f, P[P_S3]);    /* 1/s */
                const float tg = Group(wv[i]) / sr;                        /* one round trip, s */
                m[i] = std::exp(-sig * tg) / std::fmax(std::abs(HPL(wv[i])), 1e-3f);
            }
            /* |g (1+a) / (1 + a e^-jw)|^2 = m^2 at both: a quadratic in a */
            const float c1 = std::cos(wv[0]), c2 = std::cos(wv[1]);
            const float A = m[0] * m[0] - m[1] * m[1], B = 2.f * (m[0] * m[0] * c1 - m[1] * m[1] * c2);
            float a = 0.f;
            if(std::fabs(A) > 1e-9f)
            {
                const float disc = B * B - 4.f * A * A;
                if(disc >= 0.f)
                {
                    const float r1 = (-B + std::sqrt(disc)) / (2.f * A), r2 = (-B - std::sqrt(disc)) / (2.f * A);
                    a = (r1 > -1.f && r1 <= 0.f) ? r1 : (r2 > -1.f && r2 <= 0.f) ? r2 : 0.f;
                }
            }
            if(a < -0.95f) a = -0.95f;
            const float mag1 = (1.f + a) / std::sqrt(1.f + 2.f * a * c1 + a * a);
            float g = m[0] / mag1;
            /* g may pass 1: the loss is normalised at DC, where the loop's
               high-pass has already killed it. What must stay under 1 is
               the loop's whole gain, loss times high-pass, everywhere —
               capping g itself at 1 cost the partials just over the bank
               4% a trip, a T60 of half a second where the recording rings
               two */
            la = a; lb = g * (1.f + a);
            float peak = 0.f;
            for(int j = 1; j < 256; j++) { const float wj = 3.1415927f * (float)j / 256.f; const float mg = LoopMag(wj); if(mg > peak) peak = mg; }
            if(peak > 0.99995f) lb *= 0.99995f / peak;
            Tune(wr, kr);
        }
        gout = 1.f; dout = 1.f;
        on = true;
    }

    float Run(float x)
    {
        /* the input: gain, the tilt, the crossover */
        float in = gin * x;
        if(ta > 0.f) { tz += ta * (in - tz); tz2 += ta * (tz - tz2); in = tz2; }
        in = hpi[0].Run(in); if(order == 4) in = hpi[1].Run(in);
        /* the loop */
        const float y = buf[(w - D) & kMask];
        float v = fa * (y - fy1) + fx1; fx1 = y; fy1 = v;
        for(int m = 0; m < M; m++) { const float u = ad * (v - ay1[m]) + ax1[m]; ax1[m] = v; ay1[m] = u; v = u; }
        for(int m = M; m < M + M2; m++) { const float u = ad2 * (v - ay1[m]) + ax1[m]; ax1[m] = v; ay1[m] = u; v = u; }
        ly1 = lb * v - la * ly1;
        v = hpl[1].Run(hpl[0].Run(ly1));
        const float s = in + v;
        buf[w] = s; w = (w + 1) & kMask;
        float o = hpo[0].Run(s); if(order == 4) o = hpo[1].Run(o);
        o *= gout; gout *= dout;
        return o;
    }
    /* let go over ms: the old note's partials on a restrike at another note */
    void Choke(float ms, float sr) { dout = std::exp(std::log(1e-3f) / (ms * 0.001f * sr)); }
    bool Done() const { return gout < 1e-4f; }
};

/* ── the voice: the runtime's bank and pickup, this exciter ─────────── */
struct ProtoVoice
{
    kyk::ResonatorVoice rv;
    Contact    contact;
    NoiseBands noise;
    Waveguide  wg[2];
    int        wa;                        /* the waveguide the next strike rings */
    const float* P;                       /* the point's parameters, or null */
    float      K;                         /* the hammer's stiffness for this point */
    float      pnote, note;               /* the point's note and the voice's */
    int        point;
    int        variant;                   /* 0 today's burst; 1 synthesised; 2 synthesised + waveguide */
    float      sr;
    /* the reference strike's spectrum at each mode and band (unit swing) */
    Contact    cref;                      /* the reference strike (unit swing) at this point */
    float      save1[kyk::ResonatorBank::kMax], save2[kyk::ResonatorBank::kMax];
    float      vg_b[kBands];
    float      wgkick;                    /* the waveguide's input level: the felt's word at the crossover */
    float      wgnorm;                    /* 1 / sqrt(sum of the excitation window's squares) */
    int        wg_pos, wg_len;            /* the waveguide's excitation: over the contact, or the modes' ramp if longer */
    uint32_t   wrng;                      /* the waveguide's own noise */
    int        nr_pos, nr_len;            /* the noise's arrival, when it is a raised cosine */

    void Init(float sr_)
    {
        sr = sr_; rv.Init(); contact.Init(); cref.Init(); noise.Init(sr); wg[0].Init(); wg[1].Init(); wa = 0; wgkick = 0.f; wgnorm = 1.f; wrng = 0x6C8E9CF5u; nr_pos = nr_len = 0; wg_pos = wg_len = 0;
        P = nullptr; K = 1.f; pnote = note = 0.f; point = -1; variant = 0;
    }
    bool Active() const
    {
        return rv.Active() || contact.Active() || (variant > 0 && noise.Active()) || (wg[0].on && !wg[0].Done()) || (wg[1].on && !wg[1].Done());
    }

    /* the point a note plays: At()'s own rule (the nearer of the two) */
    static int Near(const kyk::ResonatorWorld& w, float param)
    {
        int a = 0;
        while(a + 1 < w.P && w.Param(a + 1) <= param) a++;
        const int b = a + 1 < w.P ? a + 1 : a;
        const float pa = w.Param(a), pb = w.Param(b);
        const float t = pb > pa ? std::fmin(1.f, std::fmax(0.f, (param - pa) / (pb - pa))) : 0.f;
        return t < 0.5f ? a : b;
    }

    /* build at a note, as the engine's At() does; Ptab the world's
       parameter table (P_COUNT a point), Kt its hammers' stiffnesses */
    void Build(const kyk::ResonatorWorld& w, const float* Ptab, const float* Kt, float param, bool keep, bool strike)
    {
        w.At(param, rv, sr, keep, strike);
        const int np = Near(w, param);
        const bool moved = keep && strike && note != 0.f && std::fabs(param - note) > 1e-4f;
        if(variant > 0 && Ptab)
        {
            P = Ptab + (size_t)np * P_COUNT;
            K = Kt[np];
            if(np != point) cref.Make(P, K, 1.f, sr);     /* the reference strike, once a point */
            /* the waveguide ringing at the old note lets go as the bank's
               ring does, and the next strike takes the other one */
            if(moved) { for(int i = 0; i < 2; i++) if(wg[i].on) wg[i].Choke(2.f, sr); wa ^= 1; }
        }
        point = np; pnote = w.Param(np); note = param;
    }

    void Strike(float velocity01)
    {
        if(variant == 0 || !P) { rv.Strike(velocity01); return; }
        const float s = rv.swing_hard > rv.swing_soft ? rv.swing_soft * std::pow(rv.swing_hard / rv.swing_soft, velocity01) : rv.swing_soft * velocity01;
        /* this strike's pulse, and what each mode hears of it against the
           reference strike: s at the bottom of the spectrum, more or less
           above as the felt is harder or softer (held within -30 .. +12 dB
           of s). 1 everywhere at the swing the fit saw */
        contact.Make(P, K, s, sr);
        const float felt = P[P_FELT];
        auto gain = [&](float f) {
            float g = contact.Felt(f, felt) / std::fmax(cref.Felt(f, felt), 1e-20f);
            const float lo = s * 0.0316f, hi = s * 3.98f;
            return g < lo ? lo : g > hi ? hi : g;
        };
        kyk::ResonatorBank& b = rv.bank;
        for(int k = 0; k < b.n; k++)
        {
            const float g = gain(b.wq[k] * sr / 6.2831853f);
            save1[k] = b.p1[k]; save2[k] = b.p2[k];
            b.p1[k] *= g; b.p2[k] *= g;
        }
        const float ramp = std::fmax((float)contact.len, P[P_RAMP] * sr);
        b.Strike(1.f, 0.f, ramp);
        for(int k = 0; k < b.n; k++) { b.p1[k] = save1[k]; b.p2[k] = save2[k]; }
        for(int k = 0; k < kBands; k++) vg_b[k] = gain(std::sqrt(BandEdge(k) * BandEdge(k + 1)));
        noise.Strike(P, vg_b, sr);
        nr_len = P[P_NRISE] > 0.f ? (int)(P[P_NRISE] * sr) : 0; nr_pos = 0;
        wgkick = 0.f;
        if(variant >= 2)
        {
            Waveguide& g = wg[wa];
            const float ratio = std::exp2((note - pnote) / 12.f);
            if(!g.on || g.Done() || g.dout < 1.f) { g.Clear(); }
            g.Set(P, ratio, sr);
            /* the waveguide's input: noise under a Hann window over the
               contact, as much energy as a unit impulse, at the felt's word
               for the crossover. The smooth pulse itself is 60-100 dB down
               up there and its low end would leak through any crossover;
               an impulse put every partial in phase, a click the passage
               measured at up to 54 dB over the ringing before it */
            if(g.on)
            {
                /* over the contact, or over the modes' fitted build-up if
                   that is longer: the partials above the bank come in as
                   the ones in it do */
                wgkick = gain(g.fc);
                wg_len = std::max(contact.len, (int)(P[P_RAMP] * sr)); wg_pos = 0;
                float e = 0.f;
                for(int n = 0; n < wg_len; n++) { const float w = 0.5f - 0.5f * std::cos(6.2831853f * ((float)n + 0.5f) / (float)wg_len); e += w * w; }
                wgnorm = e > 0.f ? 1.f / std::sqrt(e) : 1.f;
            }
        }
    }

    /* out: the voice; the parts, if asked for, each added into its own */
    void Process(float* out, int frames, float* o_modes = nullptr, float* o_noise = nullptr, float* o_wg = nullptr)
    {
        rv.Process(out, frames);
        if(o_modes) for(int i = 0; i < frames; i++) o_modes[i] += out[i];
        if(variant == 0) return;
        for(int i = 0; i < frames; i++)
        {
            float x = 0.f, dc = 0.f;
            if(contact.pos < contact.len)
            {

                if(nr_len == 0) dc = contact.cum[contact.pos] - (contact.pos > 0 ? contact.cum[contact.pos - 1] : 0.f);
                contact.pos++;
            }
            if(wgkick != 0.f && wg_pos < wg_len)
            {
                wrng ^= wrng << 13; wrng ^= wrng >> 17; wrng ^= wrng << 5;
                const float w = 0.5f - 0.5f * std::cos(6.2831853f * ((float)wg_pos + 0.5f) / (float)wg_len);
                x = wgkick * wgnorm * w * (float)(int32_t)wrng * (1.7320508f / 2147483648.f);
                wg_pos++;
            }
            bool arrived = nr_len == 0 && contact.pos == contact.len && dc > 0.f;
            if(nr_len > 0 && nr_pos < nr_len)
            {
                /* the raised cosine's share this sample */
                const float u0 = (float)nr_pos / (float)nr_len, u1 = (float)(nr_pos + 1) / (float)nr_len;
                dc = 0.5f * (std::cos(3.1415927f * u0) - std::cos(3.1415927f * u1));
                nr_pos++;
                arrived = nr_pos == nr_len;
            }
            float nz = 0.f;
            if(noise.on) nz = noise.Run(dc);
            if(arrived) noise.Arrived();
            float g = 0.f;
            if(variant >= 2)
            {
                for(int j = 0; j < 2; j++)
                {
                    if(!wg[j].on) continue;
                    g += wg[j].Run(j == wa ? x : 0.f);
                    if(wg[j].Done() && j != wa) wg[j].on = false;
                }
            }
            out[i] += nz + g;
            if(o_noise) o_noise[i] += nz;
            if(o_wg) o_wg[i] += g;
        }
    }
};

} // namespace kykp
