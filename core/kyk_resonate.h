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
    float ramp_n, ramp_len, ramp_lead;
    bool  ramping;

    void Init()
    {
        n = 0;
        ramp_n = 0.f; ramp_len = 144.f; ramp_lead = 0.f; ramping = false;
        for(int i = 0; i < kMax; i++) c1[i] = c2[i] = p1[i] = p2[i] = y1[i] = y2[i] = s1[i] = s2[i] = 0.f;
    }

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
            const float ph = phase ? phase[i] : 0.f;
            if(keep && c2[i] < 0.f)
            {
                /* the state carried across a retune as what it is — an
                   amplitude and a phase — and not as two samples. Two
                   samples of a slow oscillation read under a fast pole
                   are a small amplitude; of a fast one under a slow pole,
                   a huge one ((y1 - y2) / sin w): a gong's modes retuned
                   to a tom's came back 28 dB louder. The old pole gives the
                   old w and r; the pair is solved for A sin(phi) and
                   A cos(phi) and rewritten under the new pole. Both the
                   ringing state and the strike still ramping in */
                const float r0 = std::sqrt(-c2[i]);
                float cw = c1[i] / (2.0f * r0); cw = cw > 1.0f ? 1.0f : cw < -1.0f ? -1.0f : cw;
                const float w0 = std::acos(cw), sw0 = std::sin(w0) > 1e-6f ? std::sin(w0) : 1e-6f;
                const float cwn = std::cos(w), swn = std::sin(w);
                float* a[2] = {y1, s1}; float* b[2] = {y2, s2};
                for(int q = 0; q < 2; q++)
                {
                    const float sp = a[q][i], cp = (a[q][i] * cw - b[q][i] * r0) / sw0;   /* A sin phi, A cos phi */
                    a[q][i] = sp;
                    b[q][i] = (sp * cwn - cp * swn) / r;                                    /* A sin(phi - w) / r */
                }
            }
            c1[i] = 2.0f * r * std::cos(w);
            c2[i] = -r * r;
            p1[i] = gain[i] * std::sin(ph - w) / r;
            p2[i] = gain[i] * std::sin(ph - 2.0f * w) / (r * r);
            if(!keep) y1[i] = y2[i] = s1[i] = s2[i] = 0.f;
        }
        ramp_len = 0.003f * sr;
        if(!keep) ramping = false;
    }

    /* the hammer: the strike bank set to the given swing at every mode's
       fitted phase. A
       strike while a strike is still ramping folds the earlier one into the
       main state first, un-ramped from there on — 3 ms of envelope is
       inaudible against a second hit that close */
    /* lead samples of nothing (the burst carries the sound), then the
       ramp: 1 - fade, the burst's own raised cosine mirrored. The state
       runs from the strike so the modes' phases meet the recording's at
       the seam; the ramp only scales what is heard */
    void Strike(float swing, float lead = 0.f, float ramp = 0.f)
    {
        if(ramping) Fold();
        for(int i = 0; i < n; i++) { s1[i] = swing * p1[i]; s2[i] = swing * p2[i]; }
        ramp_n = 0.f;
        ramp_lead = lead;
        if(ramp > 0.f) ramp_len = ramp;
        ramping = true;
    }

    void Fold()
    {
        for(int i = 0; i < n; i++) { y1[i] += s1[i]; y2[i] += s2[i]; s1[i] = s2[i] = 0.f; }
        ramping = false;
    }

    /* the displacement, summed over the modes, into out (overwrite) */
    /* Mode-outer, sample-inner, in runs of up to 64 samples: each mode's
       two coefficients and two states stay in registers for the run and
       the loop is FMA-bound, where sample-outer loaded and stored every
       mode's state every sample and was load-store-bound — Bank, Zambon and
       Fontana's arrangement (TASLP 2010), which is how their piano runs
       hundreds of modes a note. The strike bank's ramp is a table of
       weights for the run; a run is cut where the ramp ends so the fold
       lands on the same sample it always did. */
    void Process(float* out, int frames)
    {
        while(frames > 0)
        {
            int m = frames < 64 ? frames : 64;
            if(ramping)
            {
                const int left = (int)(ramp_lead + ramp_len - ramp_n);
                if(left > 0 && left < m) m = left;
            }
            for(int k = 0; k < m; k++) out[k] = 0.f;
            for(int i = 0; i < n; i++)
            {
                const float a = c1[i], b = c2[i];
                float u1 = y1[i], u2 = y2[i];
                for(int k = 0; k < m; k++)
                {
                    const float y = a * u1 + b * u2;
                    u2 = u1; u1 = y;
                    out[k] += y;
                }
                y1[i] = u1; y2[i] = u2;
            }
            if(ramping)
            {
                float w[64];
                for(int k = 0; k < m; k++)
                {
                    const float u = ramp_n + (float)k - ramp_lead;
                    w[k] = u <= 0.f ? 0.f : 0.5f - 0.5f * std::cos(3.1415927f * u / ramp_len);
                }
                for(int i = 0; i < n; i++)
                {
                    const float a = c1[i], b = c2[i];
                    float u1 = s1[i], u2 = s2[i];
                    for(int k = 0; k < m; k++)
                    {
                        const float y = a * u1 + b * u2;
                        u2 = u1; u1 = y;
                        out[k] += w[k] * y;
                    }
                    s1[i] = u1; s2[i] = u2;
                }
                ramp_n += (float)m;
                if(ramp_n >= ramp_lead + ramp_len) Fold();
            }
            out += m; frames -= m;
        }
    }
};

