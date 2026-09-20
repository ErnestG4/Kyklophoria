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
#include <cstring>

namespace mb {

struct ModalBank
{
    static constexpr int kMax = 48;

    int   n = 0;
    float c1[kMax], c2[kMax];            /* 2 r cos w, -r^2 */
    float p1[kMax], p2[kMax];            /* the state a unit strike starts from: A sin(phi-w)/r, A sin(phi-2w)/r^2 */
    float y1[kMax], y2[kMax];
    /* the strike bank: a new hit rings here under a 3 ms raised-cosine ramp
       — the envelope the fit's model had, and the gains were fitted under —
       and is then added into the main state, which a linear bank allows
       exactly. An impulse into the main bank was a click the fit never
       heard; a 3 ms pulse would starve the high modes */
    float s1[kMax], s2[kMax];
    float ramp_n = 0.0f, ramp_len = 144.0f;
    bool  ramping = false;

    /* modes: frequency, decay, amplitude, and the phase at the strike. A
       decaying sine A r^n sin(n w + phi) obeys the recursion from any two
       consecutive samples, so a strike is not an impulse but a state: the
       two samples before n = 0, which is where the fitted phase — the
       hammer's timing per mode — goes in. From zero phase every partial
       rises together and the onset is a spike the recording never had */
    /* keep: retune the coefficients and leave the state ringing — a pitch
       change under a sounding note, which follows it as the oscillator's
       does; the strike bank's state is left too, mid-ramp */
    void Set(const float* hz, const float* zeta, const float* gain, int count, float sr, const float* phase = nullptr, bool keep = false)
    {
        n = count > kMax ? kMax : count;
        for(int i = 0; i < n; i++)
        {
            const float w = 6.2831853f * hz[i] / sr;
            const float r = std::exp(-zeta[i] * w);
            const float ph = phase ? phase[i] : 0.0f;
            c1[i] = 2.0f * r * std::cos(w);
            c2[i] = -r * r;
            p1[i] = gain[i] * std::sin(ph - w) / r;
            p2[i] = gain[i] * std::sin(ph - 2.0f * w) / (r * r);
            if(!keep) y1[i] = y2[i] = s1[i] = s2[i] = 0.0f;
        }
        ramp_len = 0.003f * sr;
        if(!keep) ramping = false;
    }

    /* the hammer: the strike bank set to the given swing at every mode's
       fitted phase. A
       strike while a strike is still ramping folds the earlier one into the
       main state first, un-ramped from there on — 3 ms of envelope is
       inaudible against a second hit that close */
    void Strike(float swing)
    {
        if(ramping) Fold();
        for(int i = 0; i < n; i++) { s1[i] = swing * p1[i]; s2[i] = swing * p2[i]; }
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
/* The attack the modes are not: a stored burst a point (tools/bursts.py,
 * the recording's first 40 ms minus the model's), played at strike time
 * after the pickup, scaled by the strike's swing over the burst's own. A
 * shaped world has a burst a take and crossfades the two that bracket the
 * strike's swing — hammer noise that follows velocity. The samples are
 * 16-bit in the world blob and read from there; nothing is copied. Two
 * bursts at once at most (the crossfade), each a pointer, a position, a
 * gain. */
struct BurstPlayer
{
    struct Slot { const int16_t* s; uint32_t n, pos; float gain; };
    Slot slot[2];
    int  active;

    void Init() { active = 0; for(auto& q : slot) { q.s = nullptr; q.n = q.pos = 0; q.gain = 0.0f; } }

    /* bursts: pointer to the point's burst block in the world (nbursts,
       then per burst swing, scale, len, samples), and the strike's swing */
    void Strike(const uint8_t* block, float swing)
    {
        active = 0;
        if(!block) return;
        uint16_t nb; std::memcpy(&nb, block, 2);
        const uint8_t* q = block + 2;
        /* find the two bursts bracketing the swing (bursts are in ascending swing) */
        const uint8_t* lo = nullptr; const uint8_t* hi = nullptr;
        float slo = 0.0f, shi = 0.0f;
        for(uint16_t i = 0; i < nb; i++)
        {
            float sw, sc; uint16_t n;
            std::memcpy(&sw, q, 4); std::memcpy(&sc, q + 4, 4); std::memcpy(&n, q + 8, 2);
            if(!lo || sw <= swing) { lo = q; slo = sw; }
            if(sw >= swing) { hi = q; shi = sw; break; }
            q += 10 + 2u * n;
        }
        if(!lo) return;
        if(!hi) { hi = lo; shi = slo; }
        const float t = (hi != lo && shi > slo) ? (swing - slo) / (shi - slo) : 0.0f;
        const uint8_t* pick[2] = { lo, hi };
        const float wgt[2] = { 1.f - t, t };
        const float ref[2] = { slo, shi };
        for(int i = 0; i < 2; i++)
        {
            if(wgt[i] <= 0.0f || (i == 1 && hi == lo)) continue;
            float sw, sc; uint16_t n;
            std::memcpy(&sw, pick[i], 4); std::memcpy(&sc, pick[i] + 4, 4); std::memcpy(&n, pick[i] + 8, 2);
            Slot& q2 = slot[active++];
            q2.s = (const int16_t*)(pick[i] + 10);
            q2.n = n; q2.pos = 0;
            q2.gain = wgt[i] * sc / 32767.f * (ref[i] > 0.0f ? swing / ref[i] : 1.f);
        }
    }

    void Process(float* io, int frames)
    {
        for(int a = 0; a < active; a++)
        {
            Slot& q = slot[a];
            for(int k = 0; k < frames && q.pos < q.n; k++, q.pos++)
            {
                int16_t v; std::memcpy(&v, q.s + q.pos, 2);   /* the blob may be unaligned */
                io[k] += q.gain * v;
            }
        }
    }
};

struct ModalVoice
{
    ModalBank      bank;
    Pickup         pickup;
    BurstPlayer    burst{};
    const uint8_t* bursts = nullptr;  /* the point's burst block in the world, or null */
    float          swing_soft = 1.0f, swing_hard = 1.0f;

    void Strike(float velocity01)
    {
        /* between the softest and hardest take in log; a world fitted from
           one take (swing_soft == swing_hard) scales linearly with velocity */
        const float s = swing_hard > swing_soft ? swing_soft * std::pow(swing_hard / swing_soft, velocity01) : swing_soft * velocity01;
        bank.Strike(s);
        burst.Strike(bursts, s);
    }
    void Process(float* out, int frames)
    {
        bank.Process(out, frames);
        pickup.Process(out, frames);
        burst.Process(out, frames);
    }
};

} // namespace mb
