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
 * skipped, which is exact and keeps a lattice-node read equal to the node. */
inline void Blend(const Space& s, const Weights& wt, float* mags, float* payload)
{
    const int K = s.K(), P = s.P();
    for(int k = 0; k < K; k++) mags[k] = 0.f;
    for(int j = 0; j < P; j++) payload[j] = 0.f;
    for(int c = 0; c < wt.n_corners; c++)
    {
        const float w = wt.w[c];
        if(w == 0.f) continue;
        const float* m = s.Mags(wt.idx[c]);
        for(int k = 0; k < K; k++) mags[k] += w * m[k];
        const float* pl = s.Payload(wt.idx[c]);
        for(int j = 0; j < P; j++) payload[j] += w * pl[j];
    }
}

} // namespace kyk
