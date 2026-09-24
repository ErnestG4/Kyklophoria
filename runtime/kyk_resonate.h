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

/* The strike's arithmetic, without the library.
 *
 * A note-to-note strike measured 470-odd calls into libm — decode, pole,
 * phase — and on the M7 each is a hundred cycles or more of software
 * float, which is where "an overrun about once a note" (Combust) came
 * from. Three of those are not calls at all: the format stores a mode's
 * decay, its level and its phase as BYTES, so 256 values each, and a
 * table is exact where the call was. The two that remain are a bounded
 * exponential and a sine, and a polynomial is as good as the library at
 * the precision a mode needs (a frequency to a fifth of a cent, a level
 * to a quarter of a dB).
 */
namespace fastmath {

struct Tables
{
    float zeta[256];     /* exp(-0.1 k): the decay byte */
    float level[256];    /* exp(-0.25 k / 8.6859): the level byte, 255 silent */
    float psin[256], pcos[256];   /* the phase byte over a turn */
    bool  ready;
    void Build()
    {
        if(ready) return;
        for(int i = 0; i < 256; i++)
        {
            zeta[i]  = std::exp(-0.1f * (float)i);
            level[i] = i == 255 ? 0.f : std::exp(-0.25f * (float)i * 0.1151293f);
            const float ph = (float)i * (6.2831853f / 256.f);
            psin[i] = std::sin(ph); pcos[i] = std::cos(ph);
        }
        ready = true;
    }
};
inline Tables& T() { static Tables t{}; t.Build(); return t; }

/* 2^x over the range the runtime uses (a mode's cents, a transposition,
   a level in dB): the integer part by the exponent bits, the fraction by
   a quartic within 1.3e-5 over [0,1] — a hundredth of a cent */
inline float Exp2(float x)
{
    if(x < -126.f) return 0.f;
    if(x > 127.f) return 3.4e38f;
    const int k = (int)std::floor(x);
    const float f = x - (float)k;
    /* seven terms of 2^f = e^(f ln2): within 2e-8 over [0,1], where the
       quartic's 1e-5 was enough to move a mode by a fifth of a cent and
       the format's own round trip noticed */
    const float p = 1.f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015403f)))));
    union { float f; uint32_t u; } s;
    s.u = (uint32_t)((127 + k) << 23);
    return p * s.f;
}

/* log2 of a positive float: the exponent from its bits, the mantissa's by a
   quintic on [1, 2) within 3e-5 (a five-thousandth of a dB) — for blending
   decays and levels between two points on the scale they are heard on,
   without a libm call */
inline float Log2(float x)
{
    union { float f; uint32_t u; } v; v.f = x;
    const int e = (int)((v.u >> 23) & 255u) - 127;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
    const float y = v.f - 1.f;
    return (float)e + y * (1.441825795f + y * (-0.708682101f + y * (0.415421949f + y * (-0.194422672f + y * 0.045885527f))));
}

/* exp(-x) for the pole radius, where x = zeta w is at most about 0.06:
   the series to five terms is exact to 1e-9 there */
inline float ExpNegSmall(float x)
{
    if(x > 0.2f) return Exp2(-1.44269504f * x);        /* a ghost's zeta is 1: still no call */
    return 1.f - x * (1.f - x * (0.5f - x * (0.16666667f - x * (0.041666667f - x * 0.0083333333f))));
}

/* sin and cos of w = 2 pi f / sr, |w| <= pi (f up to Nyquist): quintic
   and sextic minimax, within 2e-7 — a mode's phase to a millionth of a
   turn, where the format stores it to a 256th */
inline void SinCos(float w, float& s, float& c)
{
    const float x2 = w * w;
    /* far enough along the series that pi is still accurate: the sine to
       x^13 and the cosine to x^14, within 1e-7 over [-pi, pi] (four terms
       each left the cosine a fortieth out at pi, which is a click) */
    s = w * (1.f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * (0.00019841270f - x2 * (0.0000027557319f - x2 * (2.5052108e-8f - x2 * 1.6059044e-10f))))));
    c = 1.f - x2 * (0.5f - x2 * (0.041666667f - x2 * (0.0013888889f - x2 * (0.0000248016f - x2 * (2.7557319e-7f - x2 * (2.0876757e-9f - x2 * 1.1470746e-11f))))));
}

} // namespace fastmath

struct ResonatorBank
{
    static constexpr int kMax = 48;
    static constexpr float kWMax = 3.0f;      /* a mode's w past this (0.95 of Nyquist) is not played */

