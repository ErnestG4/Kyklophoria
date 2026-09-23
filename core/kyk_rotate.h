/* kyk_rotate.h — N-dimensional rotation of the control frame.
 *
 * R is the product of Givens rotations, one per 2-plane (i,j), i<j, in
 * lexicographic plane order: p = G_{P-1}(θ_{P-1}) · … · G_0(θ_0) · c.
 * Angles are in *turns* (1.0 = 2π) so a quarter turn is exact in float and
 * the sine table indexes without a multiply by π. Trig comes from the shared
 * table with linear interpolation: deterministic on every target, smooth
 * enough for slow orbits (2π/2048 steps, interpolated).
 *
 * The rotation pivots about `pivot` on every axis (0.5 = the centre of a
 * clamped/wrapped lattice, 0 = the origin for a sphere direction). With all
 * angles zero IsIdentity() is true and callers skip Apply(), which keeps the
 * unrotated path bit-identical (spec §7, rotation identity test).
 */
#pragma once
#include "kyk_types.h"
#include "kyk_tables.h"

namespace kyk {

/* sin/cos of t turns, table-interpolated; exact at multiples of 1/2048. */
inline void SinCosTurns(float turns, float& s, float& c)
{
    const float x  = Fract(turns) * (float)kTableSize;
    int         i  = (int)x;
    if(i >= kTableSize) i = kTableSize - 1;
    const float f  = x - (float)i;
    const int   i1 = (i + 1) & (kTableSize - 1);
    const int   q  = kTableSize / 4;
    const float s0 = kSinTable[i], s1 = kSinTable[i1];
    const float c0 = kSinTable[(i + q) & (kTableSize - 1)], c1 = kSinTable[(i1 + q) & (kTableSize - 1)];
    if(f == 0.f) { s = s0; c = c0; return; }
    s = s0 + (s1 - s0) * f;
    c = c0 + (c1 - c0) * f;
    /* the chord sits inside the circle by up to 1.2e-6; one Newton step of
     * 1/sqrt puts (s,c) back on it so long products stay orthonormal */
    const float k = 0.5f * (3.f - (s * s + c * c));
    s *= k;
    c *= k;
}

class Rotation
{
public:
    static constexpr int PlaneCount(int n) { return n * (n - 1) / 2; }

    /* plane index → axes, lexicographic: (0,1),(0,2),…,(0,n-1),(1,2),… */
    static void PlaneAxes(int n, int plane, int& i, int& j)
    {
        int p = 0;
        for(int a = 0; a < n; a++)
            for(int b = a + 1; b < n; b++)
            {
                if(p == plane) { i = a; j = b; return; }
                p++;
            }
        i = 0; j = 1;
    }

    void Init(int n)
    {
        n_ = n < 1 ? 1 : (n > kMaxN ? kMaxN : n);
        for(int p = 0; p < kMaxPlanes; p++) { angle_[p] = 0.f; base_[p] = 0.f; orbit_[p] = 0.f; rate_[p] = 0.f; }
        base_off_ = false;
        couple_ = 0.f; lock_ = 0.f; reach_ = 5; relock_ = true;
        SetIdentity();
        dirty_ = false;
    }

    int   N() const { return n_; }
    int   Planes() const { return PlaneCount(n_); }
    float Angle(int plane) const { return angle_[plane]; }
    const float* Angles() const { return angle_; }

