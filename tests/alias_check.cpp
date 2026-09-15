/* alias_check — spec §7: sweep f0 over five octaves on the brightest cell and
 * measure everything that is not a harmonic of f0. Two sources contribute:
 * content above Nyquist (should be none: the spectrum is truncated before
 * the IFFT) and the frame read interpolator's images (real, pitch- and
 * frame-size-dependent — this is what the number reports).
 *
 * Method: render 2 s per pitch, take the last 65536 samples, 4-term
 * Blackman-Harris window (−92 dB sidelobes), FFT, and report the loudest bin
 * outside ±6 bins of every harmonic below Nyquist, in dBFS (0 dBFS = a
 * full-scale sine's peak bin). Fails above the --limit (default −80 dBFS).
 *
 *   alias_check [--limit dB] [--octaves 5] [--f0 55] [--steps-per-octave 3]
 *               [--render-div 1] [--rolloff 0] [--csv out.csv]
 *               [--world N] [--pos a b c d] [--scan]
 *
 * `--world N --pos a b c d` sweeps any registered world at any cell, which is
 * where the published per-world figures come from. `--scan` does every world at
 * every corner and midpoint and reports the worst anywhere, which is the figure
 * to quote: the default single cell is a regression guard and reads as a
 * stronger claim than it is.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <complex>
#include <string>
#include "kyk_engine.h"
#include "kyk_world.h"
#include "kyk_gen.h"
#include "kyk_worlds.h"

using namespace kyk;

static World gWorld;   /* the tests all drive a lattice world */
typedef std::complex<double> cd;

