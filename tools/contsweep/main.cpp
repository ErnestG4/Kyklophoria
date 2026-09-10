/* contsweep — continuity, checked from everywhere instead of from one point.
 *
 * tests/cont_check sweeps each axis from a single base point,
 * {0.5, 0.5, 0.35, 0.35, ...}, so what it actually verifies is that four lines
 * through one point of each world are smooth. That is a thin claim to hang the
 * instrument's central promise on. Every discontinuity found so far — the
 * band-limit truncation, the ring modulator's integer ratio, Unison cutting a
 * partial at full weight — happened to cross those four lines. A cliff on a
 * face the lines miss is invisible to it, and there is no reason to think the
 * ones we found were the only ones.
 *
 * So: the same test, from thousands of random base points. The method is
 * unchanged and is the only part that matters — sweep an axis at two step
 * sizes, and a continuous function halves its largest step while a
 * discontinuity does not. Ratio near 4 is smooth, near 1 is a cliff.
 *
 * Two stages, because the fine sweep is four times the work of the coarse one
 * and almost every base point is fine. Sweep at 500 first; only if the worst
 * step clears the noise floor is it worth re-measuring at 2000 to see whether
 * halving the step halved the step. That makes a base point cost about a fifth
 * of what cont_check pays for one.
 *
 * Every base point is generated from a hash of (world, axis, index, seed), so
 * a flagged case can be reproduced exactly with --replay without storing it.
 *
 *   build/host/contsweep --points 2000 --threads 16
 *   build/host/contsweep --replay 11:2:15734        (world:axis:index)
 */
#include "kyk_worlds.h"
#include "kyk_engine.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>

using namespace kyk;

static const double kFloor    = 2e-4;   /* below this the ratio is float noise */
static const double kMinRatio = 2.0;    /* a continuous axis gives about 4 */

/* splitmix64: a base point must be a pure function of its coordinates so that
 * a failure reported tonight can be re-run tomorrow without a database. */
