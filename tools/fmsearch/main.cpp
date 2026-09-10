/* fmsearch — the FM world's seven constants, searched instead of guessed.
 *
 * The comment above FmField says the current values were "chosen by
 * measurement, not taste", and they were: three values of each of four
 * reaches, the best corner kept. That is 81 points of a seven-dimensional
 * space, and the three constants added since were never in the sweep at all.
 *
 * What is optimised, and what is deliberately not:
 *
 *   variety is the objective. It is the timbral distance covered per unit
 *   travelled through the space, averaged over random directions, and it is
 *   the number that says whether turning a knob does anything.
 *
 *   spread is a CONSTRAINT, never a target. It is the most varied direction
 *   over the least, and the temptation is to minimise it because 1.0 means
 *   isotropy. Do that and the search walks straight to noise: the three most
 *   isotropic worlds in this instrument are Field, Field II and Torus, at
 *   1.26x to 1.39x, and they are the three least legible things in it. So the
 *   rule here is only that spread must not get worse than what ships.
 *
 *   twins must stay near zero. A world can raise its variety by scattering,
 *   and scattering puts distinct positions on top of each other.
 *
 * The metric is the same one tools/kykworlds prints, and the tool checks
 * itself against the shipping constants before searching: if it cannot
 * reproduce the published 12.758 and 1.83x, it is measuring something else and
 * says so rather than optimising the wrong thing.
 */
#include "kyk_worlds.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

using namespace kyk;

static const int N = 4, K = 64, P = 8;

struct SearchRng
{
    uint64_t s = 1;
    void     Seed(uint64_t v) { s = v ? v : 1; }
    uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double   Uniform() { return (double)(Next() >> 11) * (1.0 / 9007199254740992.0); }
    double   Range(double a, double b) { return a + (b - a) * Uniform(); }
};

static double Dist(const double* a, const double* b, int k)
{ double d = 0; for(int i = 0; i < k; i++) { const double x = a[i] - b[i]; d += x * x; } return std::sqrt(d); }

static void Unit(const float* m, int k, double* out)
{
    double n2 = 0;
    for(int i = 0; i < k; i++) n2 += (double)m[i] * m[i];
    const double inv = n2 > 0 ? 1.0 / std::sqrt(n2) : 0;
    for(int i = 0; i < k; i++) out[i] = m[i] * inv;
}

struct Score { double variety, spread, twins, top, topworst; };

