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
 * The correspondence to the reference is *chained*, not direct. Neighbours in
 * a sweep match at MAC 0.84 to 0.97 and a plate matches a bar at 0.46, so a
 * plate aligned straight to the bar reference has its modes assigned by a
 * coin toss that lands differently at every parameter value — measured, the
 * veering map of that alignment showed 93 slots of the plate sweep jumping by
 * more than 300 cents between neighbours, the worst by seven octaves, and
 * none of it was veering. So each model is matched to its neighbour towards
 * the middle of its own sweep, the middle model of each family is matched to
 * the reference, and the permutations compose. The cross-family step is still
 * the coin toss, taken once per family instead of once per model. --direct
 * gives the old behaviour for comparison.
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
    Match m;
    if(a.fitted || b.fitted)
    {
        /* A recording has one strike position, so its gain rows are all the
         * same direction and MAC is 1 for every pair — there is no shape to
         * match on. Partials are matched by rank instead: the k-th lowest of
         * one to the k-th lowest of the other, which for a near-harmonic
         * instrument is the k-th partial to the k-th partial. Both models are
         * stored ascending, so the rank map is the identity. */
        m.perm.resize(N);
        for(int i = 0; i < N; i++) m.perm[i] = i;
        for(int i = 0; i < N; i++) if(i < a.nreal && i < b.nreal) { m.real_mean += mac[(size_t)i * N + i]; m.real++; }
        m.real_mean = m.real ? m.real_mean / m.real : 0.0;
        m.mean = m.real_mean;
        return m;
    }
    std::vector<double> cost((size_t)N * N);
    for(size_t k = 0; k < cost.size(); k++) cost[k] = 1.0 - mac[k];
    m.perm = Hungarian(cost, N);
    /* A reference row that is padding has the same junk pattern in every
     * model, so every real mode of b matches it equally well and the Hungarian
     * breaks the tie however it likes — which would scramble those modes'
     * order from one model to the next and turn every frequency trajectory
     * through them into a cliff. Modes with no real counterpart in the
     * reference are ordered by frequency instead, which is what "mode k" has
     * to mean when there is no shape to say otherwise. */
    {
        std::vector<std::pair<double, int>> spill;
        std::vector<int> slots;
        for(int i = a.nreal; i < N; i++) { spill.push_back({b.hz[m.perm[i]], m.perm[i]}); slots.push_back(i); }
        std::sort(spill.begin(), spill.end());
        for(size_t k = 0; k < slots.size(); k++) m.perm[slots[k]] = spill[k].second;
    }
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
    bool        direct = false;
    for(int i = 4; i < argc; i++)
    {
        const std::string a = argv[i];
        if(a == "--ref" && i + 1 < argc) ref = argv[++i];
        else if(a == "--direct") direct = true;
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
    const int F = Corpus::kFamilies;
    std::vector<double> fam((size_t)F * F, 0.0), famn((size_t)F * F, 0.0);
    for(int a = 0; a < M; a++)
        for(int b = 0; b < M; b++)
        {
            if(a == b) continue;
            const Match& m = pair[(size_t)a * M + b];
            fam[(size_t)c.m[a].family * F + c.m[b].family] += m.real_mean;
            famn[(size_t)c.m[a].family * F + c.m[b].family] += 1;
        }
    fprintf(rep, "family x family, mean over ordered pairs:\n           ");
    for(int f = 0; f < F; f++) fprintf(rep, "%8s", Corpus::FamilyName(f));
    fprintf(rep, "\n");
    for(int f = 0; f < F; f++)
    {
        fprintf(rep, "  %-8s ", Corpus::FamilyName(f));
        for(int g = 0; g < F; g++) fprintf(rep, "%8.3f", famn[(size_t)f * F + g] ? fam[(size_t)f * F + g] / famn[(size_t)f * F + g] : 0.0);
        fprintf(rep, "\n");
    }
    double within = 0, wn = 0, cross = 0, cn = 0;
    for(int f = 0; f < F; f++) for(int g = 0; g < F; g++)
        if(f == g) { within += fam[(size_t)f * F + g]; wn += famn[(size_t)f * F + g]; }
        else { cross += fam[(size_t)f * F + g]; cn += famn[(size_t)f * F + g]; }
    fprintf(rep, "\n  within-family mean %.3f, cross-family mean %.3f\n\n", within / wn, cross / cn);

    /* along each sweep, neighbour to neighbour — what interpolation asks */
    fprintf(rep, "along each sweep, neighbour to neighbour (real modes, both directions averaged):\n");
    for(int f = 0; f < F; f++)
    {
        std::vector<int> ids;
        for(int k = 0; k < M; k++) if(c.m[k].family == f) ids.push_back(k);
        if(ids.empty()) continue;
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

    /* binary: for each model, perm[k] = which of its modes is reference mode k,
       and the sign to apply, after the largest-component convention */
    /* a link a -> b: for each mode i of a, the matched mode of b and whether
       its row agrees in sign with a's */
    auto link = [&](int a, int b, std::vector<int>& perm, std::vector<int>& sg) {
        const Match& m = pair[(size_t)a * M + b];
        perm.assign(N, 0); sg.assign(N, 1);
        for(int i = 0; i < N; i++)
        {
            perm[i] = m.perm[i];
            const double* ga = &w[a].g[(size_t)i * P];
            const double* gb = &w[b].g[(size_t)m.perm[i] * P];
            sg[i] = Dot(ga, gb, P) < 0 ? -1 : 1;
        }
    };
    /* the path from the reference to each model: reference -> family hub ->
       neighbour -> ... -> model, or one direct hop */
    std::vector<std::vector<int>> toref_perm(M, std::vector<int>(N)), toref_sign(M, std::vector<int>(N));
    {
        std::vector<std::vector<int>> fam(F);
        for(int k = 0; k < M; k++) fam[c.m[k].family].push_back(k);
        for(auto& ids : fam) std::sort(ids.begin(), ids.end(), [&](int x, int y) { return c.m[x].param < c.m[y].param; });
        for(int k = 0; k < M; k++)
        {
            std::vector<int> path;   /* models along the way, reference first */
            path.push_back(refi);
            if(!direct && k != refi)
            {
                const auto& ids = fam[c.m[k].family];
                const int   hub = ids[ids.size() / 2];
                const int   pos = (int)(std::find(ids.begin(), ids.end(), k) - ids.begin());
                const int   hpos = (int)(ids.size() / 2);
                if(hub != refi) path.push_back(hub);
                if(pos < hpos) for(int q = hpos - 1; q >= pos; q--) path.push_back(ids[q]);
                else for(int q = hpos + 1; q <= pos; q++) path.push_back(ids[q]);
            }
            else if(k != refi) path.push_back(k);
            /* compose along the path */
            std::vector<int> perm(N), sg(N);
            for(int i = 0; i < N; i++) { perm[i] = i; sg[i] = 1; }
            for(size_t h = 0; h + 1 < path.size(); h++)
            {
                std::vector<int> lp, ls;
                link(path[h], path[h + 1], lp, ls);
                for(int i = 0; i < N; i++) { sg[i] *= ls[perm[i]]; perm[i] = lp[perm[i]]; }
            }
            /* the convention sign of the model's own row, then the chain */
            for(int i = 0; i < N; i++) { toref_perm[k][i] = perm[i]; toref_sign[k][i] = sign[k][perm[i]] * sg[i]; }
        }
    }
    FILE* bin = fopen(argv[3], "wb");
    if(!bin) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    fwrite("MALN", 1, 4, bin);
    uint32_t hdr[3] = {(uint32_t)M, (uint32_t)N, (uint32_t)refi};
    fwrite(hdr, 4, 3, bin);
    for(int k = 0; k < M; k++)
    {
        std::vector<int32_t> perm(N), sg(N);
        for(int i = 0; i < N; i++) { perm[i] = toref_perm[k][i]; sg[i] = toref_sign[k][i]; }
        fwrite(perm.data(), 4, N, bin);
        fwrite(sg.data(), 4, N, bin);
    }
    fclose(bin);
    fprintf(rep, "\ncorrespondence to the reference: %s\n", direct ? "direct, one Hungarian hop per model" : "chained through each family's sweep from its middle model");
    fclose(rep);
    printf("align: within-family MAC %.3f, cross-family %.3f, reference %s\n", within / wn, cross / cn, ref.c_str());
    return 0;
}