static uint64_t Mix(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

struct Worker
{
    Engine eng;
    double cur[kFrame], prev[kFrame];

    void FrameAt(const World& w, const float* p)
    {
        eng.Init(&w, 48000.f);
        eng.render_div = 1;
        eng.gain       = 1.f;
        eng.SetF0(110.f);
        eng.SetPosition(p, w.N());
        float out[24];
        for(int i = 0; i < 6; i++) eng.Process(out, 24);
        const float* f  = eng.Frame();
        double       n2 = 0;
        for(int i = 0; i < kFrame; i++) n2 += (double)f[i] * f[i];
        const double inv = n2 > 0 ? 1.0 / std::sqrt(n2) : 0;
        for(int i = 0; i < kFrame; i++) cur[i] = f[i] * inv;
    }

    /* largest one-step change along `ax`, holding the base point elsewhere */
    double Worst(const World& w, const float* base, int ax, int steps)
    {
        double worst = 0;
        for(int i = 0; i <= steps; i++)
        {
            float p[kMaxN];
            for(int a = 0; a < kMaxN; a++) p[a] = base[a];
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
};

static void BasePoint(uint8_t wi, int ax, uint32_t idx, uint64_t seed, int n, float* p)
{
    uint64_t h = Mix(seed ^ (uint64_t)wi * 0x1000003ull ^ (uint64_t)ax * 0x10001ull ^ (uint64_t)idx);
    for(int a = 0; a < kMaxN; a++)
    {
        h = Mix(h);
        /* 0.02..0.98: the very edges are the shapers' own clamp territory and
         * are already swept by the axis under test. */
        p[a] = 0.02f + 0.96f * (float)((h >> 11) & 0xFFFFF) / (float)0xFFFFF;
    }
    (void)n;
}

/* Build world `wi` into the caller's storage. */
static bool MakeWorld(uint8_t wi, World& w, Space& sp, solids::VertexTable& tbl,
                      std::vector<uint8_t>& blob)
{
    const worlds::Entry& e = worlds::Get(wi);
    if(e.kind == World::Kind::Lattice)
    {
        blob.assign(1u << 22, 0);
        const size_t n = worlds::Expand(wi, 4, 8, 64, 8, blob.data(), blob.size());
        if(!n || sp.Attach(blob.data(), n) != SpaceError::Ok) return false;
        w.UseLattice(&sp);
    }
    else if(!worlds::Point(wi, w, 8, nullptr, &tbl)) return false;
    return w.Ready() && w.N() >= 1 && w.K() >= 1;
}

struct Hit { uint8_t wi; int ax; uint32_t idx; double a, b, ratio; };

static std::mutex          gMut;
static std::vector<Hit>    gHits;
static std::atomic<long>   gDone{0}, gRefined{0};

int main(int argc, char** argv)
{
    long        points  = 2000;
    unsigned    threads = std::thread::hardware_concurrency();
    uint64_t    seed    = 1;
    const char* replay  = nullptr;
    for(int i = 1; i < argc; i++)
    {
        if(!std::strcmp(argv[i], "--points") && i + 1 < argc) points = std::atol(argv[++i]);
        else if(!std::strcmp(argv[i], "--threads") && i + 1 < argc) threads = (unsigned)std::atoi(argv[++i]);
        else if(!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if(!std::strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
    }
    if(threads < 1) threads = 1;

    if(replay)
    {
        int wi = 0, ax = 0; unsigned idx = 0;
        if(std::sscanf(replay, "%d:%d:%u", &wi, &ax, &idx) != 3) { printf("bad --replay\n"); return 2; }
        World w; Space sp; solids::VertexTable tbl; std::vector<uint8_t> blob;
        if(!MakeWorld((uint8_t)wi, w, sp, tbl, blob)) { printf("cannot build world %d\n", wi); return 2; }
        float base[kMaxN];
        BasePoint((uint8_t)wi, ax, idx, seed, w.N(), base);
        printf("%s axis %d, base point", worlds::Get((uint8_t)wi).name, ax);
        for(int a = 0; a < w.N(); a++) printf(" %.4f", base[a]);
        printf("\n");
        Worker* wk = new Worker();
        const double a = wk->Worst(w, base, ax, 500), b = wk->Worst(w, base, ax, 2000);
        printf("  step/500 %.6f  step/2000 %.6f  ratio %.2f  -> %s\n",
               a, b, b > 0 ? a / b : 0.0, (a < kFloor || (b > 0 && a / b >= kMinRatio)) ? "smooth" : "STEP");
        delete wk;
        return 0;
    }

    /* one job per (world, axis); points are split inside */
    struct Job { uint8_t wi; int ax; };
    std::vector<Job> jobs;
    for(uint8_t wi = 0; wi < worlds::kCount; wi++)
    {
        World w; Space sp; solids::VertexTable tbl; std::vector<uint8_t> blob;
        if(!MakeWorld(wi, w, sp, tbl, blob)) { printf("  %s: cannot build, skipped\n", worlds::Get(wi).name); continue; }
        for(int ax = 0; ax < w.N() && ax < 4; ax++) jobs.push_back({wi, ax});
    }
    printf("contsweep: %zu world-axis pairs x %ld base points on %u threads, seed %llu\n",
           jobs.size(), points, threads, (unsigned long long)seed);
    printf("           (cont_check tests one base point; this tests %ld)\n\n", points);

    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    std::vector<double> worstRatio(jobs.size(), 1e9);
    std::vector<double> worstStep(jobs.size(), 0.0);

    for(unsigned t = 0; t < threads; t++)
        pool.emplace_back([&]{
            Worker* wk = new Worker();
            for(;;)
            {
                const size_t j = next.fetch_add(1);
                if(j >= jobs.size()) break;
                World w; Space sp; solids::VertexTable tbl; std::vector<uint8_t> blob;
                if(!MakeWorld(jobs[j].wi, w, sp, tbl, blob)) continue;
                double wr = 1e9, ws = 0;
                for(long i = 0; i < points; i++)
                {
                    float base[kMaxN];
                    BasePoint(jobs[j].wi, jobs[j].ax, (uint32_t)i, seed, w.N(), base);
                    const double a = wk->Worst(w, base, jobs[j].ax, 500);
                    if(a > ws) ws = a;
                    gDone.fetch_add(1);
                    if(a < kFloor) continue;              /* nothing to judge */
                    const double b = wk->Worst(w, base, jobs[j].ax, 2000);
                    gRefined.fetch_add(1);
                    const double r = b > 0 ? a / b : 0;
                    if(r < wr) wr = r;
                    if(r < kMinRatio)
                    {
                        std::lock_guard<std::mutex> lk(gMut);
                        if(gHits.size() < 200) gHits.push_back({jobs[j].wi, jobs[j].ax, (uint32_t)i, a, b, r});
                    }
                }
                worstRatio[j] = wr; worstStep[j] = ws;
            }
            delete wk;
        });
    for(auto& th : pool) th.join();

    printf("  %-10s %-4s %11s %9s\n", "world", "axis", "worst step", "min ratio");
    int flagged = 0;
    for(size_t j = 0; j < jobs.size(); j++)
    {
        const bool none = worstRatio[j] > 1e8;
        printf("  %-10s %-4d %11.5f %9s%s\n", worlds::Get(jobs[j].wi).name, jobs[j].ax,
               worstStep[j], none ? "—" : std::to_string(worstRatio[j]).substr(0, 5).c_str(),
               (!none && worstRatio[j] < kMinRatio) ? "   <-- STEP" : "");
        if(!none && worstRatio[j] < kMinRatio) flagged++;
    }
    printf("\n  %ld sweeps, %ld refined (%.1f%%)\n", gDone.load(), gRefined.load(),
           gDone.load() ? 100.0 * gRefined.load() / gDone.load() : 0.0);
    if(gHits.empty()) { printf("contsweep: no discontinuity found anywhere\n"); return 0; }
    printf("\n  worst cases, reproduce with --replay world:axis:index\n");
    std::sort(gHits.begin(), gHits.end(), [](const Hit& a, const Hit& b){ return a.ratio < b.ratio; });
    for(size_t i = 0; i < gHits.size() && i < 20; i++)
        printf("    --replay %u:%d:%u   %-10s ratio %.2f  step %.5f\n",
               gHits[i].wi, gHits[i].ax, gHits[i].idx, worlds::Get(gHits[i].wi).name,
               gHits[i].ratio, gHits[i].a);
    printf("\ncontsweep: %d of %zu world-axis pairs have a discontinuity somewhere\n", flagged, jobs.size());
    return flagged ? 1 : 0;
}
