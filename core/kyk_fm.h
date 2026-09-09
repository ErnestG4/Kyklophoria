/* kyk_fm.h — a world made of frequency modulation.
 *
 * Every legible family in this instrument so far weights the harmonic series
 * multiplicatively: a tilt, a width, a formant bump. Multiplicative weights
 * *add* in log magnitude, which makes those spaces separable by construction,
 * and a separable space is lopsided by construction too — the isotropy
 * measurement put them between 6x and 11x. Rotating through a separable space
 * is close to pointless, because the axes were already the natural ones.
 *
 * FM is the exception, and it is the reason this file exists. The amplitude of
 * the k'th sideband is J_k(I), a Bessel function of the modulation index, and
 * Bessel functions do not factor: J_k(I) is not any product of a function of k
 * with a function of I. Worse than that for separability, and better for us,
 * the *positions* of the sidebands move with the ratio, so changing one axis
 * relocates energy that another axis put there. Two axes cannot be undone
 * independently. That is what a rotation is supposed to find.
 *
 * The four axes:
 *
 *   index      how far out the sidebands reach, 0 to 16. At 0 the spectrum is
 *              a single partial and the whole world collapses to a sine,
 *              which is a useful corner to have.
 *   ratio      modulator over carrier, 0.5 to 2.5, warped so the axis lingers
 *              at whole numbers. Whole ratios give a harmonic spectrum; the
 *              ground between them is inharmonic and smears across two bins,
 *              which is the metallic FM sound rather than a defect.
 *   carrier    which harmonic the sideband cluster is centred on, 1 to 8.
 *   second     a second carrier at an offset from the first, 0 to 9. Its
 *              sidebands land among the first carrier's and interfere: the
 *              deposit is signed, so they cancel, and where they cancel the
 *              spectrum has a notch that moves when any of the other three
 *              axes move. This is the axis that does most of the coupling.
 *
 * Cost is one Bessel recurrence and two deposit loops per evaluation, which
 * measured cheaper than the eigenspace's 64 exponentials.
 */
#pragma once
#include "kyk_types.h"
#include "kyk_rotate.h"   /* SinCosTurns, for the ratio warp */

