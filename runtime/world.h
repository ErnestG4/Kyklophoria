/* world.h — a condensed world (tools/export.py, .kykm) read by the runtime.
 *
 * The blob lives wherever the module put it (SDRAM); this never copies it
 * and never allocates. A point is decoded at note-on: cents to hertz,
 * a byte to a decay, a byte to a gain, and for a parameter between two
 * points the two are interpolated by slot — log frequency, log decay, dB —
 * which is a partial gliding to its neighbour's place, the way the bake's
 * spaces move. The stage is per point and interpolated the same way.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include "modal_bank.h"

namespace mb {

struct World
{
    const uint8_t* blob = nullptr;
    uint32_t size = 0;
    uint16_t N = 0, P = 0;
    uint8_t  form = 0, body = 0;
    float    lo = 0, hi = 0;

    static constexpr uint32_t kHeader = 4 + 8 + 8;
    uint32_t FixedBytes() const { return 4 + 8 * 4 + 5u * N; }

    /* a point is fixed bytes then its burst block, so the points are walked
       — at note-on, over a couple of hundred at most */
    const uint8_t* Point(int i) const
    {
        const uint8_t* q = blob + kHeader;
        for(int j = 0; j < i; j++) q = BurstEnd(q + FixedBytes());
        return q;
    }
    const uint8_t* Bursts(int i) const { return Point(i) + FixedBytes(); }
    static const uint8_t* BurstEnd(const uint8_t* b)
    {
        uint16_t nb; std::memcpy(&nb, b, 2);
        const uint8_t* q = b + 2;
        for(uint16_t k = 0; k < nb; k++) { uint16_t n; std::memcpy(&n, q + 8, 2); q += 10 + 2u * n; }
        return q;
    }

    bool Attach(const void* data, uint32_t bytes)
    {
        blob = (const uint8_t*)data; size = bytes;
        if(bytes < kHeader || std::memcmp(blob, "KYKM", 4) != 0) return false;
        uint16_t ver; std::memcpy(&ver, blob + 4, 2);
        std::memcpy(&N, blob + 6, 2); std::memcpy(&P, blob + 8, 2);
        form = blob[10]; body = blob[11];
        std::memcpy(&lo, blob + 12, 4); std::memcpy(&hi, blob + 16, 4);
        return ver == 3 && N <= ModalBank::kMax && size >= kHeader + (uint32_t)P * FixedBytes();
    }

    float Param(int i) const { float p; std::memcpy(&p, Point(i), 4); return p; }
    const uint8_t* Stage(int i) const { return Point(i) + 4; }
    const uint8_t* Modes(int i) const { return Point(i) + 4 + 32; }

    /* decode point i: hz, zeta, gain (unit = the point's loudest) per slot */
    void Decode(int i, float* hz, float* zeta, float* gain, float* phase) const
    {
        const uint8_t* m = Modes(i);
        for(int k = 0; k < N; k++)
        {
            uint16_t c; std::memcpy(&c, m + 5 * k, 2);
            hz[k]    = 20.0f * std::exp2(c / 1200.0f);
            zeta[k]  = std::exp(-0.1f * m[5 * k + 2]);
            gain[k]  = std::exp(-0.25f * m[5 * k + 3] * 0.1151293f);   /* dB -> linear */
            phase[k] = m[5 * k + 4] * (6.2831853f / 256.0f);
        }
    }

    /* the world at a parameter value, into a voice: the two neighbouring
       points interpolated by slot, the stage with them */
    void At(float param, ModalVoice& v, float sr) const
    {
        if(P == 0) return;
        int a = 0;
        while(a + 1 < P && Param(a + 1) <= param) a++;
        const int b = a + 1 < P ? a + 1 : a;
        const float pa = Param(a), pb = Param(b);
        const float t = pb > pa ? std::fmin(1.0f, std::fmax(0.0f, (param - pa) / (pb - pa))) : 0.0f;
        float ha[ModalBank::kMax], za[ModalBank::kMax], ga[ModalBank::kMax], fa[ModalBank::kMax];
        float hb[ModalBank::kMax], zb[ModalBank::kMax], gb[ModalBank::kMax], fb[ModalBank::kMax];
        Decode(a, ha, za, ga, fa);
        Decode(b, hb, zb, gb, fb);
        for(int k = 0; k < N; k++)
        {
            ha[k] = ha[k] * std::pow(hb[k] / ha[k], t);
            za[k] = za[k] * std::pow(zb[k] / za[k], t);
            ga[k] = ga[k] * std::pow((gb[k] + 1e-9f) / (ga[k] + 1e-9f), t);
            float d = fb[k] - fa[k];                 /* phase: the short way round */
            if(d > 3.1415927f) d -= 6.2831853f;
            if(d < -3.1415927f) d += 6.2831853f;
            fa[k] += t * d;
        }
        float st[8], sb[8];
        std::memcpy(st, Stage(a), 32);
        std::memcpy(sb, Stage(b), 32);
        for(int k = 0; k < 8; k++) st[k] += t * (sb[k] - st[k]);
        /* the level bytes are relative to the point's loudest mode; the
           swing into the field is absolute, so the gains get it back before
           the bank takes them */
        for(int k = 0; k < N; k++) ga[k] *= st[7];
        v.bank.Set(ha, za, ga, N, sr, fa);
        v.bursts = Bursts(t < 0.5f ? a : b);      /* a burst is not interpolated: the nearer point's */
        v.swing_soft = st[5]; v.swing_hard = st[6];
        if(form == 1) v.pickup.Set(st[0], st[1], st[2], st[3], st[4], sr);
        else if(form == 2) v.pickup.SetGap(st[1], st[2], st[3], st[4], sr);
        else v.pickup.on = false;
    }
};

} // namespace mb
