/* kyk_kepler.h — the position falls, rather than being carried round.
 *
 * The orbit mode already in the instrument is strictly Ptolemaic: uniform
 * circular motion in several planes at once, composing into a path that never
 * repeats. Epicycles. Kepler's contribution was to abandon constant speed,
 * and that is the part we did not have.
 *
 * Here a body falls through a softened central potential and the space is
 * read wherever it happens to be. The second law then does the musical work:
 * the body rushes through periapsis and lingers at apoapsis, so the timbre
 * dwells unevenly instead of gliding at a fixed rate. That is a different
 * sound from the Ptolemaic wash for a principled reason, not a parameter
 * change, which is why both are worth keeping.
 *
 * Two details make it playable rather than merely correct:
 *
 *   The orbit is planar.  Angular momentum confines a central-force orbit to
 *                         a plane, so N dimensions do not change that. The
 *                         plane is one of the same Givens planes the rotation
 *                         uses, chosen by a knob, and the remaining axes sit
 *                         wherever the player's CVs put them. Physically
 *                         right and far easier to steer than a free 4-D body.
 *
 *   The potential is softened.  A bare inverse square has a singularity at
 *                         the centre and lets a body escape to infinity, both
 *                         of which end the music. Softening removes the
 *                         singularity, and it also breaks the inverse-square
 *                         closure, so the ellipse precesses and the path is
 *                         quasi-periodic without needing two rates.
 *
 * Integrated with semi-implicit Euler, which is symplectic: it does not
 * conserve energy exactly but it does not drift secularly either, so an orbit
 * left running for an hour is still an orbit. A block is half a millisecond
 * against orbital periods of tenths of seconds upward, so the step is tiny.
 *
 * ── company ──────────────────────────────────────────────────────────────
 *
 * One body is a closed curve, and Will's objection to it is the right one: a
 * single orbit is hard to play, because everything it will ever do is visible
 * in the first cycle. What changes that is other bodies, and the reason is the
 * oldest open problem in the subject. Two bodies about a common centre are
 * integrable — the pair beats against itself, quasi-periodic, and you can hear
 * the period. Three are not, and Poincare's whole point was that no amount of
 * cleverness makes them so: the path stops repeating and never starts again.
 *
 * So `bodies` runs from one to eight, and the character of the first few is a
 * fact about celestial mechanics rather than a tuning choice.
 *
 * The voice is body 0. The others are gravitational company: they pull on it
 * and on each other, and the space is read where body 0 has got to. Summing
 * the bodies instead was the obvious alternative and is wrong here — a large
 * orbit plus a small one *is* an epicycle, and epicycles are precisely what
 * the orbit mode above already does. Perturbing a Keplerian body keeps this
 * mode Keplerian.
 *
 * `companion` is their mass, and at zero the whole system collapses exactly
 * back to the single-body case whatever `bodies` says. That is deliberate: it
 * means turning the count up cannot break a patch that was set before this
 * existed, and the knob has somewhere honest to start.
 */
#pragma once
#include "kyk_rotate.h"

namespace kyk {

/* Eight. A power of two so BodyX/BodyY can mask rather than branch, and the
 * pairwise force loop is then 28 interactions per block — against a budget
 * that already absorbs fifteen planes of Givens rotation, that is nothing.
 *
 * The first three are the ones with names: one body closes, two are integrable
 * and beat against each other, three are Poincare's problem and never repeat.
 * Past three the additions are not new in kind, but they are not redundant
 * either — a ring of six perturbers is closer to a mean field than to a
 * three-body scramble, and it behaves like it: smoother, denser, far less
 * prone to the sudden ejections that make three lively. Both ends are worth
 * having, so the range runs to eight rather than stopping where the textbook
 * stops being interesting. */
constexpr int kKeplerBodies = 8;

class Kepler
{
public:
    float gravity = 1.0f;    /* pulls harder, so orbits run faster and tighter */
    float soften  = 0.08f;   /* in space units; below this the centre is flat  */
    float damp    = 0.f;     /* 0 keeps it going forever; a little spirals in  */
    float vmax    = 6.f;     /* speed clamp, space units per second            */
    int   plane   = 0;       /* which Givens plane the orbit lives in          */
    int   bodies  = 1;       /* 1 closed, 2 beating, 3 chaotic, more = denser  */
    float companion = 0.f;   /* mass of bodies 1..n-1, relative to the centre  */
    /* Gravity on a torus. Under Wrap the space has no edges, so the attractor
     * has an image in every direction and the body should feel the nearest
     * one. Folding the displacement into ±half a turn does exactly that, and
     * it changes the character completely: a body that escapes one way
     * arrives back from the other, and there is no far radius to fall off.
     * Will's idea, and it is stranger than the flat version. */
    bool  wrap    = false;
    /* How far the body may get from the attractor before the wall turns it
     * round. Clamped, not wrapped — see the reflection in Step(). */
    float bound   = 0.62f;

