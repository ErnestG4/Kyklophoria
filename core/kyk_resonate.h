/* kyk_resonate.h — a fitted world played as resonators: the modal mode.
 *
 * The modal worlds in kyk_modal.h are tuned objects harmonicised into a
 * frame, because a frame periodic at f0 can hold nothing else. This is the
 * other thing: a bank of second-order resonators at the *measured*
 * frequencies and decays of a recorded or FEM-fitted note, struck, and — for
 * an electric world — read through its pickup. Nothing here is a frame; it
 * rings in real time and dies at the rate the recording did. The worlds
 * come from ModalBake (tools/export.py, `.kykm`), which is where the fitting,
 * the measurements and the reasons live; this file is only the runtime,
 * and it is a port of ModalBake's runtime/modal_bank.h and world.h into the
 * conventions of core/: header-only, float, no heap, no exceptions, no RTTI,
 * and nothing with a default member initialiser, so that a voice may sit in
 * .sdram_bss (CLAUDE.md, the FMC trap). Init() is what a constructor would
 * have been.
 *
 * The bank. One resonator a mode,
 *     y[n] = 2 r cos w · y[n-1] − r² · y[n-2] + x[n],   r = exp(−ζ w),
 * whose impulse response is r^n sin((n+1)w) / sin w: a decaying sine at the
 * mode's frequency and decay, so the gain is multiplied by sin w once at
 * Set() and the loop is three multiply-adds a mode a sample. A strike is a
 * 0.1 ms raised-cosine pulse of the strike's swing into every mode — the
 * hammer's own spectrum is already in the fitted gains; the pulse is only so
 * that the hit does not splatter above 12 kHz the way an impulse does.
 *
 * The pickup. The stage a shaped world carries per point: the bank's sum is
 * the tine's displacement u; a magnetic pole sees phi = 1/(1 + ((u−h)/w)²)
 * (h the voicing screw, and the reason there is a fundamental at all); an
 * electrostatic plate sees phi = 1/(1 − u/g); Faraday makes the coil's
 * voltage the first difference of phi; the coil's own LR resonance is a
 * second-order low-pass at fc, Q. Every harmonic an electric piano makes,
 * and the growl when it is played hard, comes out of these lines — the
 * metal itself is one sine (Muenster & Pfeifle, 38 kfps; ModalBake's
 * findings). Velocity is a swing, log-interpolated between the softest and
 * hardest take the fit saw, and past them.
 *
 * The world. A `.kykm` blob attached where it lies: four bytes a mode
 * (cents from 20 Hz, a log-decay byte, a quarter-dB byte under the point's
 * loudest), and per point the stage and the loudest mode's absolute gain,
 * because the swing into the field is absolute and the level bytes are not
 * — without it the pickup was driven 2.4x too hard and barked 15 dB early. A point is decoded at note-on, never a sample, and a parameter
 * between two points interpolates them by slot in log frequency, log decay
 * and dB, which is a partial gliding to its neighbour's place.
 *
 * Cost, counted (make armcost): about fifteen M7 instructions a mode a
 * sample for the bank, thirty a sample for a magnetic pickup, more for an
 * electrostatic one while its tanh is libm's. Forty-eight modes is under a
 * tenth of the core at 48 kHz; an electric world is a handful of modes.
 *
 * What this file does not do: decide what excites the bank. That is the
 * instrument's question — the oscillator, a gate, a burst — and it is
 * answered in the engine with hands on the module, not here
 * (docs/modal-mode.md).
 */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace kyk {

struct ResonatorBank
{
    static constexpr int kMax = 48;

    int   n;
    float c1[kMax], c2[kMax], g[kMax];   /* 2 r cos w, −r², gain · sin w */
    float y1[kMax], y2[kMax];
    float pulse_left, pulse_n, pulse_amp, pulse_len;

    void Init()
    {
        n = 0;
        pulse_left = pulse_n = pulse_amp = 0.f;
        pulse_len = 1.f;
        for(int i = 0; i < kMax; i++) c1[i] = c2[i] = g[i] = y1[i] = y2[i] = 0.f;
    }

    void Set(const float* hz, const float* zeta, const float* gain, int count, float sr)
    {
        n = count > kMax ? kMax : count;
        for(int i = 0; i < n; i++)
        {
            const float w = 6.2831853f * hz[i] / sr;
            const float r = std::exp(-zeta[i] * w);
            c1[i] = 2.f * r * std::cos(w);
            c2[i] = -r * r;
            g[i]  = gain[i] * std::sin(w);
            y1[i] = y2[i] = 0.f;
        }
        pulse_len = 0.0001f * sr < 4.f ? 4.f : 0.0001f * sr;
    }

    /* the hammer: a raised-cosine pulse of the given swing, added to
       whatever is still ringing */
    void Strike(float swing)
    {
        pulse_amp  = swing;
        pulse_n    = 0.f;
        pulse_left = pulse_len;
    }

    /* the displacement, summed over the modes, into out (overwrite) */
    void Process(float* out, int frames)
    {
        for(int k = 0; k < frames; k++)
        {
            float x = 0.f;
            if(pulse_left > 0.f)
            {
                const float p = (pulse_n + 0.5f) / pulse_len;
                x = pulse_amp * (1.f - std::cos(6.2831853f * p)) / pulse_len;
                pulse_n += 1.f;
                pulse_left -= 1.f;
            }
            float acc = 0.f;
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
    bool  on, gap;
    float h, inv_w, K;
    float b0, b1, b2, a1, a2;     /* the coil */
    float prev, z1, z2;

    void Init() { on = gap = false; h = 0.f; inv_w = K = 1.f; b0 = 1.f; b1 = b2 = a1 = a2 = 0.f; prev = z1 = z2 = 0.f; }

    void Set(float h_, float w_, float K_, float fc, float Q, float sr)
    {
        on = true; gap = false; h = h_; inv_w = 1.f / w_; K = K_;
        const float w0 = 6.2831853f * fc / sr;
        const float alpha = std::sin(w0) / (2.f * Q);
        const float cw = std::cos(w0), a0 = 1.f + alpha;
        b0 = (1.f - cw) * 0.5f / a0; b1 = (1.f - cw) / a0; b2 = b0;
        a1 = -2.f * cw / a0; a2 = (1.f - alpha) / a0;
        const float u0 = (0.f - h) * inv_w;
        prev = 1.f / (1.f + u0 * u0);            /* the field at rest: no click at power-on */
        z1 = z2 = 0.f;
    }

    /* the electrostatic plate: C = C0 / (1 − u/g), the reed kept short of the
       plate by a tanh, as the fit had it */
    void SetGap(float g_, float K_, float fc, float Q, float sr)
    {
        Set(0.f, g_, K_, fc, Q, sr);
        gap = true;
        prev = 1.f;
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
            const float phi = gap ? 1.f / (1.f - 0.9f * Tanh(u / 0.9f)) : 1.f / (1.f + u * u);
            const float d   = phi - prev;
            prev = phi;
            const float y = b0 * d + z1;          /* transposed direct form II */
            z1 = b1 * d - a1 * y + z2;
            z2 = b2 * d - a2 * y;
            io[k] = K * y;
        }
    }
};

struct ResonatorVoice
{
    ResonatorBank bank;
    Pickup        pickup;
    float         swing_soft, swing_hard;

    void Init() { bank.Init(); pickup.Init(); swing_soft = swing_hard = 1.f; }

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

/* A condensed world (ModalBake tools/export.py), attached where it lies. */
struct ResonatorWorld
{
    const uint8_t* blob;
    uint32_t size;
    uint16_t N, P;
    uint8_t  form, body;
    float    lo, hi;

    static constexpr uint32_t kHeader = 4 + 8 + 8;
    uint32_t PointBytes() const { return 4 + 8 * 4 + 4u * N; }

    void Init() { blob = nullptr; size = 0; N = P = 0; form = body = 0; lo = hi = 0.f; }

    bool Attach(const void* data, uint32_t bytes)
    {
        blob = (const uint8_t*)data; size = bytes;
        if(bytes < kHeader || std::memcmp(blob, "KYKM", 4) != 0) return false;
        uint16_t ver; std::memcpy(&ver, blob + 4, 2);
        std::memcpy(&N, blob + 6, 2); std::memcpy(&P, blob + 8, 2);
        form = blob[10]; body = blob[11];
        std::memcpy(&lo, blob + 12, 4); std::memcpy(&hi, blob + 16, 4);
        return ver == 1 && N <= ResonatorBank::kMax && size >= kHeader + (uint32_t)P * PointBytes();
    }

    float Param(int i) const { float p; std::memcpy(&p, blob + kHeader + i * PointBytes(), 4); return p; }
    const uint8_t* Stage(int i) const { return blob + kHeader + i * PointBytes() + 4; }
    const uint8_t* Modes(int i) const { return blob + kHeader + i * PointBytes() + 4 + 32; }

    void Decode(int i, float* hz, float* zeta, float* gain) const
    {
        const uint8_t* m = Modes(i);
        for(int k = 0; k < N; k++)
        {
            uint16_t c; std::memcpy(&c, m + 4 * k, 2);
            hz[k]   = 20.f * std::exp2(c / 1200.f);
            zeta[k] = std::exp(-0.1f * m[4 * k + 2]);
            gain[k] = std::exp(-0.25f * m[4 * k + 3] * 0.1151293f);   /* dB → linear */
        }
    }

    /* the world at a parameter value, into a voice: the two neighbouring
       points interpolated by slot, the stage with them */
    void At(float param, ResonatorVoice& v, float sr) const
    {
        if(P == 0) return;
        int a = 0;
        while(a + 1 < P && Param(a + 1) <= param) a++;
        const int b = a + 1 < P ? a + 1 : a;
        const float pa = Param(a), pb = Param(b);
        const float t = pb > pa ? std::fmin(1.f, std::fmax(0.f, (param - pa) / (pb - pa))) : 0.f;
        float ha[ResonatorBank::kMax], za[ResonatorBank::kMax], ga[ResonatorBank::kMax];
        float hb[ResonatorBank::kMax], zb[ResonatorBank::kMax], gb[ResonatorBank::kMax];
        Decode(a, ha, za, ga);
        Decode(b, hb, zb, gb);
        for(int k = 0; k < N; k++)
        {
            ha[k] = ha[k] * std::pow(hb[k] / ha[k], t);
            za[k] = za[k] * std::pow(zb[k] / za[k], t);
            ga[k] = ga[k] * std::pow((gb[k] + 1e-9f) / (ga[k] + 1e-9f), t);
        }
        float st[8], sb[8];
        std::memcpy(st, Stage(a), 32);
        std::memcpy(sb, Stage(b), 32);
        for(int k = 0; k < 8; k++) st[k] += t * (sb[k] - st[k]);
        /* the level bytes are relative to the point's loudest mode; the swing
           into the field is absolute, so the gains get the loudest back —
           before the bank takes them, which is where this once went wrong */
        for(int k = 0; k < N; k++) ga[k] *= st[7];
        v.bank.Set(ha, za, ga, N, sr);
        v.swing_soft = st[5]; v.swing_hard = st[6];
        if(form == 1) v.pickup.Set(st[0], st[1], st[2], st[3], st[4], sr);
        else if(form == 2) v.pickup.SetGap(st[1], st[2], st[3], st[4], sr);
        else v.pickup.on = false;
    }
};

} // namespace kyk
