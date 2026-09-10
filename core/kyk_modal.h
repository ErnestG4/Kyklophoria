/* kyk_modal.h — struck objects, harmonicised.
 *
 * Worlds baked from a physical model. The model runs here as a formula, never
 * as an integrator: given a geometry and a strike, it says which modes exist
 * and how loud each one is, and that is a spectrum. Nothing in the audio path
 * simulates anything.
 *
 * The frequencies cannot survive intact. A frame periodic at f0 holds integer
 * harmonics and nothing else, and real modal ratios are nowhere near that — an
 * ideal bar is 8.9% off the grid at its worst and a circular membrane 25.5%,
 * which is over four semitones. So these are *tuned* objects, in the sense a
 * vibraphone maker means it: the mode ratios are pulled onto rational ones on
 * purpose.
 *
 * That is a smaller loss than it sounds, because the frequencies are not where
 * a struck object gets its character. Strike position, geometry and mallet all
 * live in *which modes are loud*, and that is exactly representable. What you
 * lose is the clang; what you keep is the hand.
 *
 * Three things this had to get right, each of which cost a measurement:
 *
 *   Deposit, never round.  Rounding a mode to its nearest harmonic makes it
 *                          hop bins as the geometry moves. That measured 6.0
 *                          variety and three quarters of it was clicks.
 *                          Splitting each mode across the two bins it falls
 *                          between — as the FM and Unison worlds do — brings
 *                          it to 1.45 with a worst step of 0.0025.
 *
 *   A decay axis after all. An earlier version had no time axis, on the
 *                          measured grounds that time-since-strike and mallet
 *                          hardness are the same direction in spectrum space.
 *                          That measurement was taken in a spectrum holding
 *                          ninety percent of its energy in under two partials,
 *                          where every axis looks small and any two look
 *                          alike. With a real spectrum to act on they are not
 *                          alike, and Will heard it before the numbers did:
 *                          the plate and the drum were "a mild wiggle on the
 *                          sine". A struck thing needs time.
 *
 *   Geometry is the        Changing which modes *exist* moves the spectrum far
 *   strong axis.           more than tilting the ones that do, because modes
 *                          reorder and collide. It measured 2.58 against 0.55
 *                          for the mallet.
 *
 * So the axes are the ones an exciter-plus-resonator actually has: where it
 * was struck, what shape it is, how long ago, and how the damping depends on
 * the mode. The last two are a pair rather than two separate lowpasses —
 * damping alone does nothing at time zero, and time alone does nothing under
 * uniform damping, but together they sweep a family of decay *shapes* rather
 * than a family of amounts. exp(-c·n^p) with t setting c and the damping axis
 * setting p.
 *
 * Nothing here decays in real time. The spectrum is what the object looks like
 * at one instant, and the axis moves that instant.
 */
#pragma once
#include "kyk_shapes.h"

