/* exciter_check — the bow coupled to the modes (core/kyk_exciter.h,
 * docs/exciters.md). A harmonic string of twelve modes at 196 Hz, bowed at
 * 0.13 of its length, modal mass 10 g. What a bowed string does and a
 * feed-forward exciter cannot:
 *
 *   1. passive: every corner of bow speed x pressure x position, 2 s,
 *      finite and bounded;
 *   2. Helmholtz motion inside Schelleng's band of force (which, measured
 *      here, runs 1-3 N at 0.05 m/s, 3-10 at 0.2, 10-20 at 0.4: both edges
 *      scale with the bow's speed): a steady tone at the string's
 *      fundamental whose amplitude follows the bow's speed and not its
 *      pressure;
 *   3. below the band no tone, and above it the motion breaks down;
 *   4. a bow that does not move puts nothing in;
 *   5. the hammer through the same loop: its contact shortening and its
 *      tone brightening with velocity by themselves, and the string never
 *      holding more energy than the hammer lost.
 *
 * Without the force written back the string is silent; without the
 * velocity read it is pushed aside and never oscillates — either fails 2. */
#include "kyk_exciter.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace kyk;
static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while(0)

static const float sr = 48000.f, f0 = 196.f, mass = 0.01f;

static std::vector<float> Bowed(float vb, float fn, float pos, int n)
{
    ResonatorBank b; b.Init();
    float hz[12], z[12], g[12];
    for(int k = 0; k < 12; k++) { hz[k] = f0 * (k + 1); z[k] = 0.002f; g[k] = 0.f; }
    b.Set(hz, z, g, 12, sr);
    float w[12]; for(int k = 0; k < 12; k++) w[k] = std::sin(3.14159265f * pos * (k + 1));
    Bow bow; bow.Init(); bow.v_bow = vb; bow.f_n = fn;
    std::vector<float> y(n);
    ProcessBowed(b, y.data(), n, w, bow, sr, mass);
    return y;
}
/* the fundamental by autocorrelation (parabolic peak) over [a, a + len) */
static float Pitch(const std::vector<float>& y, int a, int len)
{
    auto r = [&](int L) { double c = 0, e1 = 0, e2 = 0; for(int i = a; i < a + len - L; i++) { c += (double)y[i] * y[i + L]; e1 += (double)y[i] * y[i]; e2 += (double)y[i + L] * y[i + L]; } return c / std::sqrt(e1 * e2 + 1e-30); };
    int best = 150; double bv = -2;
    for(int L = 150; L < 400; L++) { const double v = r(L); if(v > bv) { bv = v; best = L; } }
    const double l = r(best - 1), c = r(best), h = r(best + 1), den = l - 2 * c + h;
    const double off = den != 0 ? 0.5 * (l - h) / den : 0;
    return (float)(sr / (best + off));
}
static double Ac(const std::vector<float>& y, int a, int b)
{
    double m = 0; for(int i = a; i < b; i++) m += y[i]; m /= (b - a);
    double e = 0; for(int i = a; i < b; i++) e += (y[i] - m) * (y[i] - m);
    return std::sqrt(e / (b - a));                                   /* RMS of the oscillation, the static push taken out */
}

