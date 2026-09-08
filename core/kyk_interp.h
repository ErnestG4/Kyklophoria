/* kyk_interp.h — topology fold and multilinear lattice interpolation.
 *
 * Fold(): control-frame coordinates → lattice coordinates in [0,1] per axis
 *         (Clamp: saturate; Wrap: mod 1). Sphere is M2 and folds all axes at
 *         once, so it lives in the engine, not here.
 * LatticeWeights(): the 2^N corners around p and their multilinear weights.
 *         Under Wrap the upper corner index wraps to 0, so the seam between
 *         lattice column side-1 and column 0 interpolates like any other.
 * Blend(): weighted sum of corner spectra and payloads (K + P MACs per corner).
 */
#pragma once
#include "kyk_space.h"

namespace kyk {

struct Weights
{
    int      n_corners = 0;            /* 2^N */
    uint32_t idx[kMaxCorners];         /* lattice point index per corner */
    float    w[kMaxCorners];           /* weights, sum to 1 */
    int      cell[kMaxN];              /* lower corner per axis */
    float    frac[kMaxN];              /* position inside the cell, [0,1] */
};

inline float FoldAxis(float x, Topo t)
{
    switch(t)
    {
        case Topo::Wrap: return Fract(x);
        case Topo::Clamp:
        default: return Clamp01(x);
    }
}

inline void Fold(const Space& s, const float* c, float* p)
{
    for(int a = 0; a < s.N(); a++) p[a] = FoldAxis(c[a], s.TopoOf(a));
}

/* p: folded coordinates, each in [0,1]. */
inline void LatticeWeights(const Space& s, const float* p, Weights& out)
{
    const int n    = s.N();
    const int side = s.Side();
    int   hi[kMaxN];
    float f1[kMaxN];
    for(int a = 0; a < n; a++)
    {
        const bool wrap = s.TopoOf(a) == Topo::Wrap;
        /* Clamp: side-1 intervals span [0,1]. Wrap: side intervals, the last
         * one running from column side-1 back to column 0. */
        const float u  = p[a] * (float)(wrap ? side : side - 1);
        int         i  = (int)u;
        if(i < 0) i = 0;
        const int   imax = wrap ? side - 1 : side - 2;
        if(i > imax) i = imax;
        float f = u - (float)i;
        if(f < 0.f) f = 0.f;
        if(f > 1.f) f = 1.f;
        out.cell[a] = i;
        out.frac[a] = f;
        hi[a]       = (i + 1 == side) ? 0 : i + 1;   /* only reachable under Wrap */
        f1[a]       = f;
    }
    const int corners = 1 << n;
    out.n_corners     = corners;
    int ix[kMaxN];
    for(int c = 0; c < corners; c++)
    {
        float w = 1.f;
        for(int a = 0; a < n; a++)
        {
            const bool upper = (c >> a) & 1;
            ix[a] = upper ? hi[a] : out.cell[a];
            w *= upper ? f1[a] : (1.f - f1[a]);
        }
        out.idx[c] = s.LatticeIndex(ix);
        out.w[c]   = w;
    }
}

/* mags[K], payload[P] ← Σ w_c · corner_c. Corners with zero weight are
 * skipped, which is exact and keeps a lattice-node read equal to the node.
 *
 * `level` restores the loudness the corners agreed on. Parseval makes the
 * rendered RMS proportional to ||m||_2, and a weighted sum of unit vectors is
 * shorter than a unit vector unless they are parallel — by up to 3.01·N dB
 * when the corner spectra have disjoint support. The shipped Harmonic space
 * loses 0.02 dB because its cells are nearly parallel, but narrow formants
 * lose 4 to 10 dB, which is audible ducking in the middle of every morph
 * (tests/morph_check, docs/m2-notes.md). Rescaling to the weighted average of
 * the corner lengths costs one extra pass and a divide.
 *
 * This does not put a discontinuity anywhere: the scale is a smooth function
 * of position, and at a node it is exactly 1. What it does cost is the
 * identity "blending magnitudes equals averaging waveforms" — which was never
 * the property that keeps the morph clean. That property is that the renderer
 * is linear in the magnitude vector, so partials cannot cancel, and it holds
 * either way.
 */
enum class BlendLevel : uint8_t { Raw = 0, Preserve = 1 };

/* Sharpen the interpolation from smooth toward nearest-cell.
 *
 * `sharp` 0 leaves multilinear weights alone. Raising it pushes weight onto
 * the nearest corner, so the morph concentrates near cell boundaries and the
 * space starts to read as a bank of discrete waves rather than a continuum.
 * This is the one control both halves of the survey said to steal: Plaits
 * runs a `quantization` ramp along its z axis from smooth morph to hard
 * stepping with no mode switch, and Piston Honda mk2 puts morph resolution
 * under CV (docs/lit/eurorack.md).
 *
 * Implemented by repeated squaring so it costs a handful of multiplies rather
 * than a pow per corner, and interpolating between consecutive powers so the
 * exponent moves continuously. A lattice node still reads exactly: one weight
 * is 1 and the rest are 0, and every power preserves that. */
inline void SharpenWeights(Weights& wt, float sharp)
{
    if(sharp <= 0.f) return;
    if(sharp > 1.f) sharp = 1.f;
    const float s = sharp * 4.f;           /* exponent 1, 2, 4, 8, 16 */
    int         i = (int)s;
    if(i > 3) i = 3;
    const float f   = s - (float)i;
    float       sum = 0.f;
    for(int c = 0; c < wt.n_corners; c++)
    {
        float w = wt.w[c];
        for(int p = 0; p < i; p++) w *= w;   /* w^(2^i) */
        const float w2 = w * w;              /* the next power up */
        wt.w[c]        = w + (w2 - w) * f;
        sum += wt.w[c];
    }
    if(sum <= 0.f) return;
    const float inv = 1.f / sum;
    for(int c = 0; c < wt.n_corners; c++) wt.w[c] *= inv;
}

inline void Blend(const Space& s, const Weights& wt, float* mags, float* payload,
                  BlendLevel level = BlendLevel::Preserve)
{
    const int K = s.K(), P = s.P();
    for(int k = 0; k < K; k++) mags[k] = 0.f;
    for(int j = 0; j < P; j++) payload[j] = 0.f;
    float target = 0.f;   /* Σ w_c · ||m_c|| : the length the corners agree on */
    for(int c = 0; c < wt.n_corners; c++)
    {
        const float w = wt.w[c];
        if(w == 0.f) continue;
        const float* m = s.Mags(wt.idx[c]);
        float        e = 0.f;
        for(int k = 0; k < K; k++)
        {
            mags[k] += w * m[k];
            e += m[k] * m[k];
        }
        target += w * Sqrt(e);
        const float* pl = s.Payload(wt.idx[c]);
        for(int j = 0; j < P; j++) payload[j] += w * pl[j];
    }
    if(level == BlendLevel::Raw) return;
    float got = 0.f;
    for(int k = 0; k < K; k++) got += mags[k] * mags[k];
    got = Sqrt(got);
    if(got <= 1e-20f || target <= 0.f) return;
    const float g = target / got;                     /* ≥ 1, = 1 at a node */
    for(int k = 0; k < K; k++) mags[k] *= g;
}

} // namespace kyk