    /* The *static* part of a plane's angle, as a knob sets it. The effective
     * angle is this plus whatever the orbit has accumulated, which is why the
     * two are stored separately: the shell writes this every block from the
     * pot, and if the orbit shared the same variable each write would wipe out
     * the motion. That is exactly the bug the first orbit build shipped with. */
    void SetAngle(int plane, float turns)
    {
        if(plane < 0 || plane >= Planes()) return;
        base_[plane] = Fract(turns);
        Recompute(plane);
    }
    float BaseAngle(int plane) const { return plane >= 0 && plane < kMaxPlanes ? base_[plane] : 0.f; }
    /* The knobs' angles left out of the rotation, the orbit's kept: a
       resonate world's four axes are body, velocity, decay and coil, and a
       Rotate page left turned from a wavetable crossed them (Combust: "knobs
       are crossed"; a quarter turn swaps two outright). Straight by default,
       moved only by an orbit or Kepler the player runs. The knob values are
       kept, so a wavetable world gets its angles back */
    void BaseOff(bool off)
    {
        if(base_off_ == off) return;
        base_off_ = off;
        for(int p = 0; p < Planes(); p++) Recompute(p);
    }
    bool IsBaseOff() const { return base_off_; }
    /* Just the accumulated orbit phase, without the knob. */
    float OrbitPhase(int plane) const { return plane >= 0 && plane < kMaxPlanes ? orbit_[plane] : 0.f; }
    /* Nudge the orbit phase by hand. */
    void AddAngle(int plane, float dturns)
    {
        if(plane < 0 || plane >= Planes()) return;
        orbit_[plane] = Fract(orbit_[plane] + dturns);
        Recompute(plane);
    }

    /* ── Orbit ──────────────────────────────────────────────────────────
     * A rate per plane, in turns per second, signed. With one plane turning
     * a CV traces a circle; with two at unrelated rates the path is
     * quasi-periodic and never closes, which is the whole reason for having
     * more than one rotation plane. Rational rate ratios close the figure.
     *
     * Advance() is called once per block, so the angle resolution is the
     * block period: at 48 kHz / 24 samples a rate of one turn per second
     * moves 5e-4 of a turn per block, well under the sine table's 2^-11
     * quantum, so the motion is smooth rather than stepped. */
    void  SetRate(int plane, float turns_per_sec)
    {
        if(plane < 0 || plane >= kMaxPlanes) return;
        if(rate_[plane] != turns_per_sec) { rate_[plane] = turns_per_sec; relock_ = true; }
    }
    float Rate(int plane) const { return plane >= 0 && plane < kMaxPlanes ? rate_[plane] : 0.f; }
    const float* Rates() const { return rate_; }

    bool Orbiting() const
    {
        for(int p = 0; p < Planes(); p++) if(rate_[p] != 0.f) return true;
        return false;
    }

    /* ── Kuramoto coupling ──────────────────────────────────────────────
     * With coupling at zero the planes are independent and the path is
     * quasi-periodic: it never closes, which is the wash. Turning coupling up
     * lets each pair of orbits pull on the other, and the figure starts to
     * close by itself.
     *
     * Plain Kuramoto would be the wrong thing here. It drags every oscillator
     * to one common frequency and one common phase, which collapses all six
     * planes into a single rotation and throws away the reason for having
     * more than one. What we want is the *ratio* to lock — two planes at
     * three against two, closing a figure — because that is the moment Will
     * described, where the waveform snaps into a familiar shape.
     *
     * So the coupling is on a harmonic combination rather than the plain
     * phase difference. For each pair, the nearest simple rational p/q to
     * their rate ratio is found once (only when a rate knob moves), and the
     * pair is coupled through psi = q·theta_i − p·theta_j, which is stationary
     * exactly at that ratio. The pair equations reduce to
     *
     *     d(psi)/dt = (q·w_i − p·w_j) − K·sin(2*pi*psi)
     *
     * so there is a fixed point — a lock — whenever the ratio detuning is
     * within K. That is an Arnold tongue, and it behaves like one: the
     * coupling knob widens every tongue at once, and a complex ratio like 5:3
     * has a narrower tongue than 2:1 for free, because its detuning term
     * carries the larger integers. Sweep the rate knob with coupling up and
     * the orbit does not glide, it steps between simple ratios.
     *
     * Cost is one table sine per active pair per block: fifteen at N=4. */
    void SetCouple(float k) { couple_ = k < 0.f ? 0.f : (k > 8.f ? 8.f : k); }
    float Couple() const { return couple_; }
    /* The largest integer allowed in p/q, 1 to 5. At 1 the only lock is
     * unison and the orbits simply gang up. At 2 you get the octave and the
     * fifth-ish 2:1 and 1:2 and nothing else, which is broad and obvious. At 5
     * the full set is available and the sweep steps through a dense staircase.
     * A musical control, not a tuning parameter: it says how exotic a ratio
     * the orbits are willing to settle on. */
    void SetReach(int r)
    {
        const int v = r < 1 ? 1 : (r > 5 ? 5 : r);
        if(v != reach_) { reach_ = v; relock_ = true; }
    }
    int Reach() const { return reach_; }
    /* How closed the figure is, 0 to 1 — the one number worth putting in
     * front of a player.
     *
     * The measure is the smoothed mean of cos(2*pi*psi), not |cos|. At a
     * stable lock psi settles at a fixed point where sin(2*pi*psi) = detuning
     * over K, and stability puts that point on the branch where the cosine is
     * positive; small detuning holds it near 1. Unlocked, psi drifts through
     * every phase and the cosine averages to zero. Taking the absolute value
     * instead would read 1 at the unstable anti-phase too, and would read 0.64
     * rather than 0 for a freely drifting pair — a readout that never says no.
     * Smoothed over about a third of a second, because the raw value swings
     * through a full cycle every time an unlocked pair laps. */
    float Lock() const { return lock_ < 0.f ? 0.f : (lock_ > 1.f ? 1.f : lock_); }

