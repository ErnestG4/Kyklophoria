/* worldbasis — can one world be told where it is in another world's terms?
 *
 * Morphing between worlds splits into two problems and only the second is hard.
 *
 * Crossfading the *output* of two worlds is trivially click-free: the render is
 * linear in the coefficient vector, so a blend of two spectra is a spectrum,
 * and it works between any two backends for the cost of one extra Evaluate.
 * Nothing here is needed for that.
 *
 * Translating a *position* is the hard half, and it is the half that makes the
 * crossfade mean anything. Axis 0 of FM is a modulation index and axis 0 of
 * Plate is a strike position; a morph that holds the coordinates fixed passes
 * through whatever those two happen to collide at, which is arbitrary. What we
 * want is: given where you are in A, find the place in B that sounds most like
 * it, and morph towards that.
 *
 * The standard machinery for this is subspace alignment (Fernando et al., ICCV
 * 2013): fit each world a low-dimensional linear basis, and the map from one
 * set of coordinates to the other is affine — U_B^T U_A plus an offset, a 4x4
 * matrix and four numbers. It is the least-squares optimal linear map, and how
 * good it is at all is given by the principal angles between the two subspaces,
 * whose cosines are the singular values of U_B^T U_A (Bjorck & Golub, 1973).
 *
 * That is worth measuring before it is worth building, because it rests on an
 * assumption that may be false here: that a world's reachable spectra lie near
 * a 4-dimensional linear subspace of log-magnitude space.
 *
 * I predicted in this comment that FM would be the world that fails, on the
 * grounds that its sidebands are Bessel functions of the index and move with
 * the ratio. This tool said 86.3% — mostly linear. The worlds that actually
 * fail are Edge at 21%, Pulse at 27% and Unison at 28%, and in hindsight for a
 * better reason than I had: all three move a *comb* across the spectrum, and a
 * notch pattern whose period slides is precisely what no fixed linear basis
 * can hold. FM moves energy around smoothly; a comb reorders which bins exist.
 *
 * The prediction is left in only because the file that made it is the file that
 * disproved it, which is the argument for writing the tool.
 *
 * So this tool answers two questions with numbers rather than hope:
 *
 *   1. How much of each world does a 4-component linear basis actually capture?
 *   2. For each pair, how aligned are those bases — i.e. how much of what A can
 *      say is sayable in B's vocabulary at all?
 *
 * If the answers are poor, the honest feature is a plain spectral crossfade and
 * no translation table, and it is much better to learn that from this file than
 * from a knob that feels wrong.
 */
#include "kyk_worlds.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace kyk;

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

static const int K = 64, NC = 4;          /* harmonics, components kept */
static const double kFloor = 1e-4;        /* same floor kykeigen bakes with */

struct WbRng { uint64_t s = 1; void Seed(uint64_t v){ s = v?v:1; }
             uint64_t N(){ s^=s<<13; s^=s>>7; s^=s<<17; return s; }
             double U(){ return (double)(N()>>11) * (1.0/9007199254740992.0); } };

struct Basis
{
    bool   ok = false;
    char   name[16] = {0};
    double mean[K];
    double comp[NC][K];      /* orthonormal rows */
    double explained = 0;    /* fraction of log-spectral variance in NC comps */
};