namespace kyk {

/* Where the four axes reach. Defaults are the built-in FM world. */
struct FmField
{
    /* Chosen by measurement, not taste. Swept over three values of each of
     * the four reaches, the direction spread across the space ran from 1.81x
     * to well past 4x, and the whole cluster below 1.9x had the ratio axis
     * capped near 2.5 — because the ratio moves every sideband at once and
     * left wide it does five times the work of any other axis, which is what
     * lopsided means. These four are the best corner found. */
    float index_max   = 16.0f;
    float ratio_min   = 0.5f, ratio_max = 2.5f;
    float carrier_min = 1.0f, carrier_max = 8.0f;
    float second_max  = 9.0f;
    /* How much the ratio axis lingers at whole numbers, 0 to just under 1.
     *
     * A whole-number ratio gives a harmonic spectrum and anything between
     * gives an inharmonic one, so this axis crosses back and forth between
     * "a note" and "a bell" several times. Left linear it crosses them at a
     * constant rate and the harmonic ones are just points you pass through.
     * Warped, the axis slows to a crawl at each whole number and hurries
     * through the ground between: you catch the waveform locking into a
     * familiar shape, and as soon as you cross it you are back in the mire.
     *
     * The warp is r − (w/2π)·sin(2πr), whose slope is 1 − w·cos(2πr): that is
     * 1 − w at a whole number and 1 + w halfway between. It is monotone for
     * any w below 1, so the axis never doubles back, and being a
     * reparametrisation it redistributes the space's variety rather than
     * removing any. */
    float ratio_lock  = 0.75f;
    int   n = 4, k = 64;
};

namespace detail {

/* J_0..J_nmax of x, by Miller's downward recurrence.
 *
 * The upward recurrence is the obvious one and it is useless: J_n falls off
 * faster than the recurrence's other solution grows, so rounding error in
 * J_0 and J_1 swamps the answer within a few orders. Downward is stable for
 * the same reason — the unwanted solution decays as you go — so you start at
 * an order well above anything you want, with an arbitrary seed, run down,
 * and fix the scale at the end from the identity
 *
 *     J_0(x) + 2·J_2(x) + 2·J_4(x) + … = 1
 *
 * which is what makes the arbitrary seed legitimate. Values are rescaled on
 * the way down if they threaten to overflow. */
inline void BesselJ(float x, int nmax, float* J)
{
    for(int n = 0; n <= nmax; n++) J[n] = 0.f;
    if(x < 1e-5f) { J[0] = 1.f; return; }
    /* start high enough that the decaying solution has died by the time we
     * reach the orders we keep; the 4·sqrt(x) term covers large arguments */
    const int   M   = nmax + 20 + (int)(4.f * Sqrt(x));
    const float inx = 2.f / x;
    float       bjp = 0.f, bj = 1e-20f, norm = 0.f;
    for(int n = M; n >= 1; n--)
    {
        const float bjm = inx * (float)n * bj - bjp;
        bjp = bj;
        bj  = bjm;
        const float a = bj < 0.f ? -bj : bj;
        if(a > 1e10f)
        {
            const float s = 1e-10f;
            bj *= s; bjp *= s; norm *= s;
            for(int i = 0; i <= nmax; i++) J[i] *= s;
        }
        const int m = n - 1;                 /* bj is now J_m, unnormalised */
        if(m <= nmax) J[m] = bj;
        if((m & 1) == 0) norm += (m == 0) ? bj : 2.f * bj;
    }
    const float inv = norm != 0.f ? 1.f / norm : 0.f;
    for(int n = 0; n <= nmax; n++) J[n] *= inv;
}

/* Highest sideband order worth computing. J_k(I) is negligible once k runs
 * much past I, so this is generous rather than tight. */
constexpr int kFmOrders = 48;

} // namespace detail

/* Build the magnitude spectrum of a two-carrier FM pair into mags[0..k-1],
 * harmonic 1 at index 0. Not normalised; the caller does that.
 *
 * A sideband sits at carrier + order·ratio, which is not generally an integer
 * harmonic. Rather than quantise it — which would step, and step audibly —
 * its amplitude is split between the two bins it falls between. So the ratio
 * axis is continuous everywhere, and an inharmonic ratio reads as a partial
 * smeared over a neighbouring pair rather than as a jump.
 *
 * Deposits are signed, and negative frequencies fold back with their sign
 * inverted, exactly as they do in the real thing. That is where FM's notches
 * come from, and the notches are the interesting part: they move when any
 * axis moves, which is precisely the coupling that a separable world cannot
 * have. */
/* The four axes, as the world reads them. */
struct FmPoint { float index, ratio, carrier, second; };

/* Map a folded coordinate onto the four FM controls, warp included. Kept
 * here rather than in the World so that everything that knows what an FM
 * axis means lives in one file. */
inline FmPoint FmAt(const FmField& f, const float* p01)
{
    FmPoint q;
    q.index   = p01[0] * f.index_max;
    q.ratio   = f.ratio_min + p01[1] * (f.ratio_max - f.ratio_min);
    if(f.ratio_lock > 0.f)
    {
        float sn, cs;
        SinCosTurns(q.ratio, sn, cs);       /* angles are turns, so this is sin(2*pi*r) */
        q.ratio -= f.ratio_lock * (1.f / 6.2831853f) * sn;
    }
    q.carrier = f.carrier_min + p01[2] * (f.carrier_max - f.carrier_min);
    q.second  = f.n > 3 ? p01[3] * f.second_max : 0.f;
    return q;
}

inline void FmSpectrum(float index, float ratio, float carrier, float second,
                       int k, float* mags)
{
    for(int i = 0; i < k; i++) mags[i] = 0.f;
    float J[detail::kFmOrders + 1];
    detail::BesselJ(index, detail::kFmOrders, J);

    const float cs[2] = {carrier, carrier + second};
    for(int c = 0; c < 2; c++)
    {
        for(int ord = -detail::kFmOrders; ord <= detail::kFmOrders; ord++)
        {
            const int a = ord < 0 ? -ord : ord;
            float     amp = J[a];
            if(ord < 0 && (a & 1)) amp = -amp;      /* J_-n = (-1)^n J_n */
            const float mag = amp < 0.f ? -amp : amp;
            if(mag < 1e-4f) continue;
            float f = cs[c] + (float)ord * ratio;
            if(f < 0.f) { f = -f; amp = -amp; }     /* reflected, sign flipped */
            const float h  = f - 1.f;               /* harmonic 1 lives at 0 */
            /* Floor, not truncate, and no cutoff near DC.
             *
             * A sideband between DC and the fundamental has h in [-1, 0), so
             * flooring puts it between bin -1, which does not exist and is
             * discarded, and bin 0, which gets the rest. Its contribution
             * then fades to nothing as it approaches DC, continuously.
             *
             * The first version dropped anything below half a harmonic
             * outright, which cost a factor of thirty in morph smoothness:
             * a full-strength first sideband crossing that line vanished in
             * one render, a step of 0.25 in normalised spectrum distance
             * where every other world's worst was 0.008. Truncating instead
             * of flooring would have been worse still, extrapolating past
             * the fundamental rather than toward a bin we do not have. */
            int         i0 = (int)h;
            if(h < 0.f && (float)i0 != h) i0 -= 1;
            const float fr = h - (float)i0;
            if(i0 >= 0 && i0 < k) mags[i0] += amp * (1.f - fr);
            if(i0 + 1 >= 0 && i0 + 1 < k) mags[i0 + 1] += amp * fr;
        }
    }
    for(int i = 0; i < k; i++) if(mags[i] < 0.f) mags[i] = -mags[i];
}

/* The whole world as 32 bytes, for the page to evaluate itself.
 *
 * GET_BASIS already ships an analytic world's formula to the host so the
 * display can draw the terrain instead of asking the module for it a point at
 * a time. An eigenspace's blob starts with its dimension count, 1 to 6, so a
 * leading 0xFF marks this as a different shape of formula rather than a
 * malformed one — an older page reads 255 dimensions, rejects it, and simply
 * draws no terrain, which is what it already does for a tabulated world.
 *
 * Sending the reaches rather than hardcoding them in the page is the point:
 * retune the four axes here and the drawing follows without anyone
 * remembering to edit two files. */
constexpr int kFmBlobBytes = 4 + 7 * 4;

inline int FmBlob(const FmField& f, uint8_t* out)
{
    out[0] = 0xFFu;                 /* not a dimension count */
    out[1] = 4u;                    /* World::Kind::Fm */
    out[2] = (uint8_t)f.n;
    out[3] = (uint8_t)f.k;
    const float v[7] = {f.index_max, f.ratio_min, f.ratio_max,
                        f.carrier_min, f.carrier_max, f.second_max, f.ratio_lock};
    for(int i = 0; i < 7; i++) std::memcpy(out + 4 + 4 * i, &v[i], 4);
    return kFmBlobBytes;
}

} // namespace kyk
