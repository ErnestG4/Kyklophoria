/* kykeigen — bake a corpus of single-cycle waves into a Kyklophoria space.
 *
 *   kykeigen out.kyk --braids <path/to/braids/resources.cc> [--corpus <dir>]
 *            [--N 4 --side 8 --K 64 --P 8] [--extent 2.2] [--report r.txt]
 *
 * The idea, and why it is shaped this way:
 *
 *   1. Analyse every corpus cycle to K harmonic magnitudes and normalise each
 *      to unit RMS, so the space is about spectral *shape*, not loudness.
 *   2. Take logs. Reconstruction then becomes multiplicative, which keeps
 *      magnitudes positive without clamping and matches how loudness is heard.
 *      Linear PCA on raw magnitudes would happily reconstruct negative ones.
 *   3. PCA (Jacobi on the K×K covariance), keep the top N components. Those
 *      eigenvectors are the axes of the space: the directions in which real
 *      waves actually differ, ordered by how much they differ.
 *   4. **Whiten**: divide each axis by the square root of its eigenvalue. This
 *      is the step that makes the whole thing work for this instrument. Raw
 *      PCA is maximally anisotropic by construction — component 1 carries the
 *      most variance and the last carries the least — and an anisotropic space
 *      is exactly what makes rotation a lottery (docs/m2-notes.md measured a
 *      9.8x spread of variety across directions in the old parameter grid).
 *      Whitening equalises the axes so every direction is worth sweeping.
 *   5. Fill every lattice node by reconstructing from its whitened coordinate.
 *      The reconstruction is defined everywhere, so regions the corpus does
 *      not reach are extrapolated smoothly rather than left as holes.
 *
 * Payload lanes are measured from each node's own reconstructed spectrum, so
 * the filter and drive track the wave rather than floating free.
 *
 * Offline tool: it uses libm, unlike core/, because the baked file is the
 * artifact and accuracy matters more here than cross-machine bit-identity.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <dirent.h>
#include "kyk_space.h"
#include "kyk_gen.h"
#include "../corpus.h"

using namespace kyk;

using namespace kykcorpus;

/* Cyclic Jacobi eigendecomposition of a symmetric matrix, descending. */
static void Jacobi(std::vector<double>& a, int n, std::vector<double>& val, std::vector<double>& vec)
{
    vec.assign((size_t)n * n, 0.0);
    for(int i = 0; i < n; i++) vec[(size_t)i * n + i] = 1.0;
    for(int sweep = 0; sweep < 100; sweep++)
    {
        double off = 0;
        for(int p = 0; p < n; p++)
            for(int q = p + 1; q < n; q++) off += a[(size_t)p * n + q] * a[(size_t)p * n + q];
        if(off < 1e-22) break;
        for(int p = 0; p < n; p++)
            for(int q = p + 1; q < n; q++)
            {
                const double apq = a[(size_t)p * n + q];
                if(std::fabs(apq) < 1e-30) continue;
                const double theta = 0.5 * (a[(size_t)q * n + q] - a[(size_t)p * n + p]) / apq;
                const double t     = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c     = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for(int i = 0; i < n; i++)
                {
                    const double aip = a[(size_t)i * n + p], aiq = a[(size_t)i * n + q];
                    a[(size_t)i * n + p] = c * aip - s * aiq;
                    a[(size_t)i * n + q] = s * aip + c * aiq;
                }
                for(int i = 0; i < n; i++)
                {
                    const double api = a[(size_t)p * n + i], aqi = a[(size_t)q * n + i];
                    a[(size_t)p * n + i] = c * api - s * aqi;
                    a[(size_t)q * n + i] = s * api + c * aqi;
                    const double vip = vec[(size_t)i * n + p], viq = vec[(size_t)i * n + q];
                    vec[(size_t)i * n + p] = c * vip - s * viq;
                    vec[(size_t)i * n + q] = s * vip + c * viq;
                }
            }
    }
    val.assign((size_t)n, 0.0);
    for(int i = 0; i < n; i++) val[i] = a[(size_t)i * n + i];
    /* sort descending, carrying the vectors */
    std::vector<int> ord((size_t)n);
    for(int i = 0; i < n; i++) ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](int x, int y) { return val[x] > val[y]; });
    std::vector<double> v2((size_t)n), c2((size_t)n * n);
    for(int i = 0; i < n; i++)
    {
        v2[i] = val[ord[i]];
        for(int r = 0; r < n; r++) c2[(size_t)r * n + i] = vec[(size_t)r * n + ord[i]];
    }
    val.swap(v2);
    vec.swap(c2);
}