    int   n;
    float c1[kMax], c2[kMax];            /* 2 r cos w, -r^2 */
    /* each mode's pole as it was built, kept so a retune does not have to
       recover it: acos(c1 / 2r) per mode was the costliest call in a
       strike, and a sin of it another */
    float wq[kMax], cwq[kMax], swq[kMax], rq[kMax], lrq[kMax];   /* lrq: log r, so advancing k samples is an exp rather than a pow */
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
    /* the raised cosine as a rotating phasor, carried across the runs it
       is drawn in: seeding it per run was a sine and a cosine every block
       for as long as a strike was coming in, which on a piano is fifty
       blocks a note */
    float ramp_c[kStrikes], ramp_s[kStrikes];
    float held[kStrikes];     /* samples a strike bank has sat out of its lead, its state not yet advanced by them */
    /* the choke: for damp_left samples every mode decays by damp_c a
       sample more than its own pole says — the main state and the strike
       banks marked damp_bank, not a strike that arrives meanwhile */
    int   damp_left;
    float damp_c;
    bool  damp_bank[kStrikes];
    /* the tail: the last note's ring at its OWN frequencies, under a damper,
       while the new note starts from nothing. A strike at another note on
       a voice used to carry the old ring onto the new note's modes and choke
       it: at 5 ms that was heard as the old note bending ("it's just pitch
       shifting"), at 2 ms as a pop wherever the ring was still loud — a
       long decay, overlapping notes, any voice count (Combust: "a single
       push/pull, a single harsh near vertical in the waveform right at note
       start ... only when the resonance is up near halfway and beyond").
       A string the next key is struck on is another string; the one before
       is damped where it rings */
    int   tn, tail_left;
    float tail_c;
    float tc1[kMax], tc2[kMax], ty1[kMax], ty2[kMax];
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
        damp_c = fastmath::Exp2(-9.9657843f / (float)damp_left);   /* 1e-3 over the window */
        for(int k = 0; k < kStrikes; k++)
        {
            damp_bank[k] = true;
            /* a strike bank still in its lead is not heard yet and would
               arrive 60 dB down: let go of it now */
            if(ramping[k] && ramp_n[k] < ramp_lead[k]) { for(int i = 0; i < n; i++) s1[k][i] = s2[k][i] = 0.f; ramping[k] = false; held[k] = 0.f; }
        }
    }

    /* the ring let go to the tail: what is heard of the strike banks folded
       in first (one still in its lead is not heard yet and is dropped), the
       main state moved over with the poles it rings at, and the damper set
       to take it 60 dB down over ms. The bank is left silent for the new
       note. A tail still sounding from the note before is replaced: at
       40 ms it is a thousandth of itself by the next note at 25 a second */
    void Release(float ms, float sr)
    {
        for(int q = 0; q < kStrikes; q++) if(ramping[q]) Fold(q);
        /* a ring that is not sounding leaves no tail: a first note, or one
           rung out, is exactly what it was before there were tails (the
           pickup's crossfade asks Ringing(), and a silent tail said yes) */
        bool any = false;
        for(int i = 0; i < n; i++) if(std::fabs(y1[i]) > 1e-7f || std::fabs(y2[i]) > 1e-7f) { any = true; break; }
        tn = any ? n : 0;
        for(int i = 0; i < n; i++) { tc1[i] = c1[i]; tc2[i] = c2[i]; ty1[i] = y1[i]; ty2[i] = y2[i]; y1[i] = y2[i] = 0.f; }
        tail_left = any ? (int)(ms * 0.001f * sr) : 0; if(any && tail_left < 1) tail_left = 1;
        tail_c = fastmath::Exp2(-9.9657843f / (float)tail_left);   /* 1e-3 over the window */
        damp_left = 0; damp_c = 1.f;
        for(int q = 0; q < kStrikes; q++) damp_bank[q] = false;
    }

    void Init()
    {
        n = 0;
        for(int q = 0; q < kStrikes; q++) { ramp_n[q] = 0.f; ramp_len[q] = 144.f; ramp_lead[q] = 0.f; ramping[q] = false; damp_bank[q] = false; held[q] = 0.f; ramp_c[q] = 1.f; ramp_s[q] = 0.f; }
        for(int i = 0; i < kMax; i++) { c1[i] = c2[i] = p1[i] = p2[i] = g[i] = y1[i] = y2[i] = 0.f; wq[i] = cwq[i] = swq[i] = rq[i] = lrq[i] = 0.f; for(int q = 0; q < kStrikes; q++) s1[q][i] = s2[q][i] = 0.f; tc1[i] = tc2[i] = ty1[i] = ty2[i] = 0.f; }
        damp_left = 0; damp_c = 1.f;
        tn = 0; tail_left = 0; tail_c = 1.f;
    }
    bool Ringing() const
    {
        if(tn > 0 && tail_left > 0) return true;
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
    void Set(const float* hz, const float* zeta, const float* gain, int count, float sr, const float* phase = nullptr, bool keep = false, float ratio = 1.f, float lv_max = 2.f, bool by_slot = false)
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
            for(int q = 0; q < kStrikes; q++) if(held[q] > 0.f) { Advance(q, held[q]); held[q] = 0.f; }   /* read as it is now, not as it was */
            float* a[1 + kStrikes] = {y1, s1[0], s1[1]}; float* b[1 + kStrikes] = {y2, s2[0], s2[1]};
            for(int i = 0; i < n0; i++)
            {
                has[i] = false; claimed[i] = false; g0[i] = g[i]; w0[i] = wq[i];
                if(!(c2[i] < 0.f)) continue;
                const float r0 = rq[i], cw = cwq[i];
                const float sw0 = swq[i] > 1e-6f ? swq[i] : 1e-6f;
                float e = 0.f;
                for(int q = 0; q < 1 + kStrikes; q++)
                {
                    sp[q][i] = a[q][i]; cp[q][i] = (a[q][i] * cw - b[q][i] * r0) / sw0;   /* A sin phi, A cos phi */
                    e += sp[q][i] * sp[q][i] + cp[q][i] * cp[q][i];
                }
                has[i] = e > 1e-20f;
            }
        }
        /* who carries what, decided first and loudest first: a new mode
           takes the unclaimed old mode nearest by ratio, within a fifth,
           in the order of its own level. In index order a quiet mode came
           first wherever it sat below a loud one — and a blend of two
           points (At) puts quiet partials beside loud ones — claimed the
           loud ring and scaled it to its own level: a glide through a
           blend lost 90 dB of ring in 0.4 s, one rebuild at a time */
        int from_of[kMax];
        for(int i = 0; i < n; i++) from_of[i] = -1;
        if(keep)
        {
            /* by the energy a mode carries, gain^2 over its decay rate (the
               export's own rank): the modes that ring longest claim first,
               so a long ring goes on in a long mode and not in a loud
               short one beside it */
            /* unless the caller says the slots correspond (by_slot): a
               row of bodies folds its neighbours mode for mode, and slot
               order is the one it always had — taken in energy order a
               nudge along the row let a loud mode take its neighbour's
               ring, 20 dB of it lost. A note world's modes are a blend of
               two points and have no such order */
            int ord[kMax]; float en[kMax];
            const bool slots = by_slot;
            for(int i = 0; i < n; i++)
            {
                const float d = zeta[i] * hz[i];
                en[i] = slots ? (float)(n - i) : gain[i] * gain[i] / (d > 1e-12f ? d : 1e-12f);
                int k = i;
                while(k > 0 && en[ord[k - 1]] < en[i]) { ord[k] = ord[k - 1]; k--; }
                ord[k] = i;
            }
            for(int o = 0; o < n; o++)
            {
                const int i = ord[o];
                const float w = 6.2831853f * hz[i] / sr;
                if(!(w < kWMax)) continue;
                float best = 1.4f;                      /* within a fifth either way */
                int from = -1;
                for(int j = 0; j < n0; j++)
                {
                    if(!has[j] || claimed[j]) continue;
                    const float wj = w0[j] * ratio;
                    /* most candidates are nowhere near: rejected on two
                       multiplies, where every one of them was a divide.
                       On the M7 a divide is fourteen cycles the compare
                       then waits on, and the loop the compiler emits is
                       some thirty cycles a candidate with it and under
                       twenty without — 44 x 44 on a piano point, about a
                       quarter of a block's budget at every strike before
                       this (an estimate from the emitted code, not a
                       measurement on the module). The margin is a
                       hundred times the float's rounding, so what it
                       rejects has hi / lo > best exactly and the divide
                       would have rejected it too */
                    const float hi = wj > w ? wj : w, lo = wj > w ? w : wj;
                    if(hi > best * lo * 1.0001f) continue;
                    const float q = hi / lo;
                    if(q < best) { best = q; from = j; }
                }
                if(from >= 0) { claimed[from] = true; from_of[i] = from; }
            }
        }
        for(int i = 0; i < n; i++)
        {
            const float w = 6.2831853f * hz[i] / sr;
            /* a mode at or past Nyquist cannot sound at this rate, and
               SinCos is a polynomial for |w| <= pi: past it cos(10) came out
               -356, the pole far outside the unit circle, and the mode grew
               until the codec railed — the scream Combust recorded with
               v/oct at audio rate, which lands notes octaves over a world's
               top (a cello from a 4 kHz fundamental, a guitar from 2 kHz).
               Silent, its state and its strike cleared: the note keeps what
               the rate can hold */
            if(!(w < kWMax))
            {
                c1[i] = c2[i] = 0.f; wq[i] = cwq[i] = swq[i] = rq[i] = lrq[i] = 0.f;
                p1[i] = p2[i] = 0.f; g[i] = 0.f; y1[i] = y2[i] = 0.f;
                for(int q = 0; q < kStrikes; q++) s1[q][i] = s2[q][i] = 0.f;
                continue;
            }
            const float r = fastmath::ExpNegSmall(zeta[i] * w);
            const float ph = phase ? phase[i] : 0.f;
            const int from = from_of[i];
            float cwn, swn; fastmath::SinCos(w, swn, cwn);
            c1[i] = 2.0f * r * cwn;
            c2[i] = -r * r;
            wq[i] = w; cwq[i] = cwn; swq[i] = swn; rq[i] = r; lrq[i] = -zeta[i] * w;
            /* sin(ph - w) and sin(ph - 2w) from the angle sum: one cos and
               one sin a mode rather than three */
            float sph, cph; fastmath::SinCos(ph > 3.1415927f ? ph - 6.2831853f : ph, sph, cph);
            const float s1w = sph * cwn - cph * swn;
            const float c1w = cph * cwn + sph * swn;
            p1[i] = gain[i] * s1w / r;
            p2[i] = gain[i] * (s1w * cwn - c1w * swn) / (r * r);
            if(from >= 0)
            {
                const float go = std::fabs(g0[from]), gn = std::fabs(gain[i]);
                const float lv = go > 1e-9f ? (gn / go > lv_max ? lv_max : gn / go) : 1.f;
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
        held[q] = 0.f;
        for(int i = 0; i < n; i++) { s1[q][i] = swing * p1[i]; s2[q][i] = swing * p2[i]; }
        damp_bank[q] = false;                  /* a choke under way is the old note's, not this strike's */
        ramp_c[q] = 1.f; ramp_s[q] = 0.f;      /* the ramp's phasor at u = 0 */
        ramp_n[q] = 0.f;
        ramp_lead[q] = lead;
        if(ramp > 0.f) ramp_len[q] = ramp;
        ramping[q] = true;
    }

    /* a bend: every mode's frequency multiplied by ratio, the state left
       as it stands. The pole moves by a few cents and the state is read
       under the new one as very nearly the same sinusoid — no amplitude
       and phase solve, no decode, two transcendentals a mode against a
       hundred for a rebuild, and nothing of the point's but its pitch
       changes, which is what a bend is: the note keeps the timbre it was
       struck with and slides. */
    void Bend(const float* hz, const float* zeta, int count, float sr)
    {
        const int nn = count > n ? n : count;
        for(int i = 0; i < nn; i++)
        {
            const float w = 6.2831853f * hz[i] / sr;
            if(!(w < kWMax))     /* bent past Nyquist: silent, as Set has it */
            {
                c1[i] = c2[i] = 0.f; wq[i] = cwq[i] = swq[i] = rq[i] = lrq[i] = 0.f; y1[i] = y2[i] = 0.f;
                for(int q = 0; q < kStrikes; q++) s1[q][i] = s2[q][i] = 0.f;
                continue;
            }
            const float r = fastmath::ExpNegSmall(zeta[i] * w);
            float sw, cw; fastmath::SinCos(w, sw, cw);
            c1[i] = 2.0f * r * cw;
            c2[i] = -r * r;
            wq[i] = w; cwq[i] = cw; swq[i] = sw; rq[i] = r; lrq[i] = -zeta[i] * w;
        }
    }

    /* a strike bank moved on by k samples without running: each mode's
       state read as an amplitude and a phase under its pole, the
       amplitude decayed by r^k, the phase advanced by k w, and the pair
       written back. Used to skip a burst's lead (Process) */
    void Advance(int q, float k)
    {
        const float* y1s = s1[q]; float* a = s1[q]; float* b = s2[q];
        for(int i = 0; i < n; i++)
        {
            if(!(c2[i] < 0.f)) continue;
            const float r = rq[i], cw = cwq[i], w = wq[i];
            const float sw = swq[i] > 1e-6f ? swq[i] : 1e-6f;
            const float sp = y1s[i], cp = (y1s[i] * cw - b[i] * r) / sw;     /* A sin phi, A cos phi */
            const float g = fastmath::Exp2(1.4426950f * k * lrq[i]), th = k * w;   /* r^k, without a pow */
            const float sp2 = g * (sp * std::cos(th) + cp * std::sin(th));    /* A r^k sin(phi + k w) */
            const float cp2 = g * (cp * std::cos(th) - sp * std::sin(th));    /* A r^k cos(phi + k w) */
            a[i] = sp2;
            b[i] = (sp2 * cw - cp2 * sw) / r;                                   /* A sin(phi - w) / r, one sample back */
        }
    }

    /* the strike bank folded into the main state at the weight it is
       heard at now. It folded at full: a strike inside the last one's
       burst window — the lead is up to 300 ms on a piano's bass — took a
       bank that was inaudible under the burst to full in one sample, a
       pop on every fast note that the old 3 ms ramp never showed */
    void Fold(int q)
    {
        /* a bank still inside its lead is heard at weight zero, so folding
           it adds nothing at all — and bringing it up to date first was a
           pow and a sincos a mode for nothing, which is most of what a
           fast repeat cost */
        if(ramp_n[q] + held[q] <= ramp_lead[q])
        {
            for(int i = 0; i < n; i++) s1[q][i] = s2[q][i] = 0.f;
            ramping[q] = false; held[q] = 0.f;
            return;
        }
        if(held[q] > 0.f) { Advance(q, held[q]); held[q] = 0.f; }
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
                /* the lead — the burst's body, before its fade, up to
                   300 ms on a bass piano note — is heard at weight zero:
                   the bank's work through it was wasted, two banks' worth
                   on every fresh strike, and at four voices most of the
                   block. Skipped: the bank sits, and is advanced by the
                   samples it sat out — a decaying sinusoid k samples on is
                   its amplitude by r^k and its phase by k w — the moment the
                   ramp begins. The state then is what running it would have
                   given, to the rounding */
                /* d: the samples of this run still inside the lead (heard
                   at weight zero); the bank is advanced past them and
                   runs the rest of the run into the rest of the output */
                int d = 0;
                if(ramp_n[q] < ramp_lead[q])
                {
                    const float left = ramp_lead[q] - ramp_n[q];
                    d = (int)std::ceil(left);
                    if(d >= m) { ramp_n[q] += (float)m; held[q] += (float)m; continue; }
                }
                if(held[q] > 0.f || d > 0)          /* the lead ends in this run, or ended exactly at its start */
                {
                    Advance(q, held[q] + (float)d); held[q] = 0.f;
                    ramp_n[q] += (float)d;
                }
                float* o_ = out + d;
                const int mm = m - d;
                /* the raised cosine for the run as a rotating phasor: one
                   cos and one sin, then a multiply a sample, where it was a
                   cos a sample — two banks' worth on each of four voices
                   was a tenth of the block in cosf */
                float w[64];
                {
                    const float u0 = ramp_n[q] - ramp_lead[q];
                    const float step = 3.1415927f / ramp_len[q];
                    float dc_, ds_; fastmath::SinCos(step, ds_, dc_);
                    float c = ramp_c[q], s_ = ramp_s[q];
                    /* one Newton step against the drift a carried phasor
                       accumulates over a long ramp */
                    const float k2 = 1.5f - 0.5f * (c * c + s_ * s_);
                    c *= k2; s_ *= k2;
                    for(int k = 0; k < mm; k++)
                    {
                        w[k] = u0 + (float)k <= 0.f ? 0.f : 0.5f - 0.5f * c;
                        const float c2 = c * dc_ - s_ * ds_; s_ = s_ * dc_ + c * ds_; c = c2;
                    }
                    ramp_c[q] = c; ramp_s[q] = s_;
                }
                const float sc = damp_left > 0 && damp_bank[q] ? dc : 1.f, sc2 = sc * sc;
                int i = 0;
                for(; i + 1 < n; i += 2)      /* in pairs, as above */
                {
                    const float a0 = c1[i] * sc, b0 = c2[i] * sc2, a1 = c1[i + 1] * sc, b1 = c2[i + 1] * sc2;
                    float p1 = s1[q][i], p2 = s2[q][i], r1 = s1[q][i + 1], r2 = s2[q][i + 1];
                    for(int k = 0; k < mm; k++)
                    {
                        const float yp = a0 * p1 + b0 * p2;
                        const float yr = a1 * r1 + b1 * r2;
                        p2 = p1; p1 = yp;
                        r2 = r1; r1 = yr;
                        float o = o_[k];
                        o += w[k] * yp; o += w[k] * yr;
                        o_[k] = o;
                    }
                    s1[q][i] = p1; s2[q][i] = p2; s1[q][i + 1] = r1; s2[q][i + 1] = r2;
                }
                for(; i < n; i++)
                {
                    const float a = c1[i] * sc, b = c2[i] * sc2;
                    float u1 = s1[q][i], u2 = s2[q][i];
                    for(int k = 0; k < mm; k++)
                    {
                        const float y = a * u1 + b * u2;
                        u2 = u1; u1 = y;
                        o_[k] += w[k] * y;
                    }
                    s1[q][i] = u1; s2[q][i] = u2;
                }
                ramp_n[q] += (float)mm;
                if(ramp_n[q] >= ramp_lead[q] + ramp_len[q]) Fold(q);
            }
            /* the tail: the last note at its own poles pulled in by the
               damper, into the same output, so a pickup hears it as the
               tine it is */
            if(tn > 0)
            {
                const int mt = m < tail_left ? m : tail_left;
                const float tc = tail_c, tc2_ = tail_c * tail_c;
                for(int i = 0; i < tn; i++)
                {
                    const float a = tc1[i] * tc, b = tc2[i] * tc2_;
                    float u1 = ty1[i], u2 = ty2[i];
                    for(int k = 0; k < mt; k++)
                    {
                        const float y = a * u1 + b * u2;
                        u2 = u1; u1 = y;
                        out[k] += y;
                    }
                    ty1[i] = u1; ty2[i] = u2;
                }
                tail_left -= mt;
                if(tail_left <= 0) tn = 0;
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
        /* the coil's corner under Nyquist: a coil spun up (x2) on a point
           fitted high put it past, and a biquad there has a negative
           alpha — unstable */
        const float w0 = 6.2831853f * (fc < 0.45f * sr ? fc : 0.45f * sr) / sr;
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
    /* ts: where in the burst its velocity taper starts (past n: none);
       tc, tn: the taper's raised cosine as a rotating phasor; tcs, tsn: its
       step a sample */
    struct Slot { const int16_t* s; uint32_t n; float pos, rate, gain, lp, z, g, d, ts, tc, tn, tcs, tsn; };
    Slot slot[kSlots];
    int  active;
    float damp;                       /* the per-sample multiplier a new burst starts with: 1, or under it when the decay is muted */

    void Init() { active = 0; damp = 1.f; for(auto& q : slot) { q.s = nullptr; q.n = 0; q.pos = q.rate = q.gain = q.lp = q.z = 0.f; q.g = q.d = 1.f; q.ts = 3e9f; q.tc = 1.f; q.tn = 0.f; q.tcs = 1.f; q.tsn = 0.f; } }
    /* every burst playing fades out over ms: the old note's attack, when
       a strike at another note has choked its ring — a burst that played
       to its end was the old note going on for up to 390 ms under the new */
    void Choke(float ms, float sr)
    {
        const float d = fastmath::Exp2(-9.9657843f / (ms * 0.001f * sr));
        for(int a = 0; a < active; a++) if(slot[a].d > d) slot[a].d = d;
    }
    /* a bend: what is still playing is read faster or slower from where
       it has got to, as a sampler's pitch wheel does */
    void Bend(float ratio) { for(int a = 0; a < active; a++) slot[a].rate *= ratio; }
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
    /* played_len / played_fade, when given, receive the length and the fade
       (samples, at the burst's own rate) of the takes this strike plays,
       weighted as they are mixed: the modes must come in under the fade of
       the attack that SOUNDS, and they were timed from a point's first take
       whichever one velocity picked — a hole at 70-80 ms on the VCSL
       grand's C5 at full velocity, where the hardest take is shorter than
       the softest, and the two doubling where it is longer */
    /* share: how much of each take plays, 1 for all of it. Under 1 the take
       ends at that share of its length (30 ms at least) and slopes off over
       its fade scaled the same, a raised cosine; played_len and played_fade
       report what is kept, so the modes come in under the new seam */
    void Strike(const uint8_t* block, float swing, float rate = 1.0f, float lp = 0.0f, uint32_t head = 10u,
                float* played_len = nullptr, float* played_fade = nullptr, float share = 1.f)
    {
        float plen = 0.f, pfade = 0.f, pw = 0.f;
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
                    uint16_t fd = (uint16_t)(n / 3);
                    if(head >= 12u) std::memcpy(&fd, pick[i] + 10, 2);
                    float ln = (float)n, lf = (float)fd;
                    q2.ts = 3e9f; q2.tc = 1.f; q2.tn = 0.f; q2.tcs = 1.f; q2.tsn = 0.f;
                    if(share < 1.f && n > 0)
                    {
                        const float mn = 1440.f < (float)n ? 1440.f : (float)n;   /* 30 ms of the take at least */
                        ln = share * (float)n; if(ln < mn) ln = mn;
                        lf = (float)fd * ln / (float)n; if(lf < 48.f) lf = 48.f; if(lf > ln) lf = ln;
                        q2.n = (uint32_t)ln + 1u < n ? (uint32_t)ln + 1u : n;
                        q2.ts = ln - lf;
                        fastmath::SinCos(3.1415927f * q2.rate / lf, q2.tsn, q2.tcs);
                    }
                    plen += wgt[i] * ln; pfade += wgt[i] * lf; pw += wgt[i];
                }
            }
        }
        for(int k = 0; k < keep; k++) slot[active++] = old[k];
        if(played_len) *played_len = pw > 0.f ? plen / pw : -1.f;
        if(played_fade) *played_fade = pw > 0.f ? pfade / pw : -1.f;
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
                float tw = 1.f;
                if(q.pos >= q.ts)
                {
                    tw = q.tc > -1.f ? 0.5f + 0.5f * q.tc : 0.f;
                    const float c2 = q.tc * q.tcs - q.tn * q.tsn; q.tn = q.tn * q.tcs + q.tc * q.tsn; q.tc = c2;
                    if(q.tn < 0.f) q.tc = -1.f;          /* past pi: the taper is done */
                }
                io[k] += q.gain * q.g * tw * v;
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
    /* the gains depend on the sample rate alone, so every voice shares one
       measurement (Set). Constant-initialised, as fastmath::T() is: no
       guard, no constructor, nothing written before main() */
    struct SharedGains { float gain[kBands]; float sr; };
    static SharedGains& Shared() { static SharedGains s{}; return s; }
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
            const float w0 = 6.2831853f * fc / sr; float s0, c0; fastmath::SinCos(w0, s0, c0); const float alpha = s0 / (2.f * 1.41421f);
            const float a0 = 1.f + alpha;
            b0[k] = alpha / a0; a1[k] = -2.f * c0 / a0; a2[k] = (1.f - alpha) / a0;
            /* the filter's own noise power gain, measured from its impulse
               response — the octave's share of the spectrum, (hi - lo) / (sr / 2),
               was the guess before and it was 2 dB hot in the low bands and
               1 dB cold at the top, because a Q 1.41 biquad is not a brick
               wall and its width warps towards Nyquist. 2048 samples holds
               the whole ring at band 0 (2Q / w0 = 5 ms); 8 x 2048 MACs at
               note-on, on the control thread */
            if(gain_sr != sr)
            {
                /* once per sample rate, not per Set, and not per voice
                   either: the filters are the same eight on every voice,
                   so the gains are measured once and shared. Per voice it
                   was 8 x 2048 MACs on each voice's first build after
                   every world load — the first strike on voices two to
                   four, on the audio thread, some 200 000 instructions
                   counted on the desktop where the block's own work is
                   15 000: an overrun each, however the M7 schedules it */
                SharedGains& sh = Shared();
                if(sh.sr != sr)
                {
                    float g = 0.f, u1 = 0.f, u2 = 0.f, v1 = 0.f, v2 = 0.f;
                    for(int i = 0; i < 2048; i++)
                    {
                        const float u = i == 0 ? 1.f : 0.f;
                        const float v = b0[k] * (u - u2) - a1[k] * v1 - a2[k] * v2;
                        u2 = u1; u1 = u; v2 = v1; v1 = v;
                        g += v * v;
                    }
                    sh.gain[k] = std::sqrt(g);
                    if(k == kBands - 1) sh.sr = sr;
                }
                gain[k] = sh.gain[k];
                if(k == kBands - 1) gain_sr = sr;
            }
            const float share = gain[k];
            level[k] = lvl[k] > 0.f && t60[k] > 0.f ? lvl[k] / (share > 1e-6f ? share : 1e-6f) : 0.f;
            fall[k]  = t60[k] > 0.f ? fastmath::ExpNegSmall(6.91f / (t60[k] * sr)) : 0.f;
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
    float          body_top;          /* the instrument's own corner: where its envelope is 12 dB down, 0 for a world with no curve */
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
        bank.Init(); pickup.Init(); burst.Init(); wash.Init(); bursts = nullptr; swing_soft = swing_hard = 1.f; burst_rate = 1.f; sr = 48000.f; burst_len = 0; burst_head = 10; burst_fade = 0; param = 1e9f; cap = 0; body_top = 0.f;
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
        /* one take: the attack is filtered by velocity, a one-pole with its
           corner from 1 kHz at nothing to 13 kHz at full — the hammer's felt
           and the finger's pad are softer the slower they arrive. Takes
           carry this themselves, so a layered world gets no filter */
        float fc = 1e9f;
        if(!(swing_hard > swing_soft))
        {
            const float v = velocity01 < 0.f ? 0.f : velocity01 > 1.f ? 1.f : velocity01;
            if(v < 1.f) fc = 1000.f * std::exp2(3.7f * v);
        }
        /* and the attack of a point carried up the keyboard is read faster,
           which moves its own spectrum up with it: the same body curve, as
           far as a one-pole can carry it — the instrument's own corner
           (where its envelope has fallen 12 dB from its loudest band)
           divided by the read rate */
        if(burst_rate > 1.05f && body_top > 0.f) { const float c = body_top / burst_rate; if(c < fc) fc = c; }
        const float lp = fc > 0.4f * sr ? 0.f : 1.f - std::exp(-6.2831853f * fc / sr);
        /* and the recorded intro follows the velocity: all of it at the
           top, a quarter at the bottom (30 ms at least), sloping off rather
           than playing through — Combust: "dampen the intro with the
           Velocity, so it doesn't play all the way through and instead
           slopes off". A piano's bass take is up to 400 ms of recording, and
           a soft note played every millisecond of the hard one's intro */
        const float vk = velocity01 < 0.f ? 0.f : velocity01 > 1.f ? 1.f : velocity01;
