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
 */
#pragma once
#include "kyk_rotate.h"

namespace kyk {

class Kepler
{
public:
    float gravity = 1.0f;    /* pulls harder, so orbits run faster and tighter */
    float soften  = 0.08f;   /* in space units; below this the centre is flat  */
    float damp    = 0.f;     /* 0 keeps it going forever; a little spirals in  */
    float vmax    = 6.f;     /* speed clamp, space units per second            */
    int   plane   = 0;       /* which Givens plane the orbit lives in          */

    void Init(int n)
    {
        n_ = n < 2 ? 2 : (n > kMaxN ? kMaxN : n);
        Reset(0.30f, 0.f);
    }

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
        r_[0] = radius;
        r_[1] = 0.f;
        const float vc = Sqrt(gravity / radius);
        v_[0] = 0.f;
        /* k times the circular speed. Apoapsis is r·k²/(2−k²), so k must stay
         * below sqrt(2) to remain bound at all and below about 1.24 to stay
         * inside the playable cube from this radius. The top of the knob is
         * therefore a very eccentric orbit, not an escape — though winding
         * gravity down while eccentricity is high will still fling the body
         * out, and it respawns, which is a comet and worth keeping. */
        const float e = ecc < 0.f ? 0.f : (ecc > 1.f ? 1.f : ecc);
        v_[1] = vc * (0.35f + 0.89f * e);
        running_ = true;
    }

    void Step(float dt)
    {
        if(!running_ || dt <= 0.f) return;
        /* a = -G r / (|r|² + eps²)^{3/2} */
        const float e2 = soften * soften;
        const float d2 = r_[0] * r_[0] + r_[1] * r_[1] + e2;
        const float inv = 1.f / (d2 * Sqrt(d2));
        const float ax = -gravity * r_[0] * inv;
        const float ay = -gravity * r_[1] * inv;
        v_[0] += ax * dt;
        v_[1] += ay * dt;
        if(damp > 0.f)
        {
            const float k = 1.f - damp * dt;
            v_[0] *= k;
            v_[1] *= k;
        }
        const float sp = Sqrt(v_[0] * v_[0] + v_[1] * v_[1]);
        if(sp > vmax) { const float k = vmax / sp; v_[0] *= k; v_[1] *= k; }
        r_[0] += v_[0] * dt;
        r_[1] += v_[1] * dt;
        /* Anything that wanders far past the playable cube is not coming back
         * on its own within a useful time, so fold it in rather than let the
         * sound sit at a clamped edge. */
        const float far = 1.5f;
        if(r_[0] * r_[0] + r_[1] * r_[1] > far * far) Reset(0.30f, 0.55f);
    }

    /* Add the body's offset to the control frame, in the chosen plane. */
    void Apply(float* p, int n) const
    {
        if(!running_) return;
        int i, j;
        Rotation::PlaneAxes(n, plane, i, j);
        p[i] += r_[0];
        p[j] += r_[1];
    }

    float X() const { return r_[0]; }
    float Y() const { return r_[1]; }
    float Speed() const { return Sqrt(v_[0] * v_[0] + v_[1] * v_[1]); }
    /* How near periapsis the body is, 0 at its slowest and 1 at its fastest.
     * Musically the useful readout: it is high exactly when the timbre is
     * moving fastest. */
    float Rush() const
    {
        const float s = Speed();
        return s <= 0.f ? 0.f : (s > vmax ? 1.f : s / vmax);
    }

private:
    int   n_ = 4;
    float r_[2] = {0.3f, 0.f};
    float v_[2] = {0.f, 0.f};
    bool  running_ = false;
};

} // namespace kyk
