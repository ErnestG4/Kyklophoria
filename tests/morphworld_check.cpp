/* morphworld_check — blending two worlds must be as click-free as one.
 *
 * The whole claim is that a blend of two spectra is a spectrum: the render is
 * linear in the coefficient vector, so nothing about crossing between worlds
 * can introduce a discontinuity the morph within a world would not. That is an
 * argument, and arguments about this instrument have been wrong before, so it
 * is measured the way cont_check measures an axis — sweep at two step sizes and
 * a continuous function halves its largest step while a cliff does not.
 *
 * The other property is the one that protects everything already shipped:
 * morph zero, or no second world, must be bit-identical to the single-world
 * path. Not close. Identical, or the feature has changed every existing patch.
 *
 * The endpoint needs stating carefully, and getting it wrong here is what
 * taught me. Phase is a property of the world, not of the spectrum: Modal and
 * Bend and Lock render at sine phase, FM and the lattice worlds at a fixed
 * random phase. The blend combines coefficients, and the frame is then rendered
 * at whatever phase the *first* world uses. So morph 1 lands on B's spectrum
 * always, and on B's waveform only when the two share a convention. Bar into FM
 * at morph 1 is FM's spectrum at sine phase, which is a different sound from FM
 * and a perfectly good one — the same fact that makes the Shapes worlds work.
 * The invariant worth testing is therefore about the spectrum, with the frame
 * checked only for pairs that agree on phase.
 */
#include "kyk_worlds.h"
#include "kyk_engine.h"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace kyk;

static int  bad = 0;
static void ck(const char* m, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", m); if(!ok) bad++; }

static Engine eng;
static double cur[kFrame], prev[kFrame];

static void FrameAt(const World& a, const World* b, float t, const float* p)
{
    eng.Init(&a, 48000.f);
    eng.render_div = 1; eng.gain = 1.f;
    eng.SetMorph(b, t);
    eng.SetF0(110.f);
    eng.SetPosition(p, a.N());
    float o[24];
    for(int i = 0; i < 6; i++) eng.Process(o, 24);
    const float* f = eng.Frame();
    double n2 = 0;
    for(int i = 0; i < kFrame; i++) n2 += (double)f[i] * f[i];
    const double inv = n2 > 0 ? 1.0 / std::sqrt(n2) : 0;
    for(int i = 0; i < kFrame; i++) cur[i] = f[i] * inv;
}

/* largest one-step change as the morph sweeps 0 to 1 */
static double SweepMorph(const World& a, const World& b, const float* p, int steps)
{
    double worst = 0;
    for(int i = 0; i <= steps; i++)
    {
        FrameAt(a, &b, (float)i / (float)steps, p);
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
    struct P { uint8_t a, b; const char* an; const char* bn; };
    /* one well-aligned pair, one nearly orthogonal one, and a modal world
       against a bright one — the hardest case tools/worldbasis found */
    const P pairs[] = {
        {worlds::kFm,    worlds::kVowel, "FM",     "Vowel"},
        {worlds::kSaw,   worlds::kEdge,  "Saw",    "Edge"},
        {worlds::kBar,   worlds::kFm,    "Bar",    "FM"},
        {worlds::kPlate, worlds::kPulse, "Plate",  "Pulse"},
    };
    float p[kMaxN] = {0.42f, 0.61f, 0.35f, 0.55f, 0.5f, 0.5f};

    for(const P& pr : pairs)
    {
        World A, B; solids::VertexTable ta, tb;
        if(!worlds::Point(pr.a, A, 8, nullptr, &ta) || !worlds::Point(pr.b, B, 8, nullptr, &tb)) continue;

        /* morph 0 must be the single-world path exactly */
        FrameAt(A, &B, 0.f, p);
        double withB[kFrame]; std::memcpy(withB, cur, sizeof cur);
        FrameAt(A, nullptr, 0.f, p);
        double same = 0;
        for(int i = 0; i < kFrame; i++) same = std::fmax(same, std::fabs(withB[i] - cur[i]));
        char m[128];
        std::snprintf(m, sizeof m, "%s->%s: morph 0 is bit-identical to no morph", pr.an, pr.bn);
        ck(m, same == 0.0);

        /* morph 1 must reach B's spectrum exactly, whatever the phases */
        FrameAt(A, &B, 1.f, p);
        float ma[kMaxK];
        std::memcpy(ma, eng.Mags(), sizeof(float) * (size_t)A.K());
        FrameAt(B, nullptr, 0.f, p);
        double sp = 0;
        for(int i = 0; i < A.K(); i++) sp = std::fmax(sp, std::fabs((double)ma[i] - eng.Mags()[i]));
        const bool same_phase = A.PhaseMode() == B.PhaseMode();
        std::snprintf(m, sizeof m, "%s->%s: morph 1 reaches %s's spectrum (worst bin %.1e)",
                      pr.an, pr.bn, pr.bn, sp);
        ck(m, sp < 1e-6);

        /* and on B's waveform too, when the two agree on phase */
        FrameAt(A, &B, 1.f, p);
        std::memcpy(withB, cur, sizeof cur);
        FrameAt(B, nullptr, 0.f, p);
        double endp = 0;
        for(int i = 0; i < kFrame; i++) endp = std::fmax(endp, std::fabs(withB[i] - cur[i]));
        std::snprintf(m, sizeof m, "%s->%s: %s (worst sample %.1e)", pr.an, pr.bn,
                      same_phase ? "same phase, so morph 1 is B's waveform too"
                                 : "different phase, so morph 1 is B's spectrum in A's phase",
                      endp);
        ck(m, same_phase ? endp < 1e-5 : endp > 1e-3);

        /* and the sweep between them is continuous */
        const double c1 = SweepMorph(A, B, p, 400);
        const double c2 = SweepMorph(A, B, p, 1600);
        const double ratio = c2 > 0 ? c1 / c2 : 0;
        std::snprintf(m, sizeof m, "%s->%s: sweep is continuous (step %.5f -> %.5f, ratio %.2f)",
                      pr.an, pr.bn, c1, c2, ratio);
        ck(m, c1 < 2e-4 || ratio >= 2.0);
    }

    if(bad) printf("morphworld_check: %d FAILURES\n", bad);
    else    printf("morphworld_check: all passed\n");
    return bad ? 1 : 0;
}