static void Fft(std::vector<cd>& a)
{
    const size_t n = a.size();
    for(size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;
        for(; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if(i < j) std::swap(a[i], a[j]);
    }
    for(size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double)len;
        const cd     wl(std::cos(ang), std::sin(ang));
        for(size_t i = 0; i < n; i += len)
        {
            cd w(1);
            for(size_t j = 0; j < len / 2; j++)
            {
                const cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j]           = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

int main(int argc, char** argv)
{
    double limit = -80.0, f0lo = 55.0;
    int    octaves = 5, spo = 3, render_div = 1, rolloff = 0, world_sel = -1;
    float  pos_arg[4] = {0.f, 0.f, 1.f, 0.f};
    bool   have_pos = false, scan = false, have_limit = false;
    std::string csv;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if(a == "--limit") { limit = atof(next()); have_limit = true; }
        else if(a == "--octaves") octaves = atoi(next());
        else if(a == "--f0") f0lo = atof(next());
        else if(a == "--steps-per-octave") spo = atoi(next());
        else if(a == "--render-div") render_div = atoi(next());
        else if(a == "--rolloff") rolloff = atoi(next());
        else if(a == "--csv") csv = next();
        /* Any registered world, not only the default lattice. The published
           -88 dBFS was measured on one world at one position, which is a
           narrower claim than it reads as — and this is how the others get
           their own number instead of borrowing it. */
        else if(a == "--world") world_sel = atoi(next());
        else if(a == "--pos")
        {
            for(int q = 0; q < 4 && i + 1 < argc; q++) pos_arg[q] = (float)atof(next());
            have_pos = true;
        }
        else if(a == "--scan") scan = true;
    }
    /* A scan is asking a different question — the worst case anywhere rather
       than a regression on one cell — so it gets its own default limit. The
       spectral path's honest worst is the field worlds at about -65. */
    if(scan && !have_limit) limit = -60.0;
    const float sr = 48000.f;
    const int   block = 24;
    GenParams   gp;
    if(gp.k > kMaxK) gp.k = kMaxK;   /* small-frame builds cap K */
    static Engine eng;
    eng.gain         = 1.f;
    eng.render_div   = render_div;
    eng.rolloff_bins = rolloff;
    /* brightest cell: saw, no tilt, formant at the top, full harmonics */
    float pos[4] = {0.f, 0.f, 1.f, 0.f};
    if(have_pos) for(int q = 0; q < 4; q++) pos[q] = pos_arg[q];

    const size_t N = 65536;
    std::vector<double> win(N);
    double              wsum = 0;
    for(size_t i = 0; i < N; i++)
    {
        const double t = 2.0 * M_PI * (double)i / (double)(N - 1);
        win[i] = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        wsum += win[i];
    }
    const double fullscale = wsum / 2.0;   /* peak bin of a unit sine */
    const double binHz     = sr / (double)N;

    /* Which world is under test. The default is this test's own bright lattice
       cell, which is what the -80 dBFS limit was set against; a world index
       selects a built-in instead, which is how a world that aliases on purpose
       gets a figure to publish rather than a promise of one. */
    std::vector<uint8_t> blob;
    Space                space;
    solids::VertexTable  vtbl;
    const char*          wname = "test lattice";
    bool                 sel_lattice = true;
    int                  sel = -1;
    auto select = [&](int wi) -> bool {
        sel = wi;
        if(wi < 0)
        {
            wname = "test lattice"; sel_lattice = true;
            blob.resize(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
            BuildLattice(gp, blob.data(), blob.size());
            return space.Attach(blob.data(), blob.size()) == SpaceError::Ok;
        }
        if(wi >= worlds::kCount) return false;
        const worlds::Entry& e = worlds::Get((uint8_t)wi);
        wname       = e.name;
        sel_lattice = e.kind == World::Kind::Lattice;
        if(!sel_lattice) return true;
        blob.resize(1u << 22);
        /* side 8, as the rest of the suite expands the built-ins */
        const size_t n = worlds::Expand((uint8_t)wi, gp.n, 8, gp.k, gp.p, blob.data(), blob.size());
        return n && space.Attach(blob.data(), n) == SpaceError::Ok;
    };
    /* Re-pointed for every pitch, because each one re-Inits the engine and a
       vertex world rebuilds its table. */
    auto point = [&]() {
        if(sel >= 0 && !sel_lattice) worlds::Point((uint8_t)sel, gWorld, gp.p, nullptr, &vtbl);
        else gWorld.UseLattice(&space);
    };

    std::vector<float> out((size_t)(2.0 * sr) + block);
    std::vector<cd>    x(N);
    std::vector<char>  harm(N / 2, 0);
    /* One pitch sweep at one position, returning the worst non-harmonic bin in
       dBFS. `verbose` prints the per-pitch table, which a scan does not want
       fifteen hundred of. */
    auto sweep = [&](const float* p01, bool verbose, FILE* fc) -> double {
        double worst_all = -999;
        for(int step = 0; step <= octaves * spo; step++)
        {
            const double f0 = f0lo * std::pow(2.0, (double)step / spo);
            point();
            eng.Init(&gWorld, sr);
            eng.gain = 1.f; eng.render_div = render_div; eng.rolloff_bins = rolloff;
            eng.SetF0((float)f0);
            eng.SetPosition(p01, 4);
            for(size_t q = 0; q + block <= out.size(); q += block) eng.Process(&out[q], block);
            const size_t off = out.size() - N - 8;
            for(size_t i = 0; i < N; i++) x[i] = cd(out[off + i] * win[i], 0);
            Fft(x);
            /* mask harmonics */
            for(size_t i = 0; i < N / 2; i++) harm[i] = 0;
            for(int k = 1; k * f0 < sr / 2; k++)
            {
                const long c = std::lround(k * f0 / binHz);
                for(long d = -6; d <= 6; d++) if(c + d >= 0 && c + d < (long)(N / 2)) harm[(size_t)(c + d)] = 1;
            }
            double worst = 0, worst_hz = 0, ea = 0, eh = 0;
            for(size_t i = 1; i < N / 2; i++)
            {
                const double m = std::abs(x[i]);
                if(harm[i]) eh += m * m;
                else
                {
                    ea += m * m;
                    if(m > worst) { worst = m; worst_hz = (double)i * binHz; }
                }
            }
            const double worst_db = 20 * std::log10(worst / fullscale + 1e-30);
            const double ratio_db = 10 * std::log10(ea / (eh + 1e-30) + 1e-30);
            if(verbose)
                printf("%9.2f %5d %12.1f %10.0f %14.1f%s\n", f0, eng.Kcut(), worst_db, worst_hz, ratio_db,
                       worst_db > limit ? "  <-- over" : "");
            if(fc) fprintf(fc, "%.3f,%d,%.2f,%.1f,%.2f\n", f0, eng.Kcut(), worst_db, worst_hz, ratio_db);
            worst_all = std::fmax(worst_all, worst_db);
        }
        return worst_all;
    };

    printf("frame %d, read %s, render every %d block(s), rolloff %d bins, limit %.0f dBFS\n", kFrame,
#ifdef KYK_OSC_LINEAR
           "linear",
#else
           "cubic",
#endif
           render_div, rolloff, limit);

    /* --scan: every world at every corner and midpoint, which is the only way
       the headline figure means what it sounds like it means. A cell where a
       frame shaper is engaged is skipped rather than counted: those are
       measured on purpose and published separately, and folding them in here
       would put a -9 dB wavefolder in a number about spectral truncation. */
    if(scan)
    {
        const float lv[3] = {0.f, 0.5f, 1.f};
        double      worst_all = -999;
        const char* worst_name = "";
        float       worst_pos[4] = {0.f, 0.f, 0.f, 0.f};
        printf("scanning %d worlds at 81 positions each, {0, 0.5, 1}^4\n", (int)worlds::kCount);
        printf("  %-10s %9s  %-22s %s\n", "world", "worst", "at", "cells");
        for(int wi = 0; wi < (int)worlds::kCount; wi++)
        {
            if(!select(wi)) { printf("  %-10s  cannot build\n", worlds::Get((uint8_t)wi).name); continue; }
            point();
            const bool shaper = gWorld.HasShaper();
            double     w = -999;
            float      wp[4] = {0.f, 0.f, 0.f, 0.f};
            int        cells = 0;
            for(int a = 0; a < 3; a++)
                for(int b = 0; b < 3; b++)
                    for(int c = 0; c < 3; c++)
                        for(int d = 0; d < 3; d++)
                        {
                            const float p[4] = {lv[a], lv[b], lv[c], lv[d]};
                            if(shaper && (p[2] > 0.f || p[3] > 0.f)) continue;
                            cells++;
                            const double v = sweep(p, false, nullptr);
                            if(v > w) { w = v; for(int q = 0; q < 4; q++) wp[q] = p[q]; }
                        }
            printf("  %-10s %7.1f dBFS  (%.1f %.1f %.1f %.1f)%-8s %d%s\n", wname, w, wp[0], wp[1], wp[2], wp[3], "",
                   cells, shaper ? "  (shaper cells skipped)" : "");
            fflush(stdout);
            if(w > worst_all) { worst_all = w; worst_name = wname; for(int q = 0; q < 4; q++) worst_pos[q] = wp[q]; }
        }
        printf("worst non-harmonic bin anywhere: %.1f dBFS, %s at (%.1f %.1f %.1f %.1f) (%s %.0f)\n", worst_all,
               worst_name, worst_pos[0], worst_pos[1], worst_pos[2], worst_pos[3],
               worst_all > limit ? "FAIL, limit" : "ok, limit", limit);
        return worst_all > limit ? 1 : 0;
    }

    if(!select(world_sel)) { fprintf(stderr, "cannot build world %d\n", world_sel); return 1; }
    FILE* fc = csv.empty() ? nullptr : fopen(csv.c_str(), "w");
    if(fc) fprintf(fc, "f0,kcut,worst_dbfs,worst_hz,alias_energy_db\n");
    printf("world %s at (%.3f %.3f %.3f %.3f)\n", wname, pos[0], pos[1], pos[2], pos[3]);
    printf("%9s %5s %12s %10s %14s\n", "f0 Hz", "kcut", "worst dBFS", "at Hz", "alias/harm dB");
    const double worst_all = sweep(pos, true, fc);
    if(fc) fclose(fc);
    printf("worst non-harmonic bin over the sweep: %.1f dBFS (%s %.0f)\n", worst_all, worst_all > limit ? "FAIL, limit" : "ok, limit", limit);
    return worst_all > limit ? 1 : 0;
}
