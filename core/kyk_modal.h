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
 *   No decay axis.         Time-since-strike and mallet hardness are the same
 *                          direction in spectrum space: both are lowpass
 *                          filters on the mode set. Spending two axes on them
 *                          left one reading 0.07 against 0.83, whatever the
 *                          scaling. Only the mallet is here.
 *
 *   Geometry is the        Changing which modes *exist* moves the spectrum far
 *   strong axis.           more than tilting the ones that do, because modes
 *                          reorder and collide. It measured 2.58 against 0.55
 *                          for the mallet.
 *
 * A fourth axis of "stiffness" was tried and thrown out for the same reason as
 * the decay axis: stretching the high modes is what the geometry axis already
 * does, so the two were collinear and it measured 0.03 against 0.64 on the
 * drum. Temper replaces it — how hard the modes are pulled onto whole numbers
 * — which is a different move entirely, and is this world's own premise made
 * playable.
 */
#pragma once
#include "kyk_shapes.h"

namespace kyk {

enum class Body : uint8_t { Plate = 0, Bar = 1, Drum = 2 };

struct ModalField
{
    Body  body = Body::Plate;
    float geom_min = 0.62f, geom_max = 2.9f;   /* side ratio, or taper */
    float width_min = 0.02f, width_max = 0.32f;
    float temper_max = 0.95f;   /* below 1, so the attractor stays monotone */
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
    static const float r[12] = {1.f, 1.593f, 2.135f, 2.295f, 2.653f, 2.917f,
                                3.155f, 3.500f, 3.598f, 3.647f, 4.060f, 4.154f};
    n = 12;
    return r;
}

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

/* Pull a mode's frequency toward the nearest whole harmonic.
 *
 * The world's whole premise is that these objects get tuned onto the harmonic
 * grid; this makes that a knob. At zero the modes sit where the physics puts
 * them and the fractional deposit smears each one over two bins. Wound up,
 * they migrate onto whole numbers and the object rings like a tuned bar.
 *
 * Not `round()`, which jumps as the geometry moves a mode past a half-integer
 * and would put a step of the temper amount straight into the frequency. The
 * smooth attractor is the same one the FM world's ratio axis uses: subtracting
 * sin(2·pi·f)/(2·pi) has zeros exactly at the whole numbers, is monotone below
 * a temper of one, and never jumps. */
inline float ModalTemper(float fh, float t)
{
    if(t <= 0.f) return fh;
    /* Applied three times, because once is not enough to be a knob. One pass
     * can move a mode by at most t/(2·pi), about a seventh of a harmonic, and
     * a bar's second mode needs to travel a quarter of one to reach a whole
     * number — so a single pass measured 0.012 against 0.99 for the strike
     * axis, which is a dead control. The map has its fixed points at the whole
     * numbers and converges onto them, so iterating walks a mode home: 2.756
     * goes to 2.907, then 2.990, then 3.000. Each pass is monotone for t below
     * one and a composition of monotone maps is monotone, so it stays as
     * continuous as one pass was. */
    for(int i = 0; i < 3; i++)
    {
        float sn, cs;
        SinCosTurns(fh, sn, cs);
        fh -= t * (1.f / 6.2831853f) * sn;
    }
    return fh;
}

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
inline void ModalSpectrum(const ModalField& f, float strike, float geom,
                          float width, float p4, int k, float* mags)
{
    for(int i = 0; i < k; i++) mags[i] = 0.f;
    const float x = 0.04f + 0.42f * strike;    /* where it was struck */
    const float w = f.width_min + (f.width_max - f.width_min) * width;
    const float g = f.geom_min + (f.geom_max - f.geom_min) * geom;

    if(f.body == Body::Plate)
    {
        /* A rectangular plate. The side ratio reshapes which modes exist, so
         * they reorder and collide as it moves — the strong axis. */
        const float norm = 1.f / Sqrt(g * g + 1.f / (g * g));
        const float y = 0.04f + 0.42f * p4;        /* the other strike coordinate */
        for(int m = 1; m <= 6; m++)
        {
            float sn, cs;
            SinCosTurns(0.5f * (float)m * x, sn, cs);
            const float hit = (sn < 0.f ? -sn : sn) * detail::Sinc((float)m * w);
            if(hit < 1e-4f) continue;
            for(int n2 = 1; n2 <= 6; n2++)
            {
                float sy, cy;
                SinCosTurns(0.5f * (float)n2 * y, sy, cy);
                const float hit2 = hit * (sy < 0.f ? -sy : sy);
                if(hit2 < 1e-4f) continue;
                const float mm = (float)m * g, nn = (float)n2 / g;
                const float fh = Sqrt(mm * mm + nn * nn) * norm;
                /* 1/sqrt(m·n), not 1/(m·n). The steeper law let the (1,1)
                 * mode dominate everything, and that mode sits at exactly one
                 * harmonic where temper has nothing to pull, so the temper
                 * axis measured 0.028 against 1.04 for the strike. Flattening
                 * it gives the upper modes enough weight to be worth moving. */
                const float amp = hit2 / Sqrt((float)(m * n2));
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
        const float hit = (sn < 0.f ? -sn : sn) * detail::Sinc((float)n2 * w);
        if(hit < 1e-4f) continue;
        /* geom stretches or compresses the ratios: a bar tapered at the ends,
         * a drum head tensioned unevenly. At 1 it is the textbook object. */
        const float fh  = ModalTemper(1.f + (ratio[i] - 1.f) * g, p4);
        const float amp = hit * kInvHarm[n2 - 1];
        ModalDeposit(fh, amp, k, mags);
    }
}

struct ModalPoint { float strike, geom, width, stiff; };

inline ModalPoint ModalAt(const ModalField& f, const float* p01)
{
    ModalPoint q;
    q.strike = p01[0];
    q.geom   = p01[1];
    q.width  = f.n > 2 ? p01[2] : 0.4f;
    /* the plate reads this as its second strike coordinate, everything else
     * as temper, so it is not scaled here */
    q.stiff  = f.n > 3 ? (f.body == Body::Plate ? p01[3] : p01[3] * f.temper_max) : 0.f;
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