int main(int argc, char** argv)
{
    if(argc < 2) { fprintf(stderr, "usage: kykeigen out.kyk --braids <resources.cc> [--corpus <dir>] [opts]\n"); return 2; }
    std::string out_path = argv[1], braids, corpus, report;
    int         N = 4, side = 8, K = 64, P = 8;
    double      extent = 2.2;   /* whitened standard deviations spanned by the lattice */
    std::string basis_path;     /* --emit-basis: a C header the firmware can rebuild from */
    for(int i = 2; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if(a == "--braids") braids = next();
        else if(a == "--corpus") corpus = next();
        else if(a == "--report") report = next();
        else if(a == "--N") N = atoi(next());
        else if(a == "--side") side = atoi(next());
        else if(a == "--K") K = atoi(next());
        else if(a == "--P") P = atoi(next());
        else if(a == "--extent") extent = atof(next());
        else if(a == "--emit-basis") basis_path = next();
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if(N < 1 || N > kMaxN || K < 1 || K > kMaxK || P < 0 || P > kMaxP || side < 2 || side > 255)
    { fprintf(stderr, "params out of range\n"); return 2; }

    std::vector<std::vector<float>> cycles;
    int nbraids = 0, nfiles = 0;
    if(!braids.empty() && !LoadBraids(braids, cycles, nbraids))
    { fprintf(stderr, "could not read wt_waves from %s\n", braids.c_str()); return 1; }
    if(!corpus.empty()) LoadWavDir(corpus, cycles, nfiles);
    if(cycles.size() < (size_t)(N + 2))
    { fprintf(stderr, "corpus too small (%zu cycles)\n", cycles.size()); return 1; }
    printf("corpus: %zu cycles (%d from the Braids bank, %d wav files)\n", cycles.size(), nbraids, nfiles);

    /* analyse → log magnitudes */
    const double kFloor = 1e-4;      /* −80 dB; below this a harmonic is absent */
    std::vector<std::vector<double>> Y;
    Y.reserve(cycles.size());
    std::vector<double> m;
    for(const auto& c : cycles)
    {
        Analyse(c, K, m);
        double e = 0;
        for(int k = 0; k < K; k++) e += m[k] * m[k];
        if(e <= 0) continue;                       /* silent cycle */
        std::vector<double> y((size_t)K);
        for(int k = 0; k < K; k++) y[k] = std::log(m[k] + kFloor);
        Y.push_back(std::move(y));
    }
    const int S = (int)Y.size();
    printf("analysed: %d usable spectra at K=%d\n", S, K);

    std::vector<double> mean((size_t)K, 0.0);
    for(const auto& y : Y)
        for(int k = 0; k < K; k++) mean[k] += y[k];
    for(int k = 0; k < K; k++) mean[k] /= S;

    std::vector<double> cov((size_t)K * K, 0.0);
    for(const auto& y : Y)
        for(int i = 0; i < K; i++)
        {
            const double di = y[i] - mean[i];
            for(int j = i; j < K; j++) cov[(size_t)i * K + j] += di * (y[j] - mean[j]);
        }
    for(int i = 0; i < K; i++)
        for(int j = i; j < K; j++)
        {
            const double v = cov[(size_t)i * K + j] / (S - 1);
            cov[(size_t)i * K + j] = v;
            cov[(size_t)j * K + i] = v;
        }

    std::vector<double> val, vec;
    Jacobi(cov, K, val, vec);
    double total = 0;
    for(int k = 0; k < K; k++) total += std::max(0.0, val[k]);
    printf("variance explained: ");
    double cum = 0;
    for(int i = 0; i < N; i++) { cum += std::max(0.0, val[i]); printf("%s%.1f%%", i ? " + " : "", 100.0 * std::max(0.0, val[i]) / total); }
    printf("  =  %.1f%% in %d components\n", 100.0 * cum / total, N);

    /* project the corpus, whitened, and find the extent actually occupied */
    std::vector<double> lo((size_t)N, 1e9), hi((size_t)N, -1e9);
    for(const auto& y : Y)
        for(int i = 0; i < N; i++)
        {
            double c = 0;
            for(int k = 0; k < K; k++) c += (y[k] - mean[k]) * vec[(size_t)k * K + i];
            const double sd = std::sqrt(std::max(1e-12, val[i]));
            c /= sd;                                   /* whitened: unit variance per axis */
            lo[i] = std::min(lo[i], c);
            hi[i] = std::max(hi[i], c);
        }
    printf("whitened corpus extent:");
    for(int i = 0; i < N; i++) printf("  a%d [%.2f, %.2f]", i, lo[i], hi[i]);
    printf("\nlattice spans +-%.2f sd on every axis\n", extent);

    /* fill the lattice */
    std::vector<uint8_t> blob(Space::BlobSize(N, K, P, side, false));
    SpaceHeader h;
    std::memset(&h, 0, sizeof(h));
    h.magic = kSpaceMagic; h.version = kSpaceVersion;
    h.n = (uint8_t)N; h.mode = kModeLattice; h.k = (uint8_t)K; h.p = (uint8_t)P;
    h.side = (uint8_t)side; h.phase_seed = 1;
    uint32_t count = 1;
    for(int a = 0; a < N; a++) count *= (uint32_t)side;
    h.point_count = count;
    std::strncpy(h.name, "eigen", sizeof(h.name));
    std::memcpy(blob.data(), &h, sizeof(h));

    float*       w      = reinterpret_cast<float*>(blob.data() + sizeof(h));
    const size_t stride = (size_t)K + (size_t)P;
    std::vector<int> ix((size_t)N, 0);
    for(uint32_t idx = 0; idx < count; idx++)
    {
        float* mm = w + (size_t)idx * stride;
        /* node → whitened coordinate, spanning ±extent */
        double coord[kMaxN];
        for(int a = 0; a < N; a++)
            coord[a] = (2.0 * ix[a] / (double)(side - 1) - 1.0) * extent;
        /* reconstruct the log spectrum, un-whitening as we go */
        double e = 0, num = 0, den = 0, hf = 0;
        for(int k = 0; k < K; k++)
        {
            double y = mean[k];
            for(int a = 0; a < N; a++)
                y += coord[a] * std::sqrt(std::max(1e-12, val[a])) * vec[(size_t)k * K + a];
            double v = std::exp(y) - kFloor;
            if(v < 0) v = 0;
            mm[k] = (float)v;
            e += v * v;
        }
        const double g = e > 0 ? std::sqrt(2.0 / e) : 0.0;
        for(int k = 0; k < K; k++)
        {
            mm[k] = (float)(mm[k] * g);
            const double p2 = (double)mm[k] * mm[k];
            num += (k + 1) * p2;
            den += p2;
            if(k >= K / 4) hf += p2;
        }
        /* payload measured from this node's own spectrum, so the filter and
         * drive follow the wave instead of floating free */
        const double centroid = den > 0 ? (num / den - 1.0) / (double)(K - 1) : 0.0;
        const double bright   = den > 0 ? hf / den : 0.0;
        double       flat     = 0;
        for(int k = 0; k < K; k++) flat += std::log((double)mm[k] * mm[k] + 1e-12);
        flat = den > 0 ? std::exp(flat / K) / (den / K) : 0.0;      /* spectral flatness */
        double rad = 0;
        for(int a = 0; a < N; a++) rad += coord[a] * coord[a];
        rad = std::sqrt(rad) / (extent * std::sqrt((double)N));
        auto sat = [](double x) { return (float)(x < 0 ? 0 : (x > 1 ? 1 : x)); };
        float pl[kMaxP];
        pl[0] = sat(std::sqrt(centroid));      /* cutoff tracks brightness   */
        pl[1] = sat(1.0 - flat);               /* resonance: peaky, not flat */
        pl[2] = sat(bright);                   /* fm index                   */
        pl[3] = sat(0.3 + 0.7 * bright);       /* drive                      */
        pl[4] = sat(rad);                      /* cv out a: distance out     */
        pl[5] = sat(0.5 + 0.5 * coord[0] / extent);  /* cv out b: PC1        */
        pl[6] = sat(centroid);
        pl[7] = sat(flat);
        for(int j = 0; j < P; j++) mm[K + j] = pl[j];
        for(int a = 0; a < N; a++) { if(++ix[a] < side) break; ix[a] = 0; }
    }

    std::ofstream of(out_path, std::ios::binary);
    of.write((const char*)blob.data(), (std::streamsize)blob.size());
    if(!of) { fprintf(stderr, "cannot write %s\n", out_path.c_str()); return 1; }
    printf("wrote %s (%zu bytes, %u nodes)\n", out_path.c_str(), blob.size(), count);

    /* The basis is the whole space in about a kilobyte: a mean log-spectrum and
     * N component vectors, each already scaled by its standard deviation so a
     * reconstruction is just mean + sum of coord times component. Emitting it
     * lets the module rebuild this space itself, which is how a world reaches
     * the panel without an SD card. */
    if(!basis_path.empty())
    {
        FILE* f = fopen(basis_path.c_str(), "w");
        if(!f) { fprintf(stderr, "cannot write %s\n", basis_path.c_str()); return 1; }
        fprintf(f, "/* Generated by tools/kykeigen from a corpus of %d cycles. Do not edit.\n"
                   " * %d components carry %.1f%% of the log-spectral variance.\n"
                   " * Reconstruct: y[k] = mean[k] + sum_i coord[i] * comp[i][k], then exp. */\n",
                S, N, 100.0 * cum / total);
        fprintf(f, "#pragma once\nnamespace kyk {\nnamespace worlds {\n\n");
        fprintf(f, "constexpr int   kEigenK      = %d;\n", K);
        fprintf(f, "constexpr int   kEigenN      = %d;\n", N);
        fprintf(f, "constexpr float kEigenExtent = %.8ff;\n", extent);
        fprintf(f, "constexpr float kEigenFloor  = %.8ff;\n\n", kFloor);
        fprintf(f, "static const float kEigenMean[%d] = {\n", K);
        for(int k = 0; k < K; k++) fprintf(f, "%s%.8ff%s", (k % 6 == 0 ? "    " : " "), mean[k], (k % 6 == 5 || k == K - 1) ? ",\n" : ",");
        fprintf(f, "};\n\nstatic const float kEigenComp[%d][%d] = {\n", N, K);
        for(int i = 0; i < N; i++)
        {
            fprintf(f, "  { /* component %d */\n", i);
            const double sd = std::sqrt(std::max(1e-12, val[i]));
            for(int k = 0; k < K; k++)
            {
                const double c = sd * vec[(size_t)k * K + i];
                fprintf(f, "%s%.8ff%s", (k % 6 == 0 ? "    " : " "), c, (k % 6 == 5 || k == K - 1) ? ",\n" : ",");
            }
            fprintf(f, "  },\n");
        }
        fprintf(f, "};\n\n} // namespace worlds\n} // namespace kyk\n");
        fclose(f);
        printf("basis: %s (%d x %d floats, about %zu bytes of flash)\n",
               basis_path.c_str(), N + 1, K, (size_t)((N + 1) * K * sizeof(float)));
    }

    if(!report.empty())
    {
        FILE* f = fopen(report.c_str(), "w");
        if(f)
        {
            fprintf(f, "kykeigen report\ncorpus cycles %d\nK %d  N %d  side %d  P %d  extent %.2f\n", S, K, N, side, P, extent);
            for(int i = 0; i < K && i < 16; i++)
                fprintf(f, "component %2d  eigenvalue %10.5f  variance %5.2f%%\n", i, val[i], 100.0 * std::max(0.0, val[i]) / total);
            fclose(f);
            printf("report: %s\n", report.c_str());
        }
    }
    return 0;
}
