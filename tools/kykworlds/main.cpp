/* kykworlds — grade every built-in world on the same three numbers.
 *
 *   kykworlds [--K 64 --side 8 --points 200 --braids resources.cc]
 *
 * kykspace already grades a *file*, which only reaches the tabulated worlds.
 * Half the worlds are formulas with no file to inspect, and the point of a
 * table is that every row was measured the same way, so this walks the
 * registry instead and asks each world the one question the engine asks it:
 * given a folded coordinate, what is the spectrum there.
 *
 * The three numbers:
 *
 *   variety   how much the spectrum changes per unit of travel through the
 *             cube, averaged over random directions. Low means a world that
 *             sounds the same everywhere. This is measured on the normalised
 *             spectrum, so it is timbral change and not loudness change.
 *
 *   spread    the ratio of the most varied direction to the least, over 200
 *             random ones. This is the number that decides whether rotating
 *             the control frame was worth building: at 1.0 every direction is
 *             as interesting as every other and a rotation genuinely finds
 *             new ground, while at 10x the space has a few good axes and a
 *             rotation mostly finds the boring ones. Every multiplicative
 *             family measures badly here for a structural reason — a product
 *             of per-axis weights is a sum in log magnitude, which is
 *             separable, and separable means the axes were already natural.
 *
 *   twins     the fraction of random point pairs whose spectra are within 5%
 *             of each other. A world can be varied and even isotropic and
 *             still fold back on itself, and those are the places where a
 *             sweep stalls.
 *
 * Optionally also coverage: for every wave in Emilie Gillet's Braids bank,
 * the distance to the nearest point found in this world. A world can score
 * well on all three and still be a cabinet of curiosities that never lands
 * near anything a player would recognise.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include "kyk_worlds.h"
#include "../corpus.h"

using namespace kyk;

/* Unit-norm spectrum, so distance is timbre rather than level. */
static void Unit(const float* m, int k, double* out)
{
    double n2 = 0;
    for(int i = 0; i < k; i++) n2 += (double)m[i] * m[i];
    const double inv = n2 > 0 ? 1.0 / std::sqrt(n2) : 0.0;
    for(int i = 0; i < k; i++) out[i] = m[i] * inv;
}
static double Dist(const double* a, const double* b, int k)
{
    double d = 0;
    for(int i = 0; i < k; i++) { const double x = a[i] - b[i]; d += x * x; }
    return std::sqrt(d);
}

struct Row
{
    std::string name;
    const char* kind;
    double      variety, spread, twins, cover_pct, cover_med;
    bool        covered = false;
};

