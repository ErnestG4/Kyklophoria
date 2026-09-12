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
#include "kyk_stereo.h"
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

    /* ── the phase spectrum must follow the world ────────────────────────
     *
     * It did not. Phases were derived once in Init and never again, so a
     * sine-phase world reached by switching rendered at whatever convention was
     * live at boot — on the module that is Braids, which is random phase, so
     * every world built for recognisable waveforms rendered without them unless
     * it happened to be the boot world. Measured at the time: Saw fresh against
     * Saw switched differed by 3.46 at a peak of 2.25.
     *
     * Checked in both directions, because a fix that only re-derives on the way
     * into sine phase would leave the same bug on the way out. */
    {
        const uint8_t sine_w = worlds::kSaw, rand_w = worlds::kFm;
        World A, B; solids::VertexTable ta, tb;
        worlds::Point(sine_w, A, 8, nullptr, &ta);
        worlds::Point(rand_w, B, 8, nullptr, &tb);
        ck("the two worlds really do disagree about phase", A.PhaseMode() != B.PhaseMode());

        float pp[kMaxN] = {0.5f, 0.35f, 0.5f, 0.5f, 0.5f, 0.5f};
        double fresh[kFrame], switched[kFrame];
        for(int dir = 0; dir < 2; dir++)
        {
            const World& want = dir ? B : A;
            const World& other = dir ? A : B;
            eng.Init(&want, 48000.f); eng.render_div = 1; eng.gain = 1.f;
            eng.SetMorph(nullptr, 0.f); eng.SetPhaseOverride(World::Phase::Random, false);
            eng.SetF0(110.f); eng.SetPosition(pp, want.N());
            float o[24];
            for(int i = 0; i < 6; i++) eng.Process(o, 24);
            for(int i = 0; i < kFrame; i++) fresh[i] = eng.Frame()[i];

            eng.Init(&other, 48000.f); eng.render_div = 1; eng.gain = 1.f;
            eng.SetF0(110.f); eng.SetPosition(pp, other.N());
            for(int i = 0; i < 6; i++) eng.Process(o, 24);
            eng.SetWorld(&want);
            eng.SetPosition(pp, want.N());
            for(int i = 0; i < 6; i++) eng.Process(o, 24);
            for(int i = 0; i < kFrame; i++) switched[i] = eng.Frame()[i];

            double worst = 0;
            for(int i = 0; i < kFrame; i++) worst = std::fmax(worst, std::fabs(fresh[i] - switched[i]));
            char m[128];
            std::snprintf(m, sizeof m, "%s reached by switching renders as %s at boot (worst %.4f)",
                          dir ? "random phase" : "sine phase", dir ? "random phase" : "sine phase", worst);
            ck(m, worst < 1e-6);
        }
    }

    /* ── everything derived from a world must follow it ──────────────────
     *
     * The phase bug above was one instance of a class, so the class is swept
     * here. The payload is the other one that was real: it is cached against
     * the position rather than the world, so a switch with a still hand left it
     * holding the previous world's numbers — and the payload drives CV out A
     * and the page's lanes, so the module reported a world it was not playing
     * until something moved. */
    {
        World a, b; solids::VertexTable t1, t2;
        worlds::Point(worlds::kFm, a, 8, nullptr, &t1);
        worlds::Point(worlds::kDrum, b, 8, nullptr, &t2);
        StereoEngine se;
        se.Init(&a, 48000.f); se.spread = 0.05f; se.slew_ms = 0.f;
        float c[kMaxN] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        se.SetControl(c, 4);
        float lo[64], ro[64];
        for(int i = 0; i < 8; i++) se.Process(lo, ro, 24);
        float was[kMaxP];
        for(int j = 0; j < 8; j++) was[j] = se.Payload()[j];

        se.SetWorld(&b);                       /* hand perfectly still */
        for(int i = 0; i < 8; i++) se.Process(lo, ro, 24);

        StereoEngine fresh;
        fresh.Init(&b, 48000.f); fresh.spread = 0.05f; fresh.slew_ms = 0.f;
        fresh.SetControl(c, 4);
        for(int i = 0; i < 8; i++) fresh.Process(lo, ro, 24);

        double drift = 0, target = 0;
        for(int j = 0; j < 8; j++)
        {
            drift  = std::fmax(drift,  std::fabs((double)se.Payload()[j] - fresh.Payload()[j]));
            target = std::fmax(target, std::fabs((double)was[j] - fresh.Payload()[j]));
        }
        char m[136];
        std::snprintf(m, sizeof m,
                      "the payload follows a world switch with a still hand (off by %.4f, and it had %.2f to move)",
                      drift, target);
        ck(m, drift < 1e-6 && target > 1e-3);
    }

    /* ── the cosine convention ──────────────────────────────────────────
     *
     * A cosine twin is the same spectrum rendered as a different waveform. It
     * has to actually differ, it must not disturb the spectrum, and its peak
     * has to stay inside what the output gain allows — which is the whole
     * reason for the trim, since cosine phase roughly doubles the crest. */
    {
        World w; solids::VertexTable t;
        worlds::Point(worlds::kSaw, w, 8, nullptr, &t);
        float pp[kMaxN] = {0.5f, 0.35f, 0.5f, 0.5f, 0.5f, 0.5f};

        eng.Init(&w, 48000.f); eng.render_div = 1; eng.gain = 1.f;
        eng.SetMorph(nullptr, 0.f);
        eng.SetPhaseOverride(World::Phase::Random, false);
        eng.SetF0(110.f); eng.SetPosition(pp, w.N());
        float o[24];
        for(int i = 0; i < 6; i++) eng.Process(o, 24);
        double sineFrame[kFrame], sineMags[kMaxK];
        for(int i = 0; i < kFrame; i++) sineFrame[i] = eng.Frame()[i];
        for(int i = 0; i < w.K(); i++) sineMags[i] = eng.Mags()[i];
        const float trimSine = eng.PhaseTrim();

        eng.SetPhaseOverride(World::Phase::Cosine, true);
        eng.SetPosition(pp, w.N());
        for(int i = 0; i < 6; i++) eng.Process(o, 24);
        double dFrame = 0, dMags = 0;
        for(int i = 0; i < kFrame; i++) dFrame = std::fmax(dFrame, std::fabs(sineFrame[i] - eng.Frame()[i]));
        for(int i = 0; i < w.K(); i++) dMags = std::fmax(dMags, std::fabs(sineMags[i] - eng.Mags()[i]));

        char m[128];
        std::snprintf(m, sizeof m, "cosine is a different waveform (worst sample %.3f)", dFrame);
        ck(m, dFrame > 0.05);
        ck("and the identical spectrum", dMags == 0.0);
        std::snprintf(m, sizeof m, "and costs headroom: trim %.2f against %.2f", eng.PhaseTrim(), trimSine);
        ck(m, eng.PhaseTrim() < trimSine);

        /* the trim has to actually contain the peak it was sized for */
        double pk = 0, sq = 0;
        for(int i = 0; i < kFrame; i++)
        { const double v = eng.Frame()[i] * eng.PhaseTrim(); sq += v * v; if(std::fabs(v) > pk) pk = std::fabs(v); }
        const double crest = pk / std::sqrt(sq / kFrame);
        std::snprintf(m, sizeof m, "trimmed cosine peak sits under the sine peak (crest %.2f, peak %.3f)", crest, pk);
        double spk = 0;
        for(int i = 0; i < kFrame; i++) spk = std::fmax(spk, std::fabs(sineFrame[i] * trimSine));
        ck(m, pk <= spk * 1.02);

        eng.SetPhaseOverride(World::Phase::Random, false);
    }

    if(bad) printf("morphworld_check: %d FAILURES\n", bad);
    else    printf("morphworld_check: all passed\n");
    return bad ? 1 : 0;
}