struct Pickup
{
    bool  on, gap;
    float h, inv_w, K;
    float b0, b1, b2, a1, a2;     /* the coil */
    float prev, rest, z1, z2;     /* the last flux, and the flux at rest for this pole */
    float u_last;                 /* the last displacement in, for a retune: the flux under the new pole at the tine's actual place */

    void Init() { on = gap = false; h = 0.f; inv_w = K = 1.f; b0 = 1.f; b1 = b2 = a1 = a2 = 0.f; prev = rest = z1 = z2 = 0.f; u_last = 0.f; }

    void Set(float h_, float w_, float K_, float fc, float Q, float sr)
    {
        on = true; gap = false; h = h_; inv_w = 1.f / w_; K = K_;
        const float w0 = 6.2831853f * fc / sr;
        const float alpha = std::sin(w0) / (2.f * Q);
        const float cw = std::cos(w0), a0 = 1.f + alpha;
        b0 = (1.f - cw) * 0.5f / a0; b1 = (1.f - cw) / a0; b2 = b0;
        a1 = -2.f * cw / a0; a2 = (1.f - alpha) / a0;
        const float u0 = (0.f - h) * inv_w;
        rest = prev = 1.f / (1.f + u0 * u0);     /* the field at rest: no click at power-on */
        z1 = z2 = 0.f;
    }

