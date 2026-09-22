/* resonate_check — the modal mode's runtime against what its records say.
 *
 * Three things a resonator bank can get wrong, each checked so that the
 * check fails when they are: a mode's frequency (a synthetic 440 Hz mode
 * measured by zero crossings, within 0.5 cent), its decay (the envelope's
 * T60 within 2%), and the pickup's whole reason to exist — that a harder
 * strike makes more second harmonic, monotonically, on a real fitted world
 * (tests/data/tine.kykm, the EP's C3 at six velocities). Then the worlds
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
        auto blob = slurp("tests/data/tine.kykm");
        CHECK(!blob.empty(), "tests/data/tine.kykm missing");
        ResonatorWorld w; w.Init();
        CHECK(w.Attach(blob.data(), (uint32_t)blob.size()), "tine.kykm did not attach");
        CHECK(w.form == 1, "tine.kykm is a magnetic world (form 1), got %d", w.form);
        float hz[ResonatorBank::kMax], z[ResonatorBank::kMax], g[ResonatorBank::kMax], ph[ResonatorBank::kMax];
        w.Decode(0, hz, z, g, ph);
        /* point 0's lowest mode is the note that point was fitted at — the
           fixture says which, so a change of source does not change the
           check. The fit has it a few cents off and a refit moves it a few
           more, so the bound is a quarter tone. And the decode is its own
           encode's inverse: cents back from hertz land on the byte read */
        const float p0 = 440.f * std::exp2((w.Param(0) - 69.f) / 12.f);
        CHECK(std::fabs(1200 * std::log2(hz[0] / p0)) < 50.0, "point 0 decodes to %.2f Hz, its note is %.2f", hz[0], p0);
        uint16_t c0; std::memcpy(&c0, w.Modes(0), 2);
        const int back = (int)std::lround((w.ver >= 6 ? 6000.0 : 1200.0) * std::log2(hz[0] / 20.0));
        CHECK(back == (int)c0, "point 0: %.3f Hz encodes back to %d (fifths of a cent from v6), field says %d", hz[0], back, (int)c0);
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
        /* how much the bark grows, not where it starts: a pickup world's
           second harmonic must rise with the strike, and by a good deal —
           the field's curvature is the instrument. Where it starts was an
           absolute figure once, read off the fit of a record we no longer
           carry; growth is the property, and it is the one the export lost
           when the swing's scale went wrong */
        CHECK(last_h2 - first_h2 > 6.0, "the bark hardly grew with the strike: h2 %+.1f soft to %+.1f hard", first_h2, last_h2);
        printf("  tine: C3 h2 re h1 %+.1f dB soft -> %+.1f dB hard, monotonic %s\n", first_h2, last_h2, mono ? "yes" : "no");
    }

    /* 3. the Wurlitzer world: a keyboard of strikes, no NaN, interpolation between points */
    {
        auto blob = slurp("tests/data/piano.kykm");
        CHECK(!blob.empty(), "tests/data/piano.kykm missing");
        ResonatorWorld w; w.Init();
        CHECK(w.Attach(blob.data(), (uint32_t)blob.size()), "piano.kykm did not attach");
        CHECK(w.form == 0 && w.N >= 30 && w.P >= 8, "the note fixture: form %d N %d P %d", w.form, w.N, w.P);
        /* the burst: a strike with the point's burst and one without differ
           in their first 40 ms and not after 70 — the attack the modes are
           not, played once and gone. ModalBake's tools/bursts.py made them
           the recording's first 60 ms minus the modes', crossing into the
           modes over the last 30, so with the burst the first 40 ms is the
           recording's and by 70 nothing of it is left */
        {
            ResonatorVoice with; with.Init(); w.At(60.f, with, sr); with.Strike(1.f);
            ResonatorVoice without; without.Init(); w.At(60.f, without, sr); without.bursts = nullptr; without.Strike(1.f);
            /* the windows are the burst's own: its body, and a hair past
               its end — a fixture from another library has another length */
            /* the burst's own length at the rate it is read: a point
               carried down the keyboard plays its attack slower and for
               longer */
            const int blen = (int)(with.burst_len / (with.burst_rate > 0.f ? with.burst_rate : 1.f)), half = blen / 2;
            const int after = blen + (int)(0.005f * sr), n = after + 1440;
            std::vector<float> ya(n), yb(n);
            with.Process(ya.data(), n); without.Process(yb.data(), n);
            double d_early = 0, d_late = 0, e_late = 0;
            for(int i = 0; i < half; i++) d_early += (ya[i] - yb[i]) * (ya[i] - yb[i]);
            for(int i = after; i < n; i++) { d_late += (ya[i] - yb[i]) * (ya[i] - yb[i]); e_late += yb[i] * yb[i]; }
            CHECK(d_early > 0.0, "the burst added nothing over its first half");
            CHECK(d_late < 1e-6 * e_late, "the burst is still there after its end: %.3g of the signal", d_late / e_late);
            printf("  the note fixture: a %.0f ms burst adds %.1f over its first half and %.2g after its end\n", 1000.f * blen / sr, d_early, d_late);
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
            CHECK(!nan && pk > 0.f, "point %d: nan %d peak %g", i, nan, pk);
        }
        /* a quarter of the way from the first point to the second: a note
           world plays the NEARER point transposed (format v6), so the first
           slot is that point's first mode moved by the interval, not a
           blend of the two — which is what the runtime must not do */
        float ha[48], za[48], ga[48], fa[48], hb[48], zb[48], gb[48], fb[48];
        w.Decode(0, ha, za, ga, fa); w.Decode(1, hb, zb, gb, fb);
        ResonatorVoice v; v.Init();
        const float p25 = w.Param(0) + 0.25f * (w.Param(1) - w.Param(0));
        w.At(p25, v, sr);
        const float want = ha[0] * std::exp2((p25 - w.Param(0)) / 12.f);
        const float got = sr / 6.2831853f * std::acos(v.bank.c1[0] / (2.f * std::sqrt(-v.bank.c2[0])));
        CHECK(std::fabs(1200 * std::log2(got / want)) < 1.0, "a quarter of the way along, slot 0 is %.2f Hz; the nearer point transposed is %.2f", got, want);
        printf("  the note fixture: %d points struck, a quarter along slot 0 is %.2f Hz (point 0's %.2f transposed, not point 1's %.2f)\n", w.P, got, ha[0], hb[0]);
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
        auto wb = slurp("tests/data/piano.kykm");
        ResonatorWorld w; w.Init(); w.Attach(wb.data(), (uint32_t)wb.size());
        /* three semitones above point 0 (C2): nearer to it than to point 1
           (G2), so its burst, read at 2^(3/12) */
        ResonatorVoice v; v.Init(); w.At(w.Param(0) + 3.f, v, 48000.f);
        CHECK(std::fabs(v.burst_rate - std::exp2(3.f / 12.f)) < 1e-4, "three semitones above point 0 reads its burst at rate %.4f, expected %.4f", v.burst_rate, std::exp2(3.f / 12.f));
        printf("  burst: rate 1 %.0f Hz / %d samples, rate 2 %.0f Hz / %d; the 1 kHz corner %.1f dB, the 13 kHz corner %.1f dB; three semitones up reads at %.3f\n", r1.first, n1, r2.first, n2, d1, d13, v.burst_rate);
    }

    /* the carry across a retune goes to the nearest frequency, not the
       same index. Three modes ringing at 100, 200 and 300 Hz; the bank is
       retuned to 50, 105, 210 and 315 — a new low mode at index 0, as a
       family's next member or a note world's ghost puts one. The rings
       must land at 105, 210, 315 and nothing at 50, where the old index
       carry would have put the 100 Hz ring. Measured on the 200 ms after
       the retune: the line at 50 Hz at least 40 dB under the one at 105. */
    {
        auto level_at = [](const std::vector<float>& y, float hz, float sr) {
            double re = 0, im = 0; const int n = (int)y.size();
            for(int i = 0; i < n; i++) { const double ph = 6.2831853 * hz * i / sr; re += y[i] * std::cos(ph); im -= y[i] * std::sin(ph); }
            return 20 * std::log10(std::sqrt(re * re + im * im) / n + 1e-12);
        };
        const float sr = 48000.f;
        ResonatorBank b; b.Init();
        const float h0[3] = {100.f, 200.f, 300.f}, z0[3] = {0.001f, 0.001f, 0.001f}, g0[3] = {1.f, 1.f, 1.f};
        b.Set(h0, z0, g0, 3, sr);
        b.Strike(1.f);
        std::vector<float> y(4800); b.Process(y.data(), 4800);                   /* 100 ms of ring, the strike folded */
        const float h1[4] = {50.f, 105.f, 210.f, 315.f}, z1[4] = {0.001f, 0.001f, 0.001f, 0.001f}, g1[4] = {1.f, 1.f, 1.f, 1.f};
        b.Set(h1, z1, g1, 4, sr, nullptr, true);
        std::vector<float> z(9600); b.Process(z.data(), 9600);
        const double at50 = level_at(z, 50.f, sr), at105 = level_at(z, 105.f, sr), at210 = level_at(z, 210.f, sr);
        CHECK(at105 > at50 + 40.0, "the 100 Hz ring went to index 0 (50 Hz): %.1f dB there against %.1f at 105", at50, at105);
        CHECK(at210 > at50 + 40.0, "the 200 Hz ring is not at 210: %.1f dB at 210 against %.1f at 50", at210, at50);
        printf("  the carry goes by frequency: after a retune that puts a new mode at index 0, 50 Hz %.1f dB, 105 Hz %.1f, 210 Hz %.1f\n", at50, at105, at210);
    }

    /* the strike's arithmetic against the library it replaced. A
       note-to-note strike made about 470 calls into libm — on the M7 a
       hundred cycles or more each, in one block — and the format's own
       fields make most of them unnecessary: a decay, a level and a phase
       are bytes. What is left is a polynomial, and this is what it costs
       in accuracy. */
    {
        /* measured where it matters: what the polynomial does to a mode's
           frequency in cents, and to its level in dB, rather than to the
           bare value of a sine */
        double we = 0; float wat = 0;
        for(int i = 0; i <= 16000; i++)
        {
            const float x = -20.f + i * (40.f / 16000.f);
            const double r = std::fabs(kyk::fastmath::Exp2(x) / std::exp2((double)x) - 1.0);
            if(r > we) { we = r; wat = x; }
        }
        CHECK(we * 1731.2 < 0.05, "Exp2 moves a frequency by %.3f cents at %.2f", we * 1731.2, wat);
        /* the sine and cosine against the library's, in float, over the
           poles a mode can have: what a float can hold at 20 Hz is a few
           cents whichever of the two computes it — that is the type, not
           the polynomial — so what is asked is that this is no worse than
           the call it replaced */
        double wsc = 0; float wf = 0;
        for(int i = 1; i <= 20000; i++)
        {
            const float hz = 20.f * std::exp2(i * (10.f / 20000.f));
            if(hz > 23000.f) break;
            const float w = 6.2831853f * hz / 48000.f;
            float sf, cf; kyk::fastmath::SinCos(w, sf, cf);
            const double d = std::fmax(std::fabs(sf - std::sin(w)), std::fabs(cf - std::cos(w)));
            if(d > wsc) { wsc = d; wf = hz; }
        }
        CHECK(wsc < 3e-6, "SinCos differs from the library by %.3g (at %.0f Hz)", wsc, wf);
        double wz = 0;
        for(int i = 0; i <= 2000; i++)
        {
            const float x = i * (0.6f / 2000.f);
            const double e = std::exp(-(double)x);
            wz = std::fmax(wz, std::fabs(kyk::fastmath::ExpNegSmall(x) / e - 1.0));
        }
        CHECK(wz * 8.686 < 0.01, "ExpNegSmall moves a decay by %.4f dB", wz * 8.686);
        printf("  the strike's own arithmetic: at most %.3f cents from Exp2, %.1e from SinCos (the library's own float), %.4f dB from exp(-x)\n", we * 1731.2, wsc, wz * 8.686);
    }

    printf(fails ? "resonate_check: %d FAILED\n" : "resonate_check: ok\n", fails);
    return fails ? 1 : 0;
}
