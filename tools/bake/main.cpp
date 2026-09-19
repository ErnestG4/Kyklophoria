/* bake — Stage 3: PCA in the tangent space, four components, and the report.
 *
 *   bake out/corpus.mdb out/align.bin out/space-full.msp out/bake-full.txt
 *        [--variant full|lambda|linear] [--K 4] [--extent 2.2] [--holdout 3,8]
 *        [--block-weight total|entry] [--family bar|plate|bell] [--decay]
 *
 * --family bakes one family's twelve models alone, or a comma-separated list
 * of families. The brief's three are `bar,plate,bell`; the corpus has grown a
 * tine since and the headline numbers are still the brief's corpus. Every within-family number
 * in the alignment stage is better than every cross-family one, and the
 * listening set sounded like something where the grade said crossfade, so the
 * next question is what a space of one family measures.
 *
 * kykeigen's recipe — analyse, take logs, PCA, whiten, reconstruct
 * everywhere — with the analysis replaced by the alignment stage and the logs
 * replaced by the manifold maps in space.h. The PCA itself is the same
 * eigendecomposition, done on the M x M Gram matrix rather than the D x D
 * covariance because M is 36 and D is 702, and whitening is the same division
 * by the square root of each eigenvalue, for the same reason: an anisotropic
 * space is one where rotation finds nothing.
 *
 * Validation runs before the grade and is the first thing in the report. Two
 * models per family are held out, the components are fitted without them, and
 * each is reconstructed from its own coordinates: frequencies in cents, gains
 * as a relative Frobenius error, the smallest frequency (which the `full` and
 * `lambda` variants cannot make negative and `linear` can), and how far the
 * reconstructed frame is from orthonormal. Then the whole corpus is fitted and
 * that is the space that gets graded.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "../common/corpus.h"
#include "../common/space.h"

using namespace mb;

struct Aligned { std::vector<Rep> rep; int ref = -1; };

static bool LoadAligned(const std::string& path, const Corpus& c, Aligned& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if(!f) return false;
    char magic[4];
    uint32_t hdr[3];
    if(fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MALN", 4) != 0 || fread(hdr, 4, 3, f) != 3) { fclose(f); return false; }
    const int M = (int)hdr[0], N = (int)hdr[1];
    if(M != (int)c.m.size() || N != c.N) { fclose(f); return false; }
    out.ref = (int)hdr[2];
    out.rep.resize(M);
    for(int k = 0; k < M; k++)
    {
        std::vector<int32_t> perm(N), sg(N);
        if(fread(perm.data(), 4, N, f) != (size_t)N || fread(sg.data(), 4, N, f) != (size_t)N) { fclose(f); return false; }
        Rep& r = out.rep[k];
        r.hz.resize(N);
        r.zeta.resize(N);
        r.G = Mat(N, c.P);
        for(int i = 0; i < N; i++)
        {
            r.hz[i] = c.m[k].hz[perm[i]];
            r.zeta[i] = c.m[k].zeta[perm[i]];
            for(int p = 0; p < c.P; p++) r.G(i, p) = sg[i] * c.m[k].G(perm[i], p, c.P);
        }
    }
    fclose(f);
    return true;
}

/* Fit: block scales, mean, K components, sdevs, from the given models. */
static bool gBlockTotal = true;
static void Fit(const Chart& chart, const std::vector<std::vector<double>>& vecs, const std::vector<int>& use,
                int K, Space& sp, std::vector<double>& explained)
{
    const int D = chart.D(), M = (int)use.size(), B = chart.Blocks();
    sp.chart = chart; sp.K = K;
    sp.mean.assign(D, 0.0);
    for(int u : use) for(int i = 0; i < D; i++) sp.mean[i] += vecs[u][i] / M;
    /* block scales: the standard deviation of a block's entries about the mean */
    sp.block_scale.assign(B, 0.0);
    std::vector<double> cnt(B, 0.0);
    for(int u : use)
        for(int i = 0; i < D; i++)
        {
            const double d = vecs[u][i] - sp.mean[i];
            sp.block_scale[chart.BlockOf(i)] += d * d;
            cnt[chart.BlockOf(i)] += 1;
        }
    /* Two ways to weigh the blocks. `entry` gives every entry unit variance,
     * which hands the tangent block — 576 of the 768 entries — three quarters
     * of the variance the PCA is asked to explain, and the 48 frequencies six
     * per cent. `total` gives every block the same total variance, so the
     * frequencies, the subspace and the coefficients are three equal votes.
     * Neither is canonical. `total` is the default because the go/no-go is
     * whether the eigenvector half adds anything to the frequency half, and a
     * weighting that drowns the frequency half decides that before measuring. */
    for(int b = 0; b < B; b++)
    {
        const double per_entry = cnt[b] ? sp.block_scale[b] / cnt[b] : 1.0;        /* mean variance of an entry */
        const double per_model = cnt[b] ? sp.block_scale[b] / (double)M : 1.0;   /* total variance of the block */
        sp.block_scale[b] = std::sqrt(gBlockTotal ? per_model : per_entry);
    }
    for(int b = 0; b < B; b++) if(sp.block_scale[b] < 1e-12) sp.block_scale[b] = 1.0;
    /* X: M x D, centred and scaled */
    std::vector<std::vector<double>> X(M, std::vector<double>(D));
    for(int r = 0; r < M; r++)
        for(int i = 0; i < D; i++) X[r][i] = (vecs[use[r]][i] - sp.mean[i]) / sp.block_scale[chart.BlockOf(i)];
    /* Gram trick */
    std::vector<double> gram((size_t)M * M, 0.0);
    for(int r = 0; r < M; r++)
        for(int s = r; s < M; s++)
        {
            double d = 0;
            for(int i = 0; i < D; i++) d += X[r][i] * X[s][i];
            gram[(size_t)r * M + s] = gram[(size_t)s * M + r] = d;
        }
    std::vector<double> val, vec;
    Jacobi(gram, M, val, vec);
    double total = 0;
    for(double v : val) total += v > 0 ? v : 0;
    explained.assign(K, 0.0);
    sp.comp.assign(K, std::vector<double>(D, 0.0));
    sp.sdev.assign(K, 0.0);
    for(int k = 0; k < K; k++)
    {
        const double lam = val[k] > 0 ? val[k] : 0.0;
        explained[k] = total > 0 ? lam / total : 0.0;
        for(int i = 0; i < D; i++)
        {
            double s = 0;
            for(int r = 0; r < M; r++) s += X[r][i] * vec[(size_t)r * M + k];
            sp.comp[k][i] = lam > 0 ? s / std::sqrt(lam) : 0.0;
        }
        sp.sdev[k] = std::sqrt(lam / (M > 1 ? M - 1 : 1));
    }
}

