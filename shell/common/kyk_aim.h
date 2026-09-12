/* kyk_aim.h — where in another world does this timbre live?
 *
 * The same coordinates mean different things in different worlds: axis 0 of FM
 * is a modulation index and axis 0 of Plate is a strike position. A morph that
 * holds them fixed travels towards whatever those happen to collide at, which
 * is arbitrary and usually worse than the endpoints. This searches the other
 * world for the position whose spectrum is nearest the one playing.
 *
 * Coarse then fine — five per axis, then three at a fifth of the spacing — so
 * 625 plus 81 evaluations rather than a fine grid over the whole space. It runs
 * on the main loop, once, when somebody aims a morph; it is not something that
 * tracks, and it is nowhere near the audio callback.
 *
 * Distance is between unit-normalised spectra, so it compares shape and not
 * level: a quiet match is still a match, and the engine normalises the output
 * regardless.
 *
 * Measured against holding the coordinates fixed, over sixteen positions each:
 * FM to Vowel 0.999 to 0.653, Bar to FM 1.197 to 0.380, Saw to Plate 0.878 to
 * 0.523, Drum to Pulse 0.674 to 0.272. The largest gain is Bar to FM, which is
 * the pair tools/worldbasis measured as the most orthogonal of the set — the
 * less two worlds agree about what their axes mean, the more there is to gain.
 */
#pragma once
#include "kyk_world.h"

namespace kyk {

inline void AimSearch(const World& live, const World& other, const float* p0,
                      float sharp, float* off)
{
    for(int a = 0; a < kMaxN; a++) off[a] = 0.f;
    const int n = other.N() < live.N() ? other.N() : live.N();
    const int k = other.K() < live.K() ? other.K() : live.K();
    if(n < 1 || k < 1) return;

    float here[kMaxK], mags[kMaxK], pl[kMaxP];
    Weights wt;
    live.Evaluate(p0, sharp, here, pl, wt);
    float e = 0.f;
    for(int i = 0; i < k; i++) e += here[i] * here[i];
    const float inv = e > 0.f ? 1.f / Sqrt(e) : 0.f;
    for(int i = 0; i < k; i++) here[i] *= inv;

    float best[kMaxN], probe[kMaxN];
    for(int a = 0; a < kMaxN; a++) best[a] = p0[a];
    float bestD = 1e30f;

    for(int pass = 0; pass < 2; pass++)
    {
        const int   steps = pass ? 3 : 5;
        const float span  = pass ? 0.2f : 1.0f;
        const float lo    = pass ? -span * 0.5f : 0.f;
        float centre[kMaxN];
        for(int a = 0; a < kMaxN; a++) centre[a] = best[a];
        int idx[kMaxN] = {0, 0, 0, 0, 0, 0};
        for(;;)
        {
            for(int a = 0; a < kMaxN; a++) probe[a] = centre[a];
            for(int a = 0; a < n; a++)
            {
                const float u = steps > 1 ? (float)idx[a] / (float)(steps - 1) : 0.5f;
                probe[a] = pass ? centre[a] + lo + span * u : span * u;
            }
            float pf[kMaxN];
            other.Fold(probe, pf);
            other.Evaluate(pf, sharp, mags, pl, wt);
            float me = 0.f;
            for(int i = 0; i < k; i++) me += mags[i] * mags[i];
            const float mi = me > 0.f ? 1.f / Sqrt(me) : 0.f;
            float d = 0.f;
            for(int i = 0; i < k; i++) { const float x = mags[i] * mi - here[i]; d += x * x; }
            if(d < bestD) { bestD = d; for(int a = 0; a < kMaxN; a++) best[a] = probe[a]; }

            int a = 0;
            for(; a < n; a++) { if(++idx[a] < steps) break; idx[a] = 0; }
            if(a == n) break;
        }
    }
    for(int a = 0; a < kMaxN; a++) off[a] = best[a] - p0[a];
}

} // namespace kyk
