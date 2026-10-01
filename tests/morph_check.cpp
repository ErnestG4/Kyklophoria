/* morph_check — the properties that make a morph clean, and the numbers that
 * say whether a space is worth exploring. Written 2026-09-08 after measuring
 * why conventional wavetable oscillators click on a morph and we do not.
 *
 *   linearity  Every cell shares one phase spectrum, so the rendered frame is
 *              a linear function of the magnitude vector: render(a·x + b·y) =
 *              a·render(x) + b·render(y). Partials therefore cannot cancel and
 *              the morph cannot click, whatever weights the interpolator picks.
 *              This is the property a store-time-domain-frames design does not
 *              have, and it is worth a test because storing phases per cell
 *              would silently destroy it (docs/spec.md §9.3). Note it is a
 *              property of the *renderer*: Blend now rescales to hold the
 *              level (below), so a blend is a linear combination followed by a
 *              smooth gain, which preserves continuity but is not the plain
 *              average of the two waveforms.
 *
 *   level      Parseval: the rendered RMS is proportional to ||m||_2. A
 *              weighted sum of unit vectors is shorter than a unit vector
 *              unless they are parallel, by up to 3.01·N dB when the corner
 *              spectra have disjoint support. Unrescaled, the Harmonic space
 *              dips 0.02 dB, broad random content about 1.2 dB, and narrow
 *              formants 4 to 10 dB. BlendLevel::Preserve rescales to the
 *              weighted average of the corner lengths; this test holds both
 *              paths honest so the raw dip stays visible as content changes.
 *
 *   bandlimit  Harmonics above Nyquist are dropped as pitch rises. Each drop
 *              is a step in the frame, smoothed only by the per-block
 *              crossfade. Asserts the curvature at a step is no worse than
 *              the waveform's own.
 *
 *   jump       An instant full-range position change, as a stepped sequencer
 *              CV gives. A click is an isolated spike in curvature; this
 *              measures the worst curvature at the event against the 99.99th
 *              percentile of the same render. Measured 1.4x with the slew off
 *              and 0.7x with it on, where a real click is 50x or more. The
 *              first attempt at this test compared high-band energy at the
 *              jump against a window after it and read +93 dB, which was the
 *              two positions having different brightness, not a transient.
 *              Hence this metric, which cannot make that mistake.
 *
 *   diversity  Not an assertion, a report. A space whose cells are all alike
 *              is a 1-D space wearing N dimensions, and rotation has nothing
 *              to reveal in it.
 */
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include "kyk_stereo.h"
#include "kyk_world.h"
#include "kyk_gen.h"
#include "kyk_worlds.h"

using namespace kyk;

static World gWorld;   /* the tests all drive a lattice world */

static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static double Norm(const float* m, int K)
{
    double s = 0;
    for(int k = 0; k < K; k++) s += (double)m[k] * m[k];
    return std::sqrt(s);
}

