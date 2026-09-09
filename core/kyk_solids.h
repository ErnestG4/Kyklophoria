/* kyk_solids.h — worlds built on the vertices of a 4-dimensional solid.
 *
 * Will's description of what the instrument should feel like: "if you align
 * on any given plane you catch that waveform locking into a familiar shape,
 * and as quickly as you cross it you're pulled back into the mire."
 *
 * That is a specification, and this is the shape of world that satisfies it
 * directly. Put a recognisable waveform at each vertex of a regular polytope
 * and weight them by distance. At a vertex you get that wave exactly. Step
 * off it and the neighbours crowd in. The tightness of the lock is one
 * parameter.
 *
 * Two properties make it worth having beyond the poetry:
 *
 *   Non-separable.  Every other legible family here weights the harmonic
 *                   series multiplicatively, and multiplicative weightings
 *                   add in log magnitude, which is separable by construction
 *                   — measured at 6 to 11x direction spread. Distance is not
 *                   separable: |p − v|² couples every axis at once. This is
 *                   the first legible family that has a chance at isotropy.
 *
 *   Analytic.       A closed form in the position, so it evaluates anywhere
 *                   without a lattice, and the whole world is the vertex
 *                   table — a few kilobytes the page can hold and draw from.
 *
 * Four dimensions is a good place for this. There are six regular polytopes
 * in 4-D rather than the five Platonic solids of 3-D, and the 24-cell among
 * them has no three-dimensional analogue at all: 24 vertices, self-dual, a
 * shape that only exists here. Since our default N is 4, we can use it.
 */
#pragma once
#include "kyk_world.h"
#include "kyk_rotate.h"   /* SinCosTurns, for the pulse spectra */

namespace kyk {
namespace solids {

enum class Solid : uint8_t
{
    Cell24 = 0,   /* 24 vertices, unique to 4-D, self-dual */
    Cell16 = 1,   /* 8, the cross-polytope: axis directions only */
    Tesseract = 2 /* 16, the corners of the cube */
};

constexpr int kMaxVerts = World::kMaxVerts;

/* Deliberately has no default member initialisers, and must keep none.
 *
 * A table is 17 KB, so the shell puts it in SDRAM. Giving this struct a
 * non-zero default — n = 4, k = 64, sigma = 0.20f, as it had — makes it
 * non-trivially-constructible, and the compiler then emits a constructor call
 * into .init_array. That runs before main(), which is before hw.Init()
 * configures the FMC, so it stores to an SDRAM controller that is not up yet
 * and the module hard-faults before it reaches a single line of our code. It
 * shipped that way and bricked every boot until it was flashed back.
 *
 * Trivial means it lands in real .bss and nothing writes it before main.
 * Build() sets count, n, k and sigma on every path, so nothing is lost; a
 * table that was never built reads count 0, and World::UseVertices rejects
 * that as Kind::None rather than misbehaving. The shell static_asserts the
 * triviality at the point of declaration. */
struct VertexTable
{
    int   count;
    int   n, k;
    float pos[kMaxVerts][kMaxN];    /* in [0,1]^N, the engine's own coordinates */
    float spec[kMaxVerts][kMaxK];   /* unit-RMS magnitudes */