/* A model's whitened coordinates in the fitted space, and its reconstruction
 * from them. */
static void Project(const Space& sp, const std::vector<double>& v, std::vector<double>& coord, Rep& rec, double* ferr)
{
    const int D = sp.chart.D();
    std::vector<double> x(D);
    for(int i = 0; i < D; i++) x[i] = (v[i] - sp.mean[i]) / sp.block_scale[sp.chart.BlockOf(i)];
    coord.assign(sp.K, 0.0);
    std::vector<double> back = sp.mean;
    for(int k = 0; k < sp.K; k++)
    {
        double s = 0;
        for(int i = 0; i < D; i++) s += x[i] * sp.comp[k][i];
        coord[k] = sp.sdev[k] > 0 ? s / sp.sdev[k] : 0.0;
        for(int i = 0; i < D; i++) back[i] += s * sp.comp[k][i] * sp.block_scale[sp.chart.BlockOf(i)];
    }
    sp.chart.FromVector(back, rec, ferr);
}

struct Err { double cents = 0, gain = 0, minhz = 0, frame = 0; int neg = 0; };

static Err Compare(const Rep& want, const Rep& got, int nreal, int P, double ferr)
{
    Err e;
    e.frame = ferr;
    e.minhz = 1e30;
    double c2 = 0, gn = 0, gd = 0;
    for(int i = 0; i < nreal; i++)
    {
        if(got.hz[i] <= 0) { e.neg++; }
        else { const double d = 1200.0 * std::log2(got.hz[i] / want.hz[i]); c2 += d * d; }
        e.minhz = std::min(e.minhz, got.hz[i]);
        for(int p = 0; p < P; p++)
        {
            const double d = got.G(i, p) - want.G(i, p);
            gn += d * d; gd += want.G(i, p) * want.G(i, p);
        }
    }
    e.cents = std::sqrt(c2 / (nreal - e.neg > 0 ? nreal - e.neg : 1));
    e.gain  = gd > 0 ? std::sqrt(gn / gd) : 0.0;
    return e;
}