static std::vector<uint8_t> MakeSpace(GenParams gp)
{
    std::vector<uint8_t> b(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
    BuildLattice(gp, b.data(), b.size());
    return b;
}

/* The phase spectrum the engine derives for a space, so the test renders the
 * frames the engine would render. */
static void Phases(const Space& s, float* cph, float* sph)
{
    Rng            r;
    const uint32_t seed = s.Header().phase_seed;
    r.Seed(seed);
    for(int k = 0; k < kMaxK; k++)
    {
        const int i = seed ? (int)(r.Next() & (uint32_t)(kTableSize - 1)) : 0;
        sph[k]      = kSinTable[i];
        cph[k]      = kSinTable[(i + kTableSize / 4) & (kTableSize - 1)];
    }
}

static FftScratch sc;
static float      fa[kFrame], fb[kFrame], fm[kFrame];

static void TestLinearity()
{
    printf("linearity\n");
    GenParams gp;
    auto      b = MakeSpace(gp);
    Space     s;
    s.Attach(b.data(), b.size());
    float cph[kMaxK], sph[kMaxK];
    Phases(s, cph, sph);
    const int K = s.K();
    double    worst = 0;
    Rng       rng;
    rng.Seed(31337);
    for(int trial = 0; trial < 200; trial++)
    {
        const uint32_t ia = (uint32_t)((trial * 7919u) % s.PointCount());
        const uint32_t ib = (uint32_t)((trial * 104729u) % s.PointCount());
        const float    wa = rng.Uniform() * 2.f - 0.5f;   /* arbitrary weights, */
        const float    wb = rng.Uniform() * 2.f - 0.5f;   /* not just a 50/50 blend */
        float          mm[kMaxK];
        for(int k = 0; k < K; k++) mm[k] = wa * s.Mags(ia)[k] + wb * s.Mags(ib)[k];
        RenderFrame(s.Mags(ia), cph, sph, K, fa, sc);
        RenderFrame(s.Mags(ib), cph, sph, K, fb, sc);
        RenderFrame(mm, cph, sph, K, fm, sc);
        for(int n = 0; n < kFrame; n++)
            worst = std::max(worst, (double)std::fabs(fm[n] - (wa * fa[n] + wb * fb[n])));
    }
    printf("  render(a·x+b·y) vs a·render(x)+b·render(y), 200 pairs: max %.3g\n", worst);
    CHECK(worst < 1e-4, "the renderer is not linear (%g) — partials can cancel", worst);
}

static void TestLevel()
{
    printf("level\n");
    GenParams gp;
    auto      b = MakeSpace(gp);
    Space     s;
    s.Attach(b.data(), b.size());
    const int K = s.K(), N = s.N(), side = s.Side();
    double    nlo = 1e9, nhi = 0;
    for(uint32_t i = 0; i < s.PointCount(); i++)
    {
        const double n = Norm(s.Mags(i), K);
        nlo = std::min(nlo, n);
        nhi = std::max(nhi, n);
    }
    CHECK(std::fabs(nlo - std::sqrt(2.0)) < 1e-3 && std::fabs(nhi - std::sqrt(2.0)) < 1e-3,
          "nodes are not unit RMS (%.4f..%.4f)", nlo, nhi);

    float  mags[kMaxK], pl[kMaxP];
    double worst = 0, sum = 0, worstKept = 0, keptSum = 0;
    int    cells = 0, ix[kMaxN] = {0, 0, 0, 0, 0, 0};
    int    total = 1;
    for(int a = 0; a < N; a++) total *= (side - 1);
    for(int c = 0; c < total; c++)
    {
        float p[kMaxN];
        for(int a = 0; a < N; a++) p[a] = ((float)ix[a] + 0.5f) / (float)(side - 1);
        Weights w;
        LatticeWeights(s, p, w);
        Blend(s, w, mags, pl, BlendLevel::Raw);
        const double d = 20.0 * std::log10(Norm(mags, K) / std::sqrt(2.0));
        worst = std::min(worst, d);
        sum += d;
        Blend(s, w, mags, pl, BlendLevel::Preserve);
        const double dp = 20.0 * std::log10(Norm(mags, K) / std::sqrt(2.0));
        worstKept = std::min(worstKept, std::fabs(dp));
        keptSum += std::fabs(dp);
        cells++;
        for(int a = 0; a < N; a++) { if(++ix[a] < side - 1) break; ix[a] = 0; }
    }
    printf("  cell-centre level, raw:      mean %.2f dB, worst %.2f dB over %d cells (algebraic floor %.2f dB)\n",
           sum / cells, worst, cells, -3.0103 * N);
    printf("  cell-centre level, preserved: mean %.3f dB, worst %.3f dB\n", keptSum / cells, worstKept);
    CHECK(worstKept < 0.01, "level preservation is off by %.3f dB", worstKept);
}

static void TestBandlimit()
{
    printf("bandlimit\n");
    GenParams gp;
    auto      b = MakeSpace(gp);
    Space     s;
    s.Attach(b.data(), b.size());
    static Engine eng;
    gWorld.UseLattice(&s);
    eng.Init(&gWorld, 48000.f);
    eng.gain = 1.f;
    const float pos[4] = {0.f, 0.f, 1.f, 0.f};   /* brightest cell: formant at the top */
    eng.SetPosition(pos, 4);

    /* rise through many band-limit boundaries, collecting audio and kcut */
    const int          block = 24, blocks = 8000;
    std::vector<float> x((size_t)block * blocks);
    std::vector<int>   kc(blocks);
    for(int i = 0; i < blocks; i++)
    {
        const float f0 = 700.f + (1400.f - 700.f) * (float)i / (float)blocks;
        eng.SetF0(f0);
        eng.Process(&x[(size_t)i * block], block);
        kc[i] = eng.Kcut();
    }
    /* curvature per block, at a kcut step and away from one */
    auto blockmax = [&](int bi) {
        double m = 0;
        const size_t a = (size_t)bi * block;
        for(size_t n = a + 2; n < a + block && n < x.size(); n++)
            m = std::max(m, std::fabs((double)x[n] - 2.0 * x[n - 1] + x[n - 2]));
        return m;
    };
    double stepSum = 0, quietSum = 0;
    int    steps = 0, quiet = 0;
    for(int i = 2; i < blocks - 2; i++)
    {
        if(kc[i] != kc[i - 1]) { stepSum += blockmax(i); steps++; }
        else { quietSum += blockmax(i); quiet++; }
    }
    const double ratio = (steps && quiet) ? (stepSum / steps) / (quietSum / quiet) : 1.0;
    printf("  %d band-limit steps (%d→%d): curvature at a step is %.2fx the waveform's own\n",
           steps, kc.front(), kc.back(), ratio);
    CHECK(steps > 5, "the sweep should cross several band-limit steps (%d)", steps);
    CHECK(ratio < 1.5, "band-limit steps are discontinuous (%.2fx) — taper the top harmonic", ratio);
}

static void TestJump()
{
    printf("jump\n");
    GenParams gp;
    auto      b = MakeSpace(gp);
    Space     s;
    s.Attach(b.data(), b.size());
    const int block = 24, blocks = 4000, at = 2000;
    for(int trial = 0; trial < 2; trial++)
    {
        static StereoEngine eng;
        gWorld.UseLattice(&s);
    eng.Init(&gWorld, 48000.f);
        eng.SetGain(1.f);
        eng.slew_ms = trial ? 5.f : 0.f;
        std::vector<float> x((size_t)block * blocks), r((size_t)block * blocks);
        const float lo[4] = {0.f, 0.f, 0.f, 0.f}, hi[4] = {1.f, 1.f, 1.f, 1.f};
        eng.SetF0(93.75f);
        for(int i = 0; i < blocks; i++)
        {
            eng.SetControl(i < at ? lo : hi, 4);
            eng.Process(&x[(size_t)i * block], &r[(size_t)i * block], block);
        }
        /* curvature at the event vs the 99.99th percentile of everywhere else */
        std::vector<double> d2(x.size() - 2);
        for(size_t n = 0; n + 2 < x.size(); n++) d2[n] = std::fabs((double)x[n + 2] - 2.0 * x[n + 1] + x[n]);
        const size_t j = (size_t)at * block, w = 144;   /* ±3 ms */
        double       ev = 0;
        for(size_t n = j - w; n < j + w && n < d2.size(); n++) ev = std::max(ev, d2[n]);
        std::vector<double> rest;
        for(size_t n = 0; n < d2.size(); n++)
            if(n < j - w || n >= j + w) rest.push_back(d2[n]);
        std::sort(rest.begin(), rest.end());
        const double ref   = rest[(size_t)(0.9999 * (rest.size() - 1))];
        const double ratio = ref > 0 ? ev / ref : 0;
        printf("  slew %-5s worst curvature at the jump %.2fx the 99.99th percentile elsewhere\n",
               trial ? "5 ms:" : "off:", ratio);
        CHECK(ratio < 3.0, "an instant position jump clicks (%.2fx)", ratio);
    }
}

static void ReportDiversity()
{
    printf("diversity (a report, not an assertion)\n");
    GenParams gp;
    auto      b = MakeSpace(gp);
    Space     s;
    s.Attach(b.data(), b.size());
    const int K = s.K();
    std::vector<double>              cent;
    std::vector<std::vector<double>> unit;
    for(uint32_t i = 0; i < s.PointCount(); i++)
    {
        double              num = 0, den = 0;
        std::vector<double> u(K);
        for(int k = 0; k < K; k++)
        {
            const double m = s.Mags(i)[k];
            num += (k + 1) * m * m;
            den += m * m;
            u[k] = m;
        }
        cent.push_back(den > 0 ? num / den : 0);
        const double n2 = std::sqrt(den > 0 ? den : 1);
        for(int k = 0; k < K; k++) u[k] /= n2;
        unit.push_back(u);
    }
    auto sorted = cent;
    std::sort(sorted.begin(), sorted.end());
    auto q = [&](double f) { return sorted[(size_t)(f * (sorted.size() - 1))]; };
    printf("  spectral centroid: min %.2f  median %.2f  max %.2f harmonics\n", q(0), q(0.5), q(1));
    long   pairs = 0, dup = 0;
    double sum = 0;
    for(size_t i = 0; i < unit.size(); i++)
        for(size_t j = i + 1; j < unit.size(); j++)
        {
            double d = 0;
            for(int k = 0; k < K; k++) d += unit[i][k] * unit[j][k];
            d = 1.0 - std::min(1.0, d);
            sum += d;
            pairs++;
            if(d < 0.001) dup++;
        }
    printf("  shape distance: mean %.4f over %ld pairs; near-duplicate pairs %ld (%.1f%%)\n",
           sum / (double)pairs, pairs, dup, 100.0 * (double)dup / (double)pairs);
    /* per-axis travel: an axis that moves nothing is a wasted dimension */
    for(int a = 0; a < s.N(); a++)
    {
        double lo = 1e9, hi = -1e9;
        for(int t = 0; t < s.Side(); t++)
        {
            int ix[kMaxN] = {1, 1, 1, 1, 1, 1};
            ix[a]         = t;
            const double c = cent[s.LatticeIndex(ix)];
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
        printf("  axis %d sweeps centroid %.2f..%.2f harmonics%s\n", a, lo, hi, (hi - lo) < 0.25 ? "   <-- nearly inert" : "");
    }
}

/* An analytic world is a formula; a lattice is that formula sampled. This
 * measures what the sampling costs, which is the number the whole two-backend
 * design rests on: exact where the grid lands, and a few percent between. */
static void TestAnalytic()
{
    printf("analytic world\n");
    World w;
    w.UseAnalytic(BraidsBasis(), 8, nullptr);
    CHECK(w.Ready() && w.Which() == World::Kind::Analytic, "analytic world should be ready");
    CHECK(w.N() == 4 && w.K() == 64 && w.P() == 8, "dims %d %d %d", w.N(), w.K(), w.P());

    float   mags[kMaxK], pl[kMaxP];
    Weights wt;
    Rng     rng;
    rng.Seed(808);
    double worstNorm = 0;
    for(int t = 0; t < 400; t++)
    {
        float p[kMaxN];
        for(int a = 0; a < 4; a++) p[a] = rng.Uniform();
        w.Evaluate(p, 0.f, mags, pl, wt);
        CHECK(wt.n_corners == 0, "an analytic world has no corners to report");
        double e = 0;
        for(int k = 0; k < w.K(); k++)
        {
            CHECK(std::isfinite(mags[k]) && mags[k] >= 0.f, "bad magnitude");
            e += (double)mags[k] * mags[k];
        }
        worstNorm = std::max(worstNorm, std::fabs(std::sqrt(e) - std::sqrt(2.0)));
        for(int j = 0; j < w.P(); j++)
            CHECK(pl[j] >= 0.f && pl[j] <= 1.f, "payload %d out of range (%g)", j, pl[j]);
    }
    printf("  unit RMS everywhere to %.3g\n", worstNorm);
    CHECK(worstNorm < 1e-4, "analytic spectra are not unit RMS (%g)", worstNorm);

    /* sample it onto a lattice and see what tabulating costs */
    const int side = 8, K = w.K(), P = w.P();
    std::vector<uint8_t> blob(Space::BlobSize(4, K, P, side, false));
    SpaceHeader h;
    std::memset(&h, 0, sizeof(h));
    h.magic = kSpaceMagic; h.version = kSpaceVersion; h.n = 4; h.mode = kModeLattice;
    h.k = (uint8_t)K; h.p = (uint8_t)P; h.side = (uint8_t)side; h.phase_seed = 1;
    uint32_t count = 1;
    for(int a = 0; a < 4; a++) count *= (uint32_t)side;
    h.point_count = count;
    std::memcpy(blob.data(), &h, sizeof(h));
    float* out = reinterpret_cast<float*>(blob.data() + sizeof(h));
    int    ix[kMaxN] = {0, 0, 0, 0, 0, 0};
    for(uint32_t i = 0; i < count; i++)
    {
        float p[kMaxN];
        for(int a = 0; a < 4; a++) p[a] = (float)ix[a] / (float)(side - 1);
        w.Evaluate(p, 0.f, mags, pl, wt);
        std::memcpy(out + (size_t)i * (K + P), mags, sizeof(float) * (size_t)K);
        std::memcpy(out + (size_t)i * (K + P) + K, pl, sizeof(float) * (size_t)P);
        for(int a = 0; a < 4; a++) { if(++ix[a] < side) break; ix[a] = 0; }
    }
    Space s;
    CHECK(s.Attach(blob.data(), blob.size()) == SpaceError::Ok, "sampled lattice attaches");
    World tab;
    tab.UseLattice(&s);

    double atNode = 0, between = 0;
    float  lm[kMaxK];
    for(int t = 0; t < 300; t++)
    {
        float p[kMaxN];
        for(int a = 0; a < 4; a++) p[a] = (float)(rng.Next() % (uint32_t)side) / (float)(side - 1);
        w.Evaluate(p, 0.f, mags, pl, wt);
        tab.Evaluate(p, 0.f, lm, pl, wt);
        for(int k = 0; k < K; k++) atNode = std::max(atNode, (double)std::fabs(mags[k] - lm[k]));
        for(int a = 0; a < 4; a++) p[a] = rng.Uniform();
        w.Evaluate(p, 0.f, mags, pl, wt);
        tab.Evaluate(p, 0.f, lm, pl, wt);
        for(int k = 0; k < K; k++) between = std::max(between, (double)std::fabs(mags[k] - lm[k]));
    }
    printf("  formula vs its own side-%d sampling: %.2g at a grid point, %.4f between\n", side, atNode, between);
    CHECK(atNode < 1e-5, "a lattice must reproduce its source exactly at a node (%g)", atNode);
    printf("  memory: formula %zu bytes, lattice %zu bytes (%.0fx)\n",
           sizeof(float) * (size_t)(w.K() * (w.N() + 1)), blob.size(),
           (double)blob.size() / (double)(sizeof(float) * (size_t)(w.K() * (w.N() + 1))));
}

/* The Morph knob must not cost a render every block.
 *
 * SetMorph is called once per block from the panel, and it used to mark the
 * frame dirty unconditionally — which defeated the render deadband completely
 * and, worse, did so invisibly: the sound was correct throughout, and the only
 * symptom was the module rendering at the maximum rate the divider allowed on
 * any held note. The CPU reading the whole render-divider question was blocked
 * on had been measured with it in place.
 *
 * Measured here rather than described: a still hand with a couple of ADC counts
 * of jitter, twenty thousand blocks, and the count of frames actually built.
 * A moving knob must still re-render, or the deadband would be a mute button. */
static void TestMorphDeadband()
{
    static World w, target;
    static solids::VertexTable vt, vt2;
    worlds::Point(worlds::kLock, w, 8, nullptr, &vt);
    worlds::Point(worlds::kFm, target, 8, nullptr, &vt2);

    struct Run { const char* what; bool call; bool armed; bool sweep; uint32_t renders; };
    Run runs[] = {
        {"deadband alone", false, false, false, 0},
        {"SetMorph every block, no target", true, false, false, 0},
        {"SetMorph every block, target armed", true, true, false, 0},
        {"SetMorph every block, knob sweeping", true, true, true, 0},
    };
    for(Run& r : runs)
    {
        Engine eng;
        eng.Init(&w, 48000.f);
        eng.render_div = 2;
        eng.gain = 1.f;
        eng.SetF0(110.f);
        uint32_t seed = 3u;
        float    out[24];
        for(int b = 0; b < 20000; b++)
        {
            seed = seed * 1664525u + 1013904223u;
            const float jit = (float)((int)((seed >> 16) % 5u) - 2) / 65535.f;
            float c[kMaxN];
            for(int a = 0; a < kMaxN; a++) c[a] = 0.5f + jit;
            eng.SetPosition(c, 4);
            if(r.call)
            {
                /* a sweep over the whole travel across the twenty thousand
                   blocks, which is about four seconds of a hand moving */
                const float amt = r.sweep ? (float)b / 20000.f : 0.4f;
                eng.SetMorph(r.armed ? &target : nullptr, r.armed ? amt : 0.f);
            }
            eng.Process(out, 24);
        }
        r.renders = eng.Renders();
        printf("  %-38s %6u renders in 20000 blocks\n", r.what, r.renders);
    }
    /* A still patch renders once whatever the caller does with the knob. */
    CHECK(runs[0].renders <= 2, "a still patch with no morph renders once (%u)", runs[0].renders);
    CHECK(runs[1].renders <= 2,
          "calling SetMorph every block with no target must not re-render (%u)", runs[1].renders);
    CHECK(runs[2].renders <= 2,
          "nor with a target armed and the knob still (%u)", runs[2].renders);
    /* But a moving knob has to be heard, or the deadband is a mute. The sweep
       crosses the 5e-4 threshold about every ten blocks, and the divider then
       allows half of those. */
    CHECK(runs[3].renders > 500,
          "a sweeping knob must still re-render (%u)", runs[3].renders);
    CHECK(runs[3].renders < 10001,
          "but not more often than the divider allows (%u)", runs[3].renders);
}

/* The aim offset belongs to a pair of worlds, so a new target clears it —
   and a new target can be the same pointer: both shells rebuild the morph
   target in one World, so the engine is told which world it holds (the id).
   Keyed on the pointer alone, the new target was read at coordinates
   searched against the old one and telemetry said "aimed" (the 2026-09-13
   review). */
static void TestAimFollowsTarget()
{
    static World w, target;
    static solids::VertexTable vt, vt2;
    worlds::Point(worlds::kLock, w, 8, nullptr, &vt);
    worlds::Point(worlds::kFm, target, 8, nullptr, &vt2);
    Engine eng; eng.Init(&w, 48000.f);
    const float off[4] = {0.1f, -0.2f, 0.05f, 0.f};
    eng.SetMorph(&target, 0.5f, 7u);
    eng.SetMorphOffset(off, 4);
    eng.SetMorph(&target, 0.6f, 7u);                       /* the knob moving: the aim stays */
    const bool kept = eng.MorphOffset()[1] == -0.2f;
    worlds::Point(worlds::kVowel, target, 8, nullptr, &vt2); /* another world, rebuilt in place */
    eng.SetMorph(&target, 0.6f, 9u);
    const bool cleared = eng.MorphOffset()[0] == 0.f && eng.MorphOffset()[1] == 0.f;
    CHECK(kept, "the aim offset did not survive the knob moving");
    CHECK(cleared, "a new morph target at the same pointer kept the old aim offset (%g, %g)", eng.MorphOffset()[0], eng.MorphOffset()[1]);
    printf("  the aim offset: kept while the knob moves, cleared by a new target rebuilt at the same pointer\n");
}

int main()
{
    TestAimFollowsTarget();
    TestMorphDeadband();
    TestLinearity();
    TestLevel();
    TestBandlimit();
    TestJump();
    TestAnalytic();
    ReportDiversity();
    printf(fails ? "morph_check: %d FAILURES\n" : "morph_check: all passed\n", fails);
    return fails ? 1 : 0;
}
