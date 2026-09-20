/* modal_bank.h — a fitted world at runtime: resonators, a hammer, a pickup.
 *
 * The prototype of what Kyklophoria's World::Kind::Modal will run. Header-
 * only, float, no heap, no exceptions, no RTTI, fixed maximum of modes; it
 * compiles for the M7 so that its cost can be counted (make armcost) before
 * it is believed.
 *
 * Bank.   One second-order resonator a mode,
 *             y[n] = 2 r cos(w) y[n-1] - r^2 y[n-2] + x[n],
 *         r = exp(-zeta w), whose impulse response is r^n sin((n+1) w) / sin(w):
 *         a decaying sine at the mode's frequency and decay, so the gain is
 *         multiplied by sin(w) once at Set() and the bank is three
 *         multiply-adds a mode a sample. Every strike excites every mode with the same pulse
 *         — the mode's gain is where the strike position lives, as in the
 *         records — and the pulse is a 0.1 ms raised cosine rather than an
 *         impulse. The hammer's own spectrum is already in the fitted gains
 *         (the fit's model ramps its envelope over 3 ms and the gains were
 *         fitted with that), so the pulse is not the hammer; it is only
 *         there so that the strike itself does not splatter above 12 kHz,
 *         which is where an impulse's click lives (docs/findings.md).
 *
 * Pickup. The stage a record carries as `shaper bell h w K fc Q`: the bank's
 *         sum is the tine's displacement u, the coil sees the field
 *             phi = 1 / (1 + ((u - h) / w)^2),
 *         Faraday makes it the first difference of phi, and the coil's own LR
 *         resonance is a second-order low-pass at fc, Q. One division, one
 *         subtraction and a biquad a sample. A world without a pickup line
 *         bypasses it and the sum is the sound.
 *
 * Velocity is a swing: the softest and hardest takes of the fit gave two
 * swings and the world interpolates in log between them, and past them, since
 * the field does not care what was recorded.
 */
#pragma once
#include <cmath>
#include <cstdint>

namespace mb {

struct ModalBank
{
    static constexpr int kMax = 48;

    int   n = 0;
    float c1[kMax], c2[kMax], g[kMax];   /* 2 r cos w, -r^2, gain * sin w */
    float y1[kMax], y2[kMax];
    float pulse_left = 0.0f, pulse_n = 0.0f, pulse_amp = 0.0f;
    float pulse_len = 1.0f;

    void Set(const float* hz, const float* zeta, const float* gain, int count, float sr)
    {
        n = count > kMax ? kMax : count;
        for(int i = 0; i < n; i++)
        {
            const float w = 6.2831853f * hz[i] / sr;
            const float r = std::exp(-zeta[i] * w);
            c1[i] = 2.0f * r * std::cos(w);
            c2[i] = -r * r;
            g[i]  = gain[i] * std::sin(w);
            y1[i] = y2[i] = 0.0f;
        }
        pulse_len = 0.0001f * sr < 4.0f ? 4.0f : 0.0001f * sr;
    }

    /* the hammer: a raised-cosine pulse of the given swing, added to
       whatever is still ringing */
    void Strike(float swing)
    {
        pulse_amp  = swing;
        pulse_n    = 0.0f;
        pulse_left = pulse_len;
    }

    /* the displacement, summed over the modes, into out (overwrite) */
    void Process(float* out, int frames)
    {
        for(int k = 0; k < frames; k++)
        {
            float x = 0.0f;
            if(pulse_left > 0.0f)
            {
                /* a raised cosine whose samples sum to the swing: the same
                   energy into every mode as an impulse, without the impulse's
                   splatter */
                const float p = (pulse_n + 0.5f) / pulse_len;
                x = pulse_amp * (1.0f - std::cos(6.2831853f * p)) / pulse_len;
                pulse_n += 1.0f;
                pulse_left -= 1.0f;
            }
            float acc = 0.0f;
            for(int i = 0; i < n; i++)
            {
                const float y = c1[i] * y1[i] + c2[i] * y2[i] + x * g[i];
                y2[i] = y1[i];
                y1[i] = y;
                acc += y;
            }
            out[k] = acc;
        }
    }
};

struct Pickup
{
    bool  on = false, gap = false;
    float h = 0.0f, inv_w = 1.0f, K = 1.0f;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   /* the coil */
    float prev = 0.0f, z1 = 0.0f, z2 = 0.0f;

    /* the electrostatic plate: C = C0 / (1 - u/g), the reed short of the
       plate through a tanh, as the fit had it */
    void SetGap(float g_, float K_, float fc, float Q, float sr)
    {
        Set(0.0f, g_, K_, fc, Q, sr);
        gap = true;
        prev = 1.0f;
    }

    void Set(float h_, float w_, float K_, float fc, float Q, float sr)
    {
        on = true; gap = false; h = h_; inv_w = 1.0f / w_; K = K_;
        const float w0 = 6.2831853f * fc / sr;
        const float alpha = std::sin(w0) / (2.0f * Q);
        const float cw = std::cos(w0), a0 = 1.0f + alpha;
        b0 = (1.0f - cw) * 0.5f / a0; b1 = (1.0f - cw) / a0; b2 = b0;
        a1 = -2.0f * cw / a0; a2 = (1.0f - alpha) / a0;
        const float u0 = (0.0f - h) * inv_w;
        prev = 1.0f / (1.0f + u0 * u0);            /* the field at rest: no click at power-on */
        z1 = z2 = 0.0f;
    }

    /* in place: displacement in, coil voltage out */
    void Process(float* io, int frames)
    {
        if(!on) return;
        for(int k = 0; k < frames; k++)
        {
            const float u   = (io[k] - h) * inv_w;
            const float phi = gap ? 1.0f / (1.0f - 0.9f * std::tanh(u / 0.9f)) : 1.0f / (1.0f + u * u);
            const float d   = phi - prev;
            prev = phi;
            /* transposed direct form II */
            const float y = b0 * d + z1;
            z1 = b1 * d - a1 * y + z2;
            z2 = b2 * d - a2 * y;
            io[k] = K * y;
        }
    }
};

/* A world: a bank and its pickup, and the two swings the fit measured. */
struct ModalVoice
{
    ModalBank bank;
    Pickup    pickup;
    float     swing_soft = 1.0f, swing_hard = 1.0f;

    void Strike(float velocity01)
    {
        const float s = swing_soft * std::pow(swing_hard / swing_soft, velocity01);
        bank.Strike(s);
    }
    void Process(float* out, int frames)
    {
        bank.Process(out, frames);
        pickup.Process(out, frames);
    }
};

} // namespace mb