    /* the electrostatic plate: C = C0 / (1 − u/g), the reed kept short of the
       plate by a tanh, as the fit had it */
    void SetGap(float g_, float K_, float fc, float Q, float sr)
    {
        Set(0.f, g_, K_, fc, Q, sr);
        gap = true;
        rest = prev = 1.f;
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
            u_last = io[k];
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
    struct Slot { const int16_t* s; uint32_t n; float pos, rate, gain, lp, z; };
    Slot slot[2];
    int  active;

    void Init() { active = 0; for(auto& q : slot) { q.s = nullptr; q.n = 0; q.pos = q.rate = q.gain = q.lp = q.z = 0.f; } }

    /* bursts: pointer to the point's burst block in the world (nbursts,
       then per burst swing, scale, len, samples), and the strike's swing */
    /* rate: the read step, 1 at the burst's own pitch — a note between two
       points plays the nearer point's burst, and without this played it at
       that point's pitch, a semitone off at worst (lit-runtime.md); lp: a
       one-pole low-pass coefficient, 0 for none, from the strike's velocity
       on a world with one take — a soft hit is a filtered attack, not a
       quiet copy of the hard one (commuted synthesis: the hammer does not
       commute) */
    void Strike(const uint8_t* block, float swing, float rate = 1.0f, float lp = 0.0f, uint32_t head = 10u)
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
            q += head + 2u * n;
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
            q2.s = (const int16_t*)(pick[i] + head);
            q2.n = n; q2.pos = 0.0f; q2.rate = rate > 0.0f ? rate : 1.0f; q2.lp = lp; q2.z = 0.0f;
            q2.gain = wgt[i] * sc / 32767.f * (ref[i] > 0.f ? swing / ref[i] : 1.f);
        }
    }

    void Process(float* io, int frames)
    {
        for(int a = 0; a < active; a++)
        {
            Slot& q = slot[a];
            for(int k = 0; k < frames; k++)
            {
                const uint32_t i0 = (uint32_t)q.pos;
                if(i0 + 1 >= q.n) { q.pos = (float)q.n; break; }
                const float f = q.pos - (float)i0;
                int16_t v0, v1; std::memcpy(&v0, q.s + i0, 2); std::memcpy(&v1, q.s + i0 + 1, 2);   /* the blob may be unaligned */
                float v = (float)v0 + f * ((float)v1 - (float)v0);
                if(q.lp > 0.0f) { q.z += q.lp * (v - q.z); v = q.z; }
                io[k] += q.gain * v;
                q.pos += q.rate;
            }
        }
    }
};

/* The wash: what a dense body leaves after its modes and its burst — a
 * tam-tam is hundreds of modes, and three times the bank bought five per
 * cent (ModalBake's findings) — as white noise through eight octave
 * band-passes from 62.5 Hz, each under its own exponential envelope: a
 * level at the strike and a T60 a band, sixteen numbers a point
 * (tools/noise.py). Sixty-odd instructions a sample; nothing on a world
 * whose bands are zero. The noise is an xorshift, the band-pass an RBJ
 * biquad at Q 1.414 (an octave), its gain set so that unit white noise
 * comes out at the band's fitted level. */
struct NoiseLayer
{
    static constexpr int kBands = 8;
    float b0[kBands], a1[kBands], a2[kBands];     /* band-pass: b0 x[n] - b0 x[n-2] */
    float x1[kBands], x2[kBands], y1[kBands], y2[kBands];
    float env[kBands], fall[kBands], level[kBands];
    float gain[kBands], gain_sr;   /* each band-pass's noise gain, measured once per sample rate */
    float rise_n, rise_len;        /* the rise at a strike, in samples */
    uint32_t rng;
    bool  on;

    void Init()
    {
        on = false; rng = 0x9E3779B9u; gain_sr = 0.f; rise_n = rise_len = 0.f;
        for(int k = 0; k < kBands; k++) { b0[k] = a1[k] = a2[k] = x1[k] = x2[k] = y1[k] = y2[k] = env[k] = fall[k] = level[k] = gain[k] = 0.f; }
    }

