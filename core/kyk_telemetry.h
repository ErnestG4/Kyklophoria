/* kyk_telemetry.h — the binary telemetry body (docs/hostlink.md, 0x60).
 *
 * One encoder for both shells, so the desktop bridge and the module put
 * the same bytes on the wire. Layout after the u8 status the extension
 * writes:
 *
 *   u32 block · f32 f0 · u8 n · u8 k · u8 kcut · u8 p · u8 planes · u8 flags
 *   u8 stereo · u8 spread_plane · f32 spread
 *   f32 ctl[n] · f32 centre[n] · f32 posL[n] · f32 posR[n] · f32 angle[planes] · f32 payload[p]
 *   [flags&1] u8 mags[k]   — left voice, pre-bandlimit, dB: 0 = ≤ −96 dB, 255 = +6 dB
 *   [flags&2] i8 frame[256] — left voice's frame, decimated, ×40 clipped
 *   [flags&4] u8 kep_running · u8 kep_plane · f32 kep_x · f32 kep_y
 *             f32 kep_rush · f32 couple · f32 lock · u8 sharp
 *             u8 kep_bodies · f32 x,y for bodies 1..kep_bodies-1
 *             u8 page — which pager page the panel is showing
 *             u8 morph · u8 morph_world — how far towards another world, and
 *                        which one (0xFF: none, so the knob does nothing)
 *
 * That last block is how the position is moving of its own accord: the
 * falling body, and how hard the orbit planes are pulling on each other. It
 * goes last on purpose — a host that does not know about it reads everything
 * it does know by offset and ignores the tail, so adding it did not break the
 * page that was already running.
 */
#pragma once
#include "kyk_stereo.h"