static Score Grade(const FmField& f, int dirs)
{
    World w;
    w.UseFm(f, P, nullptr);
    float   mags[kMaxK], pl[kMaxP];
    Weights wt;
    double  a[kMaxK], b[kMaxK];
    auto at = [&](const float* p01, double* out)
    { w.Evaluate(p01, 0.f, mags, pl, wt); Unit(mags, K, out); };

    auto per_unit = [&](const double* d)
    {
        const int    M = 200;
        const double span = 0.9;
        double       len = 0;
        for(int i = 0; i <= M; i++)
        {
            const double t = ((double)i / M - 0.5) * span;
            float        p[kMaxN];
            for(int ax = 0; ax < N; ax++)
                p[ax] = (float)std::fmin(1.0, std::fmax(0.0, 0.5 + t * d[ax]));
            at(p, b);
            if(i) len += Dist(a, b, K);
            std::memcpy(a, b, sizeof(double) * (size_t)K);
        }
        return len / span;
    };

    SearchRng r; r.Seed(2026);                 /* same seed as kykworlds, so comparable */
    std::vector<double> v;
    for(int t = 0; t < dirs; t++)
    {
        double d[kMaxN], n2 = 0;
        for(int ax = 0; ax < N; ax++)
        {
            const double u1 = r.Uniform() + 1e-9, u2 = r.Uniform();
            d[ax] = std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2);
            n2 += d[ax] * d[ax];
        }
        n2 = std::sqrt(n2 > 0 ? n2 : 1);
        for(int ax = 0; ax < N; ax++) d[ax] /= n2;
        v.push_back(per_unit(d));
    }
    std::sort(v.begin(), v.end());
    double mean = 0;
    for(double x : v) mean += x;
    mean /= (double)v.size();
    const int lo = (int)(0.05 * v.size()), hi = (int)(0.95 * v.size());
    const double spread = v[hi] / (v[lo] > 0 ? v[lo] : 1e-9);

    const int S = 300;
    std::vector<std::vector<double>> pts; pts.reserve(S);
    for(int i = 0; i < S; i++)
    {
        float p[kMaxN];
        for(int ax = 0; ax < N; ax++) p[ax] = (float)r.Uniform();
        std::vector<double> s((size_t)K);
        at(p, s.data());
        pts.push_back(std::move(s));
    }
    long close = 0, total = 0;
    for(int i = 0; i < S; i++)
        for(int j = i + 1; j < S; j++)
        { total++; if(Dist(pts[i].data(), pts[j].data(), K) < 0.05) close++; }

    /* How much of the spectrum is piled against the K limit.
     *
     * Without this the search cheats and it is not subtle: left free it drove
     * index_max to 82 and carrier_max to 30, put 20% of the energy above
     * harmonic 48, and reported five times the variety. None of that is
     * timbre. The sidebands were being cut off by K and the "distance covered"
     * was the truncation edge moving through the spectrum.
     *
     * The constraint is principled beyond stopping the cheat. Energy near K is
     * energy the instrument cannot promise: below 375 Hz the band stops at K,
     * above it at Nyquist, so a world that lives against that edge is a world
     * whose timbre changes with pitch for no musical reason. */
    double topSum = 0, topMax = 0;
    {
        SearchRng q; q.Seed(99);
        const int T = 512;
        for(int i = 0; i < T; i++)
        {
            float p[kMaxN];
            for(int ax = 0; ax < N; ax++) p[ax] = (float)q.Uniform();
            w.Evaluate(p, 0.f, mags, pl, wt);
            double s2 = 0, hi = 0;
            for(int c = 0; c < K; c++)
            { const double v = std::fabs((double)mags[c]); s2 += v * v; if(c >= 48) hi += v * v; }
            if(s2 <= 0) continue;
            const double fr = hi / s2;
            topSum += fr; if(fr > topMax) topMax = fr;
        }
        topSum /= (double)T;
    }
    return { mean, spread, total ? 100.0 * (double)close / (double)total : 0.0,
             100.0 * topSum, 100.0 * topMax };
}

static void Print(const char* tag, const FmField& f, const Score& s)
{
    printf("  %-9s variety %7.3f  spread %5.2fx  twins %4.2f%%  top %4.1f/%4.1f%%  |  idx %5.2f  "
           "ratio %4.2f-%4.2f  carr %4.2f-%5.2f  2nd %5.2f  lock %4.2f\n",
           tag, s.variety, s.spread, s.twins, s.top, s.topworst, f.index_max, f.ratio_min,
           f.ratio_max, f.carrier_min, f.carrier_max, f.second_max, f.ratio_lock);
}

