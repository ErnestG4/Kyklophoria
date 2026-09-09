/* cont_check — every axis of every world must be continuous.
 *
 * A discontinuity in the rendered cycle is a click, and it is the one defect
 * this instrument's whole design is supposed to make impossible. The morph is
 * linear in the coefficients so blending cannot click; but a world is free to
 * put a step in the coefficients themselves, and twice in one day it did:
 *
 *   - the shaper axes stepped the band limit by a whole harmonic, which at a
 *     folder-reduced cutoff of 16 put 0.46 into the frame, and stepped the
 *     ring modulator's integer ratio, which put in 1.10 — larger than the
 *     frame's own norm.
 *   - a modal prototype rounded each mode to its nearest harmonic, so modes
 *     hopped bins as the geometry moved. That one flattered itself: it
 *     measured 6.0 "variety" and three quarters of it was the jumps.
 *
 * The test distinguishes a jump from a merely fast axis by measuring the same
 * sweep at two step sizes. Halve the step and a continuous function halves its
 * largest step; a discontinuity does not move at all. So the ratio between the
 * two is about 4 for something smooth and about 1 for a cliff, and no
 * threshold on the step size alone can tell them apart — which is why the
 * first version of the shaper passed every test we had.
 */
#include "kyk_worlds.h"
#include "kyk_engine.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace kyk;

static int      fails = 0;
static Engine   eng;
static double   cur[kFrame], prev[kFrame];

static void FrameAt(const World& w, const float* p)
{
    eng.Init(&w, 48000.f);
    eng.render_div = 1;
    eng.gain = 1.f;
    eng.SetF0(110.f);
    eng.SetPosition(p, w.N());
    float out[24];
    for(int i = 0; i < 6; i++) eng.Process(out, 24);
    const float* f = eng.Frame();
    double       n2 = 0;
    for(int i = 0; i < kFrame; i++) n2 += (double)f[i] * f[i];
    const double inv = n2 > 0 ? 1.0 / std::sqrt(n2) : 0;
    for(int i = 0; i < kFrame; i++) cur[i] = f[i] * inv;
}

/* Largest one-step change in the unit-norm cycle along one axis. */
static double Worst(const World& w, int ax, int steps)
{
    double worst = 0;
    for(int i = 0; i <= steps; i++)
    {
        float p[kMaxN] = {0.5f, 0.5f, 0.35f, 0.35f, 0.5f, 0.5f};
        p[ax] = 0.02f + 0.96f * (float)i / (float)steps;
        FrameAt(w, p);
        if(i)
        {
            double d = 0;
            for(int j = 0; j < kFrame; j++) { const double x = cur[j] - prev[j]; d += x * x; }
            d = std::sqrt(d);
            if(d > worst) worst = d;
        }
        std::memcpy(prev, cur, sizeof cur);
    }
    return worst;
}

int main()
{
    /* Below this the axis is so smooth that the ratio is measuring float
     * noise rather than the world, so there is nothing to judge. */
    const double kFloor = 2e-4;
    /* A continuous axis should give about 4. Anything under this is a step. */
    const double kMinRatio = 2.0;

    static uint8_t blob[1 << 22];
    printf("  %-10s %-6s %11s %11s %7s\n", "world", "axis", "step/500", "step/2000", "ratio");
    for(uint8_t wi = 0; wi < worlds::kCount; wi++)
    {
        const worlds::Entry& e = worlds::Get(wi);
        World                w;
        solids::VertexTable  tbl;
        Space                sp;
        if(e.kind == World::Kind::Lattice)
        {
            const size_t n = worlds::Expand(wi, 4, 8, 64, 8, blob, sizeof blob);
            if(!n || sp.Attach(blob, n) != SpaceError::Ok) continue;
            w.UseLattice(&sp);
        }
        else if(!worlds::Point(wi, w, 8, nullptr, &tbl)) continue;
        if(!w.Ready()) continue;
        /* A world that reports no dimensions is not a world this test can
         * skip quietly: the engine reads K as its band limit, so zero is
         * silence. Registering a kind and forgetting to teach N() and K()
         * about it is exactly how that happens. */
        if(w.N() < 1 || w.K() < 1)
        {
            printf("  %-10s reports N=%d K=%d   <-- BROKEN\n", e.name, w.N(), w.K());
            fails++;
            continue;
        }

        for(int ax = 0; ax < w.N() && ax < 4; ax++)
        {
            const double a = Worst(w, ax, 500);
            const double b = Worst(w, ax, 2000);
            const double r = b > 0 ? a / b : 0;
            const bool   ok = (a < kFloor) || (r >= kMinRatio);
            printf("  %-10s %-6d %11.5f %11.5f %7.2f%s\n", e.name, ax, a, b, r,
                   ok ? "" : "   <-- STEP");
            if(!ok) fails++;
        }
    }
    if(fails) printf("cont_check: %d FAILURES\n", fails);
    else printf("cont_check: all passed\n");
    return fails ? 1 : 0;
}