    float sigma;                    /* at sharp = 0; the Morph knob tightens it */
};

/* ── the waveforms that sit on the vertices ──────────────────────────────
 * Chosen to be recognisable on their own, so that arriving at one is an
 * event rather than a shade of the last. Each is unit RMS. */
inline void VertexWave(int which, int k, float* m)
{
    const int kind = which % 12;
    const int var  = (which / 12) % 2;
    float     e    = 0.f;
    for(int i = 0; i < k; i++)
    {
        const float h = (float)(i + 1);
        const int   odd = (i % 2) == 0;    /* harmonic 1,3,5… */
        float       a = 0.f;
        switch(kind)
        {
            case 0: a = 1.f / h; break;                                  /* saw          */
            case 1: a = odd ? 1.f / h : 0.f; break;                      /* square       */
            case 2: a = odd ? 1.f / (h * h) : 0.f; break;                /* triangle     */
            case 3: a = (i == 0) ? 1.f : 0.f; break;                     /* sine         */
            case 4: { float s, c; SinCosTurns(0.25f * h, s, c);          /* 25% pulse    */
                      a = (s < 0.f ? -s : s) / h; } break;
            case 5: { float s, c; SinCosTurns(0.10f * h, s, c);          /* 10% pulse    */
                      a = (s < 0.f ? -s : s) / h; } break;
            case 6: a = 1.f / (h * h); break;                            /* parabola     */
            case 7: a = (i < 8) ? 1.f : 0.f; break;                      /* 8-partial    */
            case 8: { const float d = (h - 6.f) / 2.5f;                  /* low formant  */
                      a = detail::Exp(-0.5f * d * d); } break;
            case 9: { const float d = (h - 18.f) / 5.f;                  /* high formant */
                      a = detail::Exp(-0.5f * d * d); } break;
            case 10: a = (i % 3 == 0) ? 1.f / h : 0.f; break;            /* every third  */
            default: { const float d = (h - 1.f) / 9.f;                  /* soft cluster */
                       a = detail::Exp(-0.5f * d * d) / h; } break;
        }
        if(var && a > 0.f) a *= detail::Powf01(h, -0.5f);   /* the darker twin */
        m[i] = a;
        e += a * a;
    }
    const float g = e > 0.f ? Sqrt(2.f) / Sqrt(e) : 0.f;
    for(int i = 0; i < k; i++) m[i] *= g;
}

/* Vertex positions, mapped into the engine's [0,1]^N cube. */
inline void Build(Solid s, int k, VertexTable& t)
{
    t.n = 4;
    t.k = k < 1 ? 1 : (k > kMaxK ? kMaxK : k);
    int c = 0;
    auto put = [&](float a, float b, float d, float e) {
        if(c >= kMaxVerts) return;
        /* the polytopes are defined about the origin; the cube's centre is
         * 0.5 and its half-width 0.5, so scale to just inside the faces */
        t.pos[c][0] = 0.5f + 0.42f * a;
        t.pos[c][1] = 0.5f + 0.42f * b;
        t.pos[c][2] = 0.5f + 0.42f * d;
        t.pos[c][3] = 0.5f + 0.42f * e;
        c++;
    };
    if(s == Solid::Cell24)
    {
        /* every permutation of (±1, ±1, 0, 0), normalised so |v| = 1 */
        const float r = 0.70710678f;
        for(int i = 0; i < 4; i++)
            for(int j = i + 1; j < 4; j++)
                for(int si = 0; si < 2; si++)
                    for(int sj = 0; sj < 2; sj++)
                    {
                        float v[4] = {0, 0, 0, 0};
                        v[i] = si ? r : -r;
                        v[j] = sj ? r : -r;
                        put(v[0], v[1], v[2], v[3]);
                    }
        t.sigma = 0.20f;
    }
    else if(s == Solid::Cell16)
    {
        for(int i = 0; i < 4; i++)
            for(int sg = 0; sg < 2; sg++)
            {
                float v[4] = {0, 0, 0, 0};
                v[i] = sg ? 1.f : -1.f;
                put(v[0], v[1], v[2], v[3]);
            }
        t.sigma = 0.30f;
    }
    else
    {
        const float r = 0.5f;
        for(int m = 0; m < 16; m++)
            put((m & 1) ? r : -r, (m & 2) ? r : -r, (m & 4) ? r : -r, (m & 8) ? r : -r);
        t.sigma = 0.24f;
    }
    t.count = c;
    for(int i = 0; i < c; i++) VertexWave(i, t.k, t.spec[i]);
}

/* Point a World at a table. The table must outlive the world. */
inline void Use(const VertexTable& t, World& w, int p, const uint8_t* topo)
{
    VertexField v;
    v.pos = &t.pos[0][0];
    v.spec = &t.spec[0][0];
    v.count = t.count;
    v.n = t.n;
    v.k = t.k;
    v.sigma = t.sigma;
    w.UseVertices(v, p, topo);
}

} // namespace solids
} // namespace kyk
