/* kykspace — generate and inspect space files (docs/space-format.md).
 *
 *   kykspace gen out.kyk [--N 4 --side 4 --K 64 --P 8 --seed 1 --name harmonic --wrap <axis>]
 *   kykspace info file.kyk
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include "kyk_interp.h"
#include "kyk_space.h"
#include "kyk_gen.h"

using namespace kyk;

static int Info(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); return 1; }
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)), {});
    Space                s;
    const SpaceError     e = s.Attach(blob.data(), blob.size());
    if(e != SpaceError::Ok) { printf("%s: INVALID (%s)\n", path.c_str(), Space::ErrorName(e)); return 1; }
    const SpaceHeader& h = s.Header();
    char               name[33];
    std::memcpy(name, h.name, 32);
    name[32] = 0;
    printf("%s\n  name      %s\n  N=%d side=%d points=%u  K=%d P=%d  phases=%s seed=%u\n", path.c_str(), name, s.N(),
           s.Side(), s.PointCount(), s.K(), s.P(), s.HasPhases() ? "stored" : "derived", h.phase_seed);
    printf("  topology  ");
    for(int a = 0; a < s.N(); a++) printf("%s%s", a ? "," : "", s.TopoOf(a) == Topo::Clamp ? "clamp" : s.TopoOf(a) == Topo::Wrap ? "wrap" : "sphere");
    printf("\n  bytes     %zu (%zu header + %u × %zu floats)\n", blob.size(), sizeof(SpaceHeader), s.PointCount(), s.Stride());
    /* sanity: finite values, spectral centroid range */
    double cmin = 1e9, cmax = 0;
    size_t bad = 0;
    for(uint32_t i = 0; i < s.PointCount(); i++)
    {
        const float* m = s.Mags(i);
        double       num = 0, den = 0;
        for(int k = 0; k < s.K(); k++)
        {
            if(!std::isfinite(m[k]) || m[k] < 0.f) bad++;
            num += (k + 1) * (double)m[k] * m[k];
            den += (double)m[k] * m[k];
        }
        const double c = den > 0 ? num / den : 0;
        cmin = std::fmin(cmin, c); cmax = std::fmax(cmax, c);
        for(int j = 0; j < s.P(); j++) if(!std::isfinite(s.Payload(i)[j])) bad++;
    }
    printf("  centroid  %.2f .. %.2f harmonics\n  bad values %zu\n", cmin, cmax, bad);

    /* Is it worth exploring? Three numbers, all from docs/m2-notes.md.
     *   shape distance / duplicates — is there material here at all
     *   cell-centre dip             — does the level hold across a cell (raw)
     *   direction spread            — the one that decides whether rotating
     *                                 the control frame finds anything. A
     *                                 space with privileged axes gives a wide
     *                                 spread and most rotations land on a dead
     *                                 direction. */
    const int K = s.K(), N = s.N();
    {
        double sum = 0; long pairs = 0, dup = 0;
        const uint32_t step = s.PointCount() > 512u ? s.PointCount() / 512u : 1u;
        std::vector<std::vector<double>> u;
        for(uint32_t i = 0; i < s.PointCount(); i += step)
        {
            std::vector<double> v((size_t)K); double n2 = 0;
            for(int k = 0; k < K; k++) { v[k] = s.Mags(i)[k]; n2 += v[k] * v[k]; }
            n2 = std::sqrt(n2 > 0 ? n2 : 1);
            for(int k = 0; k < K; k++) v[k] /= n2;
            u.push_back(std::move(v));
        }
        for(size_t i = 0; i < u.size(); i++)
            for(size_t j = i + 1; j < u.size(); j++)
            {
                double d = 0;
                for(int k = 0; k < K; k++) d += u[i][k] * u[j][k];
                d = 1.0 - std::fmin(1.0, d);
                sum += d; pairs++; if(d < 0.001) dup++;
            }
        printf("  variety   shape distance mean %.4f over %ld sampled pairs, near-duplicates %.1f%%\n",
               sum / (double)(pairs ? pairs : 1), pairs, 100.0 * (double)dup / (double)(pairs ? pairs : 1));
    }
    {
        float mags[kMaxK], pl[kMaxP];
        double worst = 0;
        int ix[kMaxN] = {0,0,0,0,0,0}; int cells = 1;
        for(int a = 0; a < N; a++) cells *= (s.Side() - 1);
        if(cells > 4096) cells = 4096;
        for(int c = 0; c < cells; c++)
        {
            float p[kMaxN];
            for(int a = 0; a < N; a++) p[a] = ((float)ix[a] + 0.5f) / (float)(s.Side() - 1);
            Weights w; LatticeWeights(s, p, w); Blend(s, w, mags, pl, BlendLevel::Raw);
            double n2 = 0; for(int k = 0; k < K; k++) n2 += (double)mags[k] * mags[k];
            worst = std::fmin(worst, 20.0 * std::log10(std::sqrt(n2) / std::sqrt(2.0)));
            for(int a = 0; a < N; a++) { if(++ix[a] < s.Side() - 1) break; ix[a] = 0; }
        }
        printf("  level     worst raw cell-centre dip %.2f dB (rescaled to 0 at runtime)\n", worst);
    }
    {
        float ma[kMaxK], pl[kMaxP], prev[kMaxK];
        auto per_unit = [&](const double* dir) {
            const int M = 200; double len = 0; const double span = 0.9;
            for(int i = 0; i <= M; i++)
            {
                double t = ((double)i / M - 0.5) * span; float p[kMaxN];
                for(int a = 0; a < N; a++) p[a] = (float)std::fmin(1.0, std::fmax(0.0, 0.5 + t * dir[a]));
                Weights w; LatticeWeights(s, p, w); Blend(s, w, ma, pl);
                if(i)
                {
                    double na = 0, nb = 0, d = 0;
                    for(int k = 0; k < K; k++) { na += (double)prev[k]*prev[k]; nb += (double)ma[k]*ma[k]; }
                    na = std::sqrt(na > 0 ? na : 1); nb = std::sqrt(nb > 0 ? nb : 1);
                    for(int k = 0; k < K; k++) { double x = prev[k]/na - ma[k]/nb; d += x*x; }
                    len += std::sqrt(d);
                }
                std::memcpy(prev, ma, sizeof(float) * (size_t)K);
            }
            return len / span;
        };
        Rng r; r.Seed(2026); std::vector<double> v;
        for(int t = 0; t < 200; t++)
        {
            double d[kMaxN], n2 = 0;
            for(int a = 0; a < N; a++) { double u1 = r.Uniform() + 1e-9, u2 = r.Uniform();
                d[a] = std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2); n2 += d[a] * d[a]; }
            n2 = std::sqrt(n2 > 0 ? n2 : 1);
            for(int a = 0; a < N; a++) d[a] /= n2;
            v.push_back(per_unit(d));
        }
        std::sort(v.begin(), v.end());
        double mean = 0; for(double x : v) mean += x; mean /= v.size();
        printf("  isotropy  variety/unit mean %.3f, spread across directions %.2fx (p95/p5 %.2f)\n",
               mean, v.back() / (v.front() > 0 ? v.front() : 1e-9), v[189] / (v[9] > 0 ? v[9] : 1e-9));
    }
    return bad ? 1 : 0;
}