#ifndef KYK_BURST_FULL
        const float keep = 0.25f + 0.75f * vk;
#else
        const float keep = 1.f; (void)vk;
#endif
        float plen = -1.f, pfade = -1.f;
        burst.Strike(bursts, s, burst_rate, lp, burst_head, &plen, &pfade, keep);
        /* the modes come in under the fade of the attack that is playing
           (BurstPlayer::Strike): the length and fade of the takes it picked,
           weighted as it mixes them. One take alone — every world fitted
           from one, and a layered one at a take's own swing — is exactly
           that take's; the point's first take, which this used to take
           whatever played, is kept only where nothing did */
        const float blen = burst_fade && plen >= 0.f ? plen : (float)burst_len;
        const float bfade = burst_fade && pfade >= 0.f ? pfade : (float)burst_fade;
        const float rr = burst_rate > 0.f ? burst_rate : 1.f;
        const float ramp = burst_fade ? bfade / rr : 0.003f * sr;
        const float lead = burst_fade ? (blen - bfade) / rr : 0.f;
        bank.Strike(s, lead, ramp);
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
    /* how long the last note's ring takes to fall 60 dB when the next note
       on its voice is at another pitch: a damper, not a cut (see
       ResonatorBank::Release) */
    static constexpr float kTailMs = 40.f;
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
    /* version 7 carries the instrument's own radiation envelope after the
       header: eight octave-band gains in dB from 62.5 Hz up, 0 at its
       loudest band (ModalBake's tools/body.py measures it over every mode
       of every note in the set, separating what depends on the partial
       number — the string's own rolloff — from what depends on absolute
       frequency, which is the body). A body radiates and absorbs where it
       is, not where the note is, so a point carried to another note is
       re-weighted by this curve's ratio between where each mode now
       sounds and where it was fitted. Measured, the curves are nothing
       like each other: a double bass peaks at 88 Hz and is 12 dB down by
       177, a banjo peaks at 707 Hz and falls 7 dB an octave above it, a
       mandolin is within 12 dB everywhere. */
    uint32_t HeaderBytes() const { return ver >= 7 ? kHeader + 32u : kHeader; }
    float    body[8];
    bool     has_body;
    uint32_t FixedBytes() const { return 4 + 8 * 4 + 5u * N; }
    /* the curve at a frequency, interpolated in log frequency between the
       band centres (88.4 Hz x 2^k), flat outside them */
    float Body(float hz) const
    {
        if(!has_body || !(hz > 0.f)) return 0.f;
        const float x = std::log2(hz / 88.388f);
        if(x <= 0.f) return body[0];
        if(x >= 7.f) return body[7];
        const int k = (int)x; const float t = x - (float)k;
        return body[k] + t * (body[k + 1] - body[k]);
    }

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
        const uint8_t* q = blob + HeaderBytes();
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

    void Init() { blob = nullptr; size = 0; N = P = 0; form = kind = 0; ver = 0; lo = hi = 0.f; voicing = 0.f; decay = coil = 1.f; M = 0; for(int m = 0; m < kMaxMembers; m++) member_off[m] = member_len[m] = 0; poff_[0] = kHeader; pparam_[0] = 0.f; has_body = false; for(int k = 0; k < 8; k++) body[k] = 0.f; }

    bool Attach(const void* data, uint32_t bytes)
    {
        blob = (const uint8_t*)data; size = bytes;
        if(bytes < kHeader || std::memcmp(blob, "KYKM", 4) != 0) return false;
        uint16_t v; std::memcpy(&v, blob + 4, 2); ver = (uint8_t)v;
        std::memcpy(&N, blob + 6, 2); std::memcpy(&P, blob + 8, 2);
        form = blob[10]; kind = blob[11];      /* version 4 wrote an unread body count here, always 0: a note */
        std::memcpy(&lo, blob + 12, 4); std::memcpy(&hi, blob + 16, 4);
        has_body = false;
        for(int k = 0; k < 8; k++) body[k] = 0.f;
        if(v >= 7 && kind != 2)
        {
            if(size < kHeader + 32u) return false;
            std::memcpy(body, blob + kHeader, 32);
            has_body = true;
        }
        M = 0;
        if(kind == 2)
        {
            if(v < 6 || v > 7 || size < kHeader + 1) return false;
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
        if(!((v >= 4 && v <= 7) && N <= ResonatorBank::kMax && size >= HeaderBytes() + (uint32_t)P * FixedBytes())) return false;
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
        for(int k = 0; k < N; k++) DecodeMode(m, k, k, hz, zeta, gain, phase);
    }
    void DecodeMode(const uint8_t* m, int k, int at, float* hz, float* zeta, float* gain, float* phase) const
    {
        uint16_t c; std::memcpy(&c, m + 5 * k, 2);
        hz[at]    = 20.0f * fastmath::Exp2(ver >= 6 ? c / 6000.0f : c / 1200.0f);   /* fifths of a cent from version 6 */
        zeta[at]  = fastmath::T().zeta[m[5 * k + 2]];
        gain[at]  = (ver >= 6 && m[5 * k + 3] == 255) ? 0.f : fastmath::T().level[m[5 * k + 3]];   /* dB -> linear; 255 is silence */
        phase[at] = m[5 * k + 4] * (6.2831853f / 256.0f);
    }
    /* the cap loudest modes of a point, chosen on the bytes — the log of
       gain^2 over zeta x hz is linear in the level, decay and cents bytes
       — so a capped voice decodes only the modes it will play: twelve
       exps, not forty-eight, on every At() at four voices. Returns how
       many, their indices ascending (frequency order) in idx */
    int Loudest(int i, int cap, int* idx) const
    {
        const uint8_t* m = Modes(i);
        float score[ResonatorBank::kMax]; bool take[ResonatorBank::kMax];
        for(int k = 0; k < N; k++)
        {
            uint16_t c; std::memcpy(&c, m + 5 * k, 2);
            const uint8_t d = m[5 * k + 2], l = m[5 * k + 3];
            score[k] = (ver >= 6 && l == 255) ? -1e9f : -0.0575647f * (float)l + 0.1f * (float)d - (ver >= 6 ? c / 6000.0f : c / 1200.0f) * 0.6931472f;
            take[k] = false;
        }
        for(int c = 0; c < cap && c < N; c++)
        {
            int best = -1;
            for(int k = 0; k < N; k++) if(!take[k] && (best < 0 || score[k] > score[best])) best = k;
            if(best < 0 || score[best] <= -1e8f) break;
            take[best] = true;
        }
        int n = 0;
        for(int k = 0; k < N; k++) if(take[k]) idx[n++] = k;
        return n;
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
    /* the voice bent to a new parameter: the modes it was built with,
       transposed, and the burst read faster or slower. Nothing else of
       the point changes — not the pickup, not the wash, not which point
       it came from — so a bend across a midpoint keeps the note's own
       timbre instead of stepping into the next point's, and costs a
       twentieth of a rebuild. Note worlds only; a body row's axis is a
       position, not a pitch. */
    void Bend(float param, ResonatorVoice& v, float sr) const
    {
        if(kind == 1 || v.param > 1e8f) return;
        const float ratio = std::exp2((param - v.param) / 12.f);
        int n = 0;
        for(int k = 0; k < ResonatorBank::kMax; k++) { if(v.gain[k] == 0.f && v.hz[k] == 0.f) break; v.hz[k] *= ratio; n = k + 1; }   /* the voice's own modes: a blend can hold more than the world's N */
        v.bank.Bend(v.hz, v.zeta, n, sr);
        v.burst_rate *= ratio;
        v.burst.Bend(ratio);
        v.param = param;
    }

    /* one point as it sounds at param: its modes (all, or the cap's
       loudest, on the bytes), transposed, at the point's level, and
       re-weighted by the body curve; its stage into stage when asked */
    int Transposed(int near, float param, int cap, float* ha, float* za, float* ga, float* fa, float* stage) const
    {
        int nd = N;
        if(cap > 0 && cap < N)
        {
            int idx[ResonatorBank::kMax];
            nd = Loudest(near, cap, idx);
            const uint8_t* m = Modes(near);
            for(int k = 0; k < nd; k++) DecodeMode(m, idx[k], k, ha, za, ga, fa);
        }
        else Decode(near, ha, za, ga, fa);
        float st[8];
        std::memcpy(st, Stage(near), 32);
        if(stage) std::memcpy(stage, st, 32);
        const float r = std::exp2((param - Param(near)) / 12.f);
        /* the body stays where it is. A point played at another note is
           transposed, which moves its whole spectrum — a double bass
           played two octaves up put its 2 kHz partials at 8 kHz at the
           same level, a bright hash no bass makes (Combust: "the wood
           naturally reflects lower tones and slowly absorbs higher
           ones, so you don't get that super high ring"). A body's
           radiation is a function of absolute frequency, not of the
           note, so each mode is re-weighted by the instrument's own
           measured envelope (Body(), version 7) at where it now sounds
           over where it was fitted. Nothing at all at the point
           itself. A world with no curve keeps its levels. */
        const bool bend = has_body && std::fabs(r - 1.f) > 1e-4f;
        float dbk[ResonatorBank::kMax];
        float wsum = 0.f, dmean = 0.f;
        for(int k = 0; k < nd; k++)
        {
            const float f0_ = ha[k];
            ha[k] *= r; ga[k] *= st[7];
            if(bend)
            {
                dbk[k] = Body(ha[k]) - Body(f0_);
                const float w = ga[k] * ga[k];
                wsum += w; dmean += w * dbk[k];
            }
        }
        /* the curve changes the balance, not the level: its
           energy-weighted mean is taken back out, so a note carried
           up the keyboard keeps its loudness and only its shape moves
           — the double bass's own envelope is 35 dB down by 1.4 kHz
           and applied whole it silenced the top two octaves of its
           range, which is true of a double bass and useless as an
           instrument */
        if(bend && wsum > 0.f)
        {
            dmean /= wsum;
            for(int k = 0; k < nd; k++) ga[k] *= fastmath::Exp2(0.16609640f * (dbk[k] - dmean));   /* 10^(x/20) */
        }
        return nd;
    }

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
        int nd = N;                   /* modes decoded */
        bool capped = false;          /* the voice's cap already applied, on the bytes */
        if(kind != 1)
        {
            /* between two points, both of them: each as it sounds at this
               note (Transposed), partial matched to partial, and the
               frequency, decay and level of each pair blended by where the
               note lies between them; a partial only one point has comes in
               by the same share. It was the nearest point alone, carried:
               a flip in timbre at every midpoint, 3 to 6 dB of spectral
               shape in one semitone where the notes either side of it moved
               0.3 to 0.9 (tools: keywalk) — Combust: "huge jumps in the
               output spectra ... as you play up the keyboard", "smoothed
               between known points to never get thinned out or weird".
               The blend of v5, which this is not, lerped slot k with slot k
               and summed antiphase pairs into +19 dB bumps: here the pairs
               are found by frequency, each keeps one phase (the nearer
               point's), and nothing is summed with its own opposite. At a
               point it is that point exactly. Linear throughout — the
               strike's arithmetic stays off libm */
            const float w = a == b ? 0.f : (near == a ? t : 1.f - t);    /* the far point's share: 0 at a point, 1/2 at the midpoint */
#ifndef KYK_RES_NEAREST
            if(w >= 1e-3f)
            {
                float hA[ResonatorBank::kMax], zA[ResonatorBank::kMax], gA[ResonatorBank::kMax], fA[ResonatorBank::kMax];
                float hB[ResonatorBank::kMax], zB[ResonatorBank::kMax], gB[ResonatorBank::kMax], fB[ResonatorBank::kMax];
                /* a capped voice (polyphony) blends each point's loudest
                   half again its share, not all of them: the share is all it
                   keeps, and decoding and pairing 48 a side cost a strike
                   five times what the nearest point alone did */
                const int sc = v.cap > 0 && v.cap < N ? (v.cap + v.cap / 2 < N ? v.cap + v.cap / 2 : N) : 0;
                const int nA = Transposed(a, param, sc, hA, zA, gA, fA, nullptr);
                const int nB = Transposed(b, param, sc, hB, zB, gB, fB, nullptr);
                const float wa = 1.f - t, wb = t;
                constexpr int M2 = 2 * ResonatorBank::kMax;
                float h[M2], z[M2], g[M2], f[M2];
                bool used[ResonatorBank::kMax];
                float lgB[ResonatorBank::kMax];
                for(int j = 0; j < nB; j++) { used[j] = !(gB[j] != 0.f); lgB[j] = used[j] ? 0.f : fastmath::Log2(std::fabs(gB[j])); }   /* a silent slot pairs with nothing */
                /* A's partials loudest first, each taking the partner that
                   is loudest for how close it is — a partial is several
                   close modes (a tine's beating pair, a piano's unison), and
                   taken in frequency order a mode at a hundredth of the level
                   took the other point's strongest and the blend lost 8 dB of
                   fundamental */
                int ord[ResonatorBank::kMax], no = 0;
                for(int i = 0; i < nA; i++) if(gA[i] != 0.f)
                {
                    int k = no++;
                    while(k > 0 && std::fabs(gA[ord[k - 1]]) < std::fabs(gA[i])) { ord[k] = ord[k - 1]; k--; }
                    ord[k] = i;
                }
                bool pairedA[ResonatorBank::kMax]; int partner[ResonatorBank::kMax];
                for(int i = 0; i < nA; i++) { pairedA[i] = false; partner[i] = -1; }
                /* B by frequency, so each of A's partials looks only at the
                   sixth of a tone around it: a binary search, not all of B */
                int byf[ResonatorBank::kMax];
                for(int j = 0; j < nB; j++)
                {
                    int k = j;
                    while(k > 0 && hB[byf[k - 1]] > hB[j]) { byf[k] = byf[k - 1]; k--; }
                    byf[k] = j;
                }
                for(int o = 0; o < no; o++)
                {
                    const int i = ord[o];
                    int best = -1; float bs = -1e30f;
                    const float flo = hA[i] * (1.f / 1.035f);
                    int l = 0, r = nB;
                    while(l < r) { const int mid = (l + r) >> 1; if(hB[byf[mid]] < flo) l = mid + 1; else r = mid; }
                    for(int q = l; q < nB; q++)
                    {
                        const int j = byf[q];
                        if(hB[j] > hA[i] * 1.035f) break;
                        if(used[j]) continue;
                        const float hi = hA[i] > hB[j] ? hA[i] : hB[j], lo = hA[i] > hB[j] ? hB[j] : hA[i];
                        if(hi > 1.035f * lo) continue;                    /* within a sixth of a tone, both at this note */
                        const float sc = lgB[j] - 40.f * (hi / lo - 1.f);   /* a per cent away costs 2.4 dB */
                        if(sc > bs) { bs = sc; best = j; }
                    }
                    if(best >= 0) { used[best] = true; pairedA[i] = true; partner[i] = best; }
                }
                int m = 0;
                for(int i = 0; i < nA; i++)
                {
                    if(!(gA[i] != 0.f)) continue;
                    const int best = partner[i];
                    if(pairedA[i])
                    {
                        used[best] = true;
                        /* the frequency linearly (the pair is within a
                           sixth of a tone); the decay and the level
                           geometrically, in the ratios they are heard in —
                           linearly, a neighbour ringing a tenth as long
                           nearly tripled the damping at a fifth of its share,
                           13.8 dB of shape one semitone off a point */
                        const float ma = std::fabs(gA[i]), mb = std::fabs(gB[best]);
                        const float mg = ma > 0.f && mb > 0.f ? ma * fastmath::Exp2(wb * (fastmath::Log2(mb) - fastmath::Log2(ma))) : wa * ma + wb * mb;
                        h[m] = wa * hA[i] + wb * hB[best];
                        z[m] = zA[i] > 0.f && zB[best] > 0.f ? zA[i] * fastmath::Exp2(wb * (fastmath::Log2(zB[best]) - fastmath::Log2(zA[i]))) : wa * zA[i] + wb * zB[best];
                        g[m] = near == a ? (gA[i] < 0.f ? -mg : mg) : (gB[best] < 0.f ? -mg : mg);
                        f[m] = near == a ? fA[i] : fB[best];
                    }
                    else { h[m] = hA[i]; z[m] = zA[i]; g[m] = wa * gA[i]; f[m] = fA[i]; }
                    m++;
                }
                for(int j = 0; j < nB; j++) if(!used[j]) { h[m] = hB[j]; z[m] = zB[j]; g[m] = wb * gB[j]; f[m] = fB[j]; m++; }
                /* at most the bank, or the voice's cap: the loudest by the
                   energy they carry (the export's own rank) */
                const int lim = v.cap > 0 && v.cap < ResonatorBank::kMax ? v.cap : ResonatorBank::kMax;
                if(m > lim)
                {
                    /* the lim-th largest score by quickselect, then every
                       mode over it: a pass or two over m, where picking the
                       loudest lim times over was lim passes */
                    float sc[M2], w2[M2];
                    for(int k = 0; k < m; k++) { const float d = z[k] * h[k]; sc[k] = w2[k] = g[k] * g[k] / (d > 1e-12f ? d : 1e-12f); }
                    int lo = 0, hi = m - 1; const int want = lim - 1;           /* descending: w2[want] is the cut */
                    while(lo < hi)
                    {
                        const float piv = w2[(lo + hi) >> 1];
                        int i2 = lo, j2 = hi;
                        while(i2 <= j2)
                        {
                            while(w2[i2] > piv) i2++;
                            while(w2[j2] < piv) j2--;
                            if(i2 <= j2) { const float tmp = w2[i2]; w2[i2] = w2[j2]; w2[j2] = tmp; i2++; j2--; }
                        }
                        if(want <= j2) hi = j2; else if(want >= i2) lo = i2; else break;
                    }
                    const float cut = w2[want];
                    int q = 0, ties = 0;
                    for(int k = 0; k < m; k++) if(sc[k] > cut) ties++;
                    ties = lim - ties;                                           /* how many at exactly the cut still fit */
                    for(int k = 0; k < m && q < lim; k++)
                        if(sc[k] > cut || (sc[k] == cut && ties-- > 0)) { h[q] = h[k]; z[q] = z[k]; g[q] = g[k]; f[q] = f[k]; q++; }
                    m = q;
                }
                for(int k = 0; k < m; k++) { ha[k] = h[k]; za[k] = z[k]; ga[k] = g[k]; fa[k] = f[k]; }
                nd = m; capped = true;
                const uint8_t* sa_ = Stage(a); const uint8_t* sb_ = Stage(b);
                float s0[8], s1[8]; std::memcpy(s0, sa_, 32); std::memcpy(s1, sb_, 32);
                for(int k = 0; k < 8; k++) st[k] = wa * s0[k] + wb * s1[k];
                st[7] = 1.f;                                                   /* each side's level is in its gains already */
            }
            else
#endif
            {
                nd = Transposed(near, param, v.cap > 0 && v.cap < N ? v.cap : 0, ha, za, ga, fa, st);
                capped = v.cap > 0 && v.cap < N;
            }
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
        for(int k = 0; k < nd; k++) za[k] /= decay;           /* T60 x decay */
        /* on a world with no pickup the voicing and coil axes had nothing
           to move — two of a guitar's four axes did nothing (Combust: "it
           doesn't seem to have much impact") — so there they are Rings'
           two: the voicing axis is the strike POSITION along the string, a
           comb over the harmonic number (2 sin(pi pos h), the middle of the
           string losing its even partials, the bridge end thin), blended
           in from the centre so the centre is the world as fitted; the coil
           axis is BRIGHTNESS, a tilt of the modes' gains by up to 3 dB an
           octave either way. Note worlds only: a row of bodies has no
           harmonic numbers to count */
        if(form == 0 && kind == 0 && nd > 0)
        {
            const float c0 = 0.5f + voicing / 4.f;                          /* the axis back from widths: 0..1 */
            const float amount = std::fabs(c0 - 0.5f) * 2.f;
            if(amount > 1e-3f)
            {
                const float pos = 0.05f + 0.45f * (c0 < 0.f ? 0.f : c0 > 1.f ? 1.f : c0);
                int lo = 0; for(int k = 1; k < nd; k++) if(std::fabs(ga[k]) > 0.f && (std::fabs(ga[lo]) == 0.f || ha[k] < ha[lo])) lo = k;
                const float f1 = ha[lo] > 0.f ? ha[lo] : 1.f;
                for(int k = 0; k < nd; k++)
                {
                    const float h = ha[k] / f1;
                    float w = 2.f * std::fabs(std::sin(3.1415927f * pos * h)); if(w > 1.f) w = 1.f;
                    ga[k] *= (1.f - amount) + amount * w;
                }
            }
            const float b = std::log2(coil > 1e-6f ? coil : 1e-6f);        /* -1..1 across the axis */
            if(std::fabs(b) > 1e-3f)
            {
                int lo = 0; for(int k = 1; k < nd; k++) if(std::fabs(ga[k]) > 0.f && (std::fabs(ga[lo]) == 0.f || ha[k] < ha[lo])) lo = k;
                const float f1 = ha[lo] > 0.f ? ha[lo] : 1.f;
                for(int k = 0; k < nd; k++) ga[k] *= std::pow(ha[k] / f1, 0.5f * b);
            }
        }
        /* a voice with a cap keeps its cap loudest modes — by the energy a
           mode carries, gain^2 over its decay rate, the export's own rank
           — in frequency order, and the rest are silent. Rings does this
           for its polyphony (64 / voices - 4 modes each): four voices
           stacked are as rich as one, and cost the same block. A note
           world's cap was applied on the bytes above (Loudest); a body
           row's, interpolated, is applied here on the decoded modes */
        int n = nd;
        if(!capped && v.cap > 0 && v.cap < N)
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
        for(int k = n; k < ResonatorBank::kMax; k++) { v.hz[k] = 0.f; v.zeta[k] = 0.f; v.gain[k] = 0.f; }
        /* a strike at another note on this voice: the old note choked —
           its ring over 2 ms (ResonatorBank::Choke), its attack, which is
           the recording of the old note, over 5 (BurstPlayer::Choke).
           Before the carry, not after: a strike bank inside its lead is
           let go by the choke, and then the carry has nothing to bring up
           to date. A glide or a tune change carries it on; a strike at the
           same note is a hammer on a ringing tine and adds. */
        /* now: the old ring released to the bank's tail at its own
           frequencies under a 40 ms damper (ResonatorBank::Release) and its
           attack faded over the same, and the new note built from silence,
           nothing carried: no pitch shifting, and no 2 ms step to pop */
        const bool released = keep && strike && v.param < 1e8f && std::fabs(param - v.param) > 1e-4f;
        if(released) { v.bank.Release(kTailMs, sr); v.burst.Choke(kTailMs, sr); }
        /* the pitch change this retune is, for the carry: a note world's
           notes, a body row's none; a first build has nothing to carry */
        const float ratio = (keep && kind != 1 && v.param < 1e8f) ? std::exp2((param - v.param) / 12.f) : 1.f;
        /* a ring about to be choked (a strike at another note) is carried
           no louder than it was — scaled up to the new point's level and
           pushed through the pickup for the 2 ms of its choke, the EP's C2
           ring into an F#2 strike peaked at 8.2 where the strike alone
           peaks at 0.4 — and a glide's carry at most twice as loud (the
           EP's bass points, whose shaped fits are the worst, railed the
           note sweep at four) */
        v.bank.Set(ha, za, ga, n, sr, fa, keep && !released, ratio, strike ? 1.f : 2.f, kind == 1);
        /* a strike at another note on this voice: the old note choked —
           its ring over 2 ms (ResonatorBank::Choke; the 5 ms it was gave
           the carried ring, which sits at the new note's frequencies, long
           enough to be heard as the old note bending up: Combust, "it's
           just pitch shifting"), its attack, which is the recording of the
           old note and played to its end, over 5 ms (BurstPlayer::Choke).
           A glide or a tune change carries it on; a strike at the same
           note is a hammer on a ringing tine and adds. */
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
            /* where this instrument's envelope has fallen 12 dB from its
               loudest band: the corner a transposed attack is filtered at */
            v.body_top = 0.f;
            if(has_body)
            {
                int pk = 0; for(int k = 1; k < 8; k++) if(body[k] > body[pk]) pk = k;
                for(int k = pk; k < 8; k++) if(body[k] <= body[pk] - 12.f) { v.body_top = 88.388f * std::exp2((float)k); break; }
            }
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
