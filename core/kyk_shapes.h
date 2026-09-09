/* kyk_shapes.h — a 2-D table of real waveforms, and shapers on the other axes.
 *
 * Will's report, after playing every world in the rack: "I never see things
 * lock into hard square or saws or other known base shapes, only stacks of
 * sines rolling through triangles, their walls sometimes stiffening."
 *
 * That is exactly right, and the cause was not a missing world. Every world up
 * to here renders against a fixed *random* phase spectrum. A saw's magnitudes
 * at random phase are not a saw — measured, the best circular correlation
 * against an ideal band-limited saw is 0.79, and the discontinuity that makes
 * a saw a saw is smeared into a noise burst. Stacks of sines with the right
 * spectrum. The walls stiffen where the spectrum tilts, and never become
 * walls.
 *
 * Two things fix it, and both are free:
 *
 *   Sine phase.   Saw, square, pulse and triangle are all odd-symmetric, so
 *                 all four are exact in one basis: every harmonic a quarter
 *                 turn along. Measured 1.0000 against the ideal, at a crest
 *                 factor of 2.02 — slightly better than the 2.14 the random
 *                 phase gives. Blending stays linear in the coefficients, so
 *                 the morph is exactly as click-free as it was.
 *
 *   Signed        A triangle alternates sign and a pulse needs sin(h·pi·w),
 *   coefficients. which goes negative. Taking magnitudes, as the vertex
 *                 worlds do, puts a 25% pulse at 0.83 against the ideal. A
 *                 negative coefficient is a half turn of phase, nothing more,
 *                 and it costs the renderer nothing.
 *
 * The layout is the Erica GraphicVCO's wavetable matrix, which is where this
 * instrument started: a 2-D grid of waveforms on axes 0 and 1, bilinear
 * between them, so a node *is* that waveform and the Morph knob decides how
 * hard you land on it. Row 0 is the four everyone knows.
 *
 *        col 0        col 1          col 2         col 3
 *  row 0 sine         triangle       saw           square
 *  row 1 2 partials   sharper tri    brighter saw  38% pulse
 *  row 2 3 partials   sharper still  brighter      27% pulse
 *  row 3 4 partials   near-parabola  buzz          15% pulse
 *
 * Axes 2 and 3 are shapers, and which shapers is a property of the world —
 * that is the "set of worlds" half of the request. They run on the rendered
 * single cycle rather than on the spectrum, because a wavefolder has no
 * closed form in the harmonics and pretending otherwise would give a folder
 * that does not sound like one.
 */
#pragma once
#include "kyk_types.h"
#include "kyk_tables.h"
#include "kyk_rotate.h"   /* SinCosTurns */

