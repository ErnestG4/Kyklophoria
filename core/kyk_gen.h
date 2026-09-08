/* kyk_gen.h — generative fill: a lattice whose axes are generator parameters.
 *
 * M0 ships one family, Harmonic, chosen so every axis is legible:
 *   axis 0  shape    saw → square (odd harmonics only) via a blend of the two
 *   axis 1  tilt     spectral slope, 0 = as generated (bright), 1 = k^-2 darker
 *   axis 2  formant  a resonant peak sweeping from harmonic 1 to K
 *   axis 3  hollow   even harmonics scaled 1 → 0 (full → clarinet)
 *   axes 4,5 (N > 4) comb: notch every 2nd / 3rd harmonic, depth by position
 * Spectra are RMS-normalised so cells sit at one loudness; payload lanes
 * are simple functions of position (docs/space-format.md lists them).
 * Extra families (formant stacks, bells, seeded spectral noise, WAV import)
 * are M4 and slot in beside this one.
 *
 * BuildLattice() writes a complete blob into caller memory; no allocation.
 */
#pragma once
#include "kyk_space.h"

namespace kyk {

/* Which generator filled the space. The file format does not record this —
 * a blob is a blob — but the tools name it so a space can be regenerated. */
enum class Family : uint8_t { Harmonic = 0, Field = 1 };

struct GenParams
{
    Family   family   = Family::Harmonic;
    /* Field only: `rough` is the spread of the log-magnitude field in nepers
     * (bigger = wilder spectra), `smooth` the number of lattice smoothing
     * passes (bigger = longer correlation length, so neighbouring cells are
     * more alike), `tilt` the exponent of the base rolloff the field shapes. */
    float    rough    = 0.9f;
    int      smooth   = 1;
    float    tilt     = 0.7f;
    int      n        = 4;
    int      side     = 4;
    int      k        = 64;
    int      p        = 8;
    uint32_t seed     = 1;         /* phase seed written to the header */
    uint8_t  topo[kMaxN] = {0, 0, 0, 0, 0, 0};
    const char* name  = "harmonic";
};

namespace detail {
constexpr float kLn2 = 0.69314718056f;

/* ln(x) for x > 0, by the atanh series on the reduced mantissa. No libm, so
 * the desktop and the module generate byte-identical spaces. */
inline float Ln(float x)
{
    if(x <= 0.f) return -87.f;              /* keeps the series finite */
    int   e = 0;
    float m = x;
    while(m >= 2.f) { m *= 0.5f; e++; }
    while(m < 1.f) { m *= 2.f; e--; }
    const float z = (m - 1.f) / (m + 1.f), z2 = z * z;
    float       term = z, sum = 0.f;
    for(int i = 1; i < 40; i += 2) { sum += term / (float)i; term *= z2; }
    return (float)e * kLn2 + 2.f * sum;
}

/* exp(y): range-reduce by ln2, then Taylor on the remainder. */
inline float Exp(float y)
{
    if(y > 88.f) y = 88.f;
    if(y < -88.f) y = -88.f;
    int   q = (int)(y / kLn2);
    float r = y - (float)q * kLn2;
    float t = 1.f, s = 1.f;
    for(int i = 1; i < 20; i++) { t *= r / (float)i; s += t; }
    while(q > 0) { s *= 2.f; q--; }
    while(q < 0) { s *= 0.5f; q++; }
    return s;
}

inline float Powf01(float base, float e) { return Exp(e * Ln(base)); }

/* Approximately normal, mean 0, variance 1, from the same xorshift the rest of
 * the core uses. Twelve uniforms rather than Box-Muller: no log, no cos, and
 * the tails past ±6 sigma do not matter for content. */
inline float Gauss(Rng& r)
{
    float s = 0.f;
    for(int i = 0; i < 12; i++) s += r.Uniform();
    return s - 6.f;
}
} // namespace detail

/* One hyperpoint of the Harmonic family at lattice position u[a] ∈ [0,1]. */
inline void HarmonicPoint(const GenParams& g, const float* u, float* mags, float* payload)
{
    const int   K     = g.k;
    const float shape = u[0];
    const float tilt  = g.n > 1 ? u[1] : 0.f;
    const float form  = g.n > 2 ? u[2] : 0.5f;
    const float holl  = g.n > 3 ? u[3] : 0.f;
    const float comb2 = g.n > 4 ? u[4] : 0.f;
    const float comb3 = g.n > 5 ? u[5] : 0.f;
    const float fc    = 1.f + form * (float)(K - 1);      /* formant centre, harmonics */
    const float bw    = 0.15f * (float)K + 1.f;           /* formant half-width */
    float       e     = 0.f;
    for(int i = 0; i < K; i++)
    {
        const float k   = (float)(i + 1);
        const float saw = 1.f / k;
        const float sq  = (i % 2 == 0) ? 1.f / k : 0.f;   /* odd harmonics (k = 1,3,5…) */
        float       m   = saw + (sq - saw) * shape;
        m *= detail::Powf01(k, -2.f * tilt);
        const float d = (k - fc) / bw;
        m *= 1.f + 3.f * (1.f / (1.f + d * d));          /* +12 dB resonant bump */
        if(i % 2 == 1) m *= 1.f - holl;                    /* even harmonics */
        if(i % 2 == 1) m *= 1.f - comb2;
        if(i % 3 == 2) m *= 1.f - comb3;
        mags[i] = m;
        e += m * m;
    }
    /* unit RMS: Σ m² = 2 ⇒ rms of Σ m cos = 1 */
    float norm = e > 0.f ? 1.f / detail::Powf01(e * 0.5f, 0.5f) : 0.f;
    for(int i = 0; i < K; i++) mags[i] *= norm;

    /* payload lanes, docs/space-format.md §payload */
    float pl[kMaxP];
    pl[0] = 1.f - tilt;                                  /* cutoff */
    pl[1] = form;                                        /* resonance */
    pl[2] = shape * holl;                                /* fm index */
    pl[3] = shape;                                       /* drive */
    pl[4] = holl;                                        /* cv out a */
    {
        float r = 0.f;                                   /* cv out b: distance from centre */
        for(int a = 0; a < g.n; a++) r += (u[a] - 0.5f) * (u[a] - 0.5f);
        pl[5] = detail::Powf01(r + 1e-9f, 0.5f) * 2.f;
    }
    pl[6] = 0.5f + 0.5f * (shape - tilt);
    pl[7] = form * (1.f - holl);
    for(int j = 0; j < g.p; j++) payload[j] = pl[j] < 0.f ? 0.f : (pl[j] > 1.f ? 1.f : pl[j]);
}

/* Write a whole lattice blob. Returns bytes written, 0 if cap is too small
 * or the params are outside the limits. */
inline size_t BuildHarmonicLattice(const GenParams& g, uint8_t* out, size_t cap)
{
    if(g.n < 1 || g.n > kMaxN || g.k < 1 || g.k > kMaxK || g.p < 0 || g.p > kMaxP || g.side < 2 || g.side > 255)
        return 0;
    const size_t need = Space::BlobSize(g.n, g.k, g.p, g.side, false);
    if(cap < need) return 0;
    SpaceHeader h;
    std::memset(&h, 0, sizeof(h));
    h.magic = kSpaceMagic;
    h.version = kSpaceVersion;
    h.n = (uint8_t)g.n;
    h.mode = kModeLattice;
    h.k = (uint8_t)g.k;
    h.p = (uint8_t)g.p;
    h.side = (uint8_t)g.side;
    h.flags = 0;
    for(int a = 0; a < kMaxN; a++) h.topo[a] = g.topo[a];
    h.phase_seed = g.seed;
    uint32_t count = 1;
    for(int a = 0; a < g.n; a++) count *= (uint32_t)g.side;
    h.point_count = count;
    std::strncpy(h.name, g.name ? g.name : "", sizeof(h.name));
    std::memcpy(out, &h, sizeof(h));

    float* w = reinterpret_cast<float*>(out + sizeof(h));
    int    ix[kMaxN] = {0, 0, 0, 0, 0, 0};
    float  u[kMaxN];
    float  mags[kMaxK], payload[kMaxP];
    for(uint32_t i = 0; i < count; i++)
    {
        for(int a = 0; a < g.n; a++) u[a] = (float)ix[a] / (float)(g.side - 1);
        HarmonicPoint(g, u, mags, payload);
        std::memcpy(w, mags, sizeof(float) * (size_t)g.k);
        std::memcpy(w + g.k, payload, sizeof(float) * (size_t)g.p);
        w += (size_t)g.k + (size_t)g.p;
        /* odometer, axis 0 fastest */
        for(int a = 0; a < g.n; a++)
        {
            if(++ix[a] < g.side) break;
            ix[a] = 0;
        }
    }
    return need;
}

/* ── Field family ────────────────────────────────────────────────────────
 * A correlated random field over the lattice, rather than a grid of
 * independent parameter axes.
 *
 * Why: in a separable parameter grid each axis is its own family, so the
 * space has privileged directions — measured on the Harmonic space, the
 * richest direction gives about ten times the timbral movement of the
 * poorest, which makes rotation a lottery. A stationary field's covariance
 * depends only on distance, so every direction is alike (measured spread
 * across directions: 1.8x at side 8, against 9.8x for the grid) and a
 * rotated CV sweep is as rich as an axis-aligned one. That is the property
 * that makes the rotation feature pay. docs/m2-notes.md has the numbers.
 *
 * Construction: white noise per (cell, harmonic), smoothed along each
 * lattice axis to set the correlation length, smoothed lightly along the
 * harmonic index so each cell reads as a spectral envelope rather than
 * hash, rescaled to unit variance (smoothing costs a lot of it), then
 * exponentiated onto a tilted base and normalised per cell. Smoothing
 * honours each axis's topology, so a wrapped axis has no seam in the
 * content. All scratch is one lattice line; nothing is allocated.
 */

/* Smooth `count` interleaved values per point along one lattice axis, in
 * place, with a [1,2,1]/4 kernel. */
inline void SmoothAxis(float* w, size_t stride, int offset, int count,
                       int n, int side, int axis, bool wrap)
{
    if(side < 2 || side > 256) return;
    size_t astride = 1;
    for(int a = 0; a < axis; a++) astride *= (size_t)side;
    size_t total = 1;
    for(int a = 0; a < n; a++) total *= (size_t)side;
    float line[256], sm[256];
    for(size_t i = 0; i < total; i++)
    {
        if((i / astride) % (size_t)side != 0) continue;      /* not a line start */
        for(int j = 0; j < count; j++)
        {
            for(int t = 0; t < side; t++) line[t] = w[(i + (size_t)t * astride) * stride + (size_t)(offset + j)];
            for(int t = 0; t < side; t++)
            {
                const int lo = wrap ? (t + side - 1) % side : (t > 0 ? t - 1 : 0);
                const int hi = wrap ? (t + 1) % side : (t < side - 1 ? t + 1 : side - 1);
                sm[t] = 0.25f * line[lo] + 0.5f * line[t] + 0.25f * line[hi];
            }
            for(int t = 0; t < side; t++) w[(i + (size_t)t * astride) * stride + (size_t)(offset + j)] = sm[t];
        }
    }
}

/* Rescale a strided block of the field to zero mean and unit variance. */
inline void Standardise(float* w, size_t stride, int offset, int count, uint32_t points)
{
    double sum = 0, sum2 = 0;
    const double nvals = (double)points * (double)count;
    if(nvals <= 1.0) return;
    for(uint32_t i = 0; i < points; i++)
        for(int j = 0; j < count; j++)
        {
            const double v = w[(size_t)i * stride + (size_t)(offset + j)];
            sum += v;
            sum2 += v * v;
        }
    const double mean = sum / nvals;
    double       var  = sum2 / nvals - mean * mean;
    if(var < 1e-20) var = 1e-20;
    const float inv = (float)(1.0 / Sqrt((float)var));
    for(uint32_t i = 0; i < points; i++)
        for(int j = 0; j < count; j++)
        {
            float& v = w[(size_t)i * stride + (size_t)(offset + j)];
            v        = (v - (float)mean) * inv;
        }
}

inline size_t BuildFieldLattice(const GenParams& g, uint8_t* out, size_t cap)
{
    if(g.n < 1 || g.n > kMaxN || g.k < 1 || g.k > kMaxK || g.p < 0 || g.p > kMaxP || g.side < 2 || g.side > 255)
        return 0;
    const size_t need = Space::BlobSize(g.n, g.k, g.p, g.side, false);
    if(cap < need) return 0;

    SpaceHeader h;
    std::memset(&h, 0, sizeof(h));
    h.magic   = kSpaceMagic;
    h.version = kSpaceVersion;
    h.n       = (uint8_t)g.n;
    h.mode    = kModeLattice;
    h.k       = (uint8_t)g.k;
    h.p       = (uint8_t)g.p;
    h.side    = (uint8_t)g.side;
    h.flags   = 0;
    for(int a = 0; a < kMaxN; a++) h.topo[a] = g.topo[a];
    h.phase_seed = g.seed;
    uint32_t count = 1;
    for(int a = 0; a < g.n; a++) count *= (uint32_t)g.side;
    h.point_count = count;
    std::strncpy(h.name, g.name ? g.name : "field", sizeof(h.name));
    std::memcpy(out, &h, sizeof(h));

    float*       w      = reinterpret_cast<float*>(out + sizeof(h));
    const size_t stride = (size_t)g.k + (size_t)g.p;

    Rng rng;
    rng.Seed(g.seed ? g.seed : 1u);
    for(uint32_t i = 0; i < count; i++)
        for(size_t j = 0; j < stride; j++) w[(size_t)i * stride + j] = detail::Gauss(rng);

    const int passes = g.smooth < 0 ? 0 : (g.smooth > 8 ? 8 : g.smooth);
    for(int pass = 0; pass < passes; pass++)
        for(int a = 0; a < g.n; a++)
            SmoothAxis(w, stride, 0, (int)stride, g.n, g.side, a, g.topo[a] == (uint8_t)Topo::Wrap);

    /* along the harmonic index: turn hash into an envelope */
    for(uint32_t i = 0; i < count; i++)
        for(int pass = 0; pass < 2; pass++)
        {
            float* m    = w + (size_t)i * stride;
            float  prev = m[0];
            for(int k = 0; k < g.k; k++)
            {
                const float cur = m[k];
                const float nxt = (k + 1 < g.k) ? m[k + 1] : cur;
                m[k]            = 0.25f * prev + 0.5f * cur + 0.25f * nxt;
                prev            = cur;
            }
        }

    /* smoothing eats most of the variance; put it back so `rough` is honest */
    Standardise(w, stride, 0, g.k, count);
    if(g.p > 0) Standardise(w, stride, g.k, g.p, count);

    /* the base rolloff depends only on the harmonic index: 64 series
     * evaluations, not one per cell per harmonic */
    float base[kMaxK];
    for(int k = 0; k < g.k; k++) base[k] = detail::Powf01((float)(k + 1), -g.tilt);

    for(uint32_t i = 0; i < count; i++)
    {
        float* m = w + (size_t)i * stride;
        double e = 0;
        for(int k = 0; k < g.k; k++)
        {
            const float v = base[k] * detail::Exp(g.rough * m[k]);
            m[k]             = v;
            e += (double)v * v;
        }
        const float gn = (float)(Sqrt(2.f) / Sqrt((float)(e > 0 ? e : 1)));
        for(int k = 0; k < g.k; k++) m[k] *= gn;
        /* payload: the field squashed into 0..1, so lanes drift with the space */
        for(int j = 0; j < g.p; j++)
        {
            float v = 0.5f + 0.25f * m[g.k + j];
            m[g.k + j] = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        }
    }
    return need;
}

inline size_t BuildLattice(const GenParams& g, uint8_t* out, size_t cap)
{
    return g.family == Family::Field ? BuildFieldLattice(g, out, cap)
                                     : BuildHarmonicLattice(g, out, cap);
}

} // namespace kyk
