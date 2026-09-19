/* grade — Stage 4: the numbers, the same way Kyklophoria measures its worlds.
 *
 *   grade out/space-full.msp out/corpus.mdb out/align.bin out/grade-full.txt
 *         [--dirs 200] [--bands-per-octave 6] [--kernel 0.0833] [--strike 0]
 *         [--listen 0] [--no-omega] [--damping-rate 5] [--elapsed 0.2]
 *
 * From tools/kykworlds: variety per unit of travel along a direction through
 * the centre of the cube, over 200 random directions; Spread is the 95th
 * percentile over the 5th. From tests/cont_check: every axis swept at two step
 * sizes, and the ratio of the largest step at step/500 to the largest at
 * step/2000, which is about 4 for anything smooth and about 1 for a cliff.
 *
 * The continuity sweep runs twice, on the rendered band spectrum and on the
 * vector of log frequencies. The brief calls the second a mode-veering
 * detector. It is one only where the interpolant can have a kink, and a PCA
 * space evaluated through smooth maps has none — every axis measures about 4
 * on both, and that is reported because it is true rather than because it is
 * interesting. What the second sweep *does* show is where the frequencies
 * move fastest per unit of travel, so the local step size along each axis is
 * written out as a profile, and the veering that actually exists — in the
 * corpus, where two modes of a sweep trade places between one sample and the
 * next — is measured directly on the aligned frequency trajectories, as the
 * second difference of each slot's log frequency along each family's sweep,
 * in cents. That is the veering map.
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
#include "../common/spectrum.h"

using namespace mb;

static double Dist(const std::vector<double>& a, const std::vector<double>& b)
{
    double d = 0;
    for(size_t i = 0; i < a.size(); i++) { const double x = a[i] - b[i]; d += x * x; }
    return std::sqrt(d);
}

/* kykeigen's Rng, so the directions are the same ones kykworlds draws. */
struct Rng
{
    uint32_t s = 2026;
    void     Seed(uint32_t v) { s = v ? v : 1; }
    uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double   Uniform() { return (Next() >> 8) * (1.0 / 16777216.0); }
};

