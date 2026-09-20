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
 *     y[n] = 2 r cos w · y[n-1] − r² · y[n-2],   r = exp(−ζ w),
 * a decaying sine at the mode's frequency and decay, three multiply-adds a mode
 * a sample. A strike is a *state*, not an impulse: a decaying sine obeys the
 * recursion from any two consecutive samples, so a strike bank of the same
 * modes is set to the two samples before n = 0 at the strike's swing and
 * every mode's fitted phase, its output rises under a 3 ms raised cosine —
 * the envelope the fit's model had and the gains were fitted under — and
 * its state is then added into the main bank's, which a linear bank allows
 * exactly. Two banks for 144 samples, then one. The phase is where the
 * hammer's timing per mode lives: from zero phase every partial rises
 * together and the onset is a spike the recording never had (ModalBake
 * measured 1.7x the target's energy in the first 40 ms from zero phase,
 * 1.0x from the fitted phases).
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
 * The world. A `.kykm` blob attached where it lies: five bytes a mode
 * (cents from 20 Hz, a log-decay byte, a quarter-dB byte under the point's
 * loudest, a phase byte), and per point the stage and the loudest mode's absolute gain,
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
    float c1[kMax], c2[kMax];            /* 2 r cos w, -r^2 */
    float p1[kMax], p2[kMax];            /* the state a unit strike starts from: A sin(phi-w)/r, A sin(phi-2w)/r^2 */
    float y1[kMax], y2[kMax];
    /* the strike bank: a new hit rings here under a 3 ms raised-cosine ramp
       — the envelope the fit's model had, and the gains were fitted under —
       and is then added into the main state, which a linear bank allows
       exactly. An impulse into the main bank was a click the fit never
       heard; a 3 ms pulse would starve the high modes */
    float s1[kMax], s2[kMax];
    float ramp_n, ramp_len;
    bool  ramping;

    void Init()
    {
        n = 0;
        ramp_n = 0.f; ramp_len = 144.f; ramping = false;
        for(int i = 0; i < kMax; i++) c1[i] = c2[i] = p1[i] = p2[i] = y1[i] = y2[i] = s1[i] = s2[i] = 0.f;
    }

    /* modes: frequency, decay, amplitude, and the phase at the strike. A
       decaying sine A r^n sin(n w + phi) obeys the recursion from any two
       consecutive samples, so a strike is not an impulse but a state: the
       two samples before n = 0, which is where the fitted phase — the
       hammer's timing per mode — goes in. From zero phase every partial
       rises together and the onset is a spike the recording never had */
    void Set(const float* hz, const float* zeta, const float* gain, int count, float sr, const float* phase = nullptr)
    {
        n = count > kMax ? kMax : count;
        for(int i = 0; i < n; i++)
        {
            const float w = 6.2831853f * hz[i] / sr;
            const float r = std::exp(-zeta[i] * w);
            const float ph = phase ? phase[i] : 0.f;
            c1[i] = 2.0f * r * std::cos(w);
            c2[i] = -r * r;
            p1[i] = gain[i] * std::sin(ph - w) / r;
            p2[i] = gain[i] * std::sin(ph - 2.0f * w) / (r * r);
            y1[i] = y2[i] = s1[i] = s2[i] = 0.f;
        }
        ramp_len = 0.003f * sr;
        ramping = false;
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
        ramp_n = 0.f;
        ramping = true;
    }

    void Fold()
    {
        for(int i = 0; i < n; i++) { y1[i] += s1[i]; y2[i] += s2[i]; s1[i] = s2[i] = 0.f; }
        ramping = false;
    }

    /* the displacement, summed over the modes, into out (overwrite) */
    void Process(float* out, int frames)
    {
        for(int k = 0; k < frames; k++)
        {
            float acc = 0.f;
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
                float sacc = 0.f;
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

    void Init() { active = 0; for(auto& q : slot) { q.s = nullptr; q.n = q.pos = 0; q.gain = 0.f; } }

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
        float slo = 0.f, shi = 0.f;
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
        const float t = (hi != lo && shi > slo) ? (swing - slo) / (shi - slo) : 0.f;
        const uint8_t* pick[2] = { lo, hi };
        const float wgt[2] = { 1.f - t, t };
        const float ref[2] = { slo, shi };
        for(int i = 0; i < 2; i++)
        {
            if(wgt[i] <= 0.f || (i == 1 && hi == lo)) continue;
            float sw, sc; uint16_t n;
            std::memcpy(&sw, pick[i], 4); std::memcpy(&sc, pick[i] + 4, 4); std::memcpy(&n, pick[i] + 8, 2);
            Slot& q2 = slot[active++];
            q2.s = (const int16_t*)(pick[i] + 10);
            q2.n = n; q2.pos = 0;
            q2.gain = wgt[i] * sc / 32767.f * (ref[i] > 0.f ? swing / ref[i] : 1.f);
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

struct ResonatorVoice
{
    ResonatorBank  bank;
    Pickup         pickup;
    BurstPlayer    burst;
    const uint8_t* bursts;            /* the point's burst block in the world, or null */
    float          swing_soft, swing_hard;

    void Init() { bank.Init(); pickup.Init(); burst.Init(); bursts = nullptr; swing_soft = swing_hard = 1.f; }

    void Strike(float velocity01)
    {
        const float s = swing_soft * std::pow(swing_hard / swing_soft, velocity01);
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

/* A condensed world (ModalBake tools/export.py), attached where it lies. */
struct ResonatorWorld
{
    const uint8_t* blob;
    uint32_t size;
    uint16_t N, P;
    uint8_t  form, body;
    float    lo, hi;
    /* the spin: what a pot does to a loaded point. voicing moves the pole
       off centre by that many widths on top of the fitted h; decay
       multiplies every mode's T60; coil multiplies the coil's fc. Applied at
       At(), so a sent world keeps its own fitted values as the centre */
    float    voicing, decay, coil;

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

    void Init() { blob = nullptr; size = 0; N = P = 0; form = body = 0; lo = hi = 0.f; voicing = 0.f; decay = coil = 1.f; }

    bool Attach(const void* data, uint32_t bytes)
    {
        blob = (const uint8_t*)data; size = bytes;
        if(bytes < kHeader || std::memcmp(blob, "KYKM", 4) != 0) return false;
        uint16_t ver; std::memcpy(&ver, blob + 4, 2);
        std::memcpy(&N, blob + 6, 2); std::memcpy(&P, blob + 8, 2);
        form = blob[10]; body = blob[11];
        std::memcpy(&lo, blob + 12, 4); std::memcpy(&hi, blob + 16, 4);
        return ver == 3 && N <= ResonatorBank::kMax && size >= kHeader + (uint32_t)P * FixedBytes();
    }

    float Param(int i) const { float p; std::memcpy(&p, Point(i), 4); return p; }
    const uint8_t* Stage(int i) const { return Point(i) + 4; }
    const uint8_t* Modes(int i) const { return Point(i) + 4 + 32; }

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
    void At(float param, ResonatorVoice& v, float sr) const
    {
        if(P == 0) return;
        int a = 0;
        while(a + 1 < P && Param(a + 1) <= param) a++;
        const int b = a + 1 < P ? a + 1 : a;
        const float pa = Param(a), pb = Param(b);
        const float t = pb > pa ? std::fmin(1.f, std::fmax(0.f, (param - pa) / (pb - pa))) : 0.f;
        float ha[ResonatorBank::kMax], za[ResonatorBank::kMax], ga[ResonatorBank::kMax], fa[ResonatorBank::kMax];
        float hb[ResonatorBank::kMax], zb[ResonatorBank::kMax], gb[ResonatorBank::kMax], fb[ResonatorBank::kMax];
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
        for(int k = 0; k < N; k++) za[k] /= decay;            /* T60 x decay */
        v.bank.Set(ha, za, ga, N, sr, fa);
        st[0] += voicing * st[1];                                /* h moves by widths */
        st[3] *= coil;
        v.bursts = Bursts(t < 0.5f ? a : b);      /* a burst is not interpolated: the nearer point's */
        v.swing_soft = st[5]; v.swing_hard = st[6];
        if(form == 1) v.pickup.Set(st[0], st[1], st[2], st[3], st[4], sr);
        else if(form == 2) v.pickup.SetGap(st[1], st[2], st[3], st[4], sr);
        else v.pickup.on = false;
    }
};

} // namespace kyk
