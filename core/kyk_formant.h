/* kyk_formant.h — a world of resonances, which is to say of vowels.
 *
 * Three peaks over a falling source. It is the oldest model of the voice
 * there is, and it is here because it is the one shape of spectrum a listener
 * identifies instantly: move the first two peaks around and the sound says a
 * vowel, without anyone having to be told that is what it is doing.
 *
 * Two decisions worth stating, because both are departures from the textbook.
 *
 * The peaks are placed in *harmonic number*, not in hertz. A real formant
 * sits at a fixed frequency and the harmonics slide underneath it as the
 * pitch changes, which is what makes a sung vowel stay the same vowel. This
 * world cannot do that: a World is asked for a spectrum given a position and
 * is told nothing about pitch, deliberately, because the spectrum is rendered
 * once and reused across blocks — and on this instrument the pitch input is
 * being driven at audio rate, so a pitch-dependent spectrum would mark every
 * block dirty and cost more than the whole rest of the engine. So the peaks
 * ride the pitch. That is a chipmunk vowel rather than a sung one, and it is
 * the honest trade for the architecture.
 *
 * The peaks are placed by *ratio*, not independently: the second is a
 * multiple of the first and the third a multiple of the second. That is
 * partly because it guarantees they stay in order, which independent axes do
 * not, and partly because independent peaks would make the world separable —
 * three multiplicative filters over a source are three additive terms in log
 * magnitude, one per axis, which is the exact structure that measured
 * lopsided everywhere else. Chaining them means moving the first axis moves
 * all three peaks, so no single axis can be undone on its own.
 */
#pragma once
#include "kyk_types.h"
#include "kyk_tables.h"

namespace kyk {

struct FormantField
{
    /* Swept, like the FM world's reaches. The first version used textbook
     * vowel proportions — a dominant first peak at 0.40 and 0.16 for the
     * others, and a wide first-peak range — and measured 6.0x direction
     * spread with two of its four axes doing almost nothing: the third
     * formant's axis moved the spectrum a thirty-fifth as far as the first's.
     * A knob that does nothing is worse than an unrealistic one. Levelling
     * the three peaks and shortening the first axis brought it to 2.9x with
     * every axis in play, at the cost of sounding less like one voice and
     * more like three resonances, which is what it is. */
    float f1_min = 1.0f, f1_max = 4.0f;    /* first peak, in harmonics */
    float r2_min = 1.5f, r2_max = 4.0f;    /* second peak as a multiple of the first */
    float r3_min = 1.3f, r3_max = 5.0f;    /* third as a multiple of the second */
    float q_min  = 0.06f, q_max = 0.75f;   /* peak width in octaves, wide to narrow */
    float tilt   = 1.0f;                   /* source falloff, 1/h^tilt */
    float amp2   = 1.00f, amp3 = 0.85f;    /* the upper peaks, relative to the first */
    int   n = 4, k = 64;
};

/* Magnitudes into mags[0..k-1], harmonic 1 at index 0. Not normalised.
 *
 * Each peak is a Gaussian in log-harmonic rather than linear space, so it
 * keeps its shape as it moves up the series instead of turning into a spike
 * at the bottom and a smear at the top — the same reason a filter's Q is
 * quoted in octaves and not in hertz. Peaks add rather than multiply, so a
 * peak sitting in a null of the source still shows. */
inline void FormantSpectrum(const FormantField& f, float f1, float r2, float r3,
                            float q, int k, float* mags)
{
    const float h1 = f1;
    const float h2 = f1 * r2;
    const float h3 = f1 * r2 * r3;
    const float l1 = detail::Ln(h1 > 1e-3f ? h1 : 1e-3f);
    const float l2 = detail::Ln(h2 > 1e-3f ? h2 : 1e-3f);
    const float l3 = detail::Ln(h3 > 1e-3f ? h3 : 1e-3f);
    const float w  = q * 0.6931472f;              /* q in octaves → natural log */
    const float iw = 1.f / (2.f * w * w);
    /* Below this the term is under 6e-6 of the peak, which is 100 dB down and
     * cannot survive the render. Skipping it matters: with narrow peaks most
     * harmonics are far from all three, and the guard turns three series
     * exponentials per harmonic into well under one on average. Together with
     * the ln and reciprocal tables this world went from ten microseconds an
     * evaluation to about one, in line with every other world; before that it
     * was twelve times the cost of any of them. */
    const float kFar = -12.f;
    const bool  plain = f.tilt == 1.f;
    /* The harmonic tables are sized kMaxK, which is a build-time define; a
     * larger K without regenerating them would read off the end. */
    static_assert(kMaxK <= 128, "kLnHarm/kInvHarm are 128 long; re-run tools/gen/gen_tables.py");
    for(int i = 0; i < k; i++)
    {
        const float lh = kLnHarm[i];
        const float d1 = lh - l1, d2 = lh - l2, d3 = lh - l3;
        const float e1 = -d1 * d1 * iw, e2 = -d2 * d2 * iw, e3 = -d3 * d3 * iw;
        float bump = 0.f;
        if(e1 > kFar) bump += detail::Exp(e1);
        if(e2 > kFar) bump += f.amp2 * detail::Exp(e2);
        if(e3 > kFar) bump += f.amp3 * detail::Exp(e3);
        /* the source: a falling series, so the peaks sit on a slope rather
         * than in a vacuum and the fundamental is always present */
        const float src = plain ? kInvHarm[i] : detail::Exp(-f.tilt * lh);
        mags[i] = src * (0.06f + bump);
    }
}

/* Map a folded coordinate onto the four controls. */
struct FormantPoint { float f1, r2, r3, q; };
inline FormantPoint FormantAt(const FormantField& f, const float* p01)
{
    FormantPoint p;
    p.f1 = f.f1_min + p01[0] * (f.f1_max - f.f1_min);
    p.r2 = f.r2_min + p01[1] * (f.r2_max - f.r2_min);
    p.r3 = f.r3_min + p01[2] * (f.r3_max - f.r3_min);
    /* narrow at the top of the knob, because a narrow peak is the striking
     * end and a knob should reach its extreme by being turned up */
    p.q  = f.n > 3 ? f.q_max - p01[3] * (f.q_max - f.q_min) : 0.35f;
    return p;
}

/* The whole world as 44 bytes, behind the same 0xFF marker the FM world
 * uses, so the page can draw this terrain too. See FmBlob for why the marker
 * is where a dimension count would be. */
constexpr int kFormantBlobBytes = 4 + 10 * 4;

inline int FormantBlob(const FormantField& f, uint8_t* out)
{
    out[0] = 0xFFu;
    out[1] = 5u;                    /* World::Kind::Formant */
    out[2] = (uint8_t)f.n;
    out[3] = (uint8_t)f.k;
    const float v[10] = {f.f1_min, f.f1_max, f.r2_min, f.r2_max, f.r3_min,
                         f.r3_max, f.q_min, f.q_max, f.amp2, f.amp3};
    for(int i = 0; i < 10; i++) std::memcpy(out + 4 + 4 * i, &v[i], 4);
    return kFormantBlobBytes;
}

} // namespace kyk