int main()
{
    /* 1 */
    bool finite = true; double worst = 0;
    for(float vb : {0.f, 0.05f, 0.2f, 0.5f})
        for(float fn : {0.f, 0.3f, 3.f, 20.f})
            for(float pos : {0.05f, 0.13f, 0.3f, 0.45f})
            {
                const std::vector<float> y = Bowed(vb, fn, pos, 96000);
                for(float v : y) { if(!std::isfinite(v)) finite = false; else worst = std::fmax(worst, std::fabs(v)); }
            }
    CHECK(finite && worst < 1e-2, "bowed at every corner: finite %d, largest displacement %.3g", finite, worst);
    /* 2 */
    /* in the band: 0.2 m/s at 4 N; half the speed at half the force (the
       band scales with the speed); the same speed at 3 N */
    const std::vector<float> a = Bowed(0.2f, 4.f, 0.13f, 96000), b = Bowed(0.1f, 2.f, 0.13f, 96000), c = Bowed(0.2f, 3.f, 0.13f, 96000);
    const float p = Pitch(a, 72000, 24000);
    const double cents = 1200 * std::log2(p / f0);
    const double ra = Ac(a, 72000, 96000), rb = Ac(b, 72000, 96000), rc = Ac(c, 72000, 96000);
    const double steady = 20 * std::log10(Ac(a, 84000, 96000) / Ac(a, 72000, 84000));
    CHECK(std::fabs(cents) < 10.0, "the bowed string's tone is %.1f Hz (%.1f cents from its fundamental)", p, cents);
    CHECK(std::fabs(rb / ra - 0.5) < 0.12, "half the bow speed gave %.2f of the amplitude, not a half", rb / ra);
    CHECK(std::fabs(rc / ra - 1.0) < 0.15, "less pressure in the band gave %.2f of the amplitude (Helmholtz: the same)", rc / ra);
    CHECK(std::fabs(steady) < 1.0, "the tone is not steady: %+.1f dB over a quarter second", steady);
    /* 3 */
    const std::vector<float> weak = Bowed(0.2f, 0.3f, 0.13f, 96000), hard = Bowed(0.2f, 20.f, 0.13f, 96000);
    const double rw = Ac(weak, 72000, 96000), rh = Ac(hard, 72000, 96000);
    CHECK(rw < 0.1 * ra, "under the minimum force the string still sang: %.3g of the Helmholtz amplitude", rw / ra);
    CHECK(rh < 0.25 * ra, "over the maximum force the Helmholtz motion held: %.3g of its amplitude", rh / ra);
    /* 4 */
    const std::vector<float> still = Bowed(0.f, 10.f, 0.13f, 48000);
    double e = 0; for(float v : still) e += std::fabs(v);
    CHECK(e == 0.0, "a still bow put %.3g in", e);
    /* the hammer through the same loop (Hunt-Crossley felt, alpha 2.5): the
       contact shortens as the velocity doubles (theory against a rigid wall
       0.74 a doubling; a string that gives, a little less), the tone
       brightens with it (the harmonics' centroid rises), and the string
       never holds more energy than the hammer lost */
    {
        auto struck = [&](float vel, int& contact, double& centroid, double& e_string, double& e_lost) {
            ResonatorBank bk; bk.Init();
            float hz[24], z[24], g[24], w[24];
            for(int k = 0; k < 24; k++) { hz[k] = f0 * (k + 1); z[k] = 0.001f; g[k] = 0.f; w[k] = std::sin(3.14159265f * 0.12f * (k + 1)); }
            bk.Set(hz, z, g, 24, sr);
            Hammer h; h.Init(); h.Strike(vel, 0.f);
            std::vector<float> y(24000);
            int at = 0;
            while(at < 24000 && ProcessStruck(bk, y.data() + at, 1, w, h, sr, mass)) at++;
            contact = h.contact;
            e_string = 0; for(int k = 0; k < 24; k++) { const double om = 2 * M_PI * hz[k], x = bk.y1[k], v = (bk.y1[k] - bk.y2[k]) * sr; e_string += 0.5 * mass * (v * v + om * om * x * x); }
            e_lost = 0.5 * h.mass * ((double)vel * vel - (double)h.v * h.v);
            ProcessStruck(bk, y.data() + at, 24000 - at, w, h, sr, mass);
            double num = 0, den = 0;
            for(int k = 1; k <= 24; k++) { double re = 0, im = 0; for(int i = 2400; i < 14400; i++) { const double ph = 2 * M_PI * f0 * k * i / sr; re += y[i] * std::cos(ph); im -= y[i] * std::sin(ph); } const double a = std::sqrt(re * re + im * im); num += k * a; den += a; }
            centroid = num / den;
        };
        int c1, c2, c3; double b1, b2, b3, es1, el1, es2, el2, es3, el3;
        struck(0.5f, c1, b1, es1, el1); struck(1.f, c2, b2, es2, el2); struck(2.f, c3, b3, es3, el3);
        const double r1 = (double)c2 / c1, r2 = (double)c3 / c2;
        CHECK(r1 > 0.7 && r1 < 0.88 && r2 > 0.7 && r2 < 0.88, "the contact at twice the velocity: x%.2f and x%.2f of it (felt: about 0.75-0.85)", r1, r2);
        CHECK(b1 < b2 && b2 < b3, "a harder hit is not brighter: centroid h%.2f, h%.2f, h%.2f", b1, b2, b3);
        CHECK(es1 <= 1.05 * el1 && es2 <= 1.05 * el2 && es3 <= 1.05 * el3 && es3 > 0, "the string holds more than the hammer lost: %.3g of %.3g, %.3g of %.3g, %.3g of %.3g", es1, el1, es2, el2, es3, el3);
        printf("  the hammer: contact %d, %d, %d samples at 0.5, 1, 2 m/s (x%.2f, x%.2f); centroid h%.2f -> h%.2f -> h%.2f; the string keeps %.0f-%.0f%% of what the hammer lost\n",
               c1, c2, c3, r1, r2, b1, b2, b3, 100 * std::fmin(es1 / el1, std::fmin(es2 / el2, es3 / el3)), 100 * std::fmax(es1 / el1, std::fmax(es2 / el2, es3 / el3)));
    }
    if(fails) printf("exciter_check: %d FAILED\n", fails);
    else printf("exciter_check: ok — bowed at 196 Hz the tone is %.1f Hz; amplitude x%.2f at half the speed, x%.2f at less pressure; %.2f under the band, %.2f over it\n",
                p, rb / ra, rc / ra, rw / ra, rh / ra);
    return fails ? 1 : 0;
}
