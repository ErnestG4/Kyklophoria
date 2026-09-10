/* kepler_check — the bodies must behave like bodies.
 *
 * The multi-body mode exists because one orbit is a closed curve and shows you
 * everything it will ever do in the first cycle. The fix is company, and what
 * makes it worth having is that the character of each count is a fact about
 * celestial mechanics rather than a tuning choice: one body closes, two are
 * integrable and beat against each other, three are Poincare's problem and
 * never repeat.
 *
 * That is a claim about the code, not just about the universe, so it is
 * measured here the way chaos is always measured — start a twin a hair further
 * out and see whether the hair grows. At sixty seconds the divergence goes
 * 3.4e-3, 7.1e-3, then 3.3e-1: two bodies barely worse than one, and a factor
 * of forty-seven at the third. This runs at thirty seconds, which is enough to
 * separate them and short enough to belong in a suite people run often. If a
 * refactor ever flattens that step, the mode has lost the only thing that
 * distinguishes its settings from each other.
 *
 * The other two properties are the ones that keep it playable rather than
 * correct: nothing may escape or go non-finite over a long run, and mass zero
 * must be bit-identical to the single-body orbit at any count, so that turning
 * the body knob up cannot break a patch made before the knob existed.
 */
#include "kyk_kepler.h"
#include <cstdio>
#include <cmath>

using namespace kyk;

static int  bad = 0;
static void ck(const char* m, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", m); if(!ok) bad++; }
static void Run(Kepler& k, double secs, float dt)
{ const long n = (long)(secs / dt); for(long i = 0; i < n; i++) k.Step(dt); }

int main()
{
    const float dt = 24.f / 48000.f;      /* one block at 48 kHz */

    {
        Kepler a, b;
        a.Init(4); a.bodies = 1; a.companion = 0.f; a.Reset(0.30f, 0.5f);
        b.Init(4); b.bodies = kKeplerBodies; b.companion = 0.f; b.Reset(0.30f, 0.5f);
        Run(a, 20.0, dt); Run(b, 20.0, dt);
        ck("companion 0 at full count is exactly the single-body orbit",
           a.X() == b.X() && a.Y() == b.Y());
    }
    for(int nb = 1; nb <= kKeplerBodies; nb++)
    {
        Kepler k; k.Init(4); k.bodies = nb; k.companion = 0.5f; k.Reset(0.30f, 0.5f);
        Run(k, 60.0, dt);
        const float rr = k.X() * k.X() + k.Y() * k.Y();
        char m[110];
        std::snprintf(m, sizeof m, "%d bodies: finite and bound after a minute (r=%.3f)", nb, std::sqrt(rr));
        ck(m, std::isfinite(k.X()) && std::isfinite(k.Y()) && rr < 2.25f);
    }
    double div[kKeplerBodies + 1] = {0};
    printf("\n  a twin started 1e-5 further out, separation after 30 s:\n");
    for(int nb = 1; nb <= kKeplerBodies; nb++)
    {
        Kepler a, b;
        a.Init(4); a.bodies = nb; a.companion = 0.6f; a.Reset(0.30f, 0.5f);
        b.Init(4); b.bodies = nb; b.companion = 0.6f; b.Reset(0.30f + 1e-5f, 0.5f);
        Run(a, 30.0, dt); Run(b, 30.0, dt);
        div[nb] = std::hypot(a.X() - b.X(), a.Y() - b.Y());
        printf("    %d bodies: %.3e\n", nb, div[nb]);
    }
    ck("1 body: a nudge stays a nudge (integrable)", div[1] < 0.05);
    ck("2 bodies: still essentially integrable", div[2] < 0.05);
    ck("3 bodies: a nudge grows large (chaotic)", div[3] > 0.05);
    ck("the onset is at the third body, not gradual", div[3] > 10.0 * div[2]);

    if(bad) printf("kepler_check: %d FAILURES\n", bad);
    else    printf("kepler_check: all passed\n");
    return bad ? 1 : 0;
}
