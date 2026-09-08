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
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <complex>
#include <string>
#include "kyk_engine.h"
#include "kyk_gen.h"

using namespace kyk;
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
    int    octaves = 5, spo = 3, render_div = 1, rolloff = 0;
    std::string csv;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if(a == "--limit") limit = atof(next());
        else if(a == "--octaves") octaves = atoi(next());
        else if(a == "--f0") f0lo = atof(next());
        else if(a == "--steps-per-octave") spo = atoi(next());
        else if(a == "--render-div") render_div = atoi(next());
        else if(a == "--rolloff") rolloff = atoi(next());
        else if(a == "--csv") csv = next();
    }
    const float sr = 48000.f;
    const int   block = 24;
    GenParams   gp;
    if(gp.k > kMaxK) gp.k = kMaxK;   /* small-frame builds cap K */
    std::vector<uint8_t> blob(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
    BuildLattice(gp, blob.data(), blob.size());
    Space s;
    if(s.Attach(blob.data(), blob.size()) != SpaceError::Ok) { fprintf(stderr, "space\n"); return 1; }
    static Engine eng;
    eng.gain         = 1.f;
    eng.render_div   = render_div;
    eng.rolloff_bins = rolloff;
    /* brightest cell: saw, no tilt, formant at the top, full harmonics */
    const float pos[4] = {0.f, 0.f, 1.f, 0.f};

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

    FILE* fc = csv.empty() ? nullptr : fopen(csv.c_str(), "w");
    if(fc) fprintf(fc, "f0,kcut,worst_dbfs,worst_hz,alias_energy_db\n");
    printf("frame %d, read %s, render every %d block(s), rolloff %d bins, limit %.0f dBFS\n", kFrame,
#ifdef KYK_OSC_LINEAR
           "linear",
#else
           "cubic",
#endif
           render_div, rolloff, limit);
    printf("%9s %5s %12s %10s %14s\n", "f0 Hz", "kcut", "worst dBFS", "at Hz", "alias/harm dB");
    double worst_all = -999;
    std::vector<float> out((size_t)(2.0 * sr) + block);
    for(int step = 0; step <= octaves * spo; step++)
    {
        const double f0 = f0lo * std::pow(2.0, (double)step / spo);
        eng.Init(&s, sr);
        eng.gain = 1.f; eng.render_div = render_div; eng.rolloff_bins = rolloff;
        eng.SetF0((float)f0);
        eng.SetPosition(pos, 4);
        for(size_t p = 0; p + block <= out.size(); p += block) eng.Process(&out[p], block);
        std::vector<cd> x(N);
        const size_t    off = out.size() - N - 8;
        double          peak = 0;
        for(size_t i = 0; i < N; i++) { x[i] = cd(out[off + i] * win[i], 0); peak = std::fmax(peak, std::fabs(out[off + i])); }
        Fft(x);
        /* mask harmonics */
        std::vector<char> harm(N / 2, 0);
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
                if(m > worst) { worst = m; worst_hz = i * binHz; }
            }
        }
        const double worst_db = 20 * std::log10(worst / fullscale + 1e-30);
        const double ratio_db = 10 * std::log10(ea / (eh + 1e-30) + 1e-30);
        printf("%9.2f %5d %12.1f %10.0f %14.1f%s\n", f0, eng.Kcut(), worst_db, worst_hz, ratio_db, worst_db > limit ? "  <-- over" : "");
        if(fc) fprintf(fc, "%.3f,%d,%.2f,%.1f,%.2f\n", f0, eng.Kcut(), worst_db, worst_hz, ratio_db);
        worst_all = std::fmax(worst_all, worst_db);
    }
    if(fc) fclose(fc);
    printf("worst non-harmonic bin over the sweep: %.1f dBFS (%s %.0f)\n", worst_all, worst_all > limit ? "FAIL, limit" : "ok, limit", limit);
    return worst_all > limit ? 1 : 0;
}
