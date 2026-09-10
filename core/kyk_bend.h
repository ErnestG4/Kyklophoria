/* kyk_bend.h — one waveform, and four ways to bend it.
 *
 * Will's note: "we could use some funky tables. Square and saw only table,
 * square manipulated, saw manipulated, etc. Escape the idea of 'all in one'
 * and unlock different worlds."
 *
 * That is the better organising idea and it is worth saying why. Every world
 * before this tried to span a wide range of timbre with four balanced axes,
 * and their axes kept fighting each other — the geometry axis doing eight
 * times the work of the mallet, the interval axis doing eight times the work
 * of the wave. A world that already knows what it is can spend all four axes
 * on manipulating that one thing, and each axis gets room.
 *
 * Three of them, each rooted in something you can name:
 *
 *   Saw     tilt, parity, comb, fold-point
 *   Pulse   duty, tilt, comb, fold-point
 *   Edge    the saw-to-pulse blend, duty, comb, fold-point
 *
 * The manipulations, and why each is representable here:
 *
 *   tilt        the harmonic rolloff law itself, crossfaded between h^-1.6,
 *               h^-1 and h^-0.75. Dark to buzzing, from tables, and it never
 *               stops falling — which is what keeps the crest factor down.
 *   parity      fades the even harmonics out, which walks a saw to a square
 *               without ever leaving the sine basis.
 *   comb        a periodic notch across the harmonic series whose period
 *               sweeps — the spectral shape a phaser makes, except it is
 *               static per frame and moves only when the axis does.
 *   duty        the bipolar pulse, c_h = 2(1-cos(2·pi·h·d))/(pi·h).
 *   fold-point  flips the sign of every harmonic above a moving point.
 *
 * That last one deserves a caveat rather than a boast. It leaves the magnitude
 * spectrum *exactly* unchanged and alters only the waveform, so on a steady
 * tone its direct audibility is limited — this is Ohm's law of phases and it
 * has been true since Helmholtz. What it does change is the shape and the
 * crest factor, which means it changes what a wavefolder or a ring modulator
 * downstream has to chew on, and it is dramatic on the frame display. It is
 * here because a phase-aware representation can offer it and a
 * magnitude-only one cannot, not because it is loud.
 *
 * Nothing in here evaluates a transcendental per harmonic. The pulse and the
 * comb both need cos(2·pi·h·x) at successive integer h, which is a rotation:
 * carry (cos, sin) forward with four multiplies and two adds instead of a
 * table lookup each. Four worlds in this project have now been written the
 * expensive way first.
 */
#pragma once
#include "kyk_shapes.h"