namespace kyk {

/* Which shaper each of the upper two axes drives. */
enum class Shaper : uint8_t { None = 0, Fold = 1, Ring = 2, Warp = 3 };

struct ShapeField
{
    int    cols = 4, rows = 4;
    Shaper axis2 = Shaper::Fold;
    Shaper axis3 = Shaper::Warp;
    /* The narrowest pulse. A pulse's crest factor climbs as it narrows — 3.75
     * at 25%, 4.55 at 10% — and the output gain is set for a crest of about
     * 4.3, so past this the loud corner of the table clips instead of getting
     * thinner. Measured, not guessed. */
    float  pulse_min = 0.08f;
    int    n = 4, k = 64;
};

namespace detail {

/* The waveform at grid node (col, row), as signed coefficients on the sine
 * basis, normalised to sum of squares 2 — the convention the engine renders
 * at, which puts the frame at unit RMS.
 *
 * Each column is one family and each row goes brighter or narrower down it,
 * so row 0 is the four canonical shapes and every column stays recognisable
 * as itself all the way down. */
/* One end of a column: `end` 0 is the canonical waveform at the top, 1 is
 * where that column arrives at the bottom. Signed coefficients on the sine
 * basis, unnormalised. */
inline void ShapeEnd(const ShapeField& f, int col, int end, int k, float* c)
{
    for(int i = 0; i < k; i++) c[i] = 0.f;
    switch(col)
    {
        case 0:
            /* one partial, or eight of them */
            for(int h = 1; h <= (end ? 8 : 1) && h <= k; h++) c[h - 1] = kInvHarm[h - 1];
            break;
        case 1:
            /* triangle, or the same odd 1/h^2 series with every sign positive,
             * which is a square with rounded corners. Changing the sign
             * pattern moves the waveform a long way while leaving the spectrum
             * alone, which is exactly the kind of travel a phase-blind
             * representation cannot offer at all. */
            for(int h = 1; h <= k; h += 2)
            {
                const float sg = end ? 1.f : ((((h - 1) / 2) & 1) ? -1.f : 1.f);
                const float ih = kInvHarm[h - 1];
                c[h - 1] = sg * ih * ih;                    /* 1/h^2, from the table */
            }
            break;
        case 2:
        {
            /* saw, or a saw with a resonant peak partway up the series — the
             * sound a filter sweep leaves behind, and a much bigger move than
             * simply brightening the rolloff. Brightening was what this column
             * did first, and the whole row axis measured 0.18 against 1.85 for
             * the columns: a knob that did nothing. */
            /* Logs from the table and the Gaussian skipped once it is 100 dB
             * down, for the same reason the vowel world needed both: a series
             * exp and log per harmonic put this world's Evaluate at 7.8 us,
             * nearly twice the cost of the transform it feeds. */
            const float lr = kLnHarm[10];                   /* ln 11 */
            for(int h = 1; h <= k; h++)
            {
                float v = kInvHarm[h - 1];
                if(end)
                {
                    const float d = (kLnHarm[h - 1] - lr) * (1.f / 0.45f);
                    const float e = -0.5f * d * d;
                    if(e > -12.f) v *= 1.f + 5.5f * detail::Exp(e);
                }
                c[h - 1] = v;
            }
            break;
        }
        default:
        {
            /* square, or a narrow bipolar pulse. c_h = (2/(pi·h))·(1 -
             * cos(2·pi·h·d)); at d = 0.5 the even harmonics vanish and this is
             * the square. */
            const float d = end ? f.pulse_min : 0.5f;
            for(int h = 1; h <= k; h++)
            {
                float sn, cs;
                SinCosTurns((float)h * d, sn, cs);          /* cs = cos(2·pi·h·d) */
                c[h - 1] = 2.f * (1.f - cs) / (3.14159265f * (float)h);
            }
            break;
        }
    }
}

/* The waveform at grid node (col, row): the column's two ends, crossfaded.
 * Normalised to sum of squares 2, the convention the engine renders at, which
 * puts the frame at unit RMS. Row 0 is always the canonical shape. */
inline void ShapeNode(const ShapeField& f, int col, int row, int k, float* c)
{
    const float t = f.rows > 1 ? (float)row / (float)(f.rows - 1) : 0.f;
    float b[kMaxK];
    ShapeEnd(f, col, 0, k, c);
    if(t > 0.f)
    {
        ShapeEnd(f, col, 1, k, b);
        for(int i = 0; i < k; i++) c[i] += t * (b[i] - c[i]);
    }
    float e = 0.f;
    for(int i = 0; i < k; i++) e += c[i] * c[i];
    const float g = e > 0.f ? Sqrt(2.f) / Sqrt(e) : 0.f;
    for(int i = 0; i < k; i++) c[i] *= g;
}

/* Bias a fractional grid coordinate toward the nearer node.
 *
 * At sharp 0 this is the identity and the table is a smooth crossfade. Wound
 * up, it compresses the ground between nodes so you sit on the waveform
 * itself for most of the knob's travel and cross between two of them quickly
 * — which is the whole point of a table of *recognisable* shapes. It stays
 * continuous at every setting, so the morph never steps. */
inline float SnapTo(float u, float sharp)
{
    if(sharp <= 0.f) return u;
    if(u <= 0.f) return 0.f;
    if(u >= 1.f) return 1.f;
    const float a = 1.f + 7.f * sharp * sharp;
    const float x = detail::Powf01(u, a), y = detail::Powf01(1.f - u, a);
    const float d = x + y;
    return d > 0.f ? x / d : u;
}

} // namespace detail

/* ── the shapers ─────────────────────────────────────────────────────────
 * All run on the rendered single cycle. Each is the identity at zero, so a
 * node of the table with both upper axes at zero is bit-for-bit the waveform
 * the table says it is. */

/* A sine folder, crossfaded against dry so that depth 0 is exactly dry.
 * Folding is what turns a triangle into something alive, and it is the one
 * classic shaper with no closed form in the harmonics. */
inline void FoldFrame(float* x, int n, float d)
{
    if(d <= 0.f) return;
    const float g = 1.f + 5.5f * d;
    for(int i = 0; i < n; i++)
    {
        float sn, cs;
        SinCosTurns(0.25f * g * x[i], sn, cs);      /* sin(g·x·pi/2) */
        x[i] += d * (sn - x[i]);
    }
}

/* Ring modulation against a harmonic of the cycle itself, so the result is
 * still one cycle and still lands on harmonics rather than between them. The
 * ratio walks with depth, which is what makes the axis worth turning. */
inline void RingFrame(float* x, int n, float d)
{
    if(d <= 0.f) return;
    const int m = 1 + (int)(d * 6.99f);             /* 1..7 */
    /* cos(2·pi·m·i/n) at integer i is a table entry, not a computation: the
     * frame length divides the sine table, so the index is exact and no
     * interpolation is needed. */
    const int step = kTableSize / n, q = kTableSize / 4, mask = kTableSize - 1;
    for(int i = 0; i < n; i++)
    {
        const float cs = kSinTable[(m * i * step + q) & mask];
        x[i] += d * (x[i] * cs - x[i]);
    }
}

/* Casio-style phase distortion: read the cycle through a two-piece warp that
 * hurries through the first part and dawdles through the rest. On a saw it
 * gives the resonant-formant sound; on a square it sharpens one edge and
 * softens the other.
 *
 * Done in place, with no scratch buffer, which is worth a sentence because it
 * looks unsafe. The warp v(u) satisfies v(u) >= u everywhere: below the knee
 * it is u·(0.5/c) with c < 0.5, and above it runs from 0.5 down to meeting u
 * at 1. So sample i only ever reads samples at or after i, and once i is
 * written nothing looks back at it. The single exception is the last sample,
 * where v = 1 wraps the read to index 0, which by then has been overwritten —
 * so the original is kept aside for it. */
/* Phase modulation of the read pointer by one cycle of a sine — the cycle is
 * read back through itself at a wobbling rate, which is where the CZ-style
 * moving formant comes from and is also, exactly, single-operator FM.
 *
 * The first version was Casio's actual two-segment warp: hurry through the
 * first part of the cycle, dawdle through the rest. It is unusable here and
 * the measurement said so immediately — the worst non-harmonic bin came out
 * at 0.0 dB, as loud as the loudest harmonic. The reason is that two linear
 * segments meet in a *kink*, and a derivative discontinuity radiates
 * harmonics that fall off only as 1/h². That is broadband, so no amount of
 * pulling the band limit in ahead of it helps; capping the knee moved 0.0 dB
 * to -3.2 dB and no further. Casio shipped it and it aliased.
 *
 * A sine warp has no discontinuity in any derivative, and phase modulation at
 * index A spreads a harmonic h to about h·(1+A), so the bandwidth expansion
 * is bounded by 1+A and the band limit can absorb it. Monotonicity is not
 * required — where the warp runs backwards it is through-zero PM, which is a
 * sound rather than a fault — so A is free to exceed one.
 *
 * Needs the original to read from, since the read runs both directions. */
inline void WarpFrame(float* x, float* scratch, int n, float d)
{
    if(d <= 0.f) return;
    const float A = 2.2f * d;
    for(int i = 0; i < n; i++) scratch[i] = x[i];
    /* sin(2·pi·i/n) at integer i is an exact table entry for the same reason
     * as the ring modulator, so the warp costs a lookup and a multiply. */
    const int   step = kTableSize / n, mask = kTableSize - 1;
    const float inv = 1.f / (float)n, k2pi = A * (1.f / 6.2831853f);
    for(int i = 0; i < n; i++)
    {
        const float u  = (float)i * inv;
        const float sn = kSinTable[(i * step) & mask];
        const float v  = u + k2pi * sn;
        float       p = v * (float)n;
        int         j = (int)p;
        float       fr = p - (float)j;
        if(p < 0.f) { j -= 1; fr = p - (float)j; }
        j &= (n - 1);
        const int j1 = (j + 1) & (n - 1);
        x[i] = scratch[j] + (scratch[j1] - scratch[j]) * fr;
    }
}

/* How much wider the shapers make the spectrum at this setting.
 *
 * A folder is a memoryless nonlinearity, so it multiplies bandwidth, and the
 * engine has already band-limited to exactly Nyquist before it runs. Left
 * alone the new harmonics fold straight back as aliasing — the same trap the
 * soft-clip experiment fell into (docs/m2-notes.md). Shrinking the band limit
 * by this factor before rendering leaves the fold room to work in. It is a
 * mitigation and not a cure; the honest figure is in docs/m3-notes.md. */
inline float ShapeBandScale(const ShapeField& f, float a2, float a3)
{
    float s = 1.f;
    if(f.axis2 == Shaper::Fold || f.axis3 == Shaper::Fold)
        s += 2.2f * (f.axis2 == Shaper::Fold ? a2 : a3);
    if(f.axis2 == Shaper::Warp || f.axis3 == Shaper::Warp)
        s += 2.2f * (f.axis2 == Shaper::Warp ? a2 : a3);   /* PM index */
    if(f.axis2 == Shaper::Ring || f.axis3 == Shaper::Ring)
        s += 1.0f * (f.axis2 == Shaper::Ring ? a2 : a3);
    return s;
}

/* The whole world as 32 bytes, behind the same 0xFF marker the FM and vowel
 * worlds use, so the page can evaluate this table itself and draw the grid
 * rather than a blank field. See FmBlob for why the marker sits where a
 * dimension count would. */
constexpr int kShapeBlobBytes = 4 + 4 + 6 * 4;

inline int ShapeBlob(const ShapeField& f, uint8_t* out)
{
    out[0] = 0xFFu;
    out[1] = 6u;                    /* World::Kind::Table */
    out[2] = (uint8_t)f.n;
    out[3] = (uint8_t)f.k;
    out[4] = (uint8_t)f.cols;
    out[5] = (uint8_t)f.rows;
    out[6] = (uint8_t)f.axis2;
    out[7] = (uint8_t)f.axis3;
    const float v[6] = {f.pulse_min, 0.f, 0.f, 0.f, 0.f, 0.f};
    for(int i = 0; i < 6; i++) std::memcpy(out + 8 + 4 * i, &v[i], 4);
    return kShapeBlobBytes;
}

} // namespace kyk