int main(int argc, char** argv)
{
    long     trials  = 4000;
    unsigned threads = std::thread::hardware_concurrency();
    int      dirs    = 96;
    uint64_t seed    = 12345;
    for(int i = 1; i < argc; i++)
    {
        if(!std::strcmp(argv[i], "--trials") && i + 1 < argc) trials = std::atol(argv[++i]);
        else if(!std::strcmp(argv[i], "--threads") && i + 1 < argc) threads = (unsigned)std::atoi(argv[++i]);
        else if(!std::strcmp(argv[i], "--dirs") && i + 1 < argc) dirs = std::atoi(argv[++i]);
        else if(!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
    }
    if(threads < 1) threads = 1;

    const FmField ship;                       /* the shipping constants */
    const Score   base = Grade(ship, 256);
    printf("fmsearch: the metric, checked against what ships\n");
    Print("shipping", ship, base);
    printf("           kykworlds publishes variety 12.758, spread 1.83x, twins 0.0%%\n");
    const bool sane = std::fabs(base.variety - 12.758) < 0.6 && std::fabs(base.spread - 1.83) < 0.15;
    printf("           %s\n\n", sane ? "matches — the search is measuring the right thing"
                                     : "DOES NOT MATCH — not searching, the metric is wrong");
    if(!sane) return 2;

    printf("  objective: maximise variety, subject to twins <= 0.5%%, mean energy above\n"
           "             harmonic 48 <= 2%% and worst <= 25%% (shipping: %.1f%% / %.1f%%)\n",
           base.top, base.topworst);
    printf("  %ld trials on %u threads, %d directions each\n\n", trials, threads, dirs);

    /* A hard gate at the shipping spread throws away 99.5% of the trials,
       because those constants were already chosen to keep spread low. A front
       is more use: the best variety available at each spread budget, so the
       trade is visible instead of decided here. */
    const double kBudget[] = {1.83, 2.00, 2.25, 2.50, 3.00};
    const int    nB = (int)(sizeof kBudget / sizeof kBudget[0]);
    std::atomic<long> next{0}, kept{0};
    std::mutex        mut;
    FmField           bestF[8];
    double            bestV[8];
    for(int q = 0; q < nB; q++) { bestF[q] = ship; bestV[q] = 0; }

    std::vector<std::thread> pool;
    for(unsigned t = 0; t < threads; t++)
        pool.emplace_back([&]{
            SearchRng r; r.Seed(seed * 7919ull + t * 104729ull + 1);
            for(;;)
            {
                const long i = next.fetch_add(1);
                if(i >= trials) break;
                FmField f;
                /* Widened after a first pass put index_max, second_max and
                   carrier_max within a few percent of their ceilings, which
                   means the bound was doing the choosing rather than the
                   metric. */
                f.index_max   = (float)r.Range(4.0, 90.0);
                f.ratio_min   = (float)r.Range(0.25, 2.0);
                f.ratio_max   = (float)r.Range(1.6, 9.0);
                f.carrier_min = (float)r.Range(1.0, 6.0);
                f.carrier_max = (float)r.Range(4.0, 32.0);
                f.second_max  = (float)r.Range(0.0, 48.0);
                f.ratio_lock  = (float)r.Range(0.0, 0.95);
                if(f.ratio_max <= f.ratio_min + 0.2f) continue;
                if(f.carrier_max <= f.carrier_min + 0.5f) continue;
                const Score s = Grade(f, dirs);
                if(s.twins > 0.5) continue;              /* scattering is not variety */
                /* and neither is the band edge moving. Shipping sits at 0.1%
                   mean and 14% worst, so this is generous rather than tight. */
                if(s.top > 2.0 || s.topworst > 25.0) continue;
                kept.fetch_add(1);
                std::lock_guard<std::mutex> lk(mut);
                for(int q = 0; q < nB; q++)
                    if(s.spread <= kBudget[q] && s.variety > bestV[q]) { bestV[q] = s.variety; bestF[q] = f; }
            }
        });
    for(auto& th : pool) th.join();

    printf("  %ld of %ld trials were free of twins\n\n", kept.load(), trials);
    Print("shipping", ship, base);
    /* Every winner is re-graded at the full direction count. A 96-direction
       score is noisy and the search will have selected partly on that noise,
       so the front has to be confirmed at the resolution it is quoted at. */
    for(int q = 0; q < nB; q++)
    {
        if(bestV[q] <= 0) { printf("  <=%.2fx    nothing found\n", kBudget[q]); continue; }
        const Score c = Grade(bestF[q], 256);
        char tag[16]; std::snprintf(tag, sizeof tag, "<=%.2fx", kBudget[q]);
        Print(tag, bestF[q], c);
        if(c.spread > kBudget[q]) printf("             (re-grade puts this over its budget — noise, discard)\n");
        else if(c.variety > base.variety)
            printf("             variety %+.1f%% against what ships\n",
                   100.0 * (c.variety / base.variety - 1.0));
    }
    return 0;
}