int main(int argc, char** argv)
{
    if(argc < 3) { fprintf(stderr, "usage: kykspace gen out.kyk [opts] | kykspace info file.kyk\n"); return 2; }
    const std::string cmd = argv[1], path = argv[2];
    if(cmd == "info") return Info(path);
    if(cmd != "gen") { fprintf(stderr, "unknown command %s\n", cmd.c_str()); return 2; }
    GenParams gp;
    std::string name = "harmonic";
    for(int i = 3; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if(a == "--N") gp.n = atoi(next());
        else if(a == "--side") gp.side = atoi(next());
        else if(a == "--K") gp.k = atoi(next());
        else if(a == "--P") gp.p = atoi(next());
        else if(a == "--seed") gp.seed = (uint32_t)strtoul(next(), nullptr, 0);
        else if(a == "--name") name = next();
        else if(a == "--family")
        {
            const std::string f = next();
            if(f == "field") gp.family = Family::Field;
            else if(f == "harmonic") gp.family = Family::Harmonic;
            else { fprintf(stderr, "unknown family %s (harmonic|field)\n", f.c_str()); return 2; }
            if(name == "harmonic" && gp.family == Family::Field) name = "field";
        }
        else if(a == "--rough") gp.rough = (float)atof(next());
        else if(a == "--smooth") gp.smooth = atoi(next());
        else if(a == "--tilt") gp.tilt = (float)atof(next());
        else if(a == "--wrap") { int ax = atoi(next()); if(ax >= 0 && ax < kMaxN) gp.topo[ax] = (uint8_t)Topo::Wrap; }
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    gp.name = name.c_str();
    std::vector<uint8_t> blob(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
    const size_t n = BuildLattice(gp, blob.data(), blob.size());
    if(!n) { fprintf(stderr, "generator refused the params (N≤%d, K≤%d, P≤%d, side≥2)\n", kMaxN, kMaxK, kMaxP); return 1; }
    std::ofstream out(path, std::ios::binary);
    out.write((const char*)blob.data(), (std::streamsize)n);
    if(!out) { fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
    printf("wrote %s (%zu bytes)\n", path.c_str(), n);
    return Info(path);
}
