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
        for(int p = 0; p < kMaxPlanes; p++) angle_[p] = 0.f;
        SetIdentity();
        dirty_ = false;
    }

    int   N() const { return n_; }
    int   Planes() const { return PlaneCount(n_); }
    float Angle(int plane) const { return angle_[plane]; }
    const float* Angles() const { return angle_; }

    void SetAngle(int plane, float turns)
    {
        if(plane < 0 || plane >= Planes()) return;
        turns = Fract(turns);
        if(turns != angle_[plane]) { angle_[plane] = turns; dirty_ = true; }
    }
    /* Advance a plane's angle (orbit LFOs, M2). */
    void AddAngle(int plane, float dturns) { SetAngle(plane, angle_[plane] + dturns); }

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

    int   n_ = 4;
    float angle_[kMaxPlanes];
    float m_[kMaxN][kMaxN];
    bool  dirty_ = false;
};

} // namespace kyk
