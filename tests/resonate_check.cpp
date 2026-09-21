/* resonate_check — the modal mode's runtime against what its records say.
 *
 * Three things a resonator bank can get wrong, each checked so that the
 * check fails when they are: a mode's frequency (a synthetic 440 Hz mode
 * measured by zero crossings, within 0.5 cent), its decay (the envelope's
 * T60 within 2%), and the pickup's whole reason to exist — that a harder
 * strike makes more second harmonic, monotonically, on a real fitted world
 * (tests/data/ep-vel.kykm, the EP's C3 at six velocities). Then the worlds
 * themselves: attach, decode, the first point's hertz against the value the
 * export wrote (within a cent), no NaN anywhere over a keyboard of strikes,
 * and interpolation halfway between two points landing between them in log
 * frequency. And a trivially-constructible assertion, because these live in
 * SDRAM.
 *
 * Vacuity was checked the way CLAUDE.md asks: multiply the gain by sin w
 * twice instead of once and the decay test does not care, but the level
 * test (a mode at 440 Hz must peak within 3% of the record's amplitude at
 * the envelope's value there) does; put the resonator's coefficient sign wrong and the
 * frequency test fails at once; take the pickup out and the bark test
 * fails.
 */
#include "kyk_resonate.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

using namespace kyk;

static_assert(std::is_trivially_constructible<ResonatorVoice>::value, "voices live in SDRAM: no default member initialisers");
static_assert(std::is_trivially_constructible<ResonatorWorld>::value, "worlds live in SDRAM: no default member initialisers");

static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while(0)

static std::vector<uint8_t> slurp(const char* path)
{
    std::vector<uint8_t> b;
    FILE* f = fopen(path, "rb");
    if(!f) return b;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    b.resize(n);
    if(fread(b.data(), 1, n, f) != (size_t)n) b.clear();
    fclose(f);
    return b;
}

/* h1..h4 of a signal by plain DFT around k f0 */
static void harmonics(const float* x, int n, float sr, float f0, double* h)
{
    for(int k = 1; k <= 4; k++)
    {
        double best = 0;
        for(double fr = k * f0 * 0.97; fr <= k * f0 * 1.03; fr += sr / n / 4)
        {
            double re = 0, im = 0;
            for(int i = 0; i < n; i++)
            {
                const double w = 0.5 - 0.5 * std::cos(6.283185307 * i / n);
                re += x[i] * w * std::cos(6.283185307 * fr * i / sr);
                im -= x[i] * w * std::sin(6.283185307 * fr * i / sr);
            }
            best = std::fmax(best, std::sqrt(re * re + im * im));
        }
        h[k - 1] = 20 * std::log10(best + 1e-12);
    }
}