    /* levels and T60s per band, sr; a band with no level is off */
    void Set(const float* lvl, const float* t60, float sr)
    {
        on = false;
        if(rng == 0u) rng = 0x9E3779B9u;      /* an xorshift of zero is zero forever */
        for(int k = 0; k < kBands; k++)
        {
            const float lo = 62.5f * (float)(1 << k), hi = lo * 2.f, fc = std::sqrt(lo * hi);
            const float w0 = 6.2831853f * fc / sr, alpha = std::sin(w0) / (2.f * 1.41421f);
            const float a0 = 1.f + alpha;
            b0[k] = alpha / a0; a1[k] = -2.f * std::cos(w0) / a0; a2[k] = (1.f - alpha) / a0;
            /* the filter's own noise power gain, measured from its impulse
               response — the octave's share of the spectrum, (hi - lo) / (sr / 2),
               was the guess before and it was 2 dB hot in the low bands and
               1 dB cold at the top, because a Q 1.41 biquad is not a brick
               wall and its width warps towards Nyquist. 2048 samples holds
               the whole ring at band 0 (2Q / w0 = 5 ms); 8 x 2048 MACs at
               note-on, on the control thread */
            if(gain_sr != sr)
            {
                /* once per sample rate, not per Set: 8 x 2048 MACs is 35 us
                   on the M7, and Set runs on the audio thread at a retune */
                float g = 0.f, u1 = 0.f, u2 = 0.f, v1 = 0.f, v2 = 0.f;
                for(int i = 0; i < 2048; i++)
                {
                    const float u = i == 0 ? 1.f : 0.f;
                    const float v = b0[k] * (u - u2) - a1[k] * v1 - a2[k] * v2;
                    u2 = u1; u1 = u; v2 = v1; v1 = v;
                    g += v * v;
                }
                gain[k] = std::sqrt(g);
                if(k == kBands - 1) gain_sr = sr;
            }
            const float share = gain[k];
            level[k] = lvl[k] > 0.f && t60[k] > 0.f ? lvl[k] / (share > 1e-6f ? share : 1e-6f) : 0.f;
            fall[k]  = t60[k] > 0.f ? std::exp(-6.91f / (t60[k] * sr)) : 0.f;
            env[k]   = 0.f;
            x1[k] = x2[k] = y1[k] = y2[k] = 0.f;
            if(level[k] > 0.f) on = true;
        }
    }

    /* the wash rises over the burst's window rather than starting at its
       level under the burst — a tam-tam's was +32 dB inside it — and the
       envelopes only start falling once it is up */
    void Strike(float swing, float rise = 0.f) { for(int k = 0; k < kBands; k++) env[k] += swing * level[k]; rise_n = 0.f; rise_len = rise; }

    void Process(float* io, int frames)
    {
        if(!on) return;
        for(int i = 0; i < frames; i++)
        {
            float w = 1.f;
            if(rise_n < rise_len) { w = rise_n / rise_len; rise_n += 1.f; }
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const float x = ((int32_t)rng) * (1.7320508f / 2147483648.f);    /* uniform, unit variance */
            float acc = 0.f;
            for(int k = 0; k < kBands; k++)
            {
                if(level[k] <= 0.f) continue;
                const float y = b0[k] * (x - x2[k]) - a1[k] * y1[k] - a2[k] * y2[k];
                x2[k] = x1[k]; x1[k] = x; y2[k] = y1[k]; y1[k] = y;
                acc += env[k] * y;
                if(w >= 1.f) env[k] *= fall[k];
            }
            io[i] += w * acc;
        }
    }
};

struct ResonatorVoice
{
    ResonatorBank  bank;
    Pickup         pickup;
    BurstPlayer    burst;
    NoiseLayer     wash;
    const uint8_t* bursts;            /* the point's burst block in the world, or null */
    float          swing_soft, swing_hard;
    float          burst_rate;        /* the burst's read step: the played note over the burst's own, 1 on an index world */
    float          sr;
    uint32_t       burst_head;        /* bytes before a burst's samples in this world's version */
    uint32_t       burst_fade;        /* samples the modes come in over, under the burst's fade; 0 for no burst */
    /* what the voice was built from, kept for a readout: the page asks
       what the module is playing and the module says, rather than the
       page decoding a world it never has */
    float          hz[ResonatorBank::kMax], zeta[ResonatorBank::kMax], gain[ResonatorBank::kMax];
    uint32_t       burst_len;         /* samples in the burst it would play at swing 1, 0 for none */

    void Init()
    {
        bank.Init(); pickup.Init(); burst.Init(); wash.Init(); bursts = nullptr; swing_soft = swing_hard = 1.f; burst_rate = 1.f; sr = 48000.f; burst_len = 0; burst_head = 10; burst_fade = 0;
        for(int k = 0; k < ResonatorBank::kMax; k++) hz[k] = zeta[k] = gain[k] = 0.f;
    }

