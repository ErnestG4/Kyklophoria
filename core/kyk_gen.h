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

struct GenParams
{
    int      n        = 4;
    int      side     = 4;
    int      k        = 64;
    int      p        = 8;
    uint32_t seed     = 1;         /* phase seed written to the header */
    uint8_t  topo[kMaxN] = {0, 0, 0, 0, 0, 0};
    const char* name  = "harmonic";
};

namespace detail {
inline float Powf01(float base, float e)
{
    /* base^e for base > 0 via exp2/log2 series — no libm, deterministic.
     * Only used offline in generation, so accuracy beats speed here. */
    /* log2(base): base = m·2^x, m in [1,2) */
    int   x = 0;
    float m = base;
    while(m >= 2.f) { m *= 0.5f; x++; }
    while(m < 1.f) { m *= 2.f; x--; }
    /* log2(m) by atanh series: ln(m) = 2·atanh((m-1)/(m+1)) */
    const float z  = (m - 1.f) / (m + 1.f);
    const float z2 = z * z;
    float       term = z, sum = 0.f;
    for(int i = 1; i < 40; i += 2) { sum += term / (float)i; term *= z2; }
    const float ln = 2.f * sum;
    const float y  = e * ((float)x * 0.69314718056f + ln);   /* ln(result) */
    /* exp(y): range-reduce by ln2 then Taylor */
    int   q = (int)(y / 0.69314718056f);
    float r = y - (float)q * 0.69314718056f;
    float t = 1.f, s = 1.f;
    for(int i = 1; i < 20; i++) { t *= r / (float)i; s += t; }
    while(q > 0) { s *= 2.f; q--; }
    while(q < 0) { s *= 0.5f; q++; }
    return s;
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
inline size_t BuildLattice(const GenParams& g, uint8_t* out, size_t cap)
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

} // namespace kyk
