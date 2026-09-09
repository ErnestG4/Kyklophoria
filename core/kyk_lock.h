/* kyk_lock.h — the 24-cell again, but with waveforms you can name.
 *
 * The vertex worlds were built for one sentence of Will's: "if you align on
 * any given plane you catch that waveform locking into a familiar shape, and
 * as quickly as you cross it you're pulled back into the mire." They have
 * never delivered it, and now we know why — they render at random phase, so
 * the "saw" on a vertex has a saw's spectrum and none of a saw's shape. This
 * is the same polytope with the same distance weighting, rendered at sine
 * phase with signed coefficients, so arriving at a vertex arrives at the
 * actual waveform.
 *
 * The placement is the other half of that sentence. The 24-cell's vertices are
 * every permutation of (±1, ±1, 0, 0), which falls naturally into six groups
 * of four — one group per coordinate plane, and those are exactly the six
 * Givens planes the rotation turns in. So each plane carries one *family*:
 * align with the (0,1) plane and you are among pulses, align with (1,2) and
 * you are among harmonic stacks. Which plane you line up on decides what kind
 * of thing you find there, which is a good deal more interesting than
 * twenty-four unrelated waves scattered over a solid.
 *
 *   plane (0,1)   square and three pulses
 *   plane (0,2)   the saw and its brighter and darker cousins
 *   plane (0,3)   the triangle, sharpening toward a parabola
 *   plane (1,2)   sine, and stacks of two, three and four partials
 *   plane (1,3)   bigger stacks: six, eight, twelve, sixteen
 *   plane (2,3)   combs — every second, third, fourth, fifth harmonic
 *
 * All four canonical shapes are on it: square at the top of the first group,
 * saw at the top of the second, triangle the third, sine the fourth.
 */
#pragma once
#include "kyk_shapes.h"

namespace kyk {

struct LockField
{
    int   count = 24;
    int   n = 4, k = kShapeK;
    /* Basin width. Small locks hard onto each vertex and leaves the ground
     * between them murky, which is the point; the Morph knob narrows it
     * further. */
    float sigma = 0.26f;
    float pos[kWorldNodes][kMaxN];
};

namespace detail {

/* The waveform for vertex `v`, as signed coefficients on the sine basis.
 * Groups of four, in the vertex order the polytope is built in, so group g is
 * the family sitting on Givens plane g. */
inline void LockWave(int v, int k, float* c)
{
    for(int i = 0; i < k; i++) c[i] = 0.f;
    const int g = (v / 4) % 6, m = v % 4;
    switch(g)
    {
        case 0:
        {
            /* bipolar pulse; 0.5 is the square */
            const float duty[4] = {0.5f, 0.35f, 0.22f, 0.12f};
            for(int h = 1; h <= k; h++)
            {
                float sn, cs;
                SinCosTurns((float)h * duty[m], sn, cs);
                c[h - 1] = 2.f * (1.f - cs) / (3.14159265f * (float)h);
            }
            break;
        }
        case 1:
        {
            /* saw family: every harmonic at 1/h^p, p = 1 is the saw */
            const float p[4] = {1.0f, 0.82f, 0.70f, 1.25f};
            for(int h = 1; h <= k; h++) c[h - 1] = detail::Powf01((float)h, -p[m]);
            break;
        }
        case 2:
        {
            /* triangle family: odd harmonics, alternating signs, 1/h^p */
            const float p[4] = {2.0f, 1.75f, 1.5f, 2.4f};
            for(int h = 1; h <= k; h += 2)
            {
                const float sg = (((h - 1) / 2) & 1) ? -1.f : 1.f;
                c[h - 1] = sg * detail::Powf01((float)h, -p[m]);
            }
            break;
        }
        case 3:
        {
            /* small stacks; one partial is the sine */
            const int n[4] = {1, 2, 3, 4};
            for(int h = 1; h <= n[m] && h <= k; h++) c[h - 1] = kInvHarm[h - 1];
            break;
        }
        case 4:
        {
            const int n[4] = {6, 8, 12, 16};
            for(int h = 1; h <= n[m] && h <= k; h++) c[h - 1] = kInvHarm[h - 1];
            break;
        }
        default:
        {
            /* combs: every s'th harmonic. Hollow and glassy, and the one
             * family here with no textbook name. */
            const int s[4] = {2, 3, 4, 5};
            for(int h = s[m]; h <= k; h += s[m]) c[h - 1] = kInvHarm[h - 1];
            break;
        }
    }
    float e = 0.f;
    for(int i = 0; i < k; i++) e += c[i] * c[i];
    const float g2 = e > 0.f ? Sqrt(2.f) / Sqrt(e) : 0.f;
    for(int i = 0; i < k; i++) c[i] *= g2;
}

/* Vertex positions and spectra. The polytope is every permutation of
 * (±1, ±1, 0, 0) normalised, walked plane by plane so vertex group g lies in
 * Givens plane g — which is what puts a family on a plane. */
inline void BuildLockNodes(LockField& f, float (*node)[kShapeK])
{
    if(f.k > kShapeK) f.k = kShapeK;
    const float r = 0.70710678f;
    int         c = 0;
    for(int i = 0; i < 4; i++)
        for(int j = i + 1; j < 4; j++)
            for(int si = 0; si < 2; si++)
                for(int sj = 0; sj < 2; sj++)
                {
                    if(c >= kWorldNodes) break;
                    float v[4] = {0.f, 0.f, 0.f, 0.f};
                    v[i] = si ? r : -r;
                    v[j] = sj ? r : -r;
                    for(int a = 0; a < 4; a++) f.pos[c][a] = 0.5f + 0.42f * v[a];
                    for(int a = 4; a < kMaxN; a++) f.pos[c][a] = 0.5f;
                    LockWave(c, f.k, node[c]);
                    c++;
                }
    f.count = c;
}

} // namespace detail

/* The whole world as 24 bytes plus the positions, for the page. */
constexpr int kLockBlobBytes = 4 + 4 + 4;

inline int LockBlob(const LockField& f, uint8_t* out)
{
    out[0] = 0xFFu;
    out[1] = 7u;                    /* World::Kind::Lock */
    out[2] = (uint8_t)f.n;
    out[3] = (uint8_t)f.k;
    out[4] = (uint8_t)f.count;
    out[5] = 0; out[6] = 0; out[7] = 0;
    std::memcpy(out + 8, &f.sigma, 4);
    return kLockBlobBytes;
}

} // namespace kyk