    void Advance(float dt_seconds)
    {
        const int P = Planes();
        if(couple_ <= 0.f)
        {
            lock_ = 0.f;
            for(int p = 0; p < P; p++)
                if(rate_[p] != 0.f)
                {
                    orbit_[p] = Fract(orbit_[p] + rate_[p] * dt_seconds);
                    Recompute(p);
                }
            return;
        }
        if(relock_) Relock();

        float pull[kMaxPlanes];
        for(int p = 0; p < P; p++) pull[p] = 0.f;
        float lock = 0.f;
        int   pairs = 0;
        const float norm = P > 1 ? 1.f / (float)(P - 1) : 1.f;
        for(int i = 0; i < P; i++)
        {
            if(rate_[i] == 0.f) continue;
            for(int j = i + 1; j < P; j++)
            {
                if(rate_[j] == 0.f) continue;
                const int   pn = num_[i][j], qn = den_[i][j];
                const float psi = (float)qn * orbit_[i] - (float)pn * orbit_[j];
                float       sn, cs;
                SinCosTurns(psi, sn, cs);
                const float g = couple_ * sn / (float)(pn * pn + qn * qn);
                pull[i] -= (float)qn * g;
                pull[j] += (float)pn * g;
                lock += cs;
                pairs++;
            }
        }
        const float inst = pairs ? lock / (float)pairs : 0.f;
        float a = dt_seconds * (1.f / 0.3f);
        if(a > 1.f) a = 1.f;
        lock_ += (inst - lock_) * a;
        for(int p = 0; p < P; p++)
            if(rate_[p] != 0.f)
            {
                orbit_[p] = Fract(orbit_[p] + (rate_[p] + pull[p] * norm) * dt_seconds);
                Recompute(p);
            }
    }

    /* Park every orbit back where it started without touching the knobs. */
    void ResetOrbit()
    {
        for(int p = 0; p < kMaxPlanes; p++) orbit_[p] = 0.f;
        for(int p = 0; p < Planes(); p++) Recompute(p);
    }

    bool IsIdentity() const
    {
        for(int p = 0; p < Planes(); p++) if(angle_[p] != 0.f) return false;
        return true;
    }