    void Strike(float velocity01)
    {
        /* between the softest and hardest take in log; a world fitted from
           one take (swing_soft == swing_hard) scales linearly with velocity */
        const float s = swing_hard > swing_soft ? swing_soft * std::pow(swing_hard / swing_soft, velocity01) : swing_soft * velocity01;
        /* the burst is the recording faded over its last part, and the
           modes come in under that fade: the strike bank's ramp is the
           burst's fade, at the burst's rate, where there is a burst — 3 ms
           where there is none. Nothing to cancel (docs/holistic-math.md) */
        const float ramp = burst_fade ? (float)burst_fade / (burst_rate > 0.f ? burst_rate : 1.f) : 0.003f * sr;
        const float lead = burst_fade ? (float)(burst_len - burst_fade) / (burst_rate > 0.f ? burst_rate : 1.f) : 0.f;
        bank.Strike(s, lead, ramp);
        /* one take: the attack is filtered by velocity, a one-pole with its
           corner from 1 kHz at nothing to 13 kHz at full — the hammer's felt
           and the finger's pad are softer the slower they arrive. Takes
           carry this themselves, so a layered world gets no filter */
        float lp = 0.f;
        if(!(swing_hard > swing_soft))
        {
            const float v = velocity01 < 0.f ? 0.f : velocity01 > 1.f ? 1.f : velocity01;
            const float fc = 1000.f * std::exp2(3.7f * v);
            lp = v >= 1.f ? 0.f : 1.f - std::exp(-6.2831853f * fc / sr);
        }
        burst.Strike(bursts, s, burst_rate, lp, burst_head);
        wash.Strike(s, lead + ramp);
    }
    void Process(float* out, int frames)
    {
        bank.Process(out, frames);
        pickup.Process(out, frames);
        burst.Process(out, frames);
        wash.Process(out, frames);
    }
};