namespace kyk {

constexpr uint8_t kTelSpectrum   = 0x01;
constexpr uint8_t kTelFrame      = 0x02;
constexpr uint8_t kTelMotion     = 0x04;
constexpr int     kTelFramePts   = 256;

namespace detail {
/* log2 from the float bits, ±0.01 — a display scale, not DSP */
inline float FastLog2(float x)
{
    if(x <= 0.f) return -126.f;
    uint32_t b;
    std::memcpy(&b, &x, 4);
    const int   e = (int)((b >> 23) & 0xFFu) - 127;
    const float m = 1.f + (float)(b & 0x7FFFFFu) * (1.0f / 8388608.0f);   /* [1,2) */
    return (float)e + (-0.33333333f * m * m + 2.f * m - 1.66666667f);
}
/* The byte runs from −96 dB to +6 dB, not to 0.
 *
 * The engine normalises a spectrum so its sum of squares is 2, which means a
 * single partial can legitimately reach +3 dB, and a peaky world's tallest
 * partial routinely does. Topping the scale out at 0 dB clipped exactly that
 * partial — measured 1.4 dB of error on the vowel world's second harmonic,
 * against a quantisation step of 0.4 — so the page drew its most important
 * bar short. Six dB of headroom costs 0.024 dB of resolution. */
inline uint8_t MagToU8(float m)
{
    /* Coefficients are signed for a sine-phase world — a negative one is a
     * half turn of phase, not a negative level — so the display takes the
     * magnitude. Without this a triangle's alternating partials would all
     * read as silence. */
    if(m < 0.f) m = -m;
    const float db = 6.0205999f * FastLog2(m);          /* 20·log10 */
    const float v  = 255.f + (db - 6.f) * (255.f / 102.f);
    return v <= 0.f ? 0u : (v >= 255.f ? 255u : (uint8_t)(v + 0.5f));
}
inline int8_t ToI8(float v)
{
    float s = v * 40.f;
    if(s > 127.f) s = 127.f; else if(s < -127.f) s = -127.f;
    return (int8_t)(s >= 0.f ? s + 0.5f : s - 0.5f);
}
struct Put
{
    uint8_t* p; int cap; int n = 0; bool ok = true;
    void U8(uint8_t v) { if(n + 1 > cap) { ok = false; return; } p[n++] = v; }
    void U32(uint32_t v) { for(int i = 0; i < 4; i++) U8((uint8_t)(v >> (8 * i))); }
    void F32(float v) { uint32_t u; std::memcpy(&u, &v, 4); U32(u); }
};
} // namespace detail

/* Returns bytes written, or 0 if cap is too small. */
inline int EncodeTelemetry(const StereoEngine& e, uint8_t flags, uint8_t* out, int cap,
                           uint8_t page = 0, uint8_t morph_world = 0xFFu)
{
    const World* s = e.WorldPtr();
    if(!s || !s->Ready()) return 0;
    const int    N = s->N(), K = s->K(), P = s->P(), planes = e.rot.Planes();
    detail::Put  w{out, cap};
    w.U32(e.L.Block());
    w.F32(e.L.F0());
    w.U8((uint8_t)N); w.U8((uint8_t)K); w.U8((uint8_t)e.L.Kcut()); w.U8((uint8_t)P);
    w.U8((uint8_t)planes); w.U8(flags);
    w.U8(e.IsStereo() ? 1u : 0u); w.U8((uint8_t)e.spread_plane);
    w.F32(e.spread);
    for(int a = 0; a < N; a++) w.F32(e.Control()[a]);
    for(int a = 0; a < N; a++) w.F32(e.Centre()[a]);
    for(int a = 0; a < N; a++) w.F32(e.L.Position()[a]);
    for(int a = 0; a < N; a++) w.F32(e.IsStereo() ? e.R.Position()[a] : e.L.Position()[a]);
    for(int p = 0; p < planes; p++) w.F32(e.rot.Angle(p));
    for(int j = 0; j < P; j++) w.F32(e.Payload()[j]);
    if(flags & kTelSpectrum)
        for(int k = 0; k < K; k++) w.U8(detail::MagToU8(e.L.Mags()[k]));
    if(flags & kTelFrame)
    {
        const float* f    = e.L.Frame();
        const int    step = kFrame / kTelFramePts;
        for(int i = 0; i < kTelFramePts; i++) w.U8((uint8_t)detail::ToI8(f[i * step]));
    }
    if(flags & kTelMotion)
    {
        w.U8(e.kepler.Running() ? 1u : 0u);
        w.U8((uint8_t)e.kepler.plane);
        w.F32(e.kepler.X());
        w.F32(e.kepler.Y());
        w.F32(e.kepler.Rush());
        w.F32(e.rot.Couple());
        w.F32(e.rot.Lock());
        /* The Morph knob. Not motion, but this block is where the frame grows
         * and a host that predates it ignores the tail.
         *
         * It is here because the page evaluates several worlds itself to draw
         * their terrain, and `sharp` feeds World::Evaluate for the lattice,
         * vertex, Lock and Table worlds — so without it the page draws a
         * different space from the one playing. Measured over the whole cube,
         * the difference between sharp 0 and sharp 1 is a mean of 0.047 and a
         * maximum of 0.312 in normalised log-centroid on Lock, against a
         * typical slice span of 0.27 to 0.46: worst case is essentially the
         * whole range. Ten of the thirteen drawable worlds measure exactly
         * zero and never needed it. */
        const float sh = e.sharp < 0.f ? 0.f : (e.sharp > 1.f ? 1.f : e.sharp);
        w.U8((uint8_t)(sh * 255.f + 0.5f));
        /* The company. Body 0 is already above as kep_x/kep_y — it is the one
         * the space is actually read at — so only the perturbers go here, and
         * only as many as are running. A single body therefore costs one extra
         * byte, which is what the common case should cost. */
        const int nb = e.kepler.Bodies();
        w.U8((uint8_t)nb);
        for(int b = 1; b < nb; b++) { w.F32(e.kepler.BodyX(b)); w.F32(e.kepler.BodyY(b)); }
        /* Which page the panel is on. The descriptor says what the six knobs
         * of each page are; this says which six you are currently holding, so
         * the page can mirror the panel instead of describing it in general. */
        w.U8(page);
        /* The Morph knob and its target. Without these the page cannot show
         * whether the knob is doing anything, and a knob whose effect is
         * invisible is a knob nobody trusts. */
        const float mo = e.Morph() < 0.f ? 0.f : (e.Morph() > 1.f ? 1.f : e.Morph());
        w.U8((uint8_t)(mo * 255.f + 0.5f));
        w.U8(morph_world);
    }
    return w.ok ? w.n : 0;
}

/* Fixed part of the body for a given space (for buffer sizing). */
inline int TelemetrySize(int n, int k, int p, uint8_t flags)
{
    const int planes = Rotation::PlaneCount(n);
    int       sz     = 4 + 4 + 6 + 2 + 4 + 4 * (4 * n + planes + p);
    if(flags & kTelSpectrum) sz += k;
    if(flags & kTelFrame) sz += kTelFramePts;
    if(flags & kTelMotion) sz += 2 + 20 + 1 + 1 + 8 * (kKeplerBodies - 1) + 1 + 2;
    return sz;
}

} // namespace kyk
