/* align — Stage 2: which mode of one model is which mode of another.
 *
 *   align out/corpus.mdb out/align.txt out/align.bin [--ref bar05]
 *
 * This is the stage kykeigen never needed. Spectra in a shared harmonic basis
 * are already aligned — bin 7 is the seventh harmonic in every wave — and a
 * modal operator's rows are not: mode 7 of a bar and mode 7 of a plate are the
 * seventh-lowest thing each object does, which is a different thing, and even
 * along a sweep two modes can trade places as their frequencies cross.
 *
 * The observable proxy for a mode's shape is its gain pattern across the P
 * canonical positions — a P-vector, signed. Two modes are compared by the
 * modal assurance criterion, MAC(i, j) = (g_i . g_j)^2 / (|g_i|^2 |g_j|^2),
 * which is 1 for the same shape up to scale and sign and 0 for orthogonal
 * ones. Between two models the N x N MAC matrix is turned into a one-to-one
 * correspondence by the Hungarian assignment on 1 - MAC, and the sign of every
 * row is fixed by its largest-magnitude component so that the eigenvector's
 * arbitrary sign does not read as a different shape.
 *
 * Two outputs. The text report is the deliverable: mean matched MAC for every
 * model pair, summarised within and across families, and separately along each
 * sweep (neighbour to neighbour, which is what interpolation actually asks
 * of the corpus). The binary is for Stage 3: every model aligned to one
 * reference — a permutation and a sign per mode — so that "mode k" means the
 * same thing everywhere the bake looks.
 *
 * If cross-family MAC comes out uniformly low, that is the finding and it is
 * printed as such. Nothing here tries to make it better.
 */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "../common/corpus.h"
#include "../common/hungarian.h"

using namespace mb;

static double Dot(const double* a, const double* b, int n) { double s = 0; for(int i = 0; i < n; i++) s += a[i] * b[i]; return s; }

/* Sign convention: the largest-magnitude component of every gain row is
 * positive. Applied to a copy, so the corpus stays as extracted. */
static void FixSigns(Model& m, int N, int P, std::vector<int>& sign)
{
    sign.assign(N, 1);
    for(int i = 0; i < N; i++)
    {
        double* g = &m.g[(size_t)i * P];
        int     k = 0;
        for(int p = 1; p < P; p++) if(std::fabs(g[p]) > std::fabs(g[k])) k = p;
        if(g[k] < 0) { sign[i] = -1; for(int p = 0; p < P; p++) g[p] = -g[p]; }
    }
}

static void MacMatrix(const Model& a, const Model& b, int N, int P, std::vector<double>& mac)
{
    mac.assign((size_t)N * N, 0.0);
    for(int i = 0; i < N; i++)
    {
        const double* ga = &a.g[(size_t)i * P];
        const double  na = Dot(ga, ga, P);
        for(int j = 0; j < N; j++)
        {
            const double* gb = &b.g[(size_t)j * P];
            const double  nb = Dot(gb, gb, P);
            const double  d  = Dot(ga, gb, P);
            mac[(size_t)i * N + j] = (na > 0 && nb > 0) ? d * d / (na * nb) : 0.0;
        }
    }
}

struct Match { std::vector<int> perm; double mean = 0, real_mean = 0; int real = 0; };

/* perm[i] = the mode of b matched to mode i of a. Mean MAC over all N and over
 * the modes that are real in both, which is the number that means anything. */
static Match Align(const Model& a, const Model& b, int N, int P)
{
    std::vector<double> mac;
    MacMatrix(a, b, N, P, mac);
    std::vector<double> cost((size_t)N * N);
    for(size_t k = 0; k < cost.size(); k++) cost[k] = 1.0 - mac[k];
    Match m;
    m.perm = Hungarian(cost, N);
    double s = 0, sr = 0;
    for(int i = 0; i < N; i++)
    {
        const double v = mac[(size_t)i * N + m.perm[i]];
        s += v;
        if(i < a.nreal && m.perm[i] < b.nreal) { sr += v; m.real++; }
    }
    m.mean      = s / N;
    m.real_mean = m.real ? sr / m.real : 0.0;
    return m;
}