    /* Rebuild the matrix if an angle changed. Call once per block. */
    void Update()
    {
        if(!dirty_) return;
        SetIdentity();
        for(int p = 0; p < Planes(); p++)
        {
            if(angle_[p] == 0.f) continue;
            int i, j;
            PlaneAxes(n_, p, i, j);
            float s, c;
            SinCosTurns(angle_[p], s, c);
            /* m ← G(i,j,θ) · m : rows i and j mix */
            for(int k = 0; k < n_; k++)
            {
                const float ri = m_[i][k], rj = m_[j][k];
                m_[i][k]       = c * ri - s * rj;
                m_[j][k]       = s * ri + c * rj;
            }
        }
        dirty_ = false;
    }

    /* p = pivot + R · (c − pivot) */
    void Apply(const float* c, float* p, float pivot) const
    {
        float d[kMaxN];
        for(int k = 0; k < n_; k++) d[k] = c[k] - pivot;
        for(int r = 0; r < n_; r++)
        {
            float acc = 0.f;
            for(int k = 0; k < n_; k++) acc += m_[r][k] * d[k];
            p[r] = pivot + acc;
        }
    }

    /* One Givens rotation in plane (i,j) by `turns`, in place, about pivot.
     * Used for the stereo spread (applied after the full rotation). */
    static void RotatePlane(int i, int j, float turns, float* p, float pivot)
    {
        float s, c;
        SinCosTurns(turns, s, c);
        const float x = p[i] - pivot, y = p[j] - pivot;
        p[i]          = pivot + (c * x - s * y);
        p[j]          = pivot + (s * x + c * y);
    }

    const float* Row(int r) const { return m_[r]; }

private:
    void SetIdentity()
    {
        for(int r = 0; r < kMaxN; r++)
            for(int k = 0; k < kMaxN; k++) m_[r][k] = (r == k) ? 1.f : 0.f;
    }

    void Recompute(int plane)
    {
        const float a = Fract((base_off_ ? 0.f : base_[plane]) + orbit_[plane]);
        if(a != angle_[plane]) { angle_[plane] = a; dirty_ = true; }
    }

    int   n_ = 4;
    /* Coprime p/q with both under six: the ratios an ear hears as a ratio.
     * Beyond this the tongue is so narrow that nothing captures anyway. */
    void Relock()
    {
        static const int8_t kP[] = {1,1,1,1,1,2,2,2,3,3,3,3,4,4,4,5,5,5,5};
        static const int8_t kQ[] = {1,2,3,4,5,1,3,5,1,2,4,5,1,3,5,1,2,3,4};
        const int P = Planes();
        for(int i = 0; i < P; i++)
            for(int j = i + 1; j < P; j++)
            {
                num_[i][j] = 1; den_[i][j] = 1;
                if(rate_[i] == 0.f || rate_[j] == 0.f) continue;
                const float r  = rate_[i] / rate_[j];
                const float ar = r < 0.f ? -r : r;
                float bestd = 1e30f;
                int   bp = 1, bq = 1;
                for(int t = 0; t < 19; t++)
                {
                    if(kP[t] > reach_ || kQ[t] > reach_) continue;
                    const float d0 = ar - (float)kP[t] / (float)kQ[t];
                    const float d  = d0 < 0.f ? -d0 : d0;
                    /* strictly better wins; the table runs simplest-first so
                     * a tie keeps the simpler ratio */
                    if(d < bestd - 1e-6f) { bestd = d; bp = kP[t]; bq = kQ[t]; }
                }
                num_[i][j] = (int8_t)(r < 0.f ? -bp : bp);
                den_[i][j] = (int8_t)bq;
            }
        relock_ = false;
    }

    float   couple_ = 0.f;
    float   lock_   = 0.f;
    int     reach_  = 5;
    bool    relock_ = true;
    int8_t  num_[kMaxPlanes][kMaxPlanes] = {};
    int8_t  den_[kMaxPlanes][kMaxPlanes] = {};
    float angle_[kMaxPlanes];    /* effective = base + orbit */
    float base_[kMaxPlanes];
    float orbit_[kMaxPlanes];
    bool  base_off_ = false;
    float rate_[kMaxPlanes];
    float m_[kMaxN][kMaxN];
    bool  dirty_ = false;
};

} // namespace kyk
