/* kyk_osc.h — phase accumulator over a double-buffered single-cycle frame.
 *
 * Two frames: the one the last block ended on ("from") and the one this
 * block renders ("to"). Every sample reads both at the same phase and
 * crossfades linearly across the block, so a frame rendered per block
 * becomes a continuous morph and a frame held for several blocks fades in
 * over its first. The phase is a Q32 integer: pitch is deterministic and
 * wraps for free.
 *
 * Reads: 4-point Hermite (cubic) by default, linear with KYK_OSC_LINEAR.
 */
#pragma once
#include "kyk_types.h"

namespace kyk {

class Osc
{
public:
    void Init()
    {
        for(int i = 0; i < kFrame; i++) { frame_[0][i] = 0.f; frame_[1][i] = 0.f; }
        cur_   = 0;
        phase_ = 0;
        inc_   = 0;
    }

    /* f0 / sr as a Q32 increment. float arithmetic: 24-bit mantissa ⇒
     * pitch resolution sr/2^24 ≈ 0.003 Hz at 48 kHz, same on every target. */
    void SetFreq(float f0, float sr)
    {
        float r = f0 / sr;
        if(r < 0.f) r = 0.f;
        if(r >= 0.5f) r = 0.5f;
        inc_ = (uint32_t)(r * 4294967296.0f);
    }

    /* The buffer to render the next frame into. Call Process() afterwards. */
    float*       Back() { return frame_[cur_ ^ 1]; }
    const float* Front() const { return frame_[cur_]; }
    uint32_t     Phase() const { return phase_; }
    void         ResetPhase(uint32_t p = 0) { phase_ = p; }

    /* Crossfade Front → Back over n samples, then Back becomes Front. If the
     * caller rendered nothing new, pass new_frame=false: the block reads the
     * front frame only. */
    void Process(float* out, int n, bool new_frame)
    {
        const float* a = frame_[cur_];
        const float* b = frame_[cur_ ^ 1];
        const float  dt = 1.f / (float)n;
        float        t  = 0.f;
        for(int i = 0; i < n; i++)
        {
            const float sa = Read(a, phase_);
            if(new_frame)
            {
                t += dt;
                const float sb = Read(b, phase_);
                out[i]         = sa + (sb - sa) * t;
            }
            else out[i] = sa;
            phase_ += inc_;
        }
        if(new_frame) cur_ ^= 1;
    }

    /* Copy another oscillator's front frame and phase into our front. */
    void AdoptFront(const Osc& o)
    {
        const float* src = o.Front();
        float*       dst = frame_[cur_];
        for(int i = 0; i < kFrame; i++) dst[i] = src[i];
        phase_ = o.phase_;
        inc_   = o.inc_;
    }

    static float Read(const float* f, uint32_t phase)
    {
        constexpr int      shift = 32 - kFrameLog2;
        constexpr uint32_t mask  = (uint32_t)kFrame - 1u;
        const uint32_t     i     = phase >> shift;
        const float        x     = (float)(phase & ((1u << shift) - 1u)) * (1.0f / (float)(1u << shift));
#ifdef KYK_OSC_LINEAR
        const float y0 = f[i], y1 = f[(i + 1) & mask];
        return y0 + (y1 - y0) * x;
#else
        const float ym = f[(i + mask) & mask], y0 = f[i], y1 = f[(i + 1) & mask], y2 = f[(i + 2) & mask];
        /* 4-point, 3rd-order Hermite (Catmull-Rom) */
        const float c1 = 0.5f * (y1 - ym);
        const float c2 = ym - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
        return ((c3 * x + c2) * x + c1) * x + y0;
#endif
    }

private:
    float    frame_[2][kFrame];
    int      cur_   = 0;
    uint32_t phase_ = 0;
    uint32_t inc_   = 0;
};

} // namespace kyk