int main(int argc, char** argv)
{
    if(argc < 4) { fprintf(stderr, "align corpus.mdb report.txt align.bin [--ref id]\n"); return 2; }
    std::string ref = "bar05";
    for(int i = 4; i < argc; i++)
    {
        const std::string a = argv[i];
        if(a == "--ref" && i + 1 < argc) ref = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Corpus c;
    if(!ReadCorpus(argv[1], c)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const int N = c.N, P = c.P, M = (int)c.m.size();
    int refi = -1;
    for(int k = 0; k < M; k++) if(c.m[k].id == ref) refi = k;
    if(refi < 0) { fprintf(stderr, "no model %s\n", ref.c_str()); return 1; }

    /* signs first, on a working copy */
    std::vector<Model>            w = c.m;
    std::vector<std::vector<int>> sign(M);
    for(int k = 0; k < M; k++) FixSigns(w[k], N, P, sign[k]);

    /* every ordered pair */
    std::vector<Match> pair((size_t)M * M);
    for(int a = 0; a < M; a++)
        for(int b = 0; b < M; b++)
            if(a != b) pair[(size_t)a * M + b] = Align(w[a], w[b], N, P);

    FILE* rep = fopen(argv[2], "w");
    if(!rep) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    fprintf(rep, "align — mean matched MAC, Hungarian on 1-MAC over %d modes, %d positions, %d models\n\n", N, P, M);
    fprintf(rep, "MAC is over the modes real in both models; 'all' includes the padding, which\n"
                 "matches itself across models by construction and would flatter every number.\n\n");

    /* within / cross family summary */
    double fam[3][3] = {{0}}, famn[3][3] = {{0}};
    for(int a = 0; a < M; a++)
        for(int b = 0; b < M; b++)
        {
            if(a == b) continue;
            const Match& m = pair[(size_t)a * M + b];
            fam[c.m[a].family][c.m[b].family] += m.real_mean;
            famn[c.m[a].family][c.m[b].family] += 1;
        }
    fprintf(rep, "family x family, mean over ordered pairs:\n           ");
    for(int f = 0; f < 3; f++) fprintf(rep, "%8s", Corpus::FamilyName(f));
    fprintf(rep, "\n");
    for(int f = 0; f < 3; f++)
    {
        fprintf(rep, "  %-8s ", Corpus::FamilyName(f));
        for(int g = 0; g < 3; g++) fprintf(rep, "%8.3f", famn[f][g] ? fam[f][g] / famn[f][g] : 0.0);
        fprintf(rep, "\n");
    }
    double within = 0, wn = 0, cross = 0, cn = 0;
    for(int f = 0; f < 3; f++) for(int g = 0; g < 3; g++)
        if(f == g) { within += fam[f][g]; wn += famn[f][g]; } else { cross += fam[f][g]; cn += famn[f][g]; }
    fprintf(rep, "\n  within-family mean %.3f, cross-family mean %.3f\n\n", within / wn, cross / cn);

    /* along each sweep, neighbour to neighbour — what interpolation asks */
    fprintf(rep, "along each sweep, neighbour to neighbour (real modes, both directions averaged):\n");
    for(int f = 0; f < 3; f++)
    {
        std::vector<int> ids;
        for(int k = 0; k < M; k++) if(c.m[k].family == f) ids.push_back(k);
        std::sort(ids.begin(), ids.end(), [&](int x, int y) { return c.m[x].param < c.m[y].param; });
        fprintf(rep, "  %-6s", Corpus::FamilyName(f));
        double lo = 1, s = 0; int n = 0;
        for(size_t k = 0; k + 1 < ids.size(); k++)
        {
            const double v = 0.5 * (pair[(size_t)ids[k] * M + ids[k + 1]].real_mean + pair[(size_t)ids[k + 1] * M + ids[k]].real_mean);
            fprintf(rep, " %.2f", v);
            lo = std::min(lo, v); s += v; n++;
        }
        fprintf(rep, "   mean %.3f  worst %.3f\n", n ? s / n : 0.0, lo);
    }

    /* the reference, which is what the bake aligns to */
    fprintf(rep, "\nto the reference %s (real modes in both / all %d):\n", ref.c_str(), N);
    for(int k = 0; k < M; k++)
    {
        if(k == refi) continue;
        const Match& m = pair[(size_t)refi * M + k];
        fprintf(rep, "  %-8s %-6s %6.3f  real %.3f over %2d modes   all %.3f\n", c.m[k].id.c_str(),
                Corpus::FamilyName(c.m[k].family), c.m[k].param, m.real_mean, m.real, m.mean);
    }

    /* full pair table, real-mode MAC */
    fprintf(rep, "\nevery ordered pair, real-mode MAC (row -> column):\n%-8s", "");
    for(int b = 0; b < M; b++) fprintf(rep, " %4s", c.m[b].id.substr(c.m[b].id.size() > 4 ? c.m[b].id.size() - 4 : 0).c_str());
    fprintf(rep, "\n");
    for(int a = 0; a < M; a++)
    {
        fprintf(rep, "%-8s", c.m[a].id.c_str());
        for(int b = 0; b < M; b++)
            if(a == b) fprintf(rep, "    .");
            else fprintf(rep, " %4.2f", pair[(size_t)a * M + b].real_mean);
        fprintf(rep, "\n");
    }
    fclose(rep);

    /* binary: for each model, perm[k] = which of its modes is reference mode k,
       and the sign to apply, after the largest-component convention */
    FILE* bin = fopen(argv[3], "wb");
    if(!bin) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    fwrite("MALN", 1, 4, bin);
    uint32_t hdr[3] = {(uint32_t)M, (uint32_t)N, (uint32_t)refi};
    fwrite(hdr, 4, 3, bin);
    for(int k = 0; k < M; k++)
    {
        std::vector<int32_t> perm(N), sg(N);
        if(k == refi) for(int i = 0; i < N; i++) { perm[i] = i; sg[i] = sign[k][i]; }
        else
        {
            const Match& m = pair[(size_t)refi * M + k];
            for(int i = 0; i < N; i++)
            {
                perm[i] = m.perm[i];
                /* sign: the convention, then agreement with the reference row */
                const double* gr = &w[refi].g[(size_t)i * P];
                const double* gk = &w[k].g[(size_t)m.perm[i] * P];
                sg[i] = sign[k][m.perm[i]] * (Dot(gr, gk, P) < 0 ? -1 : 1);
            }
        }
        fwrite(perm.data(), 4, N, bin);
        fwrite(sg.data(), 4, N, bin);
    }
    fclose(bin);
    printf("align: within-family MAC %.3f, cross-family %.3f, reference %s\n", within / wn, cross / cn, ref.c_str());
    return 0;
}