int main(int argc, char** argv)
{
    if(argc < 5) { fprintf(stderr, "grade space.msp corpus.mdb align.bin report.txt [options]\n"); return 2; }
    Spectrograph sg;
    int dirs = 200;
    for(int i = 5; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if(a == "--dirs") dirs = atoi(next());
        else if(a == "--bands-per-octave") sg.bands_per_octave = atoi(next());
        else if(a == "--kernel") sg.kernel = atof(next());
        else if(a == "--strike") sg.strike = atoi(next());
        else if(a == "--listen") sg.listen = atoi(next());
        else if(a == "--no-omega") sg.omega = false;
        else if(a == "--damping-rate") sg.damping_rate = atof(next());
        else if(a == "--elapsed") sg.elapsed = atof(next());
        else if(a == "--normalise-pitch") sg.normalise_pitch = true;
        else if(a == "--ring") sg.ring = true;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Space sp;
    if(!sp.Read(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    Corpus c;
    if(!ReadCorpus(argv[2], c)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    const int K = sp.K;

    FILE* rep = fopen(argv[4], "w");
    if(!rep) { fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }
    fprintf(rep, "grade — %s space, K=%d, extent %.2f; %d bands/octave over %.0f..%.0f Hz, kernel %.4f oct, "
                 "strike %d listen %d, %s, damping %.1f/s at %.3f s\n\n",
            VariantName(sp.chart.variant), K, sp.extent, sg.bands_per_octave, sg.f_lo, sg.f_hi, sg.kernel,
            sg.strike, sg.listen, sg.omega ? "1/omega" : "no 1/omega", sg.damping_rate, sg.elapsed);
    if(sg.normalise_pitch) fprintf(rep, "pitch normalised: the lowest audible mode of every point is moved to %.0f Hz before banding\n\n", sg.pitch_ref);
    if(sg.ring) fprintf(rep, "ring: modes weighed by their energy over the whole decay (%s)\n\n", sp.chart.decay ? "the space's own zeta" : "Rayleigh, the space carries no decay");

    long skipped_total = 0, evals = 0;
    auto at = [&](const double* p01, std::vector<double>& out) {
        Rep r;
        sp.At(p01, r);
        int sk = 0;
        sg.Bands(r, out, &sk);
        skipped_total += sk; evals++;
    };

    /* ── variety and spread, from kykworlds ────────────────────────────── */
    auto per_unit = [&](const double* d) {
        const int    M = 200;
        const double span = 0.9;
        double       len = 0;
        std::vector<double> a, b;
        for(int i = 0; i <= M; i++)
        {
            const double t = ((double)i / M - 0.5) * span;
            double       p[8];
            for(int ax = 0; ax < K; ax++) p[ax] = std::fmin(1.0, std::fmax(0.0, 0.5 + t * d[ax]));
            at(p, b);
            if(i) len += Dist(a, b);
            a = b;
        }
        return len / span;
    };
    Rng r;
    r.Seed(2026);
    std::vector<double> v;
    for(int t = 0; t < dirs; t++)
    {
        double d[8], n2 = 0;
        for(int ax = 0; ax < K; ax++)
        {
            const double u1 = r.Uniform() + 1e-9, u2 = r.Uniform();
            d[ax] = std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2);
            n2 += d[ax] * d[ax];
        }
        n2 = std::sqrt(n2 > 0 ? n2 : 1);
        for(int ax = 0; ax < K; ax++) d[ax] /= n2;
        v.push_back(per_unit(d));
    }
    std::sort(v.begin(), v.end());
    double mean = 0;
    for(double x : v) mean += x;
    mean /= (double)v.size();
    const int    lo = (int)(0.05 * v.size()), hi = (int)(0.95 * v.size());
    const double spread = v[hi] / (v[lo] > 0 ? v[lo] : 1e-9);

    /* twins, as kykworlds */
    const int S = 600;
    std::vector<std::vector<double>> pts;
    for(int i = 0; i < S; i++)
    {
        double p[8];
        for(int ax = 0; ax < K; ax++) p[ax] = r.Uniform();
        std::vector<double> s;
        at(p, s);
        pts.push_back(s);
    }
    long close = 0, total = 0;
    for(int i = 0; i < S; i++)
        for(int j = i + 1; j < S; j++) { total++; if(Dist(pts[i], pts[j]) < 0.05) close++; }
    const double twins = 100.0 * close / total;

    /* axis-aligned variety, for the spread's denominator to be legible */
    fprintf(rep, "variety per unit of travel: mean %.3f, 5th pct %.3f, 95th pct %.3f\n", mean, v[lo], v[hi]);
    fprintf(rep, "spread: %.2fx   (FM 1.83x, Saw 2.03x, Shapes 8.47x in Kyklophoria's table; 1.0 is isotropic)\n", spread);
    fprintf(rep, "twins: %.1f%% of random pairs within 0.05\n", twins);
    fprintf(rep, "axes:");
    for(int ax = 0; ax < K; ax++)
    {
        double d[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        d[ax] = 1.0;
        fprintf(rep, "  axis %d %.3f", ax, per_unit(d));
    }
    fprintf(rep, "\n");
    fprintf(rep, "modes skipped as unrenderable (non-positive or above the top band): %.2f per evaluation over %ld evaluations\n\n",
            (double)skipped_total / (evals ? evals : 1), evals);
    printf("grade %s%s%s: spread %.2fx  variety %.3f  twins %.1f%%\n", VariantName(sp.chart.variant),
           sg.normalise_pitch ? " (pitch normalised)" : "", sg.ring ? " (ring)" : "", spread, mean, twins);

    /* ── continuity, from cont_check, on spectra and on log frequencies ── */
    auto worst_step = [&](int ax, int steps, bool on_freq, std::vector<double>* profile) {
        double              worst = 0;
        std::vector<double> prev, cur;
        if(profile) profile->assign(steps, 0.0);
        for(int i = 0; i <= steps; i++)
        {
            double p[8] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
            p[ax] = 0.02 + 0.96 * (double)i / steps;
            if(on_freq)
            {
                Rep rr;
                sp.At(p, rr);
                cur.resize(rr.hz.size());
                for(size_t k = 0; k < rr.hz.size(); k++) cur[k] = rr.hz[k] > 0 ? std::log(rr.hz[k]) : 0.0;
            }
            else at(p, cur);
            if(i)
            {
                const double d = Dist(prev, cur);
                if(profile) (*profile)[i - 1] = d;
                worst = std::fmax(worst, d);
            }
            prev = cur;
        }
        return worst;
    };
    fprintf(rep, "continuity (cont_check): largest step at step/500 and step/2000, ratio ~4 smooth, ~1 cliff\n");
    fprintf(rep, "  %-4s %11s %11s %7s   %11s %11s %7s\n", "axis", "spec/500", "spec/2000", "ratio", "logf/500", "logf/2000", "ratio");
    std::vector<std::vector<double>> prof_spec(K), prof_freq(K);
    for(int ax = 0; ax < K; ax++)
    {
        const double a5 = worst_step(ax, 500, false, &prof_spec[ax]), a20 = worst_step(ax, 2000, false, nullptr);
        const double f5 = worst_step(ax, 500, true, &prof_freq[ax]), f20 = worst_step(ax, 2000, true, nullptr);
        fprintf(rep, "  %-4d %11.5f %11.5f %7.2f   %11.5f %11.5f %7.2f%s\n", ax, a5, a20, a20 > 0 ? a5 / a20 : 0.0,
                f5, f20, f20 > 0 ? f5 / f20 : 0.0, (a20 > 0 && a5 / a20 < 2.0) || (f20 > 0 && f5 / f20 < 2.0) ? "   <-- cliff" : "");
    }

    /* the speed profile: where along each axis the frequencies move fastest */
    fprintf(rep, "\nwhere the frequencies move fastest along each axis (log-frequency step per 1/500 of travel, "
                 "in tenths of the axis):\n");
    for(int ax = 0; ax < K; ax++)
    {
        fprintf(rep, "  axis %d:", ax);
        double mx = 0;
        for(double x : prof_freq[ax]) mx = std::fmax(mx, x);
        for(int t = 0; t < 10; t++)
        {
            double s = 0;
            for(int i = t * 50; i < (t + 1) * 50; i++) s += prof_freq[ax][i];
            fprintf(rep, " %6.4f", s / 50);
        }
        fprintf(rep, "   peak %.4f\n", mx);
    }

    /* ── the veering map: in the corpus, along each sweep ──────────────── */
    /* aligned trajectories come from align.bin; a slot whose log frequency
       bends sharply between three consecutive samples of a sweep is a mode
       that veered, or an alignment that switched — the same thing seen from
       inside and from outside. */
    {
        FILE* f = fopen(argv[3], "rb");
        std::vector<std::vector<int32_t>> perm(c.m.size());
        bool ok = f != nullptr;
        if(ok)
        {
            char magic[4]; uint32_t hdr[3];
            ok = fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "MALN", 4) == 0 && fread(hdr, 4, 3, f) == 3;
            for(size_t k = 0; ok && k < c.m.size(); k++)
            {
                perm[k].resize(c.N);
                std::vector<int32_t> sg(c.N);
                ok = fread(perm[k].data(), 4, c.N, f) == (size_t)c.N && fread(sg.data(), 4, c.N, f) == (size_t)c.N;
            }
            fclose(f);
        }
        if(ok)
        {
            fprintf(rep, "\nveering map — second difference of each aligned slot's log frequency along each sweep, in cents.\n"
                         "a smooth trajectory is under 100; a slot that trades places with its neighbour is hundreds.\n"
                         "listed: every (family, parameter, slot) over 300 cents, and each family's worst.\n");
            for(int fam = 0; fam < Corpus::kFamilies; fam++)
            {
                std::vector<int> ids;
                for(size_t k = 0; k < c.m.size(); k++) if(c.m[k].family == fam) ids.push_back((int)k);
                if(ids.size() < 3) continue;
                std::sort(ids.begin(), ids.end(), [&](int x, int y) { return c.m[x].param < c.m[y].param; });
                double worst = 0; int worst_slot = -1; double worst_param = 0;
                int    over = 0;
                fprintf(rep, "  %s:", Corpus::FamilyName(fam));
                for(size_t i = 1; i + 1 < ids.size(); i++)
                {
                    const int a = ids[i - 1], b = ids[i], d = ids[i + 1];
                    const int nreal = std::min(c.m[a].nreal, std::min(c.m[b].nreal, c.m[d].nreal));
                    for(int slot = 0; slot < c.N; slot++)
                    {
                        const int ia = perm[a][slot], ib = perm[b][slot], id = perm[d][slot];
                        if(ia >= c.m[a].nreal || ib >= c.m[b].nreal || id >= c.m[d].nreal) continue;
                        const double la = std::log2(c.m[a].hz[ia]), lb = std::log2(c.m[b].hz[ib]), ld = std::log2(c.m[d].hz[id]);
                        const double sd = 1200.0 * std::fabs(la - 2 * lb + ld);
                        if(sd > 300) { over++; fprintf(rep, " (%.3f, slot %d: %.0f)", c.m[b].param, slot, sd); }
                        if(sd > worst) { worst = sd; worst_slot = slot; worst_param = c.m[b].param; }
                    }
                    (void)nreal;
                }
                fprintf(rep, "\n    %d over 300 cents; worst %.0f cents at parameter %.3f, slot %d\n", over, worst, worst_param, worst_slot);
            }
        }
        else fprintf(rep, "\n(no alignment file; veering map skipped)\n");
    }

    /* the full profiles, for anyone who wants to plot them */
    fprintf(rep, "\nprofiles: per axis, 500 steps, spectrum step then log-frequency step\n");
    for(int ax = 0; ax < K; ax++)
    {
        fprintf(rep, "axis %d spec:", ax);
        for(double x : prof_spec[ax]) fprintf(rep, " %.5f", x);
        fprintf(rep, "\naxis %d logf:", ax);
        for(double x : prof_freq[ax]) fprintf(rep, " %.5f", x);
        fprintf(rep, "\n");
    }
    fclose(rep);
    return 0;
}