namespace kyk {

enum class Body : uint8_t { Plate = 0, Bar = 1, Drum = 2 };

struct ModalField
{
    Body  body = Body::Plate;
    float geom_min = 0.62f, geom_max = 2.9f;   /* side ratio, or taper */
    /* Vestigial: the mallet is fixed at a hard 0.03 now that time has the
     * axis it used to hold. Kept because the blob carries them and the page
     * reads the layout; the width they describe is not used. Worth recording
     * why the knob went: a mallet is a lowpass on the mode set — sinc(n·w)
     * has its first zero at n = 1/w — so at the middle of its old range only
     * six modes survived and the drum held ninety percent of its energy in
     * under two partials. A sine with a wiggle, and audibly so. */
    float width_min = 0.008f, width_max = 0.12f;
    int   n = 4, k = kShapeK;
};

namespace detail {

/* Ideal free-free bar, and a circular membrane. Both are ratios to the first
 * mode, and both are hopelessly inharmonic, which is the point of saying so
 * out loud rather than hiding it behind a nice-sounding table. */
inline const float* BarRatios(int& n)
{
    static const float r[11] = {1.f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f,
                                24.80f, 31.80f, 39.80f, 48.70f, 58.50f};
    n = 11;
    return r;
}
inline const float* DrumRatios(int& n)
{
    /* Ordered Bessel zeros over the first. Twelve of them reached only 4.15,
     * so every mode of the drum landed between harmonics one and four and the
     * world could not be bright at any setting. A membrane really does have
     * its modes packed low, but it has a great many more of them than twelve. */
    static const float r[28] = {
        1.000f, 1.593f, 2.135f, 2.295f, 2.653f, 2.917f, 3.155f, 3.500f,
        3.598f, 3.647f, 4.060f, 4.154f, 4.230f, 4.601f, 4.832f, 4.903f,
        5.412f, 5.428f, 5.579f, 5.651f, 5.976f, 6.130f, 6.156f, 6.442f,
        6.575f, 6.729f, 7.015f, 7.144f};
    n = 28;
    return r;
}

/* (i+1)^(-0.3), for i up to 64. The mode-amplitude law, as a table rather
 * than a power: a strike is close to an impulse and excites the modes roughly
 * equally, so this is a gentle tilt and not worth a series evaluation each. */
inline const float kQuarter[64] = {
    1.000000f, 0.812252f, 0.719223f, 0.659754f, 0.617034f, 0.584191f, 0.557790f, 0.535887f,
    0.517282f, 0.501187f, 0.487060f, 0.474510f, 0.463252f, 0.453066f, 0.443785f, 0.435275f,
    0.427430f, 0.420163f, 0.413403f, 0.407091f, 0.401175f, 0.395615f, 0.390375f, 0.385422f,
    0.380731f, 0.376277f, 0.372041f, 0.368004f, 0.364150f, 0.360465f, 0.356937f, 0.353553f,
    0.350305f, 0.347181f, 0.344175f, 0.341279f, 0.338485f, 0.335788f, 0.333181f, 0.330660f,
    0.328220f, 0.325856f, 0.323563f, 0.321340f, 0.319180f, 0.317083f, 0.315044f, 0.313060f,
    0.311129f, 0.309249f, 0.307418f, 0.305632f, 0.303891f, 0.302191f, 0.300532f, 0.298912f,
    0.297329f, 0.295782f, 0.294269f, 0.292789f, 0.291341f, 0.289923f, 0.288535f, 0.287175f};

/* sin(pi·x)/(pi·x), from the shared table. One divide, which is the only one
 * in here. */
inline float Sinc(float x)
{
    const float a = x < 0.f ? -x : x;
    if(a < 1e-4f) return 1.f;
    float sn, cs;
    SinCosTurns(0.5f * x, sn, cs);        /* turns: x/2 turn = pi·x radians */
    return sn / (3.14159265f * x);
}

} // namespace detail

/* Deposit one mode at fractional harmonic `fh` with amplitude `a`. Split
 * across the two bins it falls between, and run past the top so the share in
 * the last real bin fades out instead of being cut off at full weight. */
inline void ModalDeposit(float fh, float a, int k, float* mags)
{
    if(a == 0.f || fh <= 0.f || fh > (float)(k + 1)) return;
    const float h  = fh - 1.f;
    int         i0 = (int)h;
    if(h < 0.f && (float)i0 != h) i0 -= 1;
    const float fr = h - (float)i0;
    if(i0 >= 0 && i0 < k) mags[i0] += a * (1.f - fr);
    if(i0 + 1 >= 0 && i0 + 1 < k) mags[i0 + 1] += a * fr;
}

/* Magnitudes into mags[0..k-1]. Not normalised. */
/* The fourth axis means something different per body, which is the honest
 * answer rather than a compromise. A plate is struck at a *point*, so it has
 * two strike coordinates and the second is a real, independent control — the
 * mode grid is two-dimensional and each coordinate picks out a different comb.
 * A bar and a drum head are one-dimensional in this model and have no second
 * coordinate to give, so they get temper instead. Forcing temper onto the
 * plate as well measured 0.04 against 1.6 for its geometry: a dead knob. */
/* Per-mode damping. `p` runs from uniform, where every mode fades together
 * and the normalised spectrum does not change at all, to steep, where the top
 * of the mode set is gone while the bottom still rings. The shape term is
 * what keeps this off the mallet's axis: a mode that is elongated — one index
 * much larger than the other — couples to the boundary differently from a
 * square one, so two modes at the same frequency can damp differently. That
 * cannot be written as a filter on frequency, which is exactly the point. */
/* No transcendentals, on purpose.
 *
 * The obvious way to write "damping rises with frequency to a power" is
 * freq^p, and the obvious way to decay is exp(-a·t). Between them that is two
 * series evaluations per mode, and the plate has sixty-four modes: it measured
 * 5.46 us an evaluation, more than the transform it feeds. This project has
 * now made that mistake in the vowel world, the shape tables, the unison world
 * and here, so it is worth writing down what the cheap version is.
 *
 * The power becomes a crossfade between three exponents that need no
 * arithmetic at all — sqrt(f), f, f² — which covers the same range of decay
 * *shapes* the exponent did, and shapes are the point.
 *
 * The decay becomes 1/(1 + a·t) rather than exp(-a·t). Both are smooth,
 * monotone, one at t = 0 and zero at infinity; as a spectral tilt they are
 * indistinguishable, and the rational one is a single divide. Real damping is
 * not a clean exponential across modes anyway. */
inline float ModalDamp(float t, float p, int m, int n2)
{
    if(t <= 0.f) return 1.f;
    const float fm = (float)m, fn = (float)n2;
    const float f2 = (fm * fm + fn * fn) * 0.5f;                /* 1 at (1,1) */
    const float freq = Sqrt(f2);
    float       pw;
    if(p < 0.5f) { const float u = p * 2.f; pw = Sqrt(freq) + u * (freq - Sqrt(freq)); }
    else         { const float u = p * 2.f - 1.f; pw = freq + u * (f2 - freq); }
    const float shape = 1.f + 0.5f * ((fm / fn) + (fn / fm) - 2.f);
    /* Relative to the fundamental, which never decays. The spectrum is
     * normalised downstream, so only the difference between modes can survive
     * — and writing it as a difference keeps the numbers sane. The absolute
     * version drove everything but the first partial to silence by the middle
     * of the knob: ninety-three percent of the energy in harmonic one, 1.4
     * partials holding ninety percent. Which is the very thing the time axis
     * was added to fix. */
    const float a = 0.9f * (pw * shape - 1.f);
    if(a <= 0.f) return 1.f;
    return 1.f / (1.f + a * t);
}

inline void ModalSpectrum(const ModalField& f, float strike, float geom,
                          float time, float damp, int k, float* mags)
{
    for(int i = 0; i < k; i++) mags[i] = 0.f;
    const float x = 0.04f + 0.42f * strike;    /* where it was struck */
    const float w = 0.03f;                     /* a fixed, fairly hard mallet */
    const float g = f.geom_min + (f.geom_max - f.geom_min) * geom;
    const float t = time * time * 4.f;         /* squared: the early decay is the busy part */
    const float p = damp;                      /* how steeply damping follows the mode */

    if(f.body == Body::Plate)
    {
        /* A rectangular plate. The side ratio reshapes which modes exist, so
         * they reorder and collide as it moves — the strong axis. */
        const float norm = 1.f / Sqrt(g * g + 1.f / (g * g));
        for(int m = 1; m <= 8; m++)
        {
            float sn, cs;
            SinCosTurns(0.5f * (float)m * x, sn, cs);
            const float hit = (sn < 0.f ? -sn : sn) * detail::Sinc((float)m * w);
            if(hit < 1e-4f) continue;
            for(int n2 = 1; n2 <= 8; n2++)
            {
                const float hit2 = hit * ModalDamp(t, p, m, n2);
                if(hit2 < 1e-5f) continue;
                const float mm = (float)m * g, nn = (float)n2 / g;
                const float fh = Sqrt(mm * mm + nn * nn) * norm;
                /* 1/sqrt(m·n), not 1/(m·n). The steeper law let the (1,1)
                 * mode dominate everything, and that mode sits at exactly one
                 * harmonic where temper has nothing to pull, so the temper
                 * axis measured 0.028 against 1.04 for the strike. Flattening
                 * it gives the upper modes enough weight to be worth moving. */
                /* A quarter-power, not 1/(m·n) and not even 1/sqrt. A strike
                 * is close to an impulse and excites the modes roughly
                 * equally; what shapes them is *where* it lands and how wide
                 * the mallet is, both already in `hit`. Steeper laws were my
                 * invention rather than the physics, and they put half the
                 * energy in the fundamental. */
                const float amp = hit2 * detail::kQuarter[m * n2 - 1];
                ModalDeposit(fh, amp, k, mags);
            }
        }
        return;
    }

    int          count = 0;
    const float* ratio = f.body == Body::Bar ? detail::BarRatios(count)
                                             : detail::DrumRatios(count);
    for(int i = 0; i < count; i++)
    {
        const int   n2 = i + 1;
        float       sn, cs;
        SinCosTurns(0.5f * (float)n2 * x, sn, cs);
        const float hit = (sn < 0.f ? -sn : sn) * detail::Sinc((float)n2 * w)
                        * ModalDamp(t, p, n2, 1);
        if(hit < 1e-5f) continue;
        /* geom stretches or compresses the ratios: a bar tapered at the ends,
         * a drum head tensioned unevenly. At 1 it is the textbook object. */
        const float fh  = 1.f + (ratio[i] - 1.f) * g;
        const float amp = hit * detail::kQuarter[n2 - 1];
        ModalDeposit(fh, amp, k, mags);
    }
}

struct ModalPoint { float strike, geom, time, damp; };

inline ModalPoint ModalAt(const ModalField& f, const float* p01)
{
    ModalPoint q;
    q.strike = p01[0];
    q.geom   = p01[1];
    q.time   = f.n > 2 ? p01[2] : 0.f;
    q.damp   = f.n > 3 ? p01[3] : 0.5f;
    return q;
}

constexpr int kModalBlobBytes = 4 + 5 * 4;

inline int ModalBlob(const ModalField& f, uint8_t* out)
{
    out[0] = 0xFFu;
    out[1] = 9u;                    /* World::Kind::Modal */
    out[2] = (uint8_t)f.n;
    out[3] = (uint8_t)f.k;
    const float v[5] = {(float)(int)f.body, f.geom_min, f.geom_max, f.width_min, f.width_max};
    for(int i = 0; i < 5; i++) std::memcpy(out + 4 + 4 * i, &v[i], 4);
    return kModalBlobBytes;
}

} // namespace kyk