namespace kyk {

enum class Base : uint8_t { Saw = 0, Pulse = 1, Edge = 2 };

struct BendField
{
    Base base = Base::Saw;
    int  n = 4, k = kShapeK;
};

namespace detail {

/* cos(2·pi·h·x) for h = 1..k, by forward rotation. */
struct Spin
{
    float c, s, cd, sd;
    void  Init(float turns)
    {
        SinCosTurns(turns, sd, cd);
        c = cd;                     /* h = 1 */
        s = sd;
    }
    float Cos() const { return c; }
    void  Next()
    {
        const float nc = c * cd - s * sd;
        const float ns = s * cd + c * sd;
        c = nc;
        s = ns;
    }
};

/* +1 below the fold point, -1 above it, smooth across a few harmonics so the
 * axis never steps. A hard switch would be a cliff and tests/cont_check would
 * refuse it. */
inline float FoldSign(float h, float at, float width)
{
    float u = (h - at) / width;
    if(u <= 0.f) return 1.f;
    if(u >= 1.f) return -1.f;
    const float sm = u * u * (3.f - 2.f * u);      /* smoothstep */
    return 1.f - 2.f * sm;
}

} // namespace detail

/* Signed coefficients on the sine basis into mags[0..k-1]. Not normalised. */
inline void BendSpectrum(const BendField& f, float a0, float a1, float a2, float a3,
                         int k, float* mags)
{
    /* Which axis means what depends on the base — that is the point of having
     * three of these rather than one with a mode switch. */
    float duty = 0.5f, blend = 0.f, tilt, parity = 0.f;
    switch(f.base)
    {
        case Base::Saw:   tilt = a0; parity = a1; break;
        case Base::Pulse: duty = 0.5f - 0.22f * a0; tilt = a1; break;
        default:          blend = a0; duty = 0.5f - 0.26f * a1; tilt = 0.5f; break;
    }
    /* Edge trades tilt for the comb: tilt measured 0.35 against the comb's
     * 3.49, and a world with only four axes cannot afford a weak one. */
    const float comb_axis = a2;
    const float fold_axis = a3;

    /* Tilt crossfades between three harmonic laws — h^-1.6, h^-1, h^-0.75 —
     * rather than multiplying by r^h.
     *
     * An exponential tilt was the obvious way and it is a trap. Once r passes
     * one the series stops falling, and a series that does not fall is an
     * impulse: crest 4.07 on that axis alone and 6.76 in the corner with
     * parity, against the 4.3 the output gain allows. Bounding r to 1.01 kept
     * the crest but left the axis measuring 0.41, the weakest of the three
     * worlds. A power law always falls whatever the exponent, so it can be
     * taken far brighter at no cost in crest — and blending three fixed
     * exponents from tables needs no power function per harmonic. */
    const bool  bright = tilt >= 0.5f;
    const float tu = bright ? tilt * 2.f - 1.f : tilt * 2.f;
    /* comb: a notch every P harmonics. Narrowed, because sweeping the period
     * from 2 to 15 reorders every notch at once and measured 3.49 against
     * 0.48 for tilt. */
    const float P = 4.0f + 6.f * (1.f - comb_axis);
    const float fold_at = 2.f + fold_axis * (float)(k - 3);

    detail::Spin pulse, comb;
    if(f.base != Base::Saw) pulse.Init(duty);
    comb.Init(1.f / P);

    const float inv_pi = 1.f / 3.14159265f;
    for(int h = 1; h <= k; h++)
    {
        const float ih = kInvHarm[h - 1];
        const float lo = bright ? ih : kInvHarmSteep[h - 1];
        const float hi = bright ? kInvHarmHalf[h - 1] : ih;
        const float roll = lo + tu * (hi - lo);
        float       c;
        if(f.base == Base::Saw) c = roll;
        else
        {
            const float pw = 2.f * (1.f - pulse.Cos()) * inv_pi * roll;
            c = f.base == Base::Pulse ? pw : roll + blend * (pw - roll);
            pulse.Next();
        }
        /* Parity runs past zero to minus one. At a half the even harmonics
         * vanish and a saw has become a square; past that they come back
         * inverted, which is a different waveform with the same spectrum —
         * the same trick as the fold point, and free here. */
        if(parity > 0.f && (h % 2) == 0) c *= 1.f - 2.f * parity;
        c *= 1.f - 0.55f * comb.Cos();
        comb.Next();
        mags[h - 1] = c * detail::FoldSign((float)h, fold_at, 7.f);
    }
}

/* Padded to twelve. The host's parseBasis refuses anything shorter than ten
 * bytes — a guard that predates these small worlds — so an eight-byte blob
 * parsed as null and the world silently lost its terrain. */
constexpr int kBendBlobBytes = 12;

inline int BendBlob(const BendField& f, uint8_t* out)
{
    out[0] = 0xFFu;
    out[1] = 10u;                   /* World::Kind::Bend */
    out[2] = (uint8_t)f.n;
    out[3] = (uint8_t)f.k;
    out[4] = (uint8_t)f.base;
    for(int i = 5; i < kBendBlobBytes; i++) out[i] = 0;
    return kBendBlobBytes;
}

} // namespace kyk
