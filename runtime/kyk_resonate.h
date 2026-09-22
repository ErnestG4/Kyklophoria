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
    float g[kMax];                       /* each mode's gain at a unit strike, for the carry's level */
    float y1[kMax], y2[kMax];
    /* the strike bank: a new hit rings here under a 3 ms raised-cosine ramp
       — the envelope the fit's model had, and the gains were fitted under —
       and is then added into the main state, which a linear bank allows
       exactly. An impulse into the main bank was a click the fit never
       heard; a 3 ms pulse would starve the high modes */
    /* two of them: a strike inside the last one's burst window — the lead
       is up to 300 ms on a piano's bass, 260 on a Wurlitzer C3 — takes the
       second and both ramp in on their own clocks; a third folds the
       older at the weight it is heard at. One strike bank meant the old
       note's modes were dropped whenever the next note came fast */
    static constexpr int kStrikes = 2;
    float s1[kStrikes][kMax], s2[kStrikes][kMax];
    float ramp_n[kStrikes], ramp_len[kStrikes], ramp_lead[kStrikes];
    bool  ramping[kStrikes];
    /* the choke: for damp_left samples every mode decays by damp_c a
       sample more than its own pole says — the main state and the strike
       banks marked damp_bank, not a strike that arrives meanwhile */
    int   damp_left;
    float damp_c;
    bool  damp_bank[kStrikes];
    /* the old note choked: down 60 dB over ms, as a damper falling on
       the string a new note is struck on. One voice is one string; a
       strike at another note on it is a new note, not the old one
       carried on louder — a fast run carried its ring from strike to
       strike, a C3 27 dB over the C2 before it, until the pickup railed.
       A strike at the same note is a hammer on a ringing tine and adds. */
    void Choke(float ms, float sr)
    {
        damp_left = (int)(ms * 0.001f * sr);
        if(damp_left < 1) damp_left = 1;
        damp_c = std::pow(1e-3f, 1.f / (float)damp_left);
        for(int k = 0; k < kStrikes; k++) damp_bank[k] = true;
    }

    void Init()
    {
        n = 0;
        for(int q = 0; q < kStrikes; q++) { ramp_n[q] = 0.f; ramp_len[q] = 144.f; ramp_lead[q] = 0.f; ramping[q] = false; damp_bank[q] = false; }
        for(int i = 0; i < kMax; i++) { c1[i] = c2[i] = p1[i] = p2[i] = g[i] = y1[i] = y2[i] = 0.f; for(int q = 0; q < kStrikes; q++) s1[q][i] = s2[q][i] = 0.f; }
        damp_left = 0; damp_c = 1.f;
    }
    bool Ringing() const
    {
        for(int i = 0; i < n; i++) { if(std::fabs(y1[i]) > 1e-7f) return true; for(int q = 0; q < kStrikes; q++) if(std::fabs(s1[q][i]) > 1e-7f) return true; }
        return false;
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
    void Set(const float* hz, const float* zeta, const float* gain, int count, float sr, const float* phase = nullptr, bool keep = false, float ratio = 1.f)
    {
        const int n0 = n;
        n = count > kMax ? kMax : count;
        /* the state carried across a retune as what it is — an amplitude
           and a phase — and not as two samples. Two samples of a slow
           oscillation read under a fast pole are a small amplitude; of a
           fast one under a slow pole, a huge one ((y1 - y2) / sin w): a
           gong's modes retuned to a tom's came back 28 dB louder. The old
           pole gives the old w and r; the pair is solved for A sin(phi)
           and A cos(phi) and rewritten under the new pole. Both the
           ringing state and the strike still ramping in.

           And carried to the new mode NEAREST IN FREQUENCY, not to the
           same index: the modes of a point are in frequency order, so the
           k-th of one point is roughly the k-th of the next, but across a
           family's members — a Wurlitzer's sparse partials into a grand's
           dense ones — mode k is a different note, and a note world's
           ghosts (silent slots at the fundamental) took a real mode's ring
           and played it at the fundamental. `ratio` is the pitch change
           the retune is (new note over old, 1 for a body row or another
           member at the same note): the old frequencies are compared
           transposed by it, so the same instrument's k-th partial still
           follows its k-th partial up a fifth, and only a different set of
           partials is rematched. Each new mode takes the unclaimed old
           mode nearest by ratio, one to one, within a fifth; what nothing
           claims is let go. 48 x 48 ratios at a retune, nothing per
           sample.

           And carried at the level the new mode would have: the ring's
           amplitude scaled by the new mode's unit-strike gain over the
           old's, so the retuned note is the new note as far along in its
           decay as the old one was. A point's displacement units are its
           own — a pickup world's fit puts the tine's motion at whatever
           scale its coil and K then undo — and a Wurlitzer C3's tine
           carried as it stood into the next point's pickup came out at
           3.7 where a strike there peaks at 0.6. At most 12 dB up. */
        float sp[1 + kStrikes][kMax], cp[1 + kStrikes][kMax], w0[kMax], g0[kMax];
        bool  has[kMax], claimed[kMax];
        if(keep)
        {
            float* a[1 + kStrikes] = {y1, s1[0], s1[1]}; float* b[1 + kStrikes] = {y2, s2[0], s2[1]};
            for(int i = 0; i < n0; i++)
            {
                has[i] = false; claimed[i] = false; g0[i] = g[i];
                if(!(c2[i] < 0.f)) continue;
                const float r0 = std::sqrt(-c2[i]);
                float cw = c1[i] / (2.0f * r0); cw = cw > 1.0f ? 1.0f : cw < -1.0f ? -1.0f : cw;
                w0[i] = std::acos(cw);
                const float sw0 = std::sin(w0[i]) > 1e-6f ? std::sin(w0[i]) : 1e-6f;
                float e = 0.f;
                for(int q = 0; q < 1 + kStrikes; q++)
                {
                    sp[q][i] = a[q][i]; cp[q][i] = (a[q][i] * cw - b[q][i] * r0) / sw0;   /* A sin phi, A cos phi */
                    e += sp[q][i] * sp[q][i] + cp[q][i] * cp[q][i];
                }
                has[i] = e > 1e-20f;
            }
        }
        for(int i = 0; i < n; i++)
        {
            const float w = 6.2831853f * hz[i] / sr;
            const float r = std::exp(-zeta[i] * w);
            const float ph = phase ? phase[i] : 0.f;
            int from = -1;
            if(keep)
            {
                float best = 1.4f;                      /* within a fifth either way */
                for(int j = 0; j < n0; j++)
                {
                    if(!has[j] || claimed[j]) continue;
                    const float wj = w0[j] * ratio;
                    const float q = wj > w ? wj / w : w / wj;
                    if(q < best) { best = q; from = j; }
                }
            }
            c1[i] = 2.0f * r * std::cos(w);
            c2[i] = -r * r;
            p1[i] = gain[i] * std::sin(ph - w) / r;
            p2[i] = gain[i] * std::sin(ph - 2.0f * w) / (r * r);
            if(from >= 0)
            {
                claimed[from] = true;
                const float cwn = std::cos(w), swn = std::sin(w);
                const float go = std::fabs(g0[from]), gn = std::fabs(gain[i]);
                const float lv = go > 1e-9f ? (gn / go > 4.f ? 4.f : gn / go) : 1.f;
                float* a[1 + kStrikes] = {y1, s1[0], s1[1]}; float* b[1 + kStrikes] = {y2, s2[0], s2[1]};
                for(int q = 0; q < 1 + kStrikes; q++)
                {
                    a[q][i] = lv * sp[q][from];
                    b[q][i] = lv * (sp[q][from] * cwn - cp[q][from] * swn) / r;             /* A sin(phi - w) / r */
                }
            }
            else { y1[i] = y2[i] = 0.f; for(int q = 0; q < kStrikes; q++) s1[q][i] = s2[q][i] = 0.f; }
            g[i] = gain[i];
        }
        /* the ramp is the strike's business (its length is the burst's
           fade): Set used to reset it to 3 ms on every call, so a retune
           under a playing burst — a glide starting 50 ms after a strike —
           snapped the strike bank from a third of its way in to full,
           a step the ear-check caught on four worlds */
        if(!keep) for(int q = 0; q < kStrikes; q++) { ramp_len[q] = 0.003f * sr; ramp_lead[q] = 0.f; ramping[q] = false; }
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
        int q = -1;
        for(int k = 0; k < kStrikes; k++) if(!ramping[k]) { q = k; break; }
        if(q < 0)
        {
            /* both busy: fold the one further along */
            q = ramp_n[0] - ramp_lead[0] >= ramp_n[1] - ramp_lead[1] ? 0 : 1;
            Fold(q);
        }
        for(int i = 0; i < n; i++) { s1[q][i] = swing * p1[i]; s2[q][i] = swing * p2[i]; }
        damp_bank[q] = false;                  /* a choke under way is the old note's, not this strike's */
        ramp_n[q] = 0.f;
        ramp_lead[q] = lead;
        if(ramp > 0.f) ramp_len[q] = ramp;
        ramping[q] = true;
    }

    /* the strike bank folded into the main state at the weight it is
       heard at now. It folded at full: a strike inside the last one's
       burst window — the lead is up to 300 ms on a piano's bass — took a
       bank that was inaudible under the burst to full in one sample, a
       pop on every fast note that the old 3 ms ramp never showed */
    void Fold(int q)
    {
        const float u = ramp_n[q] - ramp_lead[q];
        const float w = u <= 0.f ? 0.f : u >= ramp_len[q] ? 1.f : 0.5f - 0.5f * std::cos(3.1415927f * u / ramp_len[q]);
        for(int i = 0; i < n; i++) { y1[i] += w * s1[q][i]; y2[i] += w * s2[q][i]; s1[q][i] = s2[q][i] = 0.f; }
        ramping[q] = false;
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
    /* drive: an audio signal into every mode, g x[k] a sample — Rings'
       external exciter: the bank as a resonant filter bank for whatever
       is patched into it. Null for a struck bank. */
    void Process(float* out, int frames, const float* drive = nullptr, float g = 0.f)
    {
        while(frames > 0)
        {
            int m = frames < 64 ? frames : 64;
            for(int q = 0; q < kStrikes; q++) if(ramping[q])
            {
                const int left = (int)(ramp_lead[q] + ramp_len[q] - ramp_n[q]);
                if(left > 0 && left < m) m = left;
            }
            if(damp_left > 0 && damp_left < m) m = damp_left;
            /* the hammer's absorption for this run: every mode's pole
               pulled in by dc a sample (a = dc a, b = dc^2 b is the
               recurrence of the state scaled by dc^n), the strike that is
               arriving excepted */
            const float dc = damp_left > 0 ? damp_c : 1.f, dc2 = dc * dc;
            for(int k = 0; k < m; k++) out[k] = 0.f;
            if(drive)
            {
                /* in pairs, as the free-ringing loop below: a continuous
                   exciter is this loop on every voice, so its cost is
                   what an Exciter page can afford */
                int i = 0;
                for(; i + 1 < n; i += 2)
                {
                    const float a0 = c1[i] * dc, b0 = c2[i] * dc2, a1 = c1[i + 1] * dc, b1 = c2[i + 1] * dc2;
                    float p1_ = y1[i], p2_ = y2[i], q1 = y1[i + 1], q2 = y2[i + 1];
                    for(int k = 0; k < m; k++)
                    {
                        const float d = g * drive[k];
                        const float yp = a0 * p1_ + b0 * p2_ + d;
                        const float yq = a1 * q1 + b1 * q2 + d;
                        p2_ = p1_; p1_ = yp;
                        q2 = q1; q1 = yq;
                        float o = out[k];
                        o += yp; o += yq;
                        out[k] = o;
                    }
                    y1[i] = p1_; y2[i] = p2_; y1[i + 1] = q1; y2[i + 1] = q2;
                }
                for(; i < n; i++)
                {
                    const float a = c1[i] * dc, b = c2[i] * dc2;
                    float u1 = y1[i], u2 = y2[i];
                    for(int k = 0; k < m; k++)
                    {
                        const float y = a * u1 + b * u2 + g * drive[k];
                        u2 = u1; u1 = y;
                        out[k] += y;
                    }
                    y1[i] = u1; y2[i] = u2;
                }
                drive += m;
            }
            else
            {
                /* two modes at a time. A mode's recurrence is a chain — the
                   multiply, the add, the next sample's multiply — and the
                   M7's FPU waits on each link; two chains interleaved fill
                   the wait. Ten instructions a mode a sample became eight
                   and a half for the pair, but the cycles are what fell:
                   the sums are the same operations in the same order, so
                   the output is bit for bit what one mode at a time gave */
                int i = 0;
                for(; i + 1 < n; i += 2)
                {
                    const float a0 = c1[i] * dc, b0 = c2[i] * dc2, a1 = c1[i + 1] * dc, b1 = c2[i + 1] * dc2;
                    float p1 = y1[i], p2 = y2[i], q1 = y1[i + 1], q2 = y2[i + 1];
                    for(int k = 0; k < m; k++)
                    {
                        const float yp = a0 * p1 + b0 * p2;
                        const float yq = a1 * q1 + b1 * q2;
                        p2 = p1; p1 = yp;
                        q2 = q1; q1 = yq;
                        float o = out[k];
                        o += yp; o += yq;
                        out[k] = o;
                    }
                    y1[i] = p1; y2[i] = p2; y1[i + 1] = q1; y2[i + 1] = q2;
                }
                for(; i < n; i++)
                {
                    const float a = c1[i] * dc, b = c2[i] * dc2;
                    float u1 = y1[i], u2 = y2[i];
                    for(int k = 0; k < m; k++)
                    {
                        const float y = a * u1 + b * u2;
                        u2 = u1; u1 = y;
                        out[k] += y;
                    }
                    y1[i] = u1; y2[i] = u2;
                }
            }
            for(int q = 0; q < kStrikes; q++) if(ramping[q])
            {
                float w[64];
                for(int k = 0; k < m; k++)
                {
                    const float u = ramp_n[q] + (float)k - ramp_lead[q];
                    w[k] = u <= 0.f ? 0.f : 0.5f - 0.5f * std::cos(3.1415927f * u / ramp_len[q]);
                }
                const float sc = damp_left > 0 && damp_bank[q] ? dc : 1.f, sc2 = sc * sc;
                int i = 0;
                for(; i + 1 < n; i += 2)      /* in pairs, as above */
                {
                    const float a0 = c1[i] * sc, b0 = c2[i] * sc2, a1 = c1[i + 1] * sc, b1 = c2[i + 1] * sc2;
                    float p1 = s1[q][i], p2 = s2[q][i], r1 = s1[q][i + 1], r2 = s2[q][i + 1];
                    for(int k = 0; k < m; k++)
                    {
                        const float yp = a0 * p1 + b0 * p2;
                        const float yr = a1 * r1 + b1 * r2;
                        p2 = p1; p1 = yp;
                        r2 = r1; r1 = yr;
                        float o = out[k];
                        o += w[k] * yp; o += w[k] * yr;
                        out[k] = o;
                    }
                    s1[q][i] = p1; s2[q][i] = p2; s1[q][i + 1] = r1; s2[q][i + 1] = r2;
                }
                for(; i < n; i++)
                {
                    const float a = c1[i] * sc, b = c2[i] * sc2;
                    float u1 = s1[q][i], u2 = s2[q][i];
                    for(int k = 0; k < m; k++)
                    {
                        const float y = a * u1 + b * u2;
                        u2 = u1; u1 = y;
                        out[k] += w[k] * y;
                    }
                    s1[q][i] = u1; s2[q][i] = u2;
                }
                ramp_n[q] += (float)m;
                if(ramp_n[q] >= ramp_lead[q] + ramp_len[q]) Fold(q);
            }
            if(damp_left > 0) damp_left -= m;
            out += m; frames -= m;
        }
    }
};

struct Pickup
{
    bool  on, gap;
    float h, inv_w, K;
    float h_t, inv_w_t, K_t;      /* where the field and the gain are going: a retune slews them over 10 ms */
    float b0, b1, b2, a1, a2;     /* the coil */
    /* the coil it is leaving: on a retune the old coil runs on beside the
       new for 10 ms and the output crosses from one to the other — a
       biquad's state under new coefficients is a step, and the EP's points
       each carry a coil of their own (Q 0.02 to 55; the C2 to G2 jump
       clicked at 7x the signal's slope with the state simply kept) */
    float ob0, ob1, ob2, oa1, oa2, oz1, oz2;
    float xfade_n, xfade_len;
    float prev, rest, z1, z2;     /* the last flux, and the flux at rest for this pole */
    float u_last;                 /* the last displacement in, for a retune: the flux under the new pole at the tine's actual place */

    void Init()
    {
        on = gap = false; h = h_t = 0.f; inv_w = K = inv_w_t = K_t = 1.f; b0 = 1.f; b1 = b2 = a1 = a2 = 0.f; prev = rest = z1 = z2 = 0.f; u_last = 0.f;
        ob0 = 1.f; ob1 = ob2 = oa1 = oa2 = oz1 = oz2 = 0.f; xfade_n = xfade_len = 0.f;
    }
    /* keep the coil that is playing, so a Set can be crossed into */
    void Leave(float sr)
    {
        ob0 = b0; ob1 = b1; ob2 = b2; oa1 = a1; oa2 = a2; oz1 = z1; oz2 = z2;
        xfade_n = 0.f; xfade_len = 0.01f * sr;
    }

    void Set(float h_, float w_, float K_, float fc, float Q, float sr)
    {
        on = true; gap = false; h = h_t = h_; inv_w = inv_w_t = 1.f / w_; K = K_t = K_;
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
            /* the field and the gain slew to a retune's values over about
               10 ms: the EP's points carry a coil gain K and a pole h of
               their own, and a note world takes the nearest point's whole
               — a C2 to G2 jump stepped K in one sample, a click 30x the
               signal's own slope */
            h += (h_t - h) * 0.002f; inv_w += (inv_w_t - inv_w) * 0.002f; K += (K_t - K) * 0.002f;
            u_last = io[k];
            const float u   = (io[k] - h) * inv_w;
            const float phi = gap ? 1.f / (1.f - 0.9f * Tanh(u / 0.9f)) : 1.f / (1.f + u * u);
            const float d   = phi - prev;
            prev = phi;
            float y = b0 * d + z1;                /* transposed direct form II */
            z1 = b1 * d - a1 * y + z2;
            z2 = b2 * d - a2 * y;
            if(xfade_n < xfade_len)
            {
                const float yo = ob0 * d + oz1;
                oz1 = ob1 * d - oa1 * yo + oz2;
                oz2 = ob2 * d - oa2 * yo;
                const float t = xfade_n / xfade_len;
                y = yo + t * (y - yo);
                xfade_n += 1.f;
            }
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
    /* four slots: two for this strike's crossfade, two for the last
       strike's, which play to their end rather than being cut — a cut
       burst was a click on every fast note */
    static constexpr int kSlots = 4;
    /* g: a gain the slot's samples are multiplied by, d: what g is
       multiplied by each sample — 1 for a burst that plays as recorded,
       under 1 for one fading (the old note choked, or a muted decay) */
    struct Slot { const int16_t* s; uint32_t n; float pos, rate, gain, lp, z, g, d; };
    Slot slot[kSlots];
    int  active;
    float damp;                       /* the per-sample multiplier a new burst starts with: 1, or under it when the decay is muted */

    void Init() { active = 0; damp = 1.f; for(auto& q : slot) { q.s = nullptr; q.n = 0; q.pos = q.rate = q.gain = q.lp = q.z = 0.f; q.g = q.d = 1.f; } }
    /* every burst playing fades out over ms: the old note's attack, when
       a strike at another note has choked its ring — a burst that played
       to its end was the old note going on for up to 390 ms under the new */
    void Choke(float ms, float sr)
    {
        const float d = std::pow(1e-3f, 1.f / (ms * 0.001f * sr));
        for(int a = 0; a < active; a++) if(slot[a].d > d) slot[a].d = d;
    }
    bool Playing() const
    {
        for(int a = 0; a < active; a++) if(slot[a].s && slot[a].pos + 1.f < (float)slot[a].n) return true;
        return false;
    }

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
        /* what is still playing from the last strike plays to its end —
           the two most recent slots — rather than being cut, which was a
           click on every fast note */
        Slot old[2]; int keep = 0;
        for(int a = 0; a < active && keep < 2; a++)
            if(slot[a].s && slot[a].pos + 1.f < (float)slot[a].n) old[keep++] = slot[a];
        active = 0;
        if(block)
        {
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
            if(lo)
            {
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
                    q2.g = 1.f; q2.d = damp;
                }
            }
        }
        for(int k = 0; k < keep; k++) slot[active++] = old[k];
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
                io[k] += q.gain * q.g * v;
                q.g *= q.d;
                q.pos += q.rate;
            }
            if(q.g < 1e-4f) q.pos = (float)q.n;      /* faded out: done */
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
    bool Active() const { for(int k = 0; k < kBands; k++) if(env[k] > 1e-7f) return true; return false; }

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
    float          param;             /* what the voice was built at: a note, or a position on a row; 1e9 for not yet */
    int            cap;               /* modes this voice may have, 0 for all: Rings' rule for polyphony — the bank's 48 shared out, so four voices stacked are as rich as one, and cost the same */
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
        bank.Init(); pickup.Init(); burst.Init(); wash.Init(); bursts = nullptr; swing_soft = swing_hard = 1.f; burst_rate = 1.f; sr = 48000.f; burst_len = 0; burst_head = 10; burst_fade = 0; param = 1e9f; cap = 0;
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
    void Process(float* out, int frames, const float* drive = nullptr, float g = 0.f)
    {
        bank.Process(out, frames, drive, g);
        pickup.Process(out, frames);
        burst.Process(out, frames);
        wash.Process(out, frames);
    }
    /* still making sound: a mode ringing, a burst playing or the wash falling */
    bool Active() const { return bank.Ringing() || burst.Playing() || wash.Active(); }
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
    /* a family (kind 2): a row of instruments that work the same way —
       strings, pianos — position 0 choosing among them and the note
       within each from the pitch. A container of whole .kykm files, not a
       merge: a morph between two instruments' slots is mush (holistic-
       math.md), a choice between them is an instrument. Member(m, w)
       attaches w to the m-th; everything else about a family is the
       member's. */
    static constexpr int kMaxMembers = 8;
    uint8_t  M;
    uint32_t member_off[kMaxMembers], member_len[kMaxMembers];
    /* the spin: what a pot does to a loaded point. voicing moves the pole
       off centre by that many widths on top of the fitted h; decay
       multiplies every mode's T60; coil multiplies the coil's fc. Applied at
       At(), so a sent world keeps its own fitted values as the centre */
    float    voicing, decay, coil;

    static constexpr uint32_t kHeader = 4 + 8 + 8;
    uint32_t FixedBytes() const { return 4 + 8 * 4 + 5u * N; }

    /* where each point starts, and its parameter, tabled at Attach: a
       point is fixed bytes then its burst block, and walking to point i
       read i points' burst lengths out of SDRAM. At() searched the
       parameters with that walk inside its loop — P^2 walks, each a cache
       miss, some 650 000 cycles on the EP's 85 points, three blocks — so
       every strike on a large world was an overrun (the pop at note-on)
       and a decay pot turning was a continuous one. */
    static constexpr int kMaxPoints = 128;
    uint32_t poff_[kMaxPoints];
    float    pparam_[kMaxPoints];
    const uint8_t* Point(int i) const
    {
        if(i < kMaxPoints) return blob + poff_[i];
        const uint8_t* q = blob + poff_[kMaxPoints - 1];
        for(int j = kMaxPoints - 1; j < i; j++) q = BurstEnd(NoiseEnd(q + FixedBytes()));
        return q;
    }
    void TablePoints()
    {
        const uint8_t* q = blob + kHeader;
        for(int i = 0; i < P && i < kMaxPoints; i++)
        {
            poff_[i] = (uint32_t)(q - blob);
            std::memcpy(&pparam_[i], q, 4);
            q = BurstEnd(NoiseEnd(q + FixedBytes()));
        }
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

    void Init() { blob = nullptr; size = 0; N = P = 0; form = kind = 0; ver = 0; lo = hi = 0.f; voicing = 0.f; decay = coil = 1.f; M = 0; for(int m = 0; m < kMaxMembers; m++) member_off[m] = member_len[m] = 0; poff_[0] = kHeader; pparam_[0] = 0.f; }

    bool Attach(const void* data, uint32_t bytes)
    {
        blob = (const uint8_t*)data; size = bytes;
        if(bytes < kHeader || std::memcmp(blob, "KYKM", 4) != 0) return false;
        uint16_t v; std::memcpy(&v, blob + 4, 2); ver = (uint8_t)v;
        std::memcpy(&N, blob + 6, 2); std::memcpy(&P, blob + 8, 2);
        form = blob[10]; kind = blob[11];      /* version 4 wrote an unread body count here, always 0: a note */
        std::memcpy(&lo, blob + 12, 4); std::memcpy(&hi, blob + 16, 4);
        M = 0;
        if(kind == 2)
        {
            if(v < 6 || size < kHeader + 1) return false;
            const uint8_t m = blob[kHeader];
            if(m == 0 || m > kMaxMembers || size < kHeader + 1 + 24u * m) return false;
            for(int i = 0; i < m; i++)
            {
                std::memcpy(&member_off[i], blob + kHeader + 1 + 24 * i, 4);
                std::memcpy(&member_len[i], blob + kHeader + 1 + 24 * i + 4, 4);
                if(member_off[i] + member_len[i] > size) return false;
                ResonatorWorld w; w.Init();
                if(!w.Attach(blob + member_off[i], member_len[i]) || w.kind == 2) return false;
            }
            M = m;
            return N <= ResonatorBank::kMax;
        }
        if(!((v == 4 || v == 5 || v == 6) && N <= ResonatorBank::kMax && size >= kHeader + (uint32_t)P * FixedBytes())) return false;
        TablePoints();
        return true;
    }
    /* the m-th instrument of a family, as a world of its own with this
       family's spin; a plain world is its own only member */
    bool Member(int m, ResonatorWorld& w) const
    {
        w.Init();
        if(kind != 2) { w = *this; return true; }
        if(m < 0) m = 0; if(m >= M) m = M - 1;
        if(!w.Attach(blob + member_off[m], member_len[m])) return false;
        w.voicing = voicing; w.decay = decay; w.coil = coil;
        return true;
    }
    const char* MemberName(int m) const
    {
        if(kind != 2 || m < 0 || m >= M) return "";
        return (const char*)(blob + kHeader + 1 + 24 * m + 8);   /* 16 bytes, zero padded by the export */
    }

    float Param(int i) const { if(i < kMaxPoints) return pparam_[i]; float p; std::memcpy(&p, Point(i), 4); return p; }
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
    void At(float param, ResonatorVoice& v, float sr, bool keep = false, bool strike = false) const
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
        /* a voice with a cap keeps its cap loudest modes — by the energy a
           mode carries, gain^2 over its decay rate, the export's own rank
           — in frequency order, and the rest are silent. Rings does this
           for its polyphony (64 / voices - 4 modes each): four voices
           stacked are as rich as one, and cost the same block */
        int n = N;
        if(v.cap > 0 && v.cap < N)
        {
            float score[ResonatorBank::kMax]; bool take[ResonatorBank::kMax];
            for(int k = 0; k < N; k++) { score[k] = ga[k] * ga[k] / (za[k] * ha[k] > 1e-12f ? za[k] * ha[k] : 1e-12f); take[k] = false; }
            for(int c = 0; c < v.cap; c++)
            {
                int best = -1;
                for(int k = 0; k < N; k++) if(!take[k] && (best < 0 || score[k] > score[best])) best = k;
                if(best < 0) break;
                take[best] = true;
            }
            n = 0;
            for(int k = 0; k < N; k++) if(take[k]) { ha[n] = ha[k]; za[n] = za[k]; ga[n] = ga[k]; fa[n] = fa[k]; n++; }
        }
        for(int k = 0; k < n; k++) { v.hz[k] = ha[k]; v.zeta[k] = za[k]; v.gain[k] = ga[k]; }
        for(int k = n; k < N; k++) { v.hz[k] = 0.f; v.zeta[k] = 0.f; v.gain[k] = 0.f; }
        /* the pitch change this retune is, for the carry: a note world's
           notes, a body row's none; a first build has nothing to carry */
        const float ratio = (keep && kind != 1 && v.param < 1e8f) ? std::exp2((param - v.param) / 12.f) : 1.f;
        v.bank.Set(ha, za, ga, n, sr, fa, keep, ratio);
        /* a strike at another note on this voice: the old note choked —
           its ring over 2 ms (ResonatorBank::Choke; the 5 ms it was gave
           the carried ring, which sits at the new note's frequencies, long
           enough to be heard as the old note bending up: Combust, "it's
           just pitch shifting"), its attack, which is the recording of the
           old note and played to its end, over 5 ms (BurstPlayer::Choke).
           A glide or a tune change carries it on; a strike at the same
           note is a hammer on a ringing tine and adds. */
        if(keep && strike && v.param < 1e8f && std::fabs(param - v.param) > 1e-4f) { v.bank.Choke(2.f, sr); v.burst.Choke(5.f, sr); }
        v.param = param;
        /* the decay axis is the ring's alone: the strike — the recorded
           attack — plays as it is at every setting (Combust: "turning the
           ring decay fully muted shouldn't impact the strike"). It was
           made to fade with the muted T60 for an afternoon. */
        v.burst.damp = 1.f;
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
        /* a voice at rest has nothing to carry: built fresh, so arriving at
           a world is the same as starting in it */
        if(keep && v.pickup.on && v.bank.Ringing())
        {
            /* the pickup retunes without a click. The new point's stage is
               built beside the old one and compared: if the coil is the
               same coil (a glide within one point, transposed, is the
               common case — every 2.5 ms under a sweep) nothing but the
               targets move; if the coil changed (a new point) the old one
               runs on and the output crosses to the new over 10 ms, since a
               biquad's state under new coefficients is a step. Restarting
               the crossfade on every retune was twenty clicks in a
               two-octave glide on the EP. */
            const float u_last = v.pickup.u_last;
            Pickup fresh = v.pickup;
            if(form == 1) fresh.Set(st[0], st[1], st[2], st[3], st[4], sr);
            else if(form == 2) fresh.SetGap(st[1], st[2], st[3], st[4], sr);
            const bool coil_moved = fresh.b0 != v.pickup.b0 || fresh.a1 != v.pickup.a1 || fresh.a2 != v.pickup.a2;
            if(coil_moved)
            {
                v.pickup.Leave(sr);
                v.pickup.b0 = fresh.b0; v.pickup.b1 = fresh.b1; v.pickup.b2 = fresh.b2; v.pickup.a1 = fresh.a1; v.pickup.a2 = fresh.a2;
                v.pickup.z1 = v.pickup.z2 = 0.f;      /* the new coil starts from rest and is crossed into */
            }
            /* the field and the gain are targets; the pickup slews from
               where it is, so a point change is a glide and not a step */
            v.pickup.h_t = fresh.h_t; v.pickup.inv_w_t = fresh.inv_w_t; v.pickup.K_t = fresh.K_t;
            v.pickup.rest = fresh.rest; v.pickup.gap = fresh.gap; v.pickup.on = true;
            /* the last flux re-read under the pole as it stands at the
               tine's actual displacement: the pole moved, so the flux the
               same tine makes has too, and carrying the old value — or its
               distance from rest, which was the first fix — put a step
               through d/dt: −23 dB peaks per deadband step under a voicing
               sweep (docs/holistic-math.md). The displacement is the thing
               that did not move. */
            {
                const float u = (u_last - v.pickup.h) * v.pickup.inv_w;
                v.pickup.prev = v.pickup.gap ? 1.f / (1.f - 0.9f * Pickup::Tanh(u / 0.9f)) : 1.f / (1.f + u * u);
                v.pickup.u_last = u_last;
            }
        }
        else if(form == 1) v.pickup.Set(st[0], st[1], st[2], st[3], st[4], sr);
        else if(form == 2) v.pickup.SetGap(st[1], st[2], st[3], st[4], sr);
        else v.pickup.on = false;
    }
};

} // namespace kyk