    void Init(int n)
    {
        n_ = n < 2 ? 2 : (n > kMaxN ? kMaxN : n);
        Reset(0.30f, 0.f);
    }

    int Bodies() const { return bodies < 1 ? 1 : (bodies > kKeplerBodies ? kKeplerBodies : bodies); }

    bool Running() const { return running_; }
    void Stop() { running_ = false; }

    /* Place the body `radius` from the centre and give it the tangential
     * speed a circular orbit would need, scaled by `ecc`: 1 is circular,
     * below that it falls inward and swings back out, above it flies out and
     * returns. The classic sqrt(G/r) — which is why gravity and radius set
     * the orbital period between them. */
    void Reset(float radius, float ecc)
    {
        if(radius < 0.02f) radius = 0.02f;
        const float vc = Sqrt(gravity / radius);
        /* k times the circular speed. Apoapsis is r·k²/(2−k²), so k must stay
         * below sqrt(2) to remain bound at all and below about 1.24 to stay
         * inside the playable cube from this radius. The top of the knob is
         * therefore a very eccentric orbit, not an escape — though winding
         * gravity down while eccentricity is high will still fling the body
         * out, and it respawns, which is a comet and worth keeping. */
        const float e = ecc < 0.f ? 0.f : (ecc > 1.f ? 1.f : ecc);
        /* Companions start evenly spaced round the same circle and moving the
         * same way, which is the one arrangement that is stable enough to be
         * worth hearing before the perturbations take over — start them all at
         * the same phase and they collide immediately, however soft the core. */
        const int nb = Bodies();
        for(int b = 0; b < kKeplerBodies; b++)
        {
            /* spaced over the bodies that are actually running: two start
               opposite each other, three at a third of a turn, and so on */
            const float turn = (float)b / (float)(b < nb ? nb : kKeplerBodies);
            float sn, cs;
            SinCosTurns(turn, sn, cs);
            r_[b][0] = radius * cs;
            r_[b][1] = radius * sn;
            const float sp = vc * (0.35f + 0.89f * e);
            v_[b][0] = -sp * sn;      /* tangential, same sense for all */
            v_[b][1] =  sp * cs;
        }
        running_ = true;
    }