/* A condensed world (ModalBake tools/export.py), attached where it lies. */
struct ResonatorWorld
{
    const uint8_t* blob;
    uint32_t size;
    uint16_t N, P;
    uint8_t  form, kind;   /* kind: 0 the param is a note, taken from the pitch; 1 an index into a row of bodies, taken from a position */
    uint8_t  ver;          /* 4, 5 or 6: cents are fifths of a cent from 6, a burst carries its fade from 6, level 255 is silence from 6 */
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
        for(int j = 0; j < i; j++) q = BurstEnd(NoiseEnd(q + FixedBytes()));
        return q;
    }
    /* after the fixed part: u8 nbands + nbands x (f32 level, f32 t60), then the bursts */
    const uint8_t* Noise(int i) const { return Point(i) + FixedBytes(); }
    static const uint8_t* NoiseEnd(const uint8_t* b) { return b + 1 + 8u * b[0]; }
    const uint8_t* Bursts(int i) const { return NoiseEnd(Noise(i)); }
    /* a burst: f32 swing, f32 scale, u16 len, [u16 fade from version 6,]
       i16 samples */
    uint32_t BurstHead() const { return ver >= 6 ? 12u : 10u; }
    const uint8_t* BurstEnd(const uint8_t* b) const
    {
        uint16_t nb; std::memcpy(&nb, b, 2);
        const uint8_t* q = b + 2;
        for(uint16_t k = 0; k < nb; k++) { uint16_t n; std::memcpy(&n, q + 8, 2); q += BurstHead() + 2u * n; }
        return q;
    }

    void Init() { blob = nullptr; size = 0; N = P = 0; form = kind = 0; ver = 0; lo = hi = 0.f; voicing = 0.f; decay = coil = 1.f; }

    bool Attach(const void* data, uint32_t bytes)
    {
        blob = (const uint8_t*)data; size = bytes;
        if(bytes < kHeader || std::memcmp(blob, "KYKM", 4) != 0) return false;
        uint16_t v; std::memcpy(&v, blob + 4, 2); ver = (uint8_t)v;
        std::memcpy(&N, blob + 6, 2); std::memcpy(&P, blob + 8, 2);
        form = blob[10]; kind = blob[11];      /* version 4 wrote an unread body count here, always 0: a note */
        std::memcpy(&lo, blob + 12, 4); std::memcpy(&hi, blob + 16, 4);
        return (v == 4 || v == 5 || v == 6) && N <= ResonatorBank::kMax && size >= kHeader + (uint32_t)P * FixedBytes();
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
            hz[k]    = 20.0f * std::exp2(ver >= 6 ? c / 6000.0f : c / 1200.0f);   /* fifths of a cent from version 6 */
            zeta[k]  = std::exp(-0.1f * m[5 * k + 2]);
            gain[k]  = (ver >= 6 && m[5 * k + 3] == 255) ? 0.f : std::exp(-0.25f * m[5 * k + 3] * 0.1151293f);   /* dB -> linear; 255 is silence */
            phase[k] = m[5 * k + 4] * (6.2831853f / 256.0f);
        }
    }

    /* the world at a parameter value, into a voice. A note world (kind 0)
       is the nearest point transposed to the note — every mode's frequency
       by 2^((note − point)/12), the burst read at the same rate — and not
       the two neighbours interpolated slot by slot, which the holistic pass
       measured at +12 to +19 dB louder than either point at a midpoint:
       the fitter builds attacks out of antiphase pairs that only cancel
       as fitted, and a lerp of two such pairs stops them cancelling. A
       sweep across a midpoint steps from one point's modes to the other's
       with the state carried, which the retune does without a click. A
       body row (kind 1) is still interpolated along the row — the glide
       from gong to woodblock is the point of it — on absolute gains, so a
       quiet slot is not lifted by the neighbour's louder loudest. */
    void At(float param, ResonatorVoice& v, float sr, bool keep = false) const
    {
        if(P == 0) return;
        int a = 0;
        while(a + 1 < P && Param(a + 1) <= param) a++;
        const int b = a + 1 < P ? a + 1 : a;
        const float pa = Param(a), pb = Param(b);
        float t = pb > pa ? std::fmin(1.f, std::fmax(0.f, (param - pa) / (pb - pa))) : 0.f;
        const int near = t < 0.5f ? a : b;
        float ha[ResonatorBank::kMax], za[ResonatorBank::kMax], ga[ResonatorBank::kMax], fa[ResonatorBank::kMax];
        float hb[ResonatorBank::kMax], zb[ResonatorBank::kMax], gb[ResonatorBank::kMax], fb[ResonatorBank::kMax];
        float st[8], sb[8];
        if(kind != 1)
        {
            Decode(near, ha, za, ga, fa);
            std::memcpy(st, Stage(near), 32);
            const float r = std::exp2((param - Param(near)) / 12.f);
            for(int k = 0; k < N; k++) { ha[k] *= r; ga[k] *= st[7]; }
            t = 0.f;
        }
        else
        {
            Decode(a, ha, za, ga, fa);
            Decode(b, hb, zb, gb, fb);
            std::memcpy(st, Stage(a), 32);
            std::memcpy(sb, Stage(b), 32);
            for(int k = 0; k < N; k++) { ga[k] *= st[7]; gb[k] *= sb[7]; }     /* absolute before the lerp */
            for(int k = 0; k < N; k++)
            {
                ha[k] = ha[k] * std::pow(hb[k] / ha[k], t);
                za[k] = za[k] * std::pow(zb[k] / za[k], t);
                ga[k] = ga[k] + t * (gb[k] - ga[k]);       /* linearly: a ghost is a fade, not a cliff */
                float d = fb[k] - fa[k];                    /* phase: the short way round */
                if(d > 3.1415927f) d -= 6.2831853f;
                if(d < -3.1415927f) d += 6.2831853f;
                fa[k] += t * d;
            }
            for(int k = 0; k < 7; k++) st[k] += t * (sb[k] - st[k]);
        }
        for(int k = 0; k < N; k++) za[k] /= decay;            /* T60 x decay */
        for(int k = 0; k < N; k++) { v.hz[k] = ha[k]; v.zeta[k] = za[k]; v.gain[k] = ga[k]; }
        v.bank.Set(ha, za, ga, N, sr, fa, keep);
        st[0] += voicing * st[1];                                /* h moves by widths */
        st[3] *= coil;
        {
            /* a burst is not interpolated: the nearer point's, read at the
               played note over its own, so a note between two points is not
               a semitone off in its attack; a body row plays it as it is */
            v.bursts = Bursts(near);
            v.burst_rate = kind == 1 ? 1.f : std::exp2((param - Param(near)) / 12.f);
            v.burst_head = BurstHead();
            v.sr = sr;
            uint16_t nb; std::memcpy(&nb, v.bursts, 2);
            v.burst_len = 0; v.burst_fade = 0;
            if(nb)
            {
                uint16_t n; std::memcpy(&n, v.bursts + 2 + 8, 2); v.burst_len = n;
                if(ver >= 6) { uint16_t f; std::memcpy(&f, v.bursts + 2 + 10, 2); v.burst_fade = f; }
                else v.burst_fade = n / 3;
            }
        }
        /* the wash: levels and T60s interpolated between the points, applied
           with the spin's decay; state kept on a retune */
        {
            float la[8] = {0}, ta[8] = {0}, lb[8] = {0}, tb[8] = {0};
            const uint8_t* na = Noise(kind != 1 ? near : a); const uint8_t* nb = Noise(kind != 1 ? near : b);
            for(int k = 0; k < na[0] && k < 8; k++) { std::memcpy(&la[k], na + 1 + 8 * k, 4); std::memcpy(&ta[k], na + 5 + 8 * k, 4); }
            for(int k = 0; k < nb[0] && k < 8; k++) { std::memcpy(&lb[k], nb + 1 + 8 * k, 4); std::memcpy(&tb[k], nb + 5 + 8 * k, 4); }
            for(int k = 0; k < 8; k++) { la[k] += t * (lb[k] - la[k]); ta[k] = (ta[k] + t * (tb[k] - ta[k])) * decay; }
            if(keep)
            {
                NoiseLayer w2 = v.wash; v.wash.Set(la, ta, sr);
                for(int k = 0; k < 8; k++) { v.wash.env[k] = w2.env[k]; v.wash.x1[k] = w2.x1[k]; v.wash.x2[k] = w2.x2[k]; v.wash.y1[k] = w2.y1[k]; v.wash.y2[k] = w2.y2[k]; }
                v.wash.rng = w2.rng;
            }
            else v.wash.Set(la, ta, sr);
        }
        v.swing_soft = st[5]; v.swing_hard = st[6];
        if(keep && v.pickup.on)
        {
            /* the pickup retunes without a click: its filter state and its
               last flux stay, only the field and the coil move */
            const float u_last = v.pickup.u_last, z1 = v.pickup.z1, z2 = v.pickup.z2;
            if(form == 1) v.pickup.Set(st[0], st[1], st[2], st[3], st[4], sr);
            else if(form == 2) v.pickup.SetGap(st[1], st[2], st[3], st[4], sr);
            /* the last flux re-read under the new pole at the tine's actual
               displacement: the pole moved, so the flux the same tine
               makes has too, and carrying the old value — or its distance
               from rest, which was the first fix — put a step through
               d/dt: −23 dB peaks per deadband step under a voicing sweep
               (docs/holistic-math.md). The displacement is the thing that
               did not move. */
            {
                const float u = (u_last - v.pickup.h) * v.pickup.inv_w;
                v.pickup.prev = v.pickup.gap ? 1.f / (1.f - 0.9f * Pickup::Tanh(u / 0.9f)) : 1.f / (1.f + u * u);
                v.pickup.u_last = u_last;
            }
            v.pickup.z1 = z1; v.pickup.z2 = z2;
        }
        else if(form == 1) v.pickup.Set(st[0], st[1], st[2], st[3], st[4], sr);
        else if(form == 2) v.pickup.SetGap(st[1], st[2], st[3], st[4], sr);
        else v.pickup.on = false;
    }
};

} // namespace kyk