int main(int argc, char** argv)
{
    if(argc < 5) { fprintf(stderr, "bake corpus.mdb align.bin space.msp report.txt [--variant full|lambda|linear] [--K 4] [--extent 2.2] [--holdout 3,8]\n"); return 2; }
    Variant variant = Variant::Full;
    int     K = 4;
    double  extent = 2.2;
    std::vector<int> hold = {3, 8};
    std::vector<int>         families;       /* empty: every family in the corpus */
    std::vector<std::string> wanted_names;
    bool             decay = false;
    for(int i = 5; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if(a == "--variant")
        {
            const std::string v = next();
            variant = v == "lambda" ? Variant::Lambda : v == "linear" ? Variant::Linear : v == "gonly" ? Variant::GOnly
                    : v == "diagonal" ? Variant::Diagonal : Variant::Full;
        }
        else if(a == "--K") K = atoi(next());
        else if(a == "--block-weight") gBlockTotal = std::string(next()) != "entry";
        else if(a == "--decay") decay = true;
        else if(a == "--family")
        {
            /* one name or a comma-separated list */
            const std::string f = next();
            size_t at = 0;
            while(at <= f.size())
            {
                const size_t c = f.find(',', at);
                const std::string one = f.substr(at, c == std::string::npos ? std::string::npos : c - at);
                wanted_names.push_back(one);
                if(c == std::string::npos) break;
                at = c + 1;
            }
        }
        else if(a == "--extent") extent = atof(next());
        else if(a == "--holdout")
        {
            hold.clear();
            const std::string h = next();
            size_t at = 0;
            while(at < h.size()) { hold.push_back(atoi(h.c_str() + at)); const size_t c = h.find(',', at); if(c == std::string::npos) break; at = c + 1; }
        }
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Corpus c;
    if(!ReadCorpus(argv[1], c)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    Aligned al;
    if(!LoadAligned(argv[2], c, al)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    const int M = (int)c.m.size();
    for(const std::string& nm : wanted_names)
    {
        const int q = c.FamilyIndex(nm);
        if(q < 0) { fprintf(stderr, "no family %s in the corpus\n", nm.c_str()); return 1; }
        families.push_back(q);
    }
    std::vector<int> keep;
    auto wanted = [&](int fam) { return families.empty() || std::find(families.begin(), families.end(), fam) != families.end(); };
    for(int k = 0; k < M; k++) if(wanted(c.m[k].family)) keep.push_back(k);
    if(keep.empty()) { fprintf(stderr, "no models in those families\n"); return 1; }
    if(!wanted(c.m[al.ref].family))
    {
        /* the reference has to be one of the models kept; take the middle of
           the first family asked for */
        std::vector<int> ids;
        for(int k : keep) if(c.m[k].family == families[0]) ids.push_back(k);
        std::sort(ids.begin(), ids.end(), [&](int x, int y) { return c.m[x].param < c.m[y].param; });
        al.ref = ids[ids.size() / 2];
    }
    const int family = families.size() == 1 ? families[0] : -1;

    Chart chart;
    chart.variant = variant;
    chart.decay   = decay;
    chart.SetReference(al.rep[al.ref]);
    if(!chart.frame_ok) { fprintf(stderr, "reference %s has a rank-deficient G; no frame to map at\n", c.m[al.ref].id.c_str()); return 1; }
    std::vector<std::vector<double>> vecs(M);
    for(int k = 0; k < M; k++) chart.ToVector(al.rep[k], vecs[k]);

    /* Round-trip check of the maps themselves, before any PCA: a model through
       log and exp must come back as itself. If it does not, nothing below
       means anything. */
    double worst_rt = 0;
    for(int k : keep)
    {
        Rep back;
        chart.FromVector(vecs[k], back);
        const Err e = Compare(al.rep[k], back, c.m[k].nreal, c.P, 0.0);
        worst_rt = std::max(worst_rt, std::max(e.cents, e.gain));
    }

    FILE* rep = fopen(argv[4], "w");
    if(!rep) { fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }
    fprintf(rep, "bake — variant %s%s, K=%d, extent %.2f, reference %s, D=%d over %zu models%s, blocks weighted by %s\n\n",
            VariantName(variant), decay ? " with decay" : "", K, extent, c.m[al.ref].id.c_str(), chart.D(), keep.size(),
            family >= 0 ? (std::string(" (") + c.FamilyName(family) + " only)").c_str() : "",
            gBlockTotal ? "total variance" : "entry");
    fprintf(rep, "map round trip (log then exp, no PCA): worst %.2e over cents and relative gain\n\n", worst_rt);

    /* holdouts: the given sweep positions of every family */
    std::vector<int> train, test;
    {
        std::vector<std::vector<int>> fam(c.Families());
        for(int k : keep) fam[c.m[k].family].push_back(k);
        for(auto& ids : fam)
        {
            std::sort(ids.begin(), ids.end(), [&](int x, int y) { return c.m[x].param < c.m[y].param; });
            for(size_t i = 0; i < ids.size(); i++)
                if(std::find(hold.begin(), hold.end(), (int)i) != hold.end()) test.push_back(ids[i]);
                else train.push_back(ids[i]);
        }
    }
    {
        Space sp;
        std::vector<double> expl;
        Fit(chart, vecs, train, K, sp, expl);
        fprintf(rep, "held out %zu models, fitted on %zu. variance explained by the %d components:", test.size(), train.size(), K);
        double cum = 0;
        for(int k = 0; k < K; k++) { cum += expl[k]; fprintf(rep, " %.1f%%", 100 * expl[k]); }
        fprintf(rep, "  (cumulative %.1f%%)\n\n", 100 * cum);
        fprintf(rep, "reconstruction from each model's own coordinates:\n");
        fprintf(rep, "  %-8s %-6s %7s   %9s %9s %9s %9s %5s   %s\n", "model", "family", "param", "cents", "gain err", "min hz", "frame", "neg", "coords (whitened)");
        auto row = [&](int k, const char* tag) {
            std::vector<double> coord;
            Rep    got;
            double ferr = 0;
            Project(sp, vecs[k], coord, got, &ferr);
            const Err e = Compare(al.rep[k], got, c.m[k].nreal, c.P, ferr);
            fprintf(rep, "  %-8s %-6s %7.3f   %9.1f %9.3f %9.1f %9.2e %5d  ", c.m[k].id.c_str(), c.FamilyName(c.m[k].family),
                    c.m[k].param, e.cents, e.gain, e.minhz, e.frame, e.neg);
            for(int q = 0; q < K; q++) fprintf(rep, " %6.2f", coord[q]);
            fprintf(rep, "  %s\n", tag);
            return e;
        };
        Err worst_t, worst_h;
        for(int k : test)
        {
            const Err e = row(k, "held out");
            worst_h.cents = std::max(worst_h.cents, e.cents); worst_h.gain = std::max(worst_h.gain, e.gain);
            worst_h.frame = std::max(worst_h.frame, e.frame); worst_h.neg += e.neg;
        }
        for(int k : train)
        {
            const Err e = row(k, "");
            worst_t.cents = std::max(worst_t.cents, e.cents); worst_t.gain = std::max(worst_t.gain, e.gain);
            worst_t.frame = std::max(worst_t.frame, e.frame); worst_t.neg += e.neg;
        }
        fprintf(rep, "\n  held out, worst: %.1f cents, gain error %.3f, frame %.2e, %d non-positive frequencies\n",
                worst_h.cents, worst_h.gain, worst_h.frame, worst_h.neg);
        fprintf(rep, "  in sample, worst: %.1f cents, gain error %.3f, frame %.2e, %d non-positive frequencies\n\n",
                worst_t.cents, worst_t.gain, worst_t.frame, worst_t.neg);
        printf("bake %s: held out worst %.1f cents / gain %.3f / %d negative; explained %.1f%%\n",
               VariantName(variant), worst_h.cents, worst_h.gain, worst_h.neg, 100 * cum);
    }

    /* the space that gets graded: everything in */
    {
        Space sp;
        std::vector<double> expl;
        std::vector<int> all = keep;
        Fit(chart, vecs, all, K, sp, expl);
        sp.extent = extent;
        fprintf(rep, "fitted on all %zu. variance explained:", all.size());
        double cum = 0;
        for(int k = 0; k < K; k++) { cum += expl[k]; fprintf(rep, " %.1f%%", 100 * expl[k]); }
        fprintf(rep, "  (cumulative %.1f%%)\n", 100 * cum);
        fprintf(rep, "block scales:");
        for(double b : sp.block_scale) fprintf(rep, " %.4g", b);
        fprintf(rep, "\ncomponent sdevs:");
        for(double s : sp.sdev) fprintf(rep, " %.4g", s);
        fprintf(rep, "\n\nwhere every model sits in the cube (whitened coordinate / extent, so inside is |x| < 1):\n");
        int outside = 0;
        for(int k : keep)
        {
            std::vector<double> coord;
            Rep got;
            Project(sp, vecs[k], coord, got, nullptr);
            fprintf(rep, "  %-8s", c.m[k].id.c_str());
            bool in = true;
            for(int q = 0; q < K; q++) { fprintf(rep, " %6.2f", coord[q] / extent); if(std::fabs(coord[q]) > extent) in = false; }
            fprintf(rep, "%s\n", in ? "" : "   outside");
            if(!in) outside++;
        }
        fprintf(rep, "  %d of %zu outside the cube at extent %.2f\n", outside, keep.size(), extent);
        if(!sp.Write(argv[3])) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    }
    fclose(rep);
    return 0;
}