int main(int argc, char** argv)
{
    int         K = 64, side = 8, dirs = 200;
    std::string braids;
    for(int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if(a == "--K") K = atoi(next());
        else if(a == "--side") side = atoi(next());
        else if(a == "--points") dirs = atoi(next());
        else if(a == "--braids") braids = next();
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    const int N = 4, P = 8;

    /* Corpus spectra, unit norm, so coverage is measured the same way
     * kykspace measures it: one minus the cosine similarity, and
     * "recognisably covered" means under 0.02. */
    std::vector<std::vector<double>> target;
    if(!braids.empty())
    {
        std::vector<std::vector<float>> cycles;
        int nb = 0;
        if(!kykcorpus::LoadBraids(braids, cycles, nb))
        { fprintf(stderr, "could not read wt_waves from %s\n", braids.c_str()); return 1; }
        std::vector<double> m;
        for(const auto& c : cycles)
        {
            kykcorpus::Analyse(c, K, m);
            double e = 0;
            for(double x : m) e += x * x;
            if(e <= 0) continue;
            const double inv = 1.0 / std::sqrt(e);
            std::vector<double> v((size_t)K);
            for(int i = 0; i < K; i++) v[i] = m[i] * inv;
            target.push_back(std::move(v));
        }
        fprintf(stderr, "  corpus: %zu waves from %s\n", target.size(), braids.c_str());
    }

    std::vector<Row> rows;
    std::vector<uint8_t> blob;

    for(uint8_t wi = 0; wi < worlds::kCount; wi++)
    {
        const worlds::Entry& e = worlds::Get(wi);
        World                w;
        solids::VertexTable  table;
        Space                sp;
        if(e.kind == World::Kind::Lattice)
        {
            blob.assign(Space::BlobSize(N, K, P, side, false), 0);
            const size_t n = worlds::Expand(wi, N, side, K, P, blob.data(), blob.size());
            if(!n) { fprintf(stderr, "%s: generator refused\n", e.name); continue; }
            if(sp.Attach(blob.data(), n) != SpaceError::Ok) { fprintf(stderr, "%s: bad blob\n", e.name); continue; }
            w.UseLattice(&sp);
        }
        else if(!worlds::Point(wi, w, P, nullptr, &table)) { fprintf(stderr, "%s: cannot point\n", e.name); continue; }
        if(!w.Ready()) { fprintf(stderr, "%s: not ready\n", e.name); continue; }

        float  mags[kMaxK], pl[kMaxP];
        Weights wt;
        double  a[kMaxK], b[kMaxK];
        auto at = [&](const float* p01, double* out) {
            w.Evaluate(p01, 0.f, mags, pl, wt);
            Unit(mags, K, out);
        };

        /* variety per unit of travel, along one direction through the centre */
        auto per_unit = [&](const double* d) {
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

        Rng r;
        r.Seed(2026);
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

        /* twins: random pairs whose spectra sit within 5% of each other */
        const int    S = 600;
        std::vector<std::vector<double>> pts;
        pts.reserve(S);
        for(int i = 0; i < S; i++)
        {
            float p[kMaxN];
            for(int ax = 0; ax < N; ax++) p[ax] = r.Uniform();
            std::vector<double> s((size_t)K);
            at(p, s.data());
            pts.push_back(std::move(s));
        }
        long close = 0, total = 0;
        for(int i = 0; i < S; i++)
            for(int j = i + 1; j < S; j++)
            {
                total++;
                if(Dist(pts[i].data(), pts[j].data(), K) < 0.05) close++;
            }

        Row row;
        row.name    = e.name;
        row.kind    = e.kind == World::Kind::Lattice ? "lattice"
                    : e.kind == World::Kind::Vertices ? "vertices"
                    : e.kind == World::Kind::Fm ? "fm"
                    : e.kind == World::Kind::Formant ? "formant"
                    : e.kind == World::Kind::Table ? "table" : "analytic";
        row.variety = mean;
        row.spread  = spread;
        row.twins   = total ? 100.0 * (double)close / (double)total : 0.0;

        if(!target.empty())
        {
            /* Nearest point in this world to each corpus wave, searched over a
             * cloud sampled the same way for every world so the rows compare.
             * The measure is one minus the cosine similarity, matching
             * kykspace's `cover`, and under 0.02 counts as recognisable. */
            const int C = 8000;
            std::vector<std::vector<double>> cloud;
            cloud.reserve((size_t)C);
            Rng cr;
            cr.Seed(99);
            for(int i = 0; i < C; i++)
            {
                float p[kMaxN];
                for(int ax = 0; ax < N; ax++) p[ax] = cr.Uniform();
                std::vector<double> sv((size_t)K);
                at(p, sv.data());
                cloud.push_back(std::move(sv));
            }
            std::vector<double> dists;
            dists.reserve(target.size());
            for(const auto& tv : target)
            {
                double bd = 1e9;
                for(const auto& c : cloud)
                {
                    double dot = 0;
                    for(int i = 0; i < K; i++) dot += tv[i] * c[i];
                    const double d = 1.0 - (dot > 1.0 ? 1.0 : dot);
                    if(d < bd) bd = d;
                }
                dists.push_back(bd);
            }
            std::sort(dists.begin(), dists.end());
            int near = 0;
            for(double d : dists) if(d < 0.02) near++;
            row.cover_pct = 100.0 * near / (double)dists.size();
            row.cover_med = dists[dists.size() / 2];
            row.covered   = true;
        }
        rows.push_back(row);
        fprintf(stderr, "  measured %s\n", e.name);
    }

    printf("\nEvery built-in world, same measurement, K=%d N=%d, lattices at side=%d.\n", K, N, side);
    printf("Spread is the isotropy number: 1.0 would mean every direction through\n"
           "the space is as interesting as every other, which is the condition\n"
           "under which rotating the control frame finds new ground.\n\n");
    printf("  A `table` world's numbers cover its first two axes only. The other\n"
           "  two drive shapers that act on the rendered cycle, not on the\n"
           "  spectrum, so this tool cannot see them and reads them as dead —\n"
           "  which is most of why those rows show a large spread.\n\n");
    printf("  %-10s %-9s %8s %8s %7s", "world", "backend", "variety", "spread", "twins");
    if(!rows.empty() && rows[0].covered) printf(" %8s %8s", "covers", "median");
    printf("\n");
    for(const Row& r : rows)
    {
        printf("  %-10s %-9s %8.3f %7.2fx %6.1f%%", r.name.c_str(), r.kind, r.variety, r.spread, r.twins);
        if(r.covered) printf(" %7.0f%% %8.4f", r.cover_pct, r.cover_med);
        printf("\n");
    }
    return 0;
}
