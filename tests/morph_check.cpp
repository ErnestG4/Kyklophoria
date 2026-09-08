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
#include "kyk_gen.h"

using namespace kyk;

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
    eng.Init(&s, 48000.f);
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
        eng.Init(&s, 48000.f);
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

int main()
{
    TestLinearity();
    TestLevel();
    TestBandlimit();
    TestJump();
    ReportDiversity();
    printf(fails ? "morph_check: %d FAILURES\n" : "morph_check: all passed\n", fails);
    return fails ? 1 : 0;
}