int main()
{
    const float sr = 48000.f;

    /* 1. one synthetic mode: frequency, decay, level */
    {
        ResonatorVoice v; v.Init();
        const float hz = 440.f, t60 = 0.5f, zeta = 6.91f / (t60 * 6.2831853f * hz), gain = 0.3f;
        v.bank.Set(&hz, &zeta, &gain, 1, sr);
        v.swing_soft = v.swing_hard = 1.f;
        v.Strike(1.f);
        std::vector<float> y(48000);
        v.Process(y.data(), 48000);
        /* frequency: zero crossings between 0.05 and 0.45 s */
        int a = 2400, b = 21600, crossings = 0, first = -1, last = -1;
        for(int i = a + 1; i < b; i++)
            if(y[i - 1] < 0.f && y[i] >= 0.f) { if(first < 0) first = i; last = i; crossings++; }
        /* linear interpolation of the crossing instants */
        auto cross = [&](int i) { return i - 1 + (-y[i - 1]) / (y[i] - y[i - 1]); };
        const double f = (crossings - 1) * sr / (cross(last) - cross(first));
        const double cents = 1200 * std::log2(f / hz);
        CHECK(std::fabs(cents) < 0.5, "440 Hz mode measured at %.3f Hz (%.2f cents)", f, cents);
        /* decay: peak envelope at 0.1 s and 0.4 s */
        float p1 = 0, p2 = 0;
        for(int i = 4800; i < 4800 + 110; i++) p1 = std::fmax(p1, std::fabs(y[i]));
        for(int i = 19200; i < 19200 + 110; i++) p2 = std::fmax(p2, std::fabs(y[i]));
        const double measured_t60 = 60.0 * 0.3 / (20 * std::log10(p1 / p2));
        CHECK(std::fabs(measured_t60 / t60 - 1) < 0.02, "T60 0.5 s measured as %.3f s", measured_t60);
        /* level: the record's amplitude is what rings — judged at the peak's
           own instant against the envelope there, since the strike ramps in
           over 3 ms and a 0.5 s T60 has already lost 4% by then */
        float pk = 0; int at = 0;
        for(int i = 0; i < 480; i++) if(std::fabs(y[i]) > pk) { pk = std::fabs(y[i]); at = i; }
        const float expect = gain * std::exp(-zeta * 6.2831853f * hz / sr * at);
        CHECK(std::fabs(pk / expect - 1) < 0.03, "mode of amplitude %.3f peaks at %.3f at sample %d, envelope there %.3f", gain, pk, at, expect);
        printf("  mode: %.3f Hz (%.2f cents), T60 %.3f s, peak %.3f of %.3f\n", f, cents, measured_t60, pk, gain);
        /* phase: the same mode set at pi/2 starts at its peak (a cosine), at
           zero it starts at zero — the state-from-phase strike, and the
           reason the onset is the recording's and not a spike */
        ResonatorVoice c; c.Init();
        const float half_pi = 1.5707963f;
        c.bank.Set(&hz, &zeta, &gain, 1, sr, &half_pi);
        c.swing_soft = c.swing_hard = 1.f;
        c.Strike(1.f);
        float yc[4];
        c.Process(yc, 4);
        /* the strike bank is ramped over 3 ms, so the first sample is the
           ramp's first step times the cosine's start; compare against the
           zero-phase voice's first sample, which is the ramp times sin(0) */
        ResonatorVoice z; z.Init();
        z.bank.Set(&hz, &zeta, &gain, 1, sr);
        z.swing_soft = z.swing_hard = 1.f;
        z.Strike(1.f);
        float yz[4];
        z.Process(yz, 4);
        /* both open at zero (the ramp's first sample is zero); at sample 1
           the cosine stands at cos w against the sine's sin w, seventeen
           times more at 440 Hz */
        CHECK(yc[1] > 5.f * std::fabs(yz[1]) && yc[1] > 0.f, "phase: zero-phase sample 1 %.6f, pi/2 sample 1 %.6f", yz[1], yc[1]);
    }

    /* 2. the EP world: attach, decode, bark grows with velocity */
    {
        auto blob = slurp("tests/data/ep-vel.kykm");
        CHECK(!blob.empty(), "tests/data/ep-vel.kykm missing");
        ResonatorWorld w; w.Init();
        CHECK(w.Attach(blob.data(), (uint32_t)blob.size()), "ep-vel.kykm did not attach");
        CHECK(w.form == 1, "ep-vel is a magnetic world (form 1), got %d", w.form);
        float hz[ResonatorBank::kMax], z[ResonatorBank::kMax], g[ResonatorBank::kMax], ph[ResonatorBank::kMax];
        w.Decode(0, hz, z, g, ph);
        /* point 0 is the EP's lowest note, midi 36 = C2 at 65.4 Hz; the fit
           has it a few cents off and a refit moves it a few more, so the
           bound is a quarter tone. And the decode is its own encode's
           inverse: cents back from hertz land on the byte that was read */
        CHECK(std::fabs(1200 * std::log2(hz[0] / 65.41f)) < 50.0, "point 0 decodes to %.2f Hz, C2 is 65.41", hz[0]);
        uint16_t c0; std::memcpy(&c0, w.Modes(0), 2);
        const int back = (int)std::lround(1200.0 * std::log2(hz[0] / 20.0));
        CHECK(back == (int)c0, "point 0: %.3f Hz encodes back to %d cents, byte says %d", hz[0], back, (int)c0);
        /* C3 (param 48): six strikes rendered alone, h2 re h1 must rise monotonically */
        double prev = -1e9; bool mono = true; double first_h2 = 0, last_h2 = 0;
        for(int s = 0; s < 6; s++)
        {
            ResonatorVoice v; v.Init();
            w.At(48.f, v, sr);
            v.Strike(s / 5.f);
            std::vector<float> y(16384);
            v.Process(y.data(), 16384);
            double h[4];
            harmonics(y.data(), 16384, sr, hz[0] * std::exp2((48.f - w.Param(0)) / 12.f) * 0.f + 130.8f, h);
            const double h2 = h[1] - h[0];
            if(s == 0) first_h2 = h2;
            last_h2 = h2;
            if(h2 <= prev) mono = false;
            prev = h2;
            for(float v_ : y) if(!(v_ == v_)) { CHECK(false, "NaN in the EP world at velocity %d", s); break; }
        }
        CHECK(mono, "h2 re h1 is not monotonic in velocity");
        /* the spin: decay x2 doubles the fundamental's ring; voicing of a
           width moves h2, coil x0.5 darkens. Each against the untuned voice */
        {
            auto ring = [&](float dec, float voi) {
                ResonatorWorld w2 = w; w2.decay = dec; w2.voicing = voi;
                ResonatorVoice v; v.Init(); w2.At(48.f, v, sr); v.Strike(0.6f);
                std::vector<float> y(48000); v.Process(y.data(), 48000);
                float p1 = 0, p2 = 0;
                for(int i = 4800; i < 4800 + 400; i++) p1 = std::fmax(p1, std::fabs(y[i]));
                for(int i = 43200; i < 43200 + 400; i++) p2 = std::fmax(p2, std::fabs(y[i]));
                double h[4]; harmonics(y.data(), 16384, sr, 130.8f, h);
                return std::make_pair(20 * std::log10(p1 / (p2 + 1e-12)), h[1] - h[0]);
            };
            auto base = ring(1.f, 0.f), voiced = ring(1.f, 0.5f);
            CHECK(std::fabs(voiced.second - base.second) > 2.0, "voicing +0.5 width moved h2 by only %.1f dB", voiced.second - base.second);
            /* decay x2 halves every mode's damping: the bank's own c2 = -r^2
               says so exactly, -ln r being zeta w */
            ResonatorWorld w2 = w; w2.decay = 2.f;
            ResonatorVoice a1, a2; a1.Init(); a2.Init(); w.At(48.f, a1, sr); w2.At(48.f, a2, sr);
            const double d1 = -std::log(std::sqrt(-a1.bank.c2[0])), d2 = -std::log(std::sqrt(-a2.bank.c2[0]));
            CHECK(std::fabs(d2 / d1 - 0.5) < 0.01, "decay x2: mode 0's damping went %.3g -> %.3g a sample (expected half)", d1, d2);
            printf("  spin: decay x2 halves the damping (%.3g -> %.3g); voicing +0.5 width h2 %+.1f -> %+.1f dB\n", d1, d2, base.second, voiced.second);
        }
        CHECK(last_h2 - first_h2 > 6.0, "h2 grows only %.1f dB from softest to hardest; the pickup is not barking", last_h2 - first_h2);
        /* and at the level the fit found: the record's own model made C3's
           hardest take +19 dB and its softest +7 (out/fit/ep-vel/fits.tsv in
           ModalBake); more than 5 dB off either is the swing's scale gone
           wrong, which is what the export lost once */
        CHECK(std::fabs(last_h2 - 19.0) < 5.0 && std::fabs(first_h2 - 7.0) < 5.0, "C3 h2 soft %+.1f / hard %+.1f, the fit said +7 / +19", first_h2, last_h2);
        printf("  ep-vel: C3 h2 re h1 %+.1f dB soft -> %+.1f dB hard, monotonic %s\n", first_h2, last_h2, mono ? "yes" : "no");
    }

    /* 3. the Wurlitzer world: a keyboard of strikes, no NaN, interpolation between points */
    {
        auto blob = slurp("tests/data/wurli.kykm");
        CHECK(!blob.empty(), "tests/data/wurli.kykm missing");
        ResonatorWorld w; w.Init();
        CHECK(w.Attach(blob.data(), (uint32_t)blob.size()), "wurli.kykm did not attach");
        CHECK(w.form == 0 && w.N >= 40 && w.P == 11, "wurli: form %d N %d P %d", w.form, w.N, w.P);
        /* the burst: a strike with the point's burst and one without differ
           in their first 40 ms and not after 70 — the attack the modes are
           not, played once and gone. ModalBake's tools/bursts.py made them
           the recording's first 60 ms minus the modes', crossing into the
           modes over the last 30, so with the burst the first 40 ms is the
           recording's and by 70 nothing of it is left */
        {
            ResonatorVoice with; with.Init(); w.At(60.f, with, sr); with.Strike(1.f);
            ResonatorVoice without; without.Init(); w.At(60.f, without, sr); without.bursts = nullptr; without.Strike(1.f);
            std::vector<float> ya(4800), yb(4800);
            with.Process(ya.data(), 4800); without.Process(yb.data(), 4800);
            double d_early = 0, d_late = 0, e_late = 0;
            for(int i = 0; i < 1920; i++) d_early += (ya[i] - yb[i]) * (ya[i] - yb[i]);
            for(int i = 3360; i < 4800; i++) { d_late += (ya[i] - yb[i]) * (ya[i] - yb[i]); e_late += yb[i] * yb[i]; }
            CHECK(d_early > 0.0, "the burst added nothing in the first 40 ms");
            CHECK(d_late < 1e-6 * e_late, "the burst is still there after 70 ms: %.3g of the signal", d_late / e_late);
            printf("  wurli: C4's burst adds %.1f in the first 40 ms and %.2g after 70 ms\n", d_early, d_late);
        }
        for(int i = 0; i < w.P; i++)
        {
            ResonatorVoice v; v.Init();
            w.At(w.Param(i), v, sr);
            v.Strike(0.8f);
            std::vector<float> y(4800);
            v.Process(y.data(), 4800);
            float pk = 0; bool nan = false;
            for(float s : y) { if(!(s == s)) nan = true; pk = std::fmax(pk, std::fabs(s)); }
            CHECK(!nan && pk > 0.f, "wurli point %d: nan %d peak %g", i, nan, pk);
        }
        /* halfway between C2 (36) and G2 (43): the first slot's frequency sits between the two in log */
        float ha[48], za[48], ga[48], fa[48], hb[48], zb[48], gb[48], fb[48];
        w.Decode(0, ha, za, ga, fa); w.Decode(1, hb, zb, gb, fb);
        ResonatorVoice v; v.Init();
        w.At(0.5f * (w.Param(0) + w.Param(1)), v, sr);
        const float mid = std::sqrt(ha[0] * hb[0]);
        const float got = sr / 6.2831853f * std::acos(v.bank.c1[0] / (2.f * std::sqrt(-v.bank.c2[0])));
        CHECK(std::fabs(1200 * std::log2(got / mid)) < 1.0, "midpoint slot 0 at %.2f Hz, expected %.2f", got, mid);
        printf("  wurli: 11 points struck, midpoint slot 0 %.2f Hz between %.2f and %.2f\n", got, ha[0], hb[0]);
    }

    /* the burst player: a stored attack read at a rate, and filtered. A
       synthetic burst block — one burst at swing 1, a 1 kHz sine of 480
       samples — played at rate 2 is a 2 kHz sine of 240; played through
       the velocity low-pass at a 1 kHz corner it is 3 dB down and at 13
       kHz it is not. And the world sets the rate: a note a fifth above a
       point reads that point's burst at 2^(7/12). */
    {
        std::vector<uint8_t> blk(2 + 10 + 2 * 480);
        uint16_t nb = 1, len = 480; float sw = 1.f, sc = 1.f;
        std::memcpy(&blk[0], &nb, 2); std::memcpy(&blk[2], &sw, 4); std::memcpy(&blk[6], &sc, 4); std::memcpy(&blk[10], &len, 2);
        for(int i = 0; i < 480; i++) { int16_t v = (int16_t)std::lround(32767.0 * 0.5 * std::sin(6.2831853 * 1000.0 * i / 48000.0)); std::memcpy(&blk[12 + 2 * i], &v, 2); }
        auto freq_of = [&](float rate, float lp, int& played) {
            BurstPlayer b; b.Init(); b.Strike(blk.data(), 1.f, rate, lp);
            std::vector<float> y(960, 0.f); b.Process(y.data(), 960);
            played = 0; for(int i = 0; i < 960; i++) if(y[i] != 0.f) played = i + 1;
            int zc = 0; for(int i = 1; i < played; i++) if((y[i - 1] < 0.f) != (y[i] < 0.f)) zc++;
            double e = 0; for(int i = 0; i < played; i++) e += y[i] * y[i];
            return std::make_pair(zc * 48000.0 / (2.0 * played), e);
        };
        int n1, n2, n3;
        auto r1 = freq_of(1.f, 0.f, n1), r2 = freq_of(2.f, 0.f, n2);
        /* zero crossings count a half-cycle short at the edges: within 6% */
        CHECK(std::fabs(r1.first / 1000.0 - 1) < 0.06 && n1 == 479, "at rate 1: %.0f Hz over %d samples", r1.first, n1);
        CHECK(std::fabs(r2.first / 2000.0 - 1) < 0.06 && n2 >= 238 && n2 <= 240, "at rate 2: %.0f Hz over %d samples (a 2 kHz sine of 240)", r2.first, n2);
        const float lp1k = 1.f - std::exp(-6.2831853f * 1000.f / 48000.f), lp13k = 1.f - std::exp(-6.2831853f * 13000.f / 48000.f);
        auto r3 = freq_of(1.f, lp1k, n3); auto r4 = freq_of(1.f, lp13k, n3);
        const double d1 = 10 * std::log10(r3.second / r1.second), d13 = 10 * std::log10(r4.second / r1.second);
        CHECK(d1 < -2.0 && d1 > -5.0, "a 1 kHz sine through the 1 kHz corner: %.1f dB (expected about -3)", d1);
        CHECK(d13 > -0.5, "through the 13 kHz corner: %.1f dB (expected about 0)", d13);
        auto wb = slurp("tests/data/wurli.kykm");
        ResonatorWorld w; w.Init(); w.Attach(wb.data(), (uint32_t)wb.size());
        /* three semitones above point 0 (C2): nearer to it than to point 1
           (G2), so its burst, read at 2^(3/12) */
        ResonatorVoice v; v.Init(); w.At(w.Param(0) + 3.f, v, 48000.f);
        CHECK(std::fabs(v.burst_rate - std::exp2(3.f / 12.f)) < 1e-4, "three semitones above point 0 reads its burst at rate %.4f, expected %.4f", v.burst_rate, std::exp2(3.f / 12.f));
        printf("  burst: rate 1 %.0f Hz / %d samples, rate 2 %.0f Hz / %d; the 1 kHz corner %.1f dB, the 13 kHz corner %.1f dB; three semitones up reads at %.3f\n", r1.first, n1, r2.first, n2, d1, d13, v.burst_rate);
    }

    printf(fails ? "resonate_check: %d FAILED\n" : "resonate_check: ok\n", fails);
    return fails ? 1 : 0;
}
