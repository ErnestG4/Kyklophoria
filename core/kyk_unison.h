/* kyk_unison.h — one wave stacked on itself, and what happens as it detunes.
 *
 * From the original brief: "one dimension is saw waves, stacking on themselves
 * in one direction". This is that, done in a way the representation can
 * actually hold.
 *
 * A voice at root r contributes its partials to harmonics r, 2r, 3r… of the
 * fundamental. When r is a whole number those land exactly on harmonics and
 * the stack is one tone with a thicker spectrum. When it is not, they land
 * between harmonics — and since the frame is strictly periodic at f0, "between
 * harmonics" is not available. So each partial is split across the two bins it
 * falls between, the same trick the FM world uses for its sidebands. The
 * result is not a detuned oscillator, which this engine cannot produce, but it
 * is the thing a detuned stack *sounds* like: energy smeared across
 * neighbouring partials, beating against the ones already there. Signed
 * deposits, so voices cancel where they should.
 *
 * The four axes:
 *
 *   voices    one to seven copies, the last fading in rather than appearing.
 *   interval  where the copies sit: roots at 1 + j·s for s from 1 (every
 *             harmonic, a dense buzz) to 2.6 (wide and hollow).
 *   detune    pushes the roots off whole numbers, which is where the beating
 *             lives. Zero is exactly harmonic.
 *   wave      the copied waveform: saw, through square, to a narrow pulse.
 *
 * Deliberately free of transcendentals: the saw and the square are both table
 * lookups and the per-voice gain is a running multiply. A world that spends an
 * exp per harmonic costs more than the transform it feeds, which this project
 * has now learned twice.
 */
#pragma once
#include "kyk_shapes.h"

namespace kyk {

struct UnisonField
{
    float voices_max  = 7.0f;
    /* Narrowed from 1..4 by measurement. The interval moves every partial of
     * every voice at once, so left wide it did eight times the work of any
     * other axis and the world measured 18x lopsided. */
    float span_min    = 1.0f, span_max = 2.6f;
    float detune_max  = 0.55f;
    float tilt        = 0.62f;   /* each successive voice quieter by this much */
    int   n = 4, k = kShapeK;
};

/* Magnitudes into mags[0..k-1]. Not normalised; the caller does that. */
inline void UnisonSpectrum(const UnisonField& f, float voices, float span,
                           float detune, float wave, int k, float* mags)
{
    for(int i = 0; i < k; i++) mags[i] = 0.f;
    const int   whole = (int)voices;
    const float frac  = voices - (float)whole;
    const float inv_pi = 1.f / 3.14159265f;

    float gain = 1.f;
    for(int j = 0; j <= whole && j < 16; j++)
    {
        /* the last voice fades in instead of appearing */
        float g = gain;
        if(j == whole)
        {
            if(frac <= 0.f) break;
            g *= frac;
        }
        const float root = 1.f + (float)j * span + (float)j * detune;
        if(root > (float)k) break;
        for(int m = 1; m <= k; m++)
        {
            const float fh = root * (float)m;
            if(fh > (float)k) break;
            /* The copied waveform, saw through square to a narrow pulse. The
             * first half crossfades saw into square, the second narrows the
             * duty; continuous across the join, and all of it out of the
             * harmonic table. Spanning only saw-to-square left this axis at
             * 0.45 against 8.3 for the interval — a knob barely worth turning. */
            const float im = kInvHarm[m - 1];
            float       bm;
            if(wave <= 0.5f)
            {
                const float sq = (m & 1) ? 4.f * inv_pi * im : 0.f;
                bm = im + (2.f * wave) * (sq - im);
            }
            else
            {
                const float d = 0.5f - (2.f * wave - 1.f) * 0.38f;
                float sn, cs;
                SinCosTurns((float)m * d, sn, cs);
                bm = 2.f * (1.f - cs) * inv_pi * im;
            }
            if(bm == 0.f) continue;
            const float a  = g * bm;
            /* split across the two bins it falls between */
            const float h  = fh - 1.f;
            int         i0 = (int)h;
            if(h < 0.f && (float)i0 != h) i0 -= 1;
            const float fr = h - (float)i0;
            if(i0 >= 0 && i0 < k) mags[i0] += a * (1.f - fr);
            if(i0 + 1 >= 0 && i0 + 1 < k) mags[i0 + 1] += a * fr;
        }
        gain *= f.tilt;
    }
}

struct UnisonPoint { float voices, span, detune, wave; };

inline UnisonPoint UnisonAt(const UnisonField& f, const float* p01)
{
    UnisonPoint q;
    q.voices = 1.f + p01[0] * (f.voices_max - 1.f);
    q.span   = f.span_min + p01[1] * (f.span_max - f.span_min);
    q.detune = f.n > 2 ? p01[2] * f.detune_max : 0.f;
    q.wave   = f.n > 3 ? p01[3] : 0.f;
    return q;
}

constexpr int kUnisonBlobBytes = 4 + 5 * 4;

inline int UnisonBlob(const UnisonField& f, uint8_t* out)
{
    out[0] = 0xFFu;
    out[1] = 8u;                    /* World::Kind::Unison */
    out[2] = (uint8_t)f.n;
    out[3] = (uint8_t)f.k;
    const float v[5] = {f.voices_max, f.span_min, f.span_max, f.detune_max, f.tilt};
    for(int i = 0; i < 5; i++) std::memcpy(out + 4 + 4 * i, &v[i], 4);
    return kUnisonBlobBytes;
}

} // namespace kyk
