/* core_check — unit suite for the pure core under ASan/UBSan.
 *   space:   header validation, sizes, lattice indexing
 *   interp:  weights sum to 1, node reads are exact, linear along an axis,
 *            clamp saturates, wrap seam interpolates node side-1 → node 0
 *   fft:     RenderFrame against a double-precision direct sum
 *   osc:     a single harmonic reads back as a sine; Q32 pitch
 *   engine:  bandlimit cutoff by f0; two engines are bit-identical
 */
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>
#include "kyk_engine.h"
#include "kyk_world.h"
#include "kyk_gen.h"

using namespace kyk;

static World gWorld;   /* the tests all drive a lattice world */

static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static std::vector<uint8_t> MakeSpace(GenParams gp)
{
    std::vector<uint8_t> b(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
    const size_t         n = BuildLattice(gp, b.data(), b.size());
    CHECK(n == b.size(), "BuildLattice size %zu vs %zu", n, b.size());
    return b;
}

static void TestSpace()
{
    printf("space\n");
    GenParams gp;
    auto      b = MakeSpace(gp);
    Space     s;
    CHECK(s.Attach(b.data(), b.size()) == SpaceError::Ok, "attach");
    CHECK(s.N() == 4 && s.Side() == 4 && s.K() == 64 && s.P() == 8 && s.PointCount() == 256, "header fields");
    CHECK(s.Stride() == 72, "stride %zu", s.Stride());
    int ix[4] = {1, 2, 3, 0};
    CHECK(s.LatticeIndex(ix) == 1 + 2 * 4 + 3 * 16, "lattice index");
    auto bad = b;
    bad[0] ^= 1;
    CHECK(s.Attach(bad.data(), bad.size()) == SpaceError::BadMagic, "bad magic");
    CHECK(s.Attach(b.data(), b.size() - 1) == SpaceError::BadSize, "short blob");
    CHECK(s.Attach(b.data(), 10) == SpaceError::TooShort, "too short");
    bad = b;
    bad[6] = 9; /* n */
    CHECK(s.Attach(bad.data(), bad.size()) == SpaceError::BadN, "bad N");
    /* every generated value finite and non-negative */
    CHECK(s.Attach(b.data(), b.size()) == SpaceError::Ok, "re-attach");
    size_t badv = 0;
    for(uint32_t i = 0; i < s.PointCount(); i++)
        for(int k = 0; k < s.K(); k++) if(!std::isfinite(s.Mags(i)[k]) || s.Mags(i)[k] < 0.f) badv++;
    CHECK(badv == 0, "%zu bad magnitudes", badv);
    /* generator families, all N */
    for(int n = 1; n <= kMaxN; n++)
    {
        GenParams g;
        g.n = n; g.side = n >= 5 ? 3 : 4; g.k = 16; g.p = 8;
        auto  bb = MakeSpace(g);
        Space t;
        CHECK(t.Attach(bb.data(), bb.size()) == SpaceError::Ok, "attach N=%d", n);
    }
}

static void TestInterp()
{
    printf("interp\n");
    for(int n = 1; n <= kMaxN; n++)
    {
        GenParams g;
        g.n = n; g.side = n >= 5 ? 3 : 4; g.k = 16; g.p = 8;
        auto  b = MakeSpace(g);
        Space s;
        s.Attach(b.data(), b.size());
        Rng rng;
        rng.Seed(7 + n);
        for(int trial = 0; trial < 50; trial++)
        {
            float p[kMaxN];
            for(int a = 0; a < n; a++) p[a] = rng.Uniform();
            Weights w;
            LatticeWeights(s, p, w);
            double sum = 0;
            for(int c = 0; c < w.n_corners; c++) { sum += w.w[c]; CHECK(w.w[c] >= 0.f, "negative weight"); CHECK(w.idx[c] < s.PointCount(), "idx range"); }
            CHECK(std::fabs(sum - 1.0) < 1e-6, "N=%d weights sum %.9f", n, sum);
        }
        /* node reads are exact */
        int ix[kMaxN] = {0, 0, 0, 0, 0, 0};
        for(uint32_t i = 0; i < s.PointCount(); i++)
        {
            float p[kMaxN];
            for(int a = 0; a < n; a++) p[a] = (float)ix[a] / (float)(s.Side() - 1);
            Weights w;
            LatticeWeights(s, p, w);
            float mags[kMaxK], pl[kMaxP];
            Blend(s, w, mags, pl);
            const uint32_t idx = s.LatticeIndex(ix);
            CHECK(std::memcmp(mags, s.Mags(idx), sizeof(float) * s.K()) == 0, "N=%d node %u mags not exact", n, idx);
            CHECK(std::memcmp(pl, s.Payload(idx), sizeof(float) * s.P()) == 0, "N=%d node %u payload not exact", n, idx);
            for(int a = 0; a < n; a++) { if(++ix[a] < s.Side()) break; ix[a] = 0; }
        }
    }
    /* linear along axis 0, N=4 */
    {
        GenParams g;
        auto      b = MakeSpace(g);
        Space     s;
        s.Attach(b.data(), b.size());
        const float p0[4] = {0.f, 0.3f, 0.6f, 0.9f}, p1[4] = {1.f / 3.f, 0.3f, 0.6f, 0.9f}, pm[4] = {1.f / 6.f, 0.3f, 0.6f, 0.9f};
        Weights w0, w1, wm;
        LatticeWeights(s, p0, w0); LatticeWeights(s, p1, w1); LatticeWeights(s, pm, wm);
        float m0[kMaxK], m1[kMaxK], mm[kMaxK], pl[kMaxP];
        /* geometry of the interpolator: the raw blend, before the level
         * rescale that Blend applies by default (see morph_check) */
        Blend(s, w0, m0, pl, BlendLevel::Raw); Blend(s, w1, m1, pl, BlendLevel::Raw); Blend(s, wm, mm, pl, BlendLevel::Raw);
        float maxd = 0;
        for(int k = 0; k < s.K(); k++) maxd = std::fmax(maxd, std::fabs(mm[k] - 0.5f * (m0[k] + m1[k])));
        CHECK(maxd < 1e-5f, "midpoint not the average of the ends (%g)", maxd);
        /* clamp: outside [0,1] folds onto the edge */
        const float c0[4] = {-3.f, 0.3f, 0.6f, 0.9f}, c1[4] = {7.f, 0.3f, 0.6f, 0.9f}, e1[4] = {1.f, 0.3f, 0.6f, 0.9f};
        float f[4];
        Fold(s, c0, f); Weights a; LatticeWeights(s, f, a);
        Fold(s, c1, f); Weights bq; LatticeWeights(s, f, bq);
        Weights e; LatticeWeights(s, e1, e);
        CHECK(std::memcmp(a.idx, w0.idx, sizeof(uint32_t) * a.n_corners) == 0 && std::memcmp(a.w, w0.w, sizeof(float) * a.n_corners) == 0, "clamp low");
        CHECK(std::memcmp(bq.idx, e.idx, sizeof(uint32_t) * e.n_corners) == 0 && std::memcmp(bq.w, e.w, sizeof(float) * e.n_corners) == 0, "clamp high");
    }
    /* wrap seam on axis 0: the last interval runs from column side-1 back to column 0 */
    {
        GenParams g;
        g.topo[0] = (uint8_t)Topo::Wrap;
        auto  b = MakeSpace(g);
        Space s;
        s.Attach(b.data(), b.size());
        const int   side = s.Side();
        const float y[4] = {0.f, 0.3f, 0.6f, 0.9f};
        float       pa[4] = {(float)(side - 1) / side, y[1], y[2], y[3]};   /* column side-1 */
        float       pb[4] = {0.f, y[1], y[2], y[3]};                        /* column 0 */
        float       pm[4] = {(float)(side - 0.5f) / side, y[1], y[2], y[3]}; /* halfway across the seam */
        Weights wa, wb, wm;
        LatticeWeights(s, pa, wa); LatticeWeights(s, pb, wb); LatticeWeights(s, pm, wm);
        float ma[kMaxK], mb[kMaxK], mm[kMaxK], pl[kMaxP];
        Blend(s, wa, ma, pl, BlendLevel::Raw); Blend(s, wb, mb, pl, BlendLevel::Raw); Blend(s, wm, mm, pl, BlendLevel::Raw);
        float maxd = 0;
        for(int k = 0; k < s.K(); k++) maxd = std::fmax(maxd, std::fabs(mm[k] - 0.5f * (ma[k] + mb[k])));
        CHECK(maxd < 1e-5f, "wrap seam midpoint (%g)", maxd);
        /* 1.0 folds to 0.0 exactly, and 0.999999 to nearly the same spectrum */
        const float c1[4] = {1.f, y[1], y[2], y[3]}, cz[4] = {0.99999f, y[1], y[2], y[3]};
        float f1[4], fz[4];
        Fold(s, c1, f1); Fold(s, cz, fz);
        CHECK(f1[0] == 0.f, "fold(1.0) = %g", f1[0]);
        Weights w1, wz;
        LatticeWeights(s, f1, w1); LatticeWeights(s, fz, wz);
        float m1[kMaxK], mz[kMaxK];
        Blend(s, w1, m1, pl, BlendLevel::Raw); Blend(s, wz, mz, pl, BlendLevel::Raw);
        maxd = 0;
        for(int k = 0; k < s.K(); k++) maxd = std::fmax(maxd, std::fabs(m1[k] - mz[k]));
        CHECK(maxd < 1e-3f, "seam continuity (%g)", maxd);
    }
}

static void TestFft()
{
    printf("fft (frame %d)\n", kFrame);
    static FftScratch sc;
    static float      frame[kFrame];
    float             mags[kMaxK], cph[kMaxK], sph[kMaxK];
    double            ph[kMaxK];
    Rng               rng;
    rng.Seed(42);
    const int K = kMaxK;
    for(int k = 0; k < K; k++)
    {
        mags[k]       = rng.Uniform() / (float)(k + 1);
        const int idx = (int)(rng.Next() & (uint32_t)(kTableSize - 1));
        sph[k]        = kSinTable[idx];
        cph[k]        = kSinTable[(idx + kTableSize / 4) & (kTableSize - 1)];
        ph[k]         = 2.0 * M_PI * idx / kTableSize;
    }
    RenderFrame(mags, cph, sph, K, frame, sc);
    double maxerr = 0, peak = 0;
    for(int n = 0; n < kFrame; n++)
    {
        double ref = 0;
        for(int k = 0; k < K; k++) ref += mags[k] * std::cos(2.0 * M_PI * (k + 1) * n / kFrame + ph[k]);
        maxerr = std::fmax(maxerr, std::fabs(ref - frame[n]));
        peak   = std::fmax(peak, std::fabs(ref));
    }
    printf("  max err %.3g (peak %.3g)\n", maxerr, peak);
    CHECK(maxerr < 1e-4, "RenderFrame vs direct sum: %g", maxerr);
    /* kcut truncation renders only the first kcut harmonics */
    RenderFrame(mags, cph, sph, 3, frame, sc);
    maxerr = 0;
    for(int n = 0; n < kFrame; n++)
    {
        double ref = 0;
        for(int k = 0; k < 3; k++) ref += mags[k] * std::cos(2.0 * M_PI * (k + 1) * n / kFrame + ph[k]);
        maxerr = std::fmax(maxerr, std::fabs(ref - frame[n]));
    }
    CHECK(maxerr < 1e-5, "kcut=3 render: %g", maxerr);
    RenderFrame(mags, cph, sph, 0, frame, sc);
    for(int n = 0; n < kFrame; n++) CHECK(frame[n] == 0.f, "kcut=0 must be silent");
}

static void TestOsc()
{
    printf("osc\n");
    static Osc osc;
    osc.Init();
    /* a unit sine in the back buffer, then a block that fades it in fully */
    float* b = osc.Back();
    for(int n = 0; n < kFrame; n++) b[n] = kSinTable[(n * (kTableSize / kFrame)) & (kTableSize - 1)];
    const float sr = 48000.f, f0 = 750.f;   /* 64 samples per cycle */
    osc.SetFreq(f0, sr);
    static float out[4096];
    osc.Process(out, 64, true);              /* crossfade 0 → sine over one cycle */
    osc.Process(out, 4096, false);           /* steady: 64 cycles */
    double maxerr = 0;
    for(int i = 0; i < 4096; i++)
    {
        const double ref = std::sin(2.0 * M_PI * f0 * (double)(64 + i) / sr);
        maxerr           = std::fmax(maxerr, std::fabs(ref - out[i]));
    }
    printf("  sine read-back max err %.3g\n", maxerr);
    CHECK(maxerr < 2e-4, "sine read-back %g", maxerr);
    /* Q32 increment is exact for f0 = sr/64: 2^26 */
    osc.ResetPhase(0);
    osc.Process(out, 64, false);
    CHECK(osc.Phase() == 0u, "64 samples at sr/64 must wrap exactly (phase %u)", osc.Phase());
}

static void TestEngine()
{
    printf("engine\n");
    GenParams gp;
    auto      b = MakeSpace(gp);
    Space     s;
    s.Attach(b.data(), b.size());
    static Engine e1, e2;
    gWorld.UseLattice(&s);
    e1.Init(&gWorld, 48000.f);
    gWorld.UseLattice(&s);
    e2.Init(&gWorld, 48000.f);
    const float pos[4] = {0.1f, 0.7f, 0.4f, 0.9f};
    static float o1[24], o2[24];
    /* bandlimit: kcut = floor(24000/f0 - ε), capped at K */
    struct { float f0; int kcut; } cases[] = {{100.f, 64}, {400.f, 59}, {6000.f, 3}, {12000.f, 1}, {23990.f, 1}, {24000.f, 0}};
    for(auto& c : cases)
    {
        e1.SetF0(c.f0);
        e1.SetPosition(pos, 4);
        e1.Process(o1, 24);
        CHECK(e1.Kcut() == c.kcut, "f0 %.0f: kcut %d, want %d", c.f0, e1.Kcut(), c.kcut);
        for(int k = e1.Kcut(); k < s.K(); k++) CHECK(e1.MagsBandlimited()[k] == 0.f, "bin %d above cutoff not zero", k);
    }
    /* two engines, same script, bit-identical; output finite */
    gWorld.UseLattice(&s);
    e1.Init(&gWorld, 48000.f);
    gWorld.UseLattice(&s);
    e2.Init(&gWorld, 48000.f);
    Rng rng;
    rng.Seed(99);
    bool   same = true, finite = true;
    double rms  = 0;
    for(int blk = 0; blk < 2000; blk++)
    {
        float p[4];
        for(int a = 0; a < 4; a++) p[a] = rng.Uniform() * 1.4f - 0.2f;   /* wanders past the edges: clamp */
        const float f0 = 40.f + rng.Uniform() * 2000.f;
        e1.SetF0(f0); e1.SetPosition(p, 4); e1.Process(o1, 24);
        e2.SetF0(f0); e2.SetPosition(p, 4); e2.Process(o2, 24);
        if(std::memcmp(o1, o2, sizeof(o1))) same = false;
        for(int i = 0; i < 24; i++) { if(!std::isfinite(o1[i])) finite = false; rms += (double)o1[i] * o1[i]; }
    }
    rms = std::sqrt(rms / (2000.0 * 24));
    printf("  wander rms %.3f\n", rms);
    CHECK(same, "two engines diverged");
    CHECK(finite, "non-finite output");
    CHECK(rms > 0.1 && rms < 1.0, "rms %.3f out of range", rms);
    /* render_div > 1 holds the frame and still fades */
    gWorld.UseLattice(&s);
    e1.Init(&gWorld, 48000.f);
    e1.render_div = 4;
    e1.SetPosition(pos, 4);
    for(int blk = 0; blk < 8; blk++) { e1.Process(o1, 24); CHECK(std::isfinite(o1[0]), "render_div output"); }
    /* the payload follows the position: cutoff lane = 1 - tilt (axis 1) */
    gWorld.UseLattice(&s);
    e1.Init(&gWorld, 48000.f);
    const float p2[4] = {0.f, 1.f, 0.f, 0.f};
    e1.SetPosition(p2, 4);
    e1.Process(o1, 24);
    CHECK(std::fabs(e1.Payload()[0] - 0.f) < 1e-6f, "cutoff lane at tilt=1: %g", e1.Payload()[0]);
}

int main()
{
    TestSpace();
    TestInterp();
    TestFft();
    TestOsc();
    TestEngine();
    printf(fails ? "core_check: %d FAILURES\n" : "core_check: all passed\n", fails);
    return fails ? 1 : 0;
}