/* Sample a world, PCA its log-spectra, keep NC components. */
static Basis Fit(uint8_t wi, int samples)
{
    Basis B;
    World w; Space sp; solids::VertexTable tbl;
    static std::vector<uint8_t> blob(1u << 22);
    const worlds::Entry& e = worlds::Get(wi);
    if(e.kind == World::Kind::Lattice)
    {
        const size_t n = worlds::Expand(wi, 4, 8, K, 8, blob.data(), blob.size());
        if(!n || sp.Attach(blob.data(), n) != SpaceError::Ok) return B;
        w.UseLattice(&sp);
    }
    else if(!worlds::Point(wi, w, 8, nullptr, &tbl)) return B;
    if(!w.Ready() || w.K() < K) return B;
    std::snprintf(B.name, sizeof B.name, "%s", e.name);

    WbRng r; r.Seed(1234 + wi);
    std::vector<double> X((size_t)samples * K);
    float m[kMaxK], pl[kMaxP]; Weights wt;
    for(int s = 0; s < samples; s++)
    {
        float p[kMaxN];
        for(int a = 0; a < kMaxN; a++) p[a] = (float)r.U();
        w.Evaluate(p, 0.f, m, pl, wt);
        for(int k = 0; k < K; k++) X[(size_t)s * K + k] = std::log(std::fabs((double)m[k]) + kFloor);
    }
    for(int k = 0; k < K; k++)
    {
        double s = 0;
        for(int i = 0; i < samples; i++) s += X[(size_t)i * K + k];
        B.mean[k] = s / samples;
        for(int i = 0; i < samples; i++) X[(size_t)i * K + k] -= B.mean[k];
    }
    std::vector<double> cov((size_t)K * K, 0.0);
    for(int a = 0; a < K; a++)
        for(int b = a; b < K; b++)
        {
            double s = 0;
            for(int i = 0; i < samples; i++) s += X[(size_t)i * K + a] * X[(size_t)i * K + b];
            cov[(size_t)a * K + b] = cov[(size_t)b * K + a] = s / samples;
        }
    std::vector<double> val, vec;
    Jacobi(cov, K, val, vec);
    double tot = 0, top = 0;
    for(int i = 0; i < K; i++) tot += val[i] > 0 ? val[i] : 0;
    for(int c = 0; c < NC; c++)
    {
        top += val[c] > 0 ? val[c] : 0;
        for(int k = 0; k < K; k++) B.comp[c][k] = vec[(size_t)k * K + c];
    }
    B.explained = tot > 0 ? top / tot : 0;
    B.ok = true;
    return B;
}

/* cos of the principal angles between two NC-dimensional subspaces: singular
 * values of U_B^T U_A, obtained as sqrt of the eigenvalues of M^T M. */
static void Angles(const Basis& A, const Basis& B, double* cosines)
{
    std::vector<double> M((size_t)NC * NC, 0.0);
    for(int i = 0; i < NC; i++)
        for(int j = 0; j < NC; j++)
        {
            double s = 0;
            for(int k = 0; k < K; k++) s += B.comp[i][k] * A.comp[j][k];
            M[(size_t)i * NC + j] = s;
        }
    std::vector<double> G((size_t)NC * NC, 0.0);
    for(int i = 0; i < NC; i++)
        for(int j = 0; j < NC; j++)
        {
            double s = 0;
            for(int k = 0; k < NC; k++) s += M[(size_t)k * NC + i] * M[(size_t)k * NC + j];
            G[(size_t)i * NC + j] = s;
        }
    std::vector<double> val, vec;
    Jacobi(G, NC, val, vec);
    for(int i = 0; i < NC; i++)
    { double v = val[i]; if(v < 0) v = 0; if(v > 1) v = 1; cosines[i] = std::sqrt(v); }
}

int main(int argc, char** argv)
{
    int samples = 3000;
    for(int i = 1; i < argc; i++)
        if(!std::strcmp(argv[i], "--samples") && i + 1 < argc) samples = std::atoi(argv[++i]);

    printf("worldbasis: %d samples per world, %d components of %d harmonics\n\n", samples, NC, K);
    std::vector<Basis> bs;
    printf("  %-10s %10s   what a 4-D linear basis can say about this world\n", "world", "explained");
    for(uint8_t wi = 0; wi < worlds::kCount; wi++)
    {
        Basis b = Fit(wi, samples);
        if(!b.ok) continue;
        const char* verdict = b.explained > 0.90 ? "nearly linear — translates well"
                            : b.explained > 0.75 ? "mostly linear"
                            : b.explained > 0.55 ? "partly — expect a lossy translation"
                                                 : "NOT linear — a 4-D basis misses most of it";
        printf("  %-10s %9.1f%%   %s\n", b.name, 100.0 * b.explained, verdict);
        bs.push_back(b);
    }

    printf("\n  pairwise alignment (mean cos of the 4 principal angles; 1.00 = same subspace)\n  ");
    for(size_t j = 0; j < bs.size(); j++) printf("%4.4s ", bs[j].name);
    printf("\n");
    double worst = 1, best = 0; 
    for(size_t i = 0; i < bs.size(); i++)
    {
        printf("  %-10s", bs[i].name);
        for(size_t j = 0; j < bs.size(); j++)
        {
            double c[NC]; Angles(bs[i], bs[j], c);
            double mean = 0; for(int q = 0; q < NC; q++) mean += c[q];
            mean /= NC;
            printf("%4.2f ", mean);
            if(i != j) { if(mean < worst) worst = mean; if(mean > best) best = mean; }
        }
        printf("\n");
    }
    printf("\n  off-diagonal alignment runs %.2f to %.2f\n", worst, best);
    return 0;
}
