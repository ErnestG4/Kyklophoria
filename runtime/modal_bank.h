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
 *         multiply-adds a mode a sample. A strike is an impulse of the
 *         strike's swing into a second bank of the same modes, whose output
 *         rises under a 3 ms raised cosine — the envelope the fit's model had
 *         and the gains were fitted under — and whose state is then added
 *         into the main bank's, which a linear bank allows exactly. An
 *         impulse into the main bank was a click the fit never heard; a 3 ms
 *         pulse would starve the high modes.
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
    /* the strike bank: a new hit rings here under a 3 ms raised-cosine ramp
       — the envelope the fit's model had, and the gains were fitted under —
       and is then added into the main state, which a linear bank allows
       exactly. An impulse into the main bank was a click the fit never
       heard; a 3 ms pulse would starve the high modes */
    float s1[kMax], s2[kMax];
    float ramp_n = 0.0f, ramp_len = 144.0f;
    bool  ramping = false;

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
            y1[i] = y2[i] = s1[i] = s2[i] = 0.0f;
        }
        ramp_len = 0.003f * sr;
        ramping = false;
    }

    /* the hammer: an impulse of the given swing into the strike bank. A
       strike while a strike is still ramping folds the earlier one into the
       main state first, un-ramped from there on — 3 ms of envelope is
       inaudible against a second hit that close */
    void Strike(float swing)
    {
        if(ramping) Fold();
        for(int i = 0; i < n; i++) { s1[i] = swing * g[i]; s2[i] = 0.0f; }
        ramp_n = 0.0f;
        ramping = true;
    }

    void Fold()
    {
        for(int i = 0; i < n; i++) { y1[i] += s1[i]; y2[i] += s2[i]; s1[i] = s2[i] = 0.0f; }
        ramping = false;
    }

    /* the displacement, summed over the modes, into out (overwrite) */
    void Process(float* out, int frames)
    {
        for(int k = 0; k < frames; k++)
        {
            float acc = 0.0f;
            for(int i = 0; i < n; i++)
            {
                const float y = c1[i] * y1[i] + c2[i] * y2[i];
                y2[i] = y1[i];
                y1[i] = y;
                acc += y;
            }
            if(ramping)
            {
                const float r = 0.5f - 0.5f * std::cos(3.1415927f * ramp_n / ramp_len);
                float sacc = 0.0f;
                for(int i = 0; i < n; i++)
                {
                    const float y = c1[i] * s1[i] + c2[i] * s2[i];
                    s2[i] = s1[i];
                    s1[i] = y;
                    sacc += y;
                }
                acc += r * sacc;
                if(++ramp_n >= ramp_len) Fold();
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

    /* tanh without libm: a (3,2) Pade clamped at |x| = 3, within 0.024 of
       the real thing everywhere, for the reed's plate-bound. The whole-
       function count rises by about thirty instructions with it inline;
       libm's tanh counted as one — a bl — and cost about a hundred cycles
       a sample, which the count did not see */
    static float Tanh(float x)
    {
        x = x > 3.f ? 3.f : (x < -3.f ? -3.f : x);
        const float x2 = x * x;
        const float y = x * (27.f + x2) / (27.f + 9.f * x2);
        return y > 1.f ? 1.f : (y < -1.f ? -1.f : y);
    }

    /* in place: displacement in, coil voltage out */
    void Process(float* io, int frames)
    {
        if(!on) return;
        for(int k = 0; k < frames; k++)
        {
            const float u   = (io[k] - h) * inv_w;
            const float phi = gap ? 1.0f / (1.0f - 0.9f * Tanh(u / 0.9f)) : 1.0f / (1.0f + u * u);
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