    void Step(float dt)
    {
        if(!running_ || dt <= 0.f) return;
        const int   nb = Bodies();
        const float e2 = soften * soften;
        const float m  = companion < 0.f ? 0.f : (companion > 1.f ? 1.f : companion);

        float a[kKeplerBodies][2];
        for(int b = 0; b < nb; b++)
        {
            /* a = -G r / (|r|² + eps²)^{3/2}, toward the nearest image of the
             * centre when the space wraps */
            float fx = r_[b][0], fy = r_[b][1];
            if(wrap) { fx = Wrapped(fx); fy = Wrapped(fy); }
            const float d2  = fx * fx + fy * fy + e2;
            const float inv = 1.f / (d2 * Sqrt(d2));
            a[b][0] = -gravity * fx * inv;
            a[b][1] = -gravity * fy * inv;
        }
        /* Mutual attraction, each pair once and applied both ways — Newton's
         * third law is what keeps the system from gaining momentum out of
         * nothing and wandering off. Softened with the same core, since two
         * bodies can pass arbitrarily close and an unsoftened pair would take
         * an unbounded kick in a single half-millisecond step. */
        if(m > 0.f && nb > 1)
        {
            for(int i = 0; i < nb; i++)
                for(int j = i + 1; j < nb; j++)
                {
                    float dx = r_[j][0] - r_[i][0], dy = r_[j][1] - r_[i][1];
                    if(wrap) { dx = Wrapped(dx); dy = Wrapped(dy); }
                    const float d2  = dx * dx + dy * dy + e2;
                    const float inv = 1.f / (d2 * Sqrt(d2));
                    const float g   = gravity * m * inv;
                    a[i][0] += g * dx;  a[i][1] += g * dy;
                    a[j][0] -= g * dx;  a[j][1] -= g * dy;
                }
        }
        for(int b = 0; b < nb; b++)
        {
            v_[b][0] += a[b][0] * dt;
            v_[b][1] += a[b][1] * dt;
            if(damp > 0.f)
            {
                const float k = 1.f - damp * dt;
                v_[b][0] *= k;
                v_[b][1] *= k;
            }
            const float sp = Sqrt(v_[b][0] * v_[b][0] + v_[b][1] * v_[b][1]);
            if(sp > vmax) { const float k = vmax / sp; v_[b][0] *= k; v_[b][1] *= k; }
            r_[b][0] += v_[b][0] * dt;
            r_[b][1] += v_[b][1] * dt;
            if(wrap)
            {
                /* keep the stored offset in one turn so it cannot drift away */
                r_[b][0] = Wrapped(r_[b][0]);
                r_[b][1] = Wrapped(r_[b][1]);
            }
        }
        if(!wrap)
        {
            /* A wall, not a relaunch.
             *
             * This used to detect an escape and call Reset on the whole system.
             * That is a position jump: the body teleports and the timbre goes
             * with it, which is the one discontinuity this instrument is built
             * to make impossible. A reflection reverses the radial component of
             * the velocity and leaves the position exactly where it was, so the
             * morph stays continuous by the same argument as everything else —
             * and the body never restarts, so it keeps exploring instead of
             * being periodically returned to the same initial condition.
             *
             * Energy is conserved on the bounce, so nothing decays. The
             * tangential component is untouched, which is what makes this a
             * specular wall and not a sticky one: a body arriving nearly
             * side-on leaves nearly side-on and keeps circulating.
             *
             * A central attractor inside a circular wall is a billiard, and
             * bounded, aperiodic, never-resetting is exactly the behaviour
             * asked for. */
            for(int b = 0; b < nb; b++)
            {
                const float x = r_[b][0], y = r_[b][1];
                const float r2 = x * x + y * y;
                if(r2 <= bound * bound || r2 <= 0.f) continue;
                const float r   = Sqrt(r2);
                const float nx  = x / r, ny = y / r;         /* outward normal */
                const float vn  = v_[b][0] * nx + v_[b][1] * ny;
                if(vn > 0.f)                                  /* only if outbound */
                {
                    v_[b][0] -= 2.f * vn * nx;
                    v_[b][1] -= 2.f * vn * ny;
                }
                /* Put it back on the wall rather than leaving it outside, or a
                 * body that overshot in one step would bounce every step. */
                r_[b][0] = nx * bound;
                r_[b][1] = ny * bound;
            }
        }
        /* The one thing a bounce cannot rescue is arithmetic that has stopped
         * being a number. Nothing here should produce one, but a NaN would be
         * silent and permanent, so it is caught rather than trusted away. */
        for(int b = 0; b < nb; b++)
            if(!(r_[b][0] == r_[b][0]) || !(r_[b][1] == r_[b][1]))
            { Reset(0.30f, 0.55f); break; }
    }

    /* Add the body's offset to the control frame, in the chosen plane. */
    void Apply(float* p, int n) const
    {
        if(!running_) return;
        int i, j;
        Rotation::PlaneAxes(n, plane, i, j);
        p[i] += r_[0][0];
        p[j] += r_[0][1];
    }

    float X() const { return r_[0][0]; }
    float Y() const { return r_[0][1]; }
    float BodyX(int b) const { return r_[b & (kKeplerBodies - 1)][0]; }
    float BodyY(int b) const { return r_[b & (kKeplerBodies - 1)][1]; }
    float Speed() const { return Sqrt(v_[0][0] * v_[0][0] + v_[0][1] * v_[0][1]); }
    /* How near periapsis the body is, 0 at its slowest and 1 at its fastest.
     * Musically the useful readout: it is high exactly when the timbre is
     * moving fastest. */
    float Rush() const
    {
        const float s = Speed();
        return s <= 0.f ? 0.f : (s > vmax ? 1.f : s / vmax);
    }

private:
    /* fold into [-0.5, +0.5): the shortest way round a unit torus */
    static float Wrapped(float x)
    {
        x = x - (float)(int)x;
        if(x >= 0.5f) x -= 1.f;
        else if(x < -0.5f) x += 1.f;
        return x;
    }

    int   n_ = 4;
    float r_[kKeplerBodies][2] = {{0.3f, 0.f}};
    float v_[kKeplerBodies][2] = {};
    bool  running_ = false;
};

} // namespace kyk
