/* kyk_engine.h — the oscillator: position → interpolate → bandlimit →
 * frame → phase-accumulator read, one block at a time.
 *
 *   c (control frame) ─► [R·c: M2] ─► Fold ─► LatticeWeights ─► Blend ─► mags, payload
 *   mags ─► bandlimit(K by f0) ─► RenderFrame ─► Osc back buffer ─► crossfade read
 *
 * Everything position-dependent runs once per block (or once per
 * render_div blocks); the per-sample work is two frame reads. Telemetry
 * accessors expose what the web surface draws (docs/hostlink.md).
 */
#pragma once
#include <atomic>
#include "kyk_world.h"

/* KYK_RESONATOR 0: a build without the resonator (the wavetable firmware,
   shell/alchemy MODE=wavetable). Its paths are compiled out, not only idle:
   every resonator entry point returns on a constant, and the stereo pair
   holds no voices. 1 (the default) everywhere else: the modal firmware, the
   desktop shell, the suite */
#ifndef KYK_RESONATOR
#define KYK_RESONATOR 1
#endif
#include "kyk_resonate.h"
#include "kyk_exciter.h"
#include "kyk_fft.h"
#include "kyk_osc.h"

namespace kyk {

/* A strike's voice, built before the strike, off the audio thread
   (Engine::PlanStrike, ServeStrikePlan). The module holds every strike at
   least 4 ms for its pitch to settle; the main loop is idle meanwhile, and
   the rebuild — At(), its libm and its divides, on the M7 the one thing a
   strike block has that a plain block does not — was done in the audio
   callback on top of four voices ringing (Combust: "again I think it's the
   strikes", overruns on the Piano at four voices). What the voice is built
   from: */
struct StrikeKey
{
    const World* world;
    uint32_t     gen;        /* Engine's world generation: a world rebuilt in place keeps its pointer */
    int          member;
    float        param;
    int          cap;
    float        tune[3];
    bool         on;
};
inline bool SameStrike(const StrikeKey& a, const StrikeKey& b)
{
    return a.on && b.on && a.world == b.world && a.gen == b.gen && a.member == b.member && a.param == b.param && a.cap == b.cap
        && a.tune[0] == b.tune[0] && a.tune[1] == b.tune[1] && a.tune[2] == b.tune[2];
}
/* and the room it is built in, lent by the shell (SDRAM on the module, so no
   default initialisers): the member world, tuned, and the voice built fresh
   from it. state: 0 empty, 1 being written, 2 ready for a strike keyed so */
struct StrikePlan
{
    ResonatorWorld    w;
    ResonatorVoice    v;
    StrikeKey         key;
    volatile uint32_t state;
};

/* a resonator's voices and what is kept per voice, lent to the engine that
   plays one (EngineCore::LendVoices). Engine owns four and lends them to
   itself; the stereo pair lends its left engine four and its right none —
   the right never plays a resonator (the left plays, the right copies), and
   its four were some 23 KB of the M7's AXI SRAM, where the code runs too:
   the room the exciters need (docs/exciters.md, stage 4). A template on the
   voice count gave the room back as code compiled twice (1796376) */
struct ResonatorVoices
{
    static constexpr int kN = 4;
    ResonatorVoice v[kN];
    float          note[kN];
    bool           dirty[kN];
    uint32_t       struck[kN];
    float          wl[kN][ResonatorBank::kMax], wr[kN][ResonatorBank::kMax];
    float          w_s[kN], w_c[kN], w_param[kN], w_hz0[kN];
    int            w_n[kN];
};

/* what strikes a resonator (EngineCore::SetExciterType; docs/exciters.md):
   the recorded attack played beside the modes, as the worlds were fitted, or
   an exciter coupled to the modes — its force computed every sample from where
   the string is under it, and put back into the modes. Hammer and Pluck leave
   the string when the contact ends; Bow, Reed and Lips are to come.
   Trained: the hammer each point was trained with (format 9, ModalBake
   excfit) — full synthesis; a world or a point without one plays its
   recorded attack. Bow, Reed and Lips keep going: they drive the newest
   voice every sample for as long as there is energy (the velocity axis, read
   every block — Play P4, J6, an orbit), and a strike only chooses the note */
enum class ResExciter : uint8_t { Recorded = 0, Hammer = 1, Pluck = 2, Trained = 3, Bow = 4, Reed = 5, Lips = 6 };
inline bool Sustained(ResExciter t) { return t == ResExciter::Bow || t == ResExciter::Reed || t == ResExciter::Lips; }

class EngineCore
{
    static constexpr bool kResonator = KYK_RESONATOR != 0;
public:
    /* Tunables the shell may set between blocks. */
    /* Unit-RMS cells with a worst measured crest factor of 4.2, so this keeps
     * even the peakiest cell inside full scale with the level knob wide open. */
    float gain         = 0.23f;
    int   render_div   = 1;      /* render a new frame every render_div blocks */
    /* Which block within that period this voice renders on. The stereo pair
     * sets the two voices to different phases so they never transform on the
     * same block: the average work is unchanged, the worst block halves, and
     * overruns are a property of the worst block. The two ears then hold
     * frames from adjacent blocks, which is well under a millisecond apart
     * and inaudible. */
    int   render_phase = 0;
    int   rolloff_bins = 0;
    float sharp        = 0.f;   /* see SharpenWeights */      /* raised-cosine taper over the top bins below the cutoff */

    /* the voices this engine plays a resonator with (ResonatorVoices), or
       none: without them every resonator entry point does nothing */
    void LendVoices(ResonatorVoices* rv, bool fresh = true)
    {
        if(!rv) { rcap_ = 0; rvoices_ = nullptr; rvnote_ = nullptr; rvdirty_ = nullptr; rvstruck_ = nullptr; rwl_ = rwr_ = nullptr; rw_s_ = rw_c_ = rw_param_ = rw_hz0_ = nullptr; rw_n_ = nullptr; return; }
        rcap_ = ResonatorVoices::kN;
        rvoices_ = rv->v; rvnote_ = rv->note; rvdirty_ = rv->dirty; rvstruck_ = rv->struck;
        rwl_ = rv->wl; rwr_ = rv->wr; rw_s_ = rv->w_s; rw_c_ = rv->w_c; rw_param_ = rv->w_param; rw_hz0_ = rv->w_hz0; rw_n_ = rv->w_n;
        if(fresh)
            for(int v = 0; v < rcap_; v++)
            {
                rvoices_[v].Init(); rvoices_[v].release_ms = rrelease_ms_;
                rvnote_[v] = 1e9f; rvdirty_[v] = false; rvstruck_[v] = 0u;
                rw_s_[v] = -1.f; rw_c_[v] = rw_param_[v] = rw_hz0_[v] = 0.f; rw_n_[v] = 0;
            }
    }
    int Voices() const { return rcap_; }

    void Init(const World* world, float sr)
    {
        world_ = world;
        sr_    = sr;
        osc_.Init();
        block_ = 0;
        renders_ = 0;
        f0_    = 110.f;
        kcut_  = 0;
        kcut_want_ = 1 << 20;
        hold_  = 0;
        for(int a = 0; a < kMaxN; a++) { c_[a] = 0.5f; p_[a] = 0.5f; rendered_[a] = 1e9f; }
        for(int k = 0; k < kMaxK; k++) { mags_[k] = 0.f; mags_bl_[k] = 0.f; }
        for(int j = 0; j < kMaxP; j++) payload_[j] = 0.f;
        DerivePhases();
        phase_dirty_ = false;
        dirty_ = true;
        for(int v = 0; v < rcap_; v++) { rvoices_[v].Init(); rvoices_[v].release_ms = rrelease_ms_; }
        rgen_++; if(rplan_) rplan_->state = 0u;
        rcv_ = -1; rsv_ = -1;          /* a contact or a bow under way belonged to the voices just rebuilt */
        rtuned_.Init(); rtuned_for_ = nullptr; rtuned_member_ = -1; rmw_for_[0] = rmw_for_[1] = nullptr; rmw_m_[0] = rmw_m_[1] = -1; rmorph_m_ = -1;   /* the member is state derived from the world: rebuilt here and only here */
        ractive_ = 0; rpoly_ = 1; rmember_ = 0; rdriven_ = 0; rhold_ = false;
        for(int v = 0; v < rcap_; v++) { rvnote_[v] = 1e9f; rvdirty_[v] = false; rvstruck_[v] = 0u; }
        rframe_ = false;
        if(kResonator && world && world->IsResonate() && rcap_) { rmember_ = ResMemberOf(c_[0]); rvnote_[0] = ResParam(); Tuned().At(rvnote_[0], rvoices_[0], sr_); }
    }

    /* ── the resonate path ───────────────────────────────────────────────
     * A resonate world is a ResonatorVoice after the oscillator, whose
     * frame is silent for that kind. The voice is built from the world at
     * the current pitch in SetWorld; a strike where the pitch has moved
     * retunes the bank with its state ringing on, as the oscillator follows
     * the pitch, and every strike adds to what rings, as a hammer does. Who strikes is the shell's business (a host action, a
     * gate, a pot for the velocity — docs/modal-mode.md). */
    /* at_plan: strike at the note the staged voice was built for, when one
       is ready for everything else as it stands. For a strike whose pitch
       never settled — the shell waited its 30 ms and the CV is still moving,
       audio-rate v/oct — the note a millisecond ago is as much the note as
       the one now, and it is the one already built. A strike whose pitch
       settled takes the pitch as it is (the plan may be a step behind) */
    void Strike(float velocity01, bool at_plan = false)
    {
        if(!kResonator || !world_ || !world_->IsResonate() || !rcap_) return;
        rforce_ = 1e9f;
        if(at_plan && rplan_ && rplan_->state == 2u)
        {
            const StrikeKey& k = rplan_->key;
            bool same = k.on && k.world == world_ && k.gen == rgen_ && k.member == ResMemberOf(c_[0])
                     && k.cap == (rpoly_ > 1 ? ResonatorBank::kMax / rpoly_ : 0);
            for(int i = 0; i < 3; i++) same = same && k.tune[i] == rtune_[i];
            if(same) rforce_ = k.param;
        }
        if(rpoly_ > 1)
        {
            /* which voice: the one already ringing at this note, if any —
               a key struck again is the same string struck again, and the
               hammer adds to what rings (Combust: "playing the same note
               repeatedly shouldn't 'steal' another voice but restart the
               last one right?"). Round-robin gave a note repeated four
               times all four voices and damped three other notes to do it.
               Otherwise a voice that is silent, and failing that the one
               struck longest ago; only the newest follows the pitch */
            rstriking_ = true;
            const float p = ResParam();
            rstriking_ = false;
            const ResonatorWorld& r = world_->Res();
            const float same = r.kind == 1 ? 0.005f * (r.hi - r.lo) : 0.5f;
            int pick = -1;
            for(int v = 0; v < rpoly_ && pick < 0; v++)
                if(rvnote_[v] != 1e9f && std::fabs(rvnote_[v] - p) < same && rvoices_[v].Active()) pick = v;
            for(int k = 1; k <= rpoly_ && pick < 0; k++)
            {
                const int v = (ractive_ + k) % rpoly_;
                if(!rvoices_[v].Active()) pick = v;
            }
            if(pick < 0)
            {
                pick = 0;
                for(int v = 1; v < rpoly_; v++) if(rvstruck_[v] < rvstruck_[pick]) pick = v;
            }
            ractive_ = pick;
        }
        rvstruck_[ractive_] = ++rstrikes_;
        rstriking_ = true;
        Retune();          /* the pitch is taken here, locked or not */
        rstriking_ = false; rsince_ = 0; rlate_ = false; rlate_n_ = 0;
        /* the strike a little harder the faster the playing, when asked —
           Combust: "the strike velocity gets slightly harder as the strike
           frequency increases... I meant how fast you're playing". The
           density of strikes is a leaky count with a one-second time
           constant, so it reads as strikes a second; at six a second and
           above the strike is vtrack_ x 0.5 harder, nothing at rest. It was
           0.2 at eight: at four notes a second that was 0.1 of velocity at
           full, 2 dB on a layered world, and "doesn't seem to be doing
           anything" (Combust). Off by default */
        float v = velocity01;
        if(vtrack_ != 0.f) { const float d = rdens_ > 6.f ? 1.f : rdens_ / 6.f; v += vtrack_ * 0.5f * d; v = v > 1.f ? 1.f : v; }
        rdens_ += 1.f;
        rlast_v_ = v;
        if(Sustained(rexc_)) rsv_ = -1;            /* no attack: the bow, the reed or the lips take the new voice next block */
        else if(rexc_ == ResExciter::Recorded || (rexc_ == ResExciter::Trained && !rvoices_[ractive_].exc_type)) rvoices_[ractive_].Strike(v);
        else StrikeCoupled(rvoices_[ractive_], v);
        rforce_ = 1e9f;
    }
    /* 1, 2 or 4 voices. Changing it cuts nothing: a voice past the new
       count rings out and is then skipped, and the round goes on from the
       last voice within the count. On the module this is a pot, and
       silencing four voices at the turn of it was a click */
    void SetPolyphony(int n)
    {
        if(!rcap_) return;
        n = n < 1 ? 1 : n > rcap_ ? rcap_ : n;
        if(n == rpoly_) return;
        rpoly_ = n;
        if(ractive_ >= n) ractive_ = n - 1;
        /* Rings' rule: the bank's modes shared out among the voices, so
           four voices stacked are as rich as one and cost the same —
           Combust: "white room talk". Each voice keeps its loudest 48 / n
           modes, rebuilt one a block */
        for(int v = 0; v < rcap_; v++) { rvoices_[v].cap = n > 1 ? ResonatorBank::kMax / n : 0; rvdirty_[v] = true; }
    }
    int Polyphony() const { return rpoly_; }
    /* Rings' external exciter: an audio block driven into the voice that
       follows the pitch, g x a sample into every mode — the world as a
       resonant filter bank for whatever is patched in. Set per block; the
       pointer is read by the Process that follows and then dropped. */
    void SetExciter(const float* x, float gain) { exciter_ = x; exgain_ = gain; }
    /* a resonator heard from two points along the string: the next Process
       also writes outR, the left ear at centre - 0.12 x spread and the right
       at centre + 0.12 x spread (fractions of the string, 0.02 .. 0.5), each
       mode weighted as a point there hears it — 2|sin(pi p h)| at harmonic h,
       at most 1, blended in by the spread so that no spread is every mode at
       1 and both ears the mono output exactly. The same comb the position
       axis applies to the strike, here to the listening. A pointer for one
       block, as the exciter's is. Note worlds only; a pickup world is mono */
    void SetListen(float* outR, float spread01, float centre) { rout_ = outR; rlisten_s_ = spread01 < 0.f ? 0.f : spread01 > 1.f ? 1.f : spread01; rlisten_c_ = centre; }
    /* a strike is waiting (the module holds one for the CV to settle): the
       pitch is not taken by the voice still ringing meanwhile */
    void HoldPitch(bool on) { rhold_ = on; }
    /* how long a stolen voice's last note takes to fall 60 dB (ms, 5..1000;
       ResonatorDefaults::kReleaseMs by default) */
    /* how much of the audio callback's budget the last block took (0..1+),
       from the shell. Over 0.85 every voice's tail — a stolen note's ring,
       fading anyway — is brought to its end within 2 ms (ResonatorBank::
       Hurry): a load nothing can plan for (Combust: "sometimes people smash
       notes together. roll their fingers across the keys. Call in v/oct with
       audio rate") gives up the one thing that can go without a sound */
    void SetLoad(float frac)
    {
        if(!(frac > 0.85f)) return;
        for(int v = 0; v < rcap_; v++) rvoices_[v].bank.Hurry(2.f, sr_);
        rhurried_++;
    }
    uint32_t Hurried() const { return rhurried_; }   /* blocks the load hurried the tails */
    void SetReleaseMs(float ms)
    {
        rrelease_ms_ = ms < 5.f ? 5.f : ms > 1000.f ? 1000.f : ms;
        for(int v = 0; v < rcap_; v++) rvoices_[v].release_ms = rrelease_ms_;
    }
    float ReleaseMs() const { return rrelease_ms_; }
    static float NoteOf(float hz) { return 69.f + 12.f * std::log2(hz > 1.f ? hz / 440.f : 1.f / 440.f); }
    /* Where on its axis the world is played: a note world at the pitch, an
     * index world — a row of bodies, gong to woodblock — where position 0
     * puts it, lo to hi. The second is the world's one position axis doing
     * what a position does everywhere else here: choosing the timbre. */
    float ResParam() const
    {
        if(rstriking_ && rforce_ < 1e8f) return rforce_;       /* a strike at the staged note (Strike, at_plan) */
        const ResonatorWorld& r = world_->Res();
        if(r.kind == 1) return r.lo + (r.hi - r.lo) * (c_[0] < 0.f ? 0.f : c_[0] > 1.f ? 1.f : c_[0]);
        const float note = NoteOf(f0_);
        if(!pitch_lock_) return note;
        /* locked: the nearest semitone, with a tenth of a semitone of
           hysteresis between strikes so a CV on a boundary does not
           chatter; a strike takes the nearest outright, since a strike is
           a moment and the CV is what it is then */
        const float held = rcap_ ? rvnote_[ractive_] : 1e9f;
        if(!rstriking_ && held != 1e9f && std::fabs(note - held) <= 0.6f) return held;
        return std::floor(note + 0.5f);
    }
    /* The pitch lock, on by default. A struck string does not bend with
       the pitch pot: locked, the ring keeps the note it was struck at and
       the next strike takes the pitch then, to the semitone. On a modular
       the pitch CV steps and jitters, and a ring that followed it slid
       into every note behind the strike — a piano tail bending up to the
       next note (Combust: "drunk, sliding into position at the last
       second"). A bank driven by the exciter with no strikes follows the
       pitch by the semitone, which is a quantiser. Unlocked, the ring
       follows the pitch by the cent: a bend, for whoever wants one. */
    void SetPitchLock(bool on) { if(pitch_lock_ != on) { pitch_lock_ = on; if(rcap_) rvnote_[ractive_] = 1e9f; } }   /* re-read: locked, the nearest semitone; free, the cent */
    void SetVelocityTrack(float amount) { vtrack_ = amount; }   /* 0..1: how much harder the strike gets with fast playing (0.5 of velocity at full, at six strikes a second) */
    float VelocityTrack() const { return vtrack_; }
    float LastStrikeVelocity() const { return rlast_v_; }
    bool PitchLock() const { return pitch_lock_; }
    /* the voice follows its parameter with its state ringing on; an index
       world follows the pot every block, a note world its pitch */
    /* where the body axis lies in a family, as two members and the share of
       the second: m0 and m0 + 1, t in 0..1 */
    /* and only between two members that can be blended — the same kind and
       the same pickup form; any other pair switches as it always did, with
       its hysteresis */
    bool MorphAt(int& m0, float& t)
    {
        const ResonatorWorld& r = world_->Res();
        if(!rmorph_ || !rscr_ || !rmw_ || r.kind != 2 || r.M < 2) return false;
        const float x = (c_[0] < 0.f ? 0.f : c_[0] > 1.f ? 1.f : c_[0]) * (float)(r.M - 1);
        m0 = (int)x; if(m0 > r.M - 2) m0 = r.M - 2;
        t = x - (float)m0;
        const ResonatorWorld& A = MemberWorld(0, m0);
        const ResonatorWorld& B = MemberWorld(1, m0 + 1);
        return A.kind == 0 && B.kind == 0 && A.form == B.form;
    }
    /* a member attached, two cached (the pair a morph sits between) */
    const ResonatorWorld& MemberWorld(int slot, int m)
    {
        if(rmw_for_[slot] != world_ || rmw_m_[slot] != m) { world_->Res().Member(m, rmw_[slot]); rmw_for_[slot] = world_; rmw_m_[slot] = m; }
        ResonatorWorld& w = rmw_[slot];
        w.voicing = rtune_[0]; w.decay = rtune_[1]; w.coil = rtune_[2];
        return w;
    }
    /* a voice built at param: At() on the member playing, or, morphing, the
       two members either side blended and built on the nearer */
    void BuildVoice(float param, ResonatorVoice& v, bool keep, bool strike)
    {
        int m0; float t;
        if(MorphAt(m0, t) && t > 0.02f && t < 0.98f)
        {
            const ResonatorWorld& A = MemberWorld(0, m0);
            const ResonatorWorld& B = MemberWorld(1, m0 + 1);
            {
                ResonatorVoice& sa = rscr_[0]; ResonatorVoice& sb = rscr_[1];
                sa.Init(); sb.Init(); sa.cap = sb.cap = v.cap;
                A.At(param, sa, sr_); B.At(param, sb, sr_);
                auto count = [](const ResonatorVoice& x) { int k = 0; while(k < ResonatorBank::kMax && !(x.gain[k] == 0.f && x.hz[k] == 0.f)) k++; return k; };
                float h[ResonatorBank::kMax], z[ResonatorBank::kMax], g[ResonatorBank::kMax], f[ResonatorBank::kMax], st[8];
                const int lim = v.cap > 0 && v.cap < ResonatorBank::kMax ? v.cap : ResonatorBank::kMax;
                const int m = ResonatorWorld::PairBlend(sa.hz, sa.zeta, sa.gain, sa.phase, count(sa), sb.hz, sb.zeta, sb.gain, sb.phase, count(sb),
                                                        t, t < 0.5f, lim, h, z, g, f);
                for(int k = 0; k < 8; k++) st[k] = (1.f - t) * sa.stage[k] + t * sb.stage[k];
                const ResonatorWorld& W = t < 0.5f ? A : B;
                int a, b; float tt; const int near = W.NearPoint(param, a, b, tt);
                v.exc_wset = false;       /* a blend of two members: no point's weights line up with it */
                W.Build(param, v, sr_, keep, strike, h, z, g, f, m, st, near, a, b, tt);
                return;
            }
        }
        if(strike && keep && TakePlan(param, v)) return;
        Tuned().At(param, v, sr_, keep, strike);
    }
    /* the strike takes the staged voice when it is the one it would build:
       the same world, member, note, cap and tune, and a note other than the
       one ringing (a release — the case the staged voice is built for). The
       pickup is set here, from the old voice's, as Build sets it */
    bool TakePlan(float param, ResonatorVoice& v)
    {
        if(!rplan_ || rplan_->state != 2u) return false;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        StrikeKey now;
        now.world = world_; now.gen = rgen_; now.member = rmember_; now.param = param; now.cap = v.cap;
        for(int i = 0; i < 3; i++) now.tune[i] = rtune_[i];
        now.on = true;
        if(!SameStrike(rplan_->key, now)) return false;
        if(!(v.param < 1e8f && std::fabs(param - v.param) > 1e-4f)) return false;
        v.TakeStaged(rplan_->v, sr_);
        const ResonatorWorld& w = rplan_->w;
        float st[8]; for(int i = 0; i < 8; i++) st[i] = v.stage[i];
        st[0] += w.voicing * st[1]; st[3] *= w.coil;          /* as Build spins the stage */
        w.SetPickup(v, st, sr_, true);
        rplan_->state = 0u; rplan_taken_++;
        return true;
    }
    /* each mode of a voice as the two listening points hear it (SetListen) */
    void ListenWeights(const ResonatorVoice& v, float* wl, float* wr) const
    {
        const int n = v.bank.n;
        const float s = rlisten_s_;
        float pl = rlisten_c_ - 0.12f * s, pr = rlisten_c_ + 0.12f * s;
        pl = pl < 0.02f ? 0.02f : pl > 0.5f ? 0.5f : pl;
        pr = pr < 0.02f ? 0.02f : pr > 0.5f ? 0.5f : pr;
        const float f0 = v.param < 1e8f ? 440.f * fastmath::Exp2((v.param - 69.f) / 12.f) : 0.f;
        for(int k = 0; k < n; k++)
        {
            if(!(f0 > 0.f) || !(v.hz[k] > 0.f) || s <= 0.f) { wl[k] = wr[k] = 1.f; continue; }
            const float h = v.hz[k] / f0;
            float x = pl * h; x -= std::floor(x);
            float sn, cs; fastmath::SinCos(3.1415927f * x, sn, cs);
            float w = 2.f * (sn < 0.f ? -sn : sn); w = w > 1.f ? 1.f : w;
            wl[k] = (1.f - s) + s * w;
            x = pr * h; x -= std::floor(x);
            fastmath::SinCos(3.1415927f * x, sn, cs);
            w = 2.f * (sn < 0.f ? -sn : sn); w = w > 1.f ? 1.f : w;
            wr[k] = (1.f - s) + s * w;
        }
    }
    void Retune()
    {
        if(!kResonator || !rcap_) return;
        /* At() is 48 modes of exp2, exp, pow, cos and sin — some 60 us on
           the M7, an eighth of a 24-sample block — so it runs on a move the
           ear can hear, two cents or a two-hundredth of a body row, and not
           on a pot's jitter, which used to run it every block until the
           module overran. A sweep pays it per block, as the wavetable pays
           its render */
        const float p = ResParam();
        const ResonatorWorld& r = world_->Res();
        const float eps = r.kind == 1 ? 0.005f * (r.hi - r.lo) : (pitch_lock_ ? 0.02f : 0.03f);
        const int m = ResMemberOf(c_[0]);
        float& note = rvnote_[ractive_];
        int mm0; float mt;
        if(MorphAt(mm0, mt))
        {
            /* morphing: every voice rebuilt when the axis has moved a
               hundredth of the way between two members, one a block, rings
               carried; the member playing is the nearer */
            if(mm0 != rmorph_m_ || std::fabs(mt - rmorph_t_) > 0.01f) { rmorph_m_ = mm0; rmorph_t_ = mt; for(int v = 0; v < rcap_; v++) rvdirty_[v] = true; }
            rmember_ = mt < 0.5f ? mm0 : mm0 + 1;
        }
        else if(m != rmember_) { rmember_ = m; for(int v = 0; v < rcap_; v++) rvdirty_[v] = true; }   /* another instrument: every voice rebuilt, its ring carried */
        /* does the active voice take the pitch? Not within the deadband;
           and locked, not at all unless this is the strike, the bank is
           being driven (the pitch is then the only thing playing it), or
           the pitch jumps a semitone or more within 30 ms of the strike —
           a sequencer whose CV lands after its gate: the note belongs to
           the strike it followed, and a strike that held the old note
           would be the wrong note for as long as it rang */
        bool move, late = false;
        if(note == 1e9f) move = true;
        else if(std::fabs(p - note) <= eps) move = false;
        else
        {
            /* once per strike: a late CV is one step after the gate. At
               audio-rate v/oct the pitch jumps every block, and this rebuilt
               the voice every block for 30 ms after each strike — 1 940
               rebuilds a second at a strike every 4 ms, eight a strike, where
               the strikes alone are 230 (desktop stress; Combust: "Call in
               v/oct with audio rate", overruns) */
            /* and not while the next strike is waiting (HoldPitch): the pitch
               then is that strike's. It was taken as the last one's late CV
               — a roll with the notes under 30 ms apart dragged the note
               still ringing to the next note, and the next strike found it
               there and struck the same voice again */
            /* and not until it has stood for four blocks (2 ms) with no
               strike arriving: a keyboard's CV leads its gate by a moment,
               and in that moment the next note's pitch looked like the last
               one's late CV just the same */
            const bool maybe = !rlate_ && !rhold_ && rsince_ < 0.03f * sr_ && std::fabs(p - note) > 0.4f;
            if(!rstriking_) rlate_n_ = maybe ? rlate_n_ + 1 : 0;
            late = maybe && rlate_n_ >= 4;
            /* driven: something is actually coming in (Process), and no
               strike is waiting — the shell holds a strike for the CV to
               settle, and in those milliseconds the pitch belongs to the
               strike coming, not to the note still ringing */
            const bool driven = rdriven_ > 0 && !rhold_;
            move = !(pitch_lock_ && r.kind != 1 && !rstriking_ && !late && !driven);
        }
        if(move || rvdirty_[ractive_])
        {
            /* free, and only the pitch has moved: a bend — the modes this
               voice was struck with, transposed, and the burst read at the
               new rate. A rebuild is At(), which on the module is about two
               blocks of budget (a hundred transcendentals), and free ran one
               every time the CV moved two cents: it overran, and at every
               midpoint it stepped into the next point's timbre. A bend is a
               twentieth of that and keeps the note it was struck with,
               which is what a bend is. Three cents, and at most one every
               four blocks — five hundred a second, smoother than a pot. */
            if(move && !rstriking_ && !rvdirty_[ractive_] && note != 1e9f && !pitch_lock_ && r.kind != 1 && rbend_ >= 4)
            {
                Tuned().Bend(p, rvoices_[ractive_], sr_);
                note = p; rbend_ = 0;
                return;
            }
            if(!move && !rvdirty_[ractive_]) return;
            /* a strike's build is the one this block has; anything else
               waits a block, the pitch or tune one block late */
            if(rdid_ && !rstriking_) return;
            /* and a rebuild that is not a strike — a CV on the body, decay
               or coil, a driven bank following the pitch, an index world's
               pot — at most one every four blocks (2 ms): at audio rate each
               of these was a rebuild every block on top of the strikes. The
               late CV is the exception, once per strike, and immediate */
            if(!rstriking_ && !late && rsettle_ < 4) return;
            if(!rstriking_) rsettle_ = 0;
            if(late && !rstriking_) rlate_ = true;
            /* a tune change under the lock rebuilds the voice at the note
               it holds, not at wherever the pitch has gone since */
            BuildVoice(move ? p : note, rvoices_[ractive_], true, rstriking_); at_count_++; rdid_ = true;
            if(move) note = p;
            rvdirty_[ractive_] = false;
            return;
        }
        /* the other voices follow a tune or a member change one per
           block, round-robin, so a decay or coil that an orbit keeps
           moving costs one At() a block whatever the voice count, and a
           voice lags by a few blocks, which nobody can hear decay do */
        /* and never two in one block: a strike already costs a build, and
           a pot crossing its deadband in the same block cost a second —
           the module reported an overrun about once a note (Combust). The
           other voices wait a block; nobody hears decay do that. */
        if(rdid_ || rsettle_ < 4) return;
        for(int k = 1; k < rcap_; k++)
        {
            const int v = (ractive_ + k) % rcap_;
            if(!rvdirty_[v]) continue;
            if(rvnote_[v] != 1e9f && rvoices_[v].Active()) { BuildVoice(rvnote_[v], rvoices_[v], true, false); at_count_++; rdid_ = true; rsettle_ = 0; rvdirty_[v] = false; return; }
            rvdirty_[v] = false;              /* silent, or never built: its next strike builds it */
        }
    }
    /* The spin on a resonator: voicing (the pickup's pole off its fitted
     * centre, in widths), decay (every mode's T60, x) and coil (the coil's
     * resonance, x). A knob's business, so it lives on the engine and not
     * on the world, which is shared and const; applied through a copy of
     * the world's reader, which is a pointer and a dozen floats. Takes
     * effect on the next block, the state ringing on. */
    enum class Tune : uint8_t { Voicing = 0, Decay = 1, Coil = 2 };
    void SetTune(Tune which, float v)
    {
        float& t = rtune_[(int)which];
        /* a deadband, since on the module this is a pot read every block:
           two hundredths of a width, one per cent of a ratio — under the
           ear, over the jitter, and At() is 60 us it must not spend on
           jitter */
        const float eps = which == Tune::Voicing ? 0.02f : 0.01f * (t > 1.f ? t : 1.f);
        if(std::fabs(t - v) <= eps) return;
        t = v;
        for(int i = 0; i < rcap_; i++) rvdirty_[i] = true;   /* every voice rebuilt at the note it holds, one a block */
    }
    float GetTune(Tune which) const { return rtune_[(int)which]; }
    /* with no voices lent, a silent one: the readouts ask whatever engine they are given */
    static const ResonatorVoice& NoVoice() { static const ResonatorVoice z{}; return z; }
    const ResonatorVoice& Voice() const { return rcap_ ? rvoices_[ractive_] : NoVoice(); }
    const ResonatorVoice& VoiceAt(int v) const { return rcap_ ? rvoices_[v < 0 ? 0 : v >= rcap_ ? rcap_ - 1 : v] : NoVoice(); }
    float ResParamNow() const { return rcap_ ? rvnote_[ractive_] : 1e9f; }
    uint32_t AtCount() const { return at_count_; }   /* voices built since Init: the bench's measure of how often At() runs */
    float ControlAt(int a) const { return a >= 0 && a < kMaxN ? c_[a] : 0.f; }
    /* On a modular the spin is jacks and pots, not a page: with this set
       (the module sets it; the desktop does not, so the page's sliders
       work there) the control frame is read every block as the spin —
       axis 0 the voicing on a note world (the body on an index world,
       which At() reads itself), 2 the decay, 3 the coil, each 0.5 the
       world as fitted — and axis 1 is the velocity a trigger strikes with.
       The frame is pot plus CV, the same numbers a wavetable world reads
       as positions, so the panel needs no second map to remember. */
    void TuneFromControl(bool on) { tune_from_control_ = on; }
    /* the decay axis: four times at the top, the world as fitted at the
       centre, and at the bottom every T60 a 256th of itself — a 10 s
       piano string to 40 ms, a thonk on muted strings (Combust: "at low
       end it should basically be a thonk on fully muted strings"). The
       half below centre is steeper than the half above because muting is
       what the bottom is for */
    static float DecayOf(float c) { return c < 0.5f ? std::exp2((c - 0.5f) * 16.f) : std::exp2((c - 0.5f) * 4.f); }
    static float CoilOf(float c)  { return std::exp2((c - 0.5f) * 2.f); }     /* half to double */
    static float VoicingOf(float c) { return (c - 0.5f) * 4.f; }              /* +-2 widths */
    /* the member playing, attached once per member change rather than
       per At() (attaching tables the points, a read per point out of
       SDRAM), with the spin as it stands now */
    const ResonatorWorld& Tuned()
    {
        if(rtuned_for_ != world_ || rtuned_member_ != rmember_)
        {
            world_->Res().Member(rmember_, rtuned_);     /* a plain world is its own only member */
            rtuned_for_ = world_; rtuned_member_ = rmember_;
        }
        rtuned_.voicing = rtune_[0]; rtuned_.decay = rtune_[1]; rtuned_.coil = rtune_[2];
        return rtuned_;
    }
    /* which instrument of a family position 0 chooses: the row quantised,
       with a tenth of a step of hysteresis so a pot on a boundary does not
       chatter between two instruments */
    int ResMemberOf(float c) const
    {
        const ResonatorWorld& r = world_->Res();
        if(r.kind != 2 || r.M < 2) return 0;
        const float x = (c < 0.f ? 0.f : c > 1.f ? 1.f : c) * (float)(r.M - 1);
        const int cur = rmember_;
        if(x > (float)cur + 0.6f) return cur + 1 < r.M ? (int)(x + 0.4f) : cur;
        if(x < (float)cur - 0.6f) return (int)(x + 0.6f);
        return cur;
    }
    int  ResMember() const { return rmember_; }
    /* the family's body axis as a morph: between two members the voice is
       both, partial paired with partial (ResonatorWorld::PairBlend) and
       blended by where the axis lies, built on the nearer member — its
       attack and wash. It was a switch at the midpoint, with hysteresis.
       Members of different kinds (a pickup EP beside a plain Wurlitzer)
       still switch: a pickup is not a thing half of a note has. Off by
       default until it has been heard; -DKYK_MEMBER_MORPH=1 builds it on */
    void SetMemberMorph(bool on) { if(rmorph_ != on) { rmorph_ = on; rmorph_m_ = -1; for(int v = 0; v < rcap_; v++) rvdirty_[v] = true; } }
    bool MemberMorph() const { return rmorph_; }
    /* two members and two voices of room for the morph (trivially
       constructible, so SDRAM will do); null takes it away */
    /* the exciter: its type, and its shape — timbre (the felt's hardness,
       the plectrum's stiffness), position (along the string: 0.5 as fitted,
       away from it a comb over the harmonics), noise (the contact's own, 0..1)
       and mass (the hammer's, and where the finger lets go). A struck exciter
       takes over the voice it strikes until its contact ends; the recorded
       attack is the default, and what the worlds were fitted with */
    void SetExciterType(ResExciter t) { rexc_ = t; }
    ResExciter ExciterType() const { return rexc_; }
    void SetExciterShape(float timbre01, float position01, float noise01, float mass01)
    {
        auto c = [](float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; };
        rexc_timbre_ = c(timbre01); rexc_pos_ = c(position01); rexc_noise_ = c(noise01); rexc_mass_ = c(mass01);
    }
    int CoupledVoice() const { return rcv_; }   /* the voice a contact is driving now, -1 none */
    /* the staged strike's room (StrikePlan); none lent, every strike builds inline */
    void SetStrikePlan(StrikePlan* p) { rplan_ = p; if(p) p->state = 0u; }
    uint32_t PlansTaken() const { return rplan_taken_; }   /* strikes that took a staged voice */
    /* the audio thread, each block a strike is waiting: say which voice it
       will build — the note it will take, the member, the cap and the tune
       as they stand. Cheap; the building is ServeStrikePlan's */
    void PlanStrike()
    {
        if(!kResonator || !rplan_ || !rcap_ || !world_ || !world_->IsResonate()) return;
        StrikeKey k;
        rstriking_ = true; k.param = ResParam(); rstriking_ = false;
        k.world = world_; k.gen = rgen_; k.member = ResMemberOf(c_[0]);
        k.cap = rpoly_ > 1 ? ResonatorBank::kMax / rpoly_ : 0;
        for(int i = 0; i < 3; i++) k.tune[i] = rtune_[i];
        k.on = !rmorph_;                      /* a family morph builds from two members: inline */
        if(SameStrike(k, rq_) || (!k.on && !rq_.on)) return;
        rq_seq_ = rq_seq_ + 1u;               /* odd: being written */
        std::atomic_signal_fence(std::memory_order_seq_cst);
        rq_ = k;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        rq_seq_ = rq_seq_ + 1u;
    }
    /* the main loop: build the voice the waiting strike asked for, fresh,
       into the plan, unless it is built already. The audio callback
       interrupts this whenever it likes; it reads the plan only once state
       says ready, and the request is read under its sequence number. True
       if a voice was built */
    bool ServeStrikePlan()
    {
        if(!kResonator || !rplan_) return false;
        StrikeKey k; k.on = false;
        for(int tries = 0; tries < 4; tries++)
        {
            const uint32_t s0 = rq_seq_;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            k = rq_;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            if(!(s0 & 1u) && rq_seq_ == s0) break;
            k.on = false;
        }
        /* a request from before the world changed: its world may be gone
           (a slot reloaded, its region reused), and the strike would refuse
           the voice anyway. SetWorld runs on this same loop, so rgen_ here
           is current */
        if(!k.on || k.gen != rgen_ || k.world != world_ || !k.world || !k.world->IsResonate()) return false;
        if(rplan_->state == 2u && SameStrike(rplan_->key, k)) return false;
        rplan_->state = 1u;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        StrikePlan& pl = *rplan_;
        if(!k.world->Res().Member(k.member, pl.w)) { pl.state = 0u; return false; }
        pl.w.voicing = k.tune[0]; pl.w.decay = k.tune[1]; pl.w.coil = k.tune[2];
        pl.v.Init(); pl.v.cap = k.cap;
        pl.w.At(k.param, pl.v, sr_, false, false);
        pl.key = k;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        pl.state = 2u;
        return true;
    }
    void SetMorphScratch(ResonatorVoice* two_voices, ResonatorWorld* two_worlds) { rscr_ = two_voices; rmw_ = two_worlds; rmw_for_[0] = rmw_for_[1] = nullptr; rmw_m_[0] = rmw_m_[1] = -1; rmorph_m_ = -1; }

    /* Position moves that are smaller than this are not worth a re-render.
     * The pots and CVs are read through a 16-bit ADC, so a perfectly still
     * knob still jitters by a few counts and every block was being marked
     * dirty. At side 8 a cell spans 1/7 of the axis, so this deadband is
     * about a third of a percent of a cell: far below anything audible, and
     * it takes a held, unmodulated note from rendering forever down to
     * rendering once. */
    float move_eps = 5e-4f;
    /* The same size of nothing, for the blend amount. A morph is linear in the
       coefficients, so 5e-4 of the way between two worlds is 5e-4 of the
       spectral difference — below anything audible by the same argument. */
    float morph_eps = 5e-4f;

    /* Control-frame position, n ≤ kMaxN (axes beyond the space's N ignored). */
    void SetPosition(const float* c, int n)
    {
        for(int a = 0; a < n && a < kMaxN; a++) c_[a] = c[a];
        /* Compare against the position the current frame was rendered at, not
         * the previous call. Comparing to the previous call loses slow drift
         * entirely: a three-second sweep moves 1.7e-4 per block, every step
         * falls under the threshold, and the frame never updates at all. */
        for(int a = 0; a < n && a < kMaxN; a++)
        {
            const float d = c_[a] - rendered_[a];
            if(d > move_eps || d < -move_eps) { dirty_ = true; break; }
        }
    }
    void SetF0(float f0)
    {
        /* Only a change that moves the band limit needs a new frame; pitch
         * itself is the phase increment and costs nothing.
         *
         * With an audio-rate signal on v/oct — an additive wave from a
         * sequencer, say — f0 swings every block and the band limit chases it,
         * which marked the frame dirty on every single block and pinned the
         * engine at its maximum render rate permanently. So the limit is
         * asymmetric: it must drop the instant f0 rises, or partials cross
         * Nyquist and alias, but it may climb back lazily. A fast pitch wobble
         * then re-renders on the way down and waits on the way up, and the
         * only cost is a slightly duller tone at the bottom of the swing. */
        if(f0 != f0_)
        {
            f0_ = f0;
            const int want = KcutFor(f0);
            if(want < kcut_want_) { kcut_want_ = want; dirty_ = true; hold_ = 0; }
            else if(want > kcut_want_ && ++hold_ >= kKcutHold) { kcut_want_ = want; dirty_ = true; hold_ = 0; }
        }
    }

    void Process(float* out, int n)
    {
        /* a resonate world's frame is silence whatever the position, so it
           is rendered once at SetWorld and never again: the render was
           running on every pot jitter for nothing, and on the module that
           was the wavetable's whole cost under a voice it does not need */
        if(world_ && world_->IsResonate() && rframe_)
        {
            dirty_ = false;
            /* but the position is still what telemetry reports as heard
               (Position(), posL), and the render was the only thing that
               folded it: skipped, posL sat where the world was loaded, and
               the page's sliders — which follow posL so that an orbit shows —
               never moved under the pots (Combust, twice: "the decay and
               brightness etc in the web client aren't changing"). Four
               folds a block */
            world_->Fold(c_, p_);
        }
        const uint32_t period = (uint32_t)(render_div < 1 ? 1 : render_div);
        const bool     due    = ((block_ + (uint32_t)render_phase) % period) == 0u;
        const bool render = due && dirty_ && world_ && world_->Ready();
        if(render)
        {
            /* On the audio thread, so the tables cannot tear under a render. */
            if(phase_dirty_) { DerivePhases(); phase_dirty_ = false; }
            for(int a = 0; a < kMaxN; a++) rendered_[a] = c_[a];
            morph_r_ = morph_;
            world_->Fold(c_, p_);
            world_->Evaluate(p_, sharp, mags_, payload_, wt_);
            /* Blend a second world in, if one is set.
             *
             * This is the whole of cross-world morphing on the audio side, and
             * it is three lines because the representation was built for it:
             * the render is linear in the coefficient vector, so a blend of two
             * spectra is a spectrum and cannot click, whatever the two worlds
             * are or which backends they use. It costs one extra Evaluate and
             * no extra transform, since the FFT is the same size regardless.
             *
             * What it deliberately does not do is translate the position.
             * Axis 0 of FM is a modulation index and axis 0 of Plate is a
             * strike position, so holding the coordinates fixed morphs through
             * whatever those collide at. Measured (tools/worldbasis), the
             * subspaces of most pairs of worlds sit about seventy degrees
             * apart, so no small matrix can fix that in general — the honest
             * place for the translation is the host, which has every
             * evaluator and can search for the matching point before it asks
             * for the morph. */
            if(morph_ > 0.f && morph_world_ && morph_world_->Ready()
               && morph_world_->K() == world_->K())
            {
                float mb[kMaxK], pb[kMaxP];
                Weights wb;
                /* The other world is read at an offset, because the same
                 * coordinates mean different things in different worlds: axis 0
                 * of FM is a modulation index and axis 0 of Plate is a strike
                 * position. Without it a morph blends towards whatever those
                 * happen to collide at, which is arbitrary. The offset is
                 * searched for once, by whoever aims the morph. */
                for(int a = 0; a < kMaxN; a++) pmo_[a] = c_[a] + moff_[a];
                morph_world_->Fold(pmo_, pm_);
                morph_world_->Evaluate(pm_, sharp, mb, pb, wb);
                const float t = morph_ > 1.f ? 1.f : morph_;
                const int   k = world_->K();
                for(int i = 0; i < k; i++) mags_[i] += t * (mb[i] - mags_[i]);
                for(int j = 0; j < world_->P(); j++) payload_[j] += t * (pb[j] - payload_[j]);
            }
            Bandlimit();
            /* A shaped world renders into the scratch and its last shaper
             * writes the oscillator's frame, so no stage has to copy a buffer.
             * Each shaper is the identity at zero, so a node of a shape table
             * with both shaper axes down is exactly the waveform the table
             * names. */
            if(world_->HasShaper())
            {
                RenderFrame(mags_bl_, cph_, sph_, kcut_, shape_, sc_);
                world_->Shape(osc_.Back(), shape_, kFrame, p_);
            }
            else RenderFrame(mags_bl_, cph_, sph_, kcut_, osc_.Back(), sc_);
            dirty_ = false;
            renders_++;
            rframe_ = true;
        }
        osc_.SetFreq(f0_, sr_);
        osc_.Process(out, n, render);
        if(kResonator && world_ && world_->IsResonate())
        {
            if(tune_from_control_)
            {
                if(world_->Res().kind == 0) SetTune(Tune::Voicing, VoicingOf(c_[0]));   /* on a family or a row, axis 0 is the instrument */
                SetTune(Tune::Decay, DecayOf(c_[2]));
                SetTune(Tune::Coil, CoilOf(c_[3]));
            }
            /* one voice built a block at most (see Retune): the flag is
               cleared at the end of the block, not here, so the build a
               strike did before this Process counts — cleared here, a strike
               block with a decay or coil CV moving built two voices, one
               block in eight under a roll (desktop stress, 975 of 8000) */
            /* the bank counts as driven — its pitch following the CV under
               the lock, as a quantiser — only while something is coming in:
               J1 over -54 dBFS with the amount over 1 %, held 100 ms past
               the last of it. It was any amount over zero, and the amount
               is the Stereo page's sixth pot, which a wavetable world uses
               as CV out A's depth and which keeps whatever it was left at:
               with nothing patched the lock was off, and every CV step
               retuned the note still ringing to the next note in the four
               milliseconds the strike waits — the pop at the note start that
               grew with the resonance (Combust). +13 to +26 dB of edge in
               that window at an amount of 0.01 %, measured; none at 0 */
            if(exciter_ && exgain_ >= 2e-4f)
            {
                float pk = 0.f;
                for(int i = 0; i < n; i++) { const float a = std::fabs(exciter_[i]); if(a > pk) pk = a; }
                if(pk >= 0.002f) rdriven_ = (uint32_t)(0.1f * sr_);
            }
            if(rdriven_ > 0) rdriven_ = rdriven_ > (uint32_t)n ? rdriven_ - (uint32_t)n : 0u;
            Retune();
            if(rbend_ < 64) rbend_++;
            if(rsettle_ < 64) rsettle_++;
            if(rsince_ < 0xFFFFFFu) rsince_ += (uint32_t)n;
            rdens_ *= 1.f - (float)n / sr_;                   /* the strike count leaks with a one-second time constant */
            float tmp[48], tmpR[48];
            const bool lr = rout_ != nullptr && world_->Res().kind != 1;
            if(rout_) for(int i = 0; i < n; i++) rout_[i] = 0.f;
            for(int v = 0; v < rcap_; v++)
            {
                /* a voice past the count rings out and is then skipped */
                if(v >= rpoly_ && !rvoices_[v].Active()) continue;
                /* the weights, again only when an ear has moved a
                   five-hundredth of the string or the voice was rebuilt:
                   worked out every block they were a third of the cost */
                float* wl = rwl_[v]; float* wr = rwr_[v];
                if(lr)
                {
                    const ResonatorVoice& vv = rvoices_[v];
                    const bool moved = std::fabs(rlisten_s_ - rw_s_[v]) > 2e-3f || std::fabs(rlisten_c_ - rw_c_[v]) > 2e-3f
                                    || vv.param != rw_param_[v] || vv.bank.n != rw_n_[v] || vv.hz[0] != rw_hz0_[v];
                    if(moved)
                    {
                        ListenWeights(vv, wl, wr);
                        rw_s_[v] = rlisten_s_; rw_c_[v] = rlisten_c_; rw_param_[v] = vv.param; rw_n_[v] = vv.bank.n; rw_hz0_[v] = vv.hz[0];
                    }
                }
                for(int i = 0; i < n; i += 48)
                {
                    const int m = n - i < 48 ? n - i : 48;
                    const bool drive = v == ractive_ && exciter_ && exgain_ > 0.f;
                    /* the bow on the string only while there is energy: at none
                       it is lifted and the note rings free (a bow resting still
                       on a string would damp it), its state kept for when it
                       comes back down */
                    if(Sustained(rexc_) && v == ractive_ && (c_[1] > 1e-3f || rsv_ != v))
                    {
                        if(rsv_ != v) { StartSustained(v); rsv_ = v; }
                        RunSustained(rvoices_[v], tmp, m);
                        if(lr) for(int k = 0; k < m; k++) tmpR[k] = tmp[k];
                    }
                    else if(v == rcv_)
                    {
                        /* the contact is driving this voice: sample by
                           sample, mono for its few milliseconds */
                        if(!RunCoupled(rvoices_[v], tmp, m)) rcv_ = -1;
                        if(lr) for(int k = 0; k < m; k++) tmpR[k] = tmp[k];
                    }
                    else if(lr) rvoices_[v].ProcessLR(tmp, tmpR, m, wl, wr, drive ? exciter_ + i : nullptr, exgain_);
                    else rvoices_[v].Process(tmp, m, drive ? exciter_ + i : nullptr, exgain_);
                    /* a voice that has gone to infinity or NaN stays there —
                       a linear bank's state is fed back forever — so it is
                       reset, not played: silent until its next strike builds
                       it, where a NaN left in reached the codec every block */
                    float sum = 0.f, pk = 0.f;
                    for(int k = 0; k < m; k++) { sum += tmp[k]; pk = std::fmax(pk, std::fabs(tmp[k])); }
                    if(lr) for(int k = 0; k < m; k++) { sum += tmpR[k]; pk = std::fmax(pk, std::fabs(tmpR[k])); }
                    /* and a voice far louder than any voice plays — 8 in its
                       own units, where a struck note peaks under 1 — is reset
                       the same way, whatever made it: a brake, not a sound.
                       A runaway is a click this way, and was full scale held
                       by the limiter (the trained re-strike, 28 September) */
                    if(!(sum - sum == 0.f) || (pk > kVoiceBrake && !drive))   /* a bank J1 drives is a filter on a live input, and may ring loud: the limiter is its guard */
                    {
                        rvoices_[v].Init();
                        if(v == rcv_) rcv_ = -1;
                        if(v == rsv_) rsv_ = -1;
                        rvoices_[v].cap = rpoly_ > 1 ? ResonatorBank::kMax / rpoly_ : 0;
                        rvoices_[v].release_ms = rrelease_ms_;
                        rvnote_[v] = 1e9f; rvdirty_[v] = false;
                        continue;
                    }
                    for(int k = 0; k < m; k++) out[i + k] += tmp[k];
                    if(rout_) { if(lr) for(int k = 0; k < m; k++) rout_[i + k] += tmpR[k]; else for(int k = 0; k < m; k++) rout_[i + k] += tmp[k]; }
                }
            }
            exciter_ = nullptr;
            rdid_ = false;
        }
        /* Cells are normalised to unit RMS, so peak depends on how the
         * harmonics happen to line up. Measured crest factor across a baked
         * space: median 2.5, p95 3.2, worst 4.2. The old default put peaks
         * well past full scale and the module popped on the loud cells.
         *
         * The fix is headroom, not saturation. A soft clip was tried here and
         * tests/alias_check rejected it outright: a memoryless nonlinearity
         * multiplies the bandwidth of a signal that is bandlimited right up to
         * Nyquist, and the aliasing figure fell from -88 to -27 dBFS. Any
         * saturation stage has to be oversampled, which is an M3 job for the
         * drive lane. Until then this path stays strictly linear. */
        /* The phase trim rides with the gain rather than scaling the frame, so
           it costs one multiply that was happening anyway and never touches the
           stored spectrum — telemetry and the page still see the world's real
           coefficients. */
        const float g = gain * PhaseTrim();
        for(int i = 0; i < n; i++) out[i] *= g;
        if(rout_) { for(int i = 0; i < n; i++) rout_[i] *= g; rout_ = nullptr; }
        block_++;
    }

    void ResetPhase(uint32_t p = 0) { osc_.ResetPhase(p); }

    /* Swap the world under a running voice. One pointer write, so the audio
     * thread either sees the old world or the new one and never a mixture —
     * provided the caller finished building the new World before calling. */
    /* The world to blend towards, and how far. morph 0 or a null world is
     * exactly the single-world path, bit for bit. */
    void SetMorph(const World* w, float amount)
    {
        /* The aim offset belongs to a *pair* of worlds, so changing either end
         * makes it meaningless — and a meaningless offset is worse than none,
         * because it silently reads the target somewhere nobody chose. Cleared
         * on the world changing and not on the amount, since this is called
         * every block from the Morph knob and clearing on every call would
         * undo an aim the instant it was made. */
        if(w != morph_world_) for(int a = 0; a < kMaxN; a++) moff_[a] = 0.f;
        /* Dirty only when something actually moved, which is the same deadband
         * SetPosition has and for the same reason — except that here the cost
         * was not a jittering ADC, it was a caller.
         *
         * This is called every block from the Morph knob, and it used to mark
         * dirty unconditionally, which defeated the render deadband completely:
         * measured on a still patch over 20,000 blocks, 10,000 renders at
         * divider 2 against 2 with the deadband alone, and identically with no
         * morph target set at all. The module rendered at the maximum rate the
         * divider allowed, permanently, on any held or unmodulated note — and
         * the 33% average / 97% max CPU reading that the whole render-divider
         * question was blocked on was measured with this in place.
         *
         * Compared against what the current frame was *rendered* with, not
         * against the last call, for the reason SetPosition spells out: a slow
         * sweep moves less than the threshold per block and would never
         * re-render at all. And crossing zero always counts, because that is
         * the difference between blending and not blending rather than a
         * difference of degree. */
        const bool world_moved = w != morph_world_;
        const bool on_changed  = (amount > 0.f) != (morph_r_ > 0.f);
        const float d          = amount - morph_r_;
        morph_world_ = w;
        morph_ = amount;
        if(world_moved || on_changed || d > morph_eps || d < -morph_eps) dirty_ = true;
    }
    /* Where in the other world to read. Zero means "the same coordinates",
       which is the honest default when nobody has aimed it. */
    void SetMorphOffset(const float* off, int n)
    {
        for(int a = 0; a < kMaxN; a++) moff_[a] = (off && a < n) ? off[a] : 0.f;
        dirty_ = true;
    }
    const float* MorphOffset() const { return moff_; }
    const World* MorphWorld() const { return morph_world_; }
    float        Morph() const { return morph_; }

    /* Override the world's own phase convention. Phase::Random here means
     * "use whatever the world asked for"; the other two force the issue, which
     * is what makes a cosine twin of any world reachable without doubling the
     * world list — including a user world somebody imported this morning. */
    void SetPhaseOverride(World::Phase p, bool on)
    { ph_over_ = p; ph_over_on_ = on; dirty_ = true; phase_dirty_ = true; }
    bool PhaseOverridden() const { return ph_over_on_; }

    /* What cosine phase costs in headroom.
     *
     * Every harmonic at zero phase means every harmonic peaks together at the
     * start of the cycle. Measured over nine worlds at eighty-one positions
     * each, worst-case crest roughly doubles: Saw 4.35 to 7.73, Lock 3.82 to
     * 7.63, Unison 4.33 to 7.93, and eight of the nine land above the 4.3 the
     * output gain allows. Plate is the exception at 3.97, because a modal
     * spectrum is sparse enough that aligning its partials does not stack much.
     *
     * A fixed half rather than a measured peak on purpose: a gain that tracked
     * the frame would move as the morph moved, and a level that breathes with
     * the timbre is worse than one that is simply six decibels down and stays
     * there. Turning it back up is the player's decision to make. */
    float PhaseTrim() const
    { return EffectivePhase() == World::Phase::Cosine ? 0.5f : 1.f; }

    World::Phase EffectivePhase() const
    { return ph_over_on_ ? ph_over_ : (world_ ? world_->PhaseMode() : World::Phase::Random); }

    void SetWorld(const World* w)
    {
        world_ = w;
        dirty_ = true;
        /* The phase spectrum belongs to the world, so it has to follow one.
         *
         * It did not, and that was a shipping bug with teeth: phases were
         * derived once in Init and never again, so a sine-phase world reached
         * by *switching* rendered at whatever convention happened to be live at
         * boot. On the module that is Braids, which is random phase — so Saw,
         * Pulse, Edge, Lock, Shapes and the modal worlds all rendered at random
         * phase unless one of them was the boot world. Measured, Saw fresh
         * against Saw reached by switching differs by 3.46 at a peak of 2.25:
         * not a subtle difference, and precisely the defect the whole
         * sine-phase representation exists to remove.
         *
         * Flagged rather than derived here, because this is called from the
         * main loop while the audio thread renders, and rewriting the phase
         * tables underneath a render is a tear. The render picks it up. */
        phase_dirty_ = true;
        /* The aim offset was searched for against the world being replaced, so
         * it does not describe this one. Same reason as above. */
        for(int a = 0; a < kMaxN; a++) moff_[a] = 0.f;
        /* The band-limit hold exists to stop an audio-rate *pitch* from
         * re-rendering every block. A world switch is not pitch, and leaving
         * the hold in place meant a new world could be clamped to the old
         * one's cutoff for up to 64 blocks — about 32 ms of the wrong
         * brightness every time you changed world. */
        kcut_want_ = 1 << 20;
        hold_      = 0;
        for(int a = 0; a < kMaxN; a++) rendered_[a] = 1e9f;   /* force a re-render */
        morph_r_ = 1e9f;                                     /* and the blend with it */
        /* the voice is state derived from the world: rebuilt here and only
         * here, silent, at the current pitch — arriving at a resonate world
         * is the same as starting in it */
        for(int v = 0; v < rcap_; v++) { rvoices_[v].Init(); rvoices_[v].release_ms = rrelease_ms_; }
        rgen_++; if(rplan_) rplan_->state = 0u;
        rcv_ = -1; rsv_ = -1;          /* a contact or a bow under way belonged to the voices just rebuilt */
        rtuned_.Init(); rtuned_for_ = nullptr; rtuned_member_ = -1; rmw_for_[0] = rmw_for_[1] = nullptr; rmw_m_[0] = rmw_m_[1] = -1; rmorph_m_ = -1;   /* the member is state derived from the world: rebuilt here and only here */
        ractive_ = 0; rmember_ = 0;
        for(int v = 0; v < rcap_; v++) { rvnote_[v] = 1e9f; rvdirty_[v] = false; rvstruck_[v] = 0u; }
        rframe_ = false;
        if(kResonator && w && w->IsResonate() && rcap_) { rmember_ = ResMemberOf(c_[0]); rvnote_[0] = ResParam(); Tuned().At(rvnote_[0], rvoices_[0], sr_); }
    }

    /* ── the coupled exciter ─────────────────────────────────────────────── */
    /* a strike by a coupled exciter: the voice's strike banks folded (a
       struck exciter drives the main state itself), the contact's weights —
       each mode driven as strongly as the fitted instrument has it, the
       mode's unit-strike gain over the loudest, with the position's comb on
       top — and the hammer or the finger set going at the string where it is.
       No recorded attack. The voice then runs coupled (RunCoupled) until the
       contact ends */
    void StrikeCoupled(ResonatorVoice& vv, float vel)
    {
        ResonatorBank& b = vv.bank;
        for(int q = 0; q < ResonatorBank::kStrikes; q++) if(b.ramping[q]) b.Fold(q);
        float gmax = 0.f, f1 = 1e9f;
        for(int k = 0; k < b.n; k++)
        {
            const float g = std::fabs(vv.gain[k]);
            if(g > gmax) gmax = g;
            if(g > 0.f && vv.hz[k] > 0.f && vv.hz[k] < f1) f1 = vv.hz[k];
        }
        const float amount = std::fabs(rexc_pos_ - 0.5f) * 2.f;
        const float pos = 0.05f + 0.45f * rexc_pos_;
        const bool trained = rexc_ == ResExciter::Trained && vv.exc_type == 1;
        float xc = 0.f;
        for(int k = 0; k < ResonatorBank::kMax; k++)
        {
            if(k >= b.n || gmax <= 0.f) { rcw_[k] = 0.f; continue; }
            const float base = trained && vv.exc_wset ? vv.exc_w[k] : std::fabs(vv.gain[k]) / gmax;
            float comb = 1.f;
            if(amount > 1e-3f && f1 < 1e8f)
            {
                float x = pos * vv.hz[k] / f1; x -= std::floor(x);
                float sn, cs; fastmath::SinCos(3.1415927f * x, sn, cs);
                float w = 2.f * (sn < 0.f ? -sn : sn); w = w > 1.f ? 1.f : w;
                comb = (1.f - amount) + amount * w;
            }
            /* each mode's polarity as the recording has it: the sign of its
               sine component at the strike, g cos(phase). The contact cannot
               tell (the force comes back through sum w y, and y goes as w, so
               w^2), but the output sums the modes as they are: all pushed the
               same way they started in phase and piled into a pulse — in the
               treble a 2 ms bump 5-9x the recording's peak at the same
               loudness (the pop; exclevel), where the recording's modes start
               with a coherence of 0.06-0.32 */
            float cp, sp; fastmath::SinCos(vv.phase[k] > 3.1415927f ? vv.phase[k] - 6.2831853f : vv.phase[k], sp, cp);
            rcw_[k] = vv.gain[k] * cp < 0.f ? -base * comb : base * comb;
            xc += rcw_[k] * b.y1[k];
        }
        b.quiet = false;
        const float v = vel < 0.f ? 0.f : vel > 1.f ? 1.f : vel;
        rcg_ = 1.f;
        if(trained)
        {
            /* the point's hammer, the page's shape as offsets around it (the
               centre of each is the hammer as trained): timbre the felt's
               stiffness, x/÷ 30 at the ends; mass x/÷ 3; the noise from none
               to 4x. The speed and the level between the three layers'
               (velocity 0.15, 0.55, 0.95), in logs, held past the ends */
            auto lerp3 = [&](const float* q) {
                const float lo = 0.15f, mid = 0.55f, hi = 0.95f;
                const float a0 = std::log(q[0] > 1e-9f ? q[0] : 1e-9f), a1 = std::log(q[1] > 1e-9f ? q[1] : 1e-9f), a2 = std::log(q[2] > 1e-9f ? q[2] : 1e-9f);
                const float x = v < lo ? lo : v > hi ? hi : v;
                return std::exp(x < mid ? a0 + (a1 - a0) * (x - lo) / (mid - lo) : a1 + (a2 - a1) * (x - mid) / (hi - mid));
            };
            rham_.Init();
            rham_.k     = vv.exc_k * std::exp(6.8f * (rexc_timbre_ - 0.5f));
            rham_.alpha = vv.exc_alpha;
            rham_.mu    = vv.exc_mu;
            rham_.mass  = vv.exc_mass * std::exp(2.2f * (rexc_mass_ - 0.5f));
            rcg_ = lerp3(vv.exc_gain);
            /* the contact runs in the hammer's own units and is heard at rcg_
               (RunCoupled scales the output, and the ring when it lets go);
               a string still ringing from the last strike is in the output's
               units, so it goes into the hammer's first — the hand-back in
               reverse. Without this a re-strike of a ringing note met a
               string thousands of times too big for the felt, the force ran
               away, and the result was scaled up again: 300-690x the first
               strike, the limiter held at full scale (Combust: "C1 may have
               just deafened me") */
            if(rcg_ > 0.f)
            {
                const float inv = 1.f / rcg_;
                for(int k = 0; k < b.n; k++) { b.y1[k] *= inv; b.y2[k] *= inv; }
            }
            xc = 0.f; for(int k = 0; k < b.n; k++) xc += rcw_[k] * b.y1[k];
            rham_.Strike(lerp3(vv.exc_speed), xc);
            const float nm = 2.f * rexc_noise_;                                 /* none, as trained at the centre, 4x at the top */
            rcn_.Init(sr_, vv.exc_nfc > 20.f ? vv.exc_nfc : 2000.f, 0.7f, vv.exc_noise * nm * nm);
            rcv_ = ractive_;
            return;
        }
        if(rexc_ == ResExciter::Hammer)
        {
            rham_.Init();
            rham_.mass  = 0.003f + 0.017f * rexc_mass_;                          /* 3 to 20 g */
            rham_.k     = 1e7f * fastmath::Exp2(9.9657843f * rexc_timbre_);    /* 1e7 to 1e10: the felt soft to hard */
            rham_.alpha = 2.2f + 1.3f * rexc_timbre_;
            rham_.Strike(0.3f * fastmath::Exp2(4.f * v), xc);                  /* 0.3 to 4.8 m/s */
        }
        else
        {
            rplk_.Init();
            rplk_.k       = 200.f * fastmath::Exp2(6.643856f * rexc_timbre_);  /* 200 to 20 000 N/m */
            rplk_.v       = 0.05f + 0.45f * v;                                 /* the finger's speed */
            rplk_.release = (0.3f + 3.7f * v) * (0.5f + rexc_mass_);           /* the force it lets go at */
            rplk_.Start(xc);
        }
        rcn_.Init(sr_, 1500.f + 6500.f * rexc_timbre_, 0.7f, 2e-5f * rexc_noise_);
        rcv_ = ractive_;
    }
    /* the coupled voice's block: its main modes run with the contact, its
       tail beside them, then the pickup, the old attack still fading and the
       wash as Process has them. False when the contact has ended: the voice
       goes back to the ordinary loop, ringing as the contact left it */
    bool RunCoupled(ResonatorVoice& vv, float* out, int m)
    {
        ContactNoise* cn = rexc_noise_ > 0.f ? &rcn_ : nullptr;
        const bool on = rexc_ != ResExciter::Pluck
            ? ProcessStruck(vv.bank, out, m, rcw_, rham_, sr_, kModalMass, cn)
            : ProcessPlucked(vv.bank, out, m, rcw_, rplk_, sr_, kModalMass, cn);
        /* the trained level: the contact runs in the hammer's own units and
           the voice is heard at the level it was trained to (the voice's
           today at that velocity) — the output scaled now, and the ring by
           the same when the contact lets go, so the hand-over is seamless */
        if(rcg_ != 1.f)
        {
            for(int k = 0; k < m; k++) out[k] *= rcg_;
            if(!on) { for(int k = 0; k < vv.bank.n; k++) { vv.bank.y1[k] *= rcg_; vv.bank.y2[k] *= rcg_; } rcg_ = 1.f; }
        }
        vv.bank.ProcessTail(out, m);
        vv.pickup.Process(out, m);
        vv.burst.Process(out, m);
        vv.wash.Process(out, m);
        return on;
    }
    /* the sustained exciter takes a voice: its weights (a bow's at its
       position along the string, sin(pi beta h); a reed's and the lips' at
       the mouthpiece, sqrt(f1 / f), so each resonance's impedance peak falls
       as a bore's does — equal peaks over a piano's 47 partials squeaked on
       the 7th and 9th, and with a bore's losses ran to NaN), its state from
       rest, and the output's scale — for a bow 2 pi f1 against the Helmholtz
       amplitude v / (2 pi f1 beta) at this note, without the beta: what the
       bow reaches of it grows about as beta does (measured, the Piano's C2,
       C3 and C5), so a bow near the bridge and one over the middle sound
       about alike loud; for a reed and the lips the mouthpiece pressure,
       already of order one. The limiter (RunSustained) holds whatever is
       left */
    void StartSustained(int v)
    {
        ResonatorVoice& vv = rvoices_[v];
        ResonatorBank& b = vv.bank;
        for(int q = 0; q < ResonatorBank::kStrikes; q++) if(b.ramping[q]) b.Fold(q);
        b.quiet = false;
        /* the note's fundamental: the mode nearest the note it was struck
           at, when there is one within 6 %; else the lowest mode with a
           fifth of the loudest one's gain. Not the lowest mode: the Piano's
           C5 has its soundboard at 108 Hz */
        float gmax = 0.f;
        for(int k = 0; k < b.n; k++) gmax = std::fmax(gmax, std::fabs(vv.gain[k]));
        float f1 = 1e9f;
        const ResonatorWorld& rw = world_->Res();
        if(rw.kind != 1 && rvnote_[v] != 1e9f)
        {
            const float fn = 440.f * fastmath::Exp2((rvnote_[v] - 69.f) * (1.f / 12.f));
            float best = 0.06f;
            for(int k = 0; k < b.n; k++)
            {
                const float d = std::fabs(vv.hz[k] / fn - 1.f);
                if(vv.gain[k] != 0.f && d < best) { best = d; f1 = vv.hz[k]; }
            }
        }
        if(!(f1 < 1e8f))
            for(int k = 0; k < b.n; k++) if(std::fabs(vv.gain[k]) >= 0.2f * gmax && vv.hz[k] > 0.f && vv.hz[k] < f1) f1 = vv.hz[k];
        if(!(f1 < 1e8f)) f1 = 110.f;
        const float beta = 0.05f + 0.25f * rexc_pos_;
        rsq1_ = 0.f;
        for(int k = 0; k < ResonatorBank::kMax; k++)
        {
            /* a ghost (gain 0, zeta 1: a voice with fewer modes than the
               world's N is padded with them at its fundamental) is not
               driven — the Piano's C5 has seventeen, and weighted they were
               a broadband gain the reed relaxed on at 340-460 Hz */
            if(k >= b.n || vv.gain[k] == 0.f) { rsw_[k] = 0.f; continue; }
            const float h = vv.hz[k] / f1;
            /* only the world's harmonics, for every sustained exciter: a mode
               under the fundamental (a soundboard's — bowed, the Piano's C5
               sank to its 108 Hz board) or off the harmonics (the C3 has a
               body mode at 406 Hz beside its 392, and the lips locked onto
               it 100 cents sharp) is not the string's or the bore's */
            const float d = h - std::floor(h + 0.5f);
            const float xw = d * d * (1.f / (0.02f * 0.02f));
            const float win = h < 0.97f || xw > 16.f ? 0.f : fastmath::ExpNegSmall(xw);
            if(rexc_ == ResExciter::Bow)
            {
                float x = beta * h; x -= std::floor(x);
                float sn, cs; fastmath::SinCos(3.1415927f * x, sn, cs);
                rsw_[k] = sn * win;
            }
            else rsw_[k] = std::sqrt(1.f / h) * win;             /* a bore's: each resonance's peak z f1/f */
            if(k < b.n && std::fabs(vv.hz[k] - f1) < 1e-3f * f1 && b.lrq[k] < 0.f) rsq1_ = std::fmax(rsq1_, b.wq[k] / (-2.f * b.lrq[k]));
        }
        /* the lips' registers: the 1st to the 4th harmonic, each where
           this voice has it (a piano's are stretched) — and one the voice
           lacks (the Piano's C2 has no 4th; a lip tuned to nothing plays
           its own frequency) plays the nearest below it */
        for(int n = 0; n < 4; n++)
        {
            float bh = 0.f, bw = 0.f;
            for(int k = 0; k < b.n; k++)
            {
                const float h = vv.hz[k] / f1;
                if(std::fabs(h - (float)(n + 1)) < 0.025f * (float)(n + 1) && rsw_[k] > bw) { bw = rsw_[k]; bh = h; }
            }
            rsreg_[n] = bw > 0.1f ? bh : n ? rsreg_[n - 1] : 1.f;
        }
        rbow_.Init(); rreed_.Init(); rlips_.Init();
        rlips_.f_lip = f1;
        rsg_ = rexc_ == ResExciter::Bow ? 6.2831853f * f1 * 0.6f : 0.3f;
        rsbeta_ = beta;
        rslim_ = 1.f;
        rsf1_ = f1;
    }
    /* the sustained voice's block: the energy from the velocity axis, the
       shape from the page, the exciter coupled to the modes sample by sample,
       the tail beside them, the pickup; the output scaled, and under a peak
       limiter of its own at 0.8 (instant down, a second back up) — a bow
       pushed past its band or a reed over-blown must not rail */
    void RunSustained(ResonatorVoice& vv, float* out, int m)
    {
        const float energy = c_[1] < 0.f ? 0.f : c_[1] > 1.f ? 1.f : c_[1];
        if(rexc_ == ResExciter::Bow)
        {
            /* the pressure with the speed, inside Schelleng's window: the
               most force that keeps the Helmholtz motion is 2 Z0 v /
               ((mu_s - mu_d) beta), Z0 = 2 M f1 the string's impedance (M
               twice the modal mass), and past about a tenth of it the Piano
               went raucous; under a hundredth it thins to the octave or
               builds for seconds (a fixed 2 N did both: louder bowing came
               out quieter). The timbre is the fraction, a light surface
               (0.009) to a pressed one (0.07) */
            rbow_.v_bow = 0.5f * energy;                                           /* 0 to 0.5 m/s */
            const float fmax = 2.f * (4.f * kModalMass * rsf1_) * rbow_.v_bow / ((rbow_.mu_s - rbow_.mu_d) * rsbeta_);
            rbow_.f_n   = fmax * 0.025f * fastmath::Exp2(3.f * (rexc_timbre_ - 0.5f));
            /* a string's losses: no mode sharper than the fundamental's Q
               over sqrt(h), so the fundamental is the one the bow holds. A
               piano's are not so — a unison's second polarisation rings far
               longer (the C4's octave, Q 4478 against the fundamental's 427)
               — and bowed, it took the note to the octave or the 3rd */
            if(rsq1_ > 0.f)
            {
                ResonatorBank& b = vv.bank;
                const float w1 = 6.2831853f * rsf1_ / sr_;
                for(int k = 0; k < b.n; k++)
                {
                    if(b.wq[k] <= w1) continue;
                    const float a = b.wq[k] * 0.5f * std::sqrt(b.wq[k] / w1) / rsq1_;
                    const float rc = fastmath::ExpNegSmall(a);
                    if(b.rq[k] > rc) { b.rq[k] = rc; b.lrq[k] = -a; b.c1[k] = 2.f * rc * b.cwq[k]; b.c2[k] = -rc * rc; }
                }
            }
            ProcessBowed(vv.bank, out, m, rsw_, rbow_, sr_, kModalMass);
        }
        else
        {
            /* a bore's losses: the world's tuning, but no resonance sharper
               than Q 30 while a reed or the lips drive it. A piano's modes
               ring for seconds, and an instability grows as f / Q — on the
               fundamental over seconds, so the high partials won and the
               reed squeaked; the prototypes' bores were Q 50. Once let go
               the note falls as a wind instrument's does */
            ResonatorBank& b = vv.bank;
            for(int k = 0; k < b.n; k++)
            {
                const float rc = fastmath::ExpNegSmall(b.wq[k] * (0.5f / kBoreQ));
                if(b.rq[k] > rc) { b.rq[k] = rc; b.lrq[k] = -b.wq[k] * (0.5f / kBoreQ); b.c1[k] = 2.f * rc * b.cwq[k]; b.c2[k] = -rc * rc; }
            }
            if(rexc_ == ResExciter::Reed)
            {
                rreed_.gamma = 0.2f + 0.75f * energy;                               /* speaks at about a fifth of the throw, nearly shut at the top */
                rreed_.zeta  = 0.1f + 0.5f * rexc_timbre_;                          /* the embouchure */
                ProcessBlown(vv.bank, out, m, rsw_, rreed_, 10.f + 50.f * rexc_mass_);
            }
            else
            {
                /* the timbre the register, in four: the lip tuned a quarter
                   to a tenth of the harmonics' spacing under the 1st to the
                   4th resonance, which plays that resonance within about 25
                   cents (lip Q 15, the Piano; tuned on the resonance, 60
                   sharp; between two, often nothing; tuned 0.75-0.9 of it,
                   the 4th's lip sat on the 3rd) — and across each quarter a
                   lip's small bend upward */
                const float tr = (rexc_timbre_ < 0.f ? 0.f : rexc_timbre_ > 0.999f ? 0.999f : rexc_timbre_) * 4.f;
                const int reg = (int)tr;
                rlips_.gamma = 0.95f * energy;
                rlips_.f_lip = rsf1_ * (rsreg_[reg] - (reg ? 0.4f : 0.25f) + 0.15f * (tr - (float)reg));
                rlips_.q     = 10.f + 10.f * rexc_mass_;
                ProcessLipped(vv.bank, out, m, rsw_, rlips_, 30.f, sr_);
            }
        }
        vv.bank.ProcessTail(out, m);
        for(int k = 0; k < m; k++)
        {
            float y = out[k] * rsg_;
            const float a = std::fabs(y) * rslim_;
            if(a > 0.8f) rslim_ = 0.8f / std::fabs(y);                             /* down at once */
            else rslim_ += (1.f - rslim_) * (1.f / sr_);                            /* back over about a second */
            out[k] = y * rslim_;
        }
        vv.pickup.Process(out, m);
        vv.burst.Process(out, m);
        vv.wash.Process(out, m);
    }

    /* the modal mass the contact's force moves (kg): one number for every
       world until the exciter is trained per note (docs/exciters.md) */
    static constexpr float kModalMass = 0.01f;
    static constexpr float kVoiceBrake = 8.f;   /* a voice's peak, in its own units, past which it is reset (a struck note peaks under 1) */
    static constexpr float kBoreQ = 30.f;       /* the sharpest resonance a reed or the lips drive (RunSustained) */

    /* ── pairing (kyk_stereo.h) ──────────────────────────────────────────── */
    /* Match another voice's phase and block count without rendering. */
    void FollowPhase(const EngineCore& o)
    {
        osc_.ResetPhase(o.osc_.Phase());
        block_ = o.block_;
    }
    /* Take the other voice's current frame as our own, so the next render
     * crossfades from it instead of from stale content. */
    void AdoptFrame(const EngineCore& o)
    {
        osc_.AdoptFront(o.osc_);
        block_ = o.block_;
        kcut_  = o.kcut_;
        for(int k = 0; k < kMaxK; k++) { mags_[k] = o.mags_[k]; mags_bl_[k] = o.mags_bl_[k]; }
        dirty_ = true;
    }

    /* ── telemetry ──────────────────────────────────────────────────────── */
    uint32_t       Block() const { return block_; }
    /* How many frames this voice has actually built. A render is the only
     * expensive thing the engine does — everything else is a phase increment —
     * so this over a known number of blocks is the honest CPU proxy, and it is
     * what caught the band-limit treadmill. */
    uint32_t       Renders() const { return renders_; }
    float          F0() const { return f0_; }
    int            Kcut() const { return kcut_; }
    const float*   Position() const { return p_; }          /* folded */
    const float*   Control() const { return c_; }           /* as set */
    const Weights& LastWeights() const { return wt_; }
    const float*   Mags() const { return mags_; }            /* interpolated, pre-bandlimit */
    const float*   MagsBandlimited() const { return mags_bl_; }
    const float*   Payload() const { return payload_; }
    const float*   Frame() const { return osc_.Front(); }
    const World*   WorldPtr() const { return world_; }
    const Space*   SpacePtr() const { return world_ ? world_->SpacePtr() : nullptr; }

private:
    void DerivePhases()
    {
        /* Three conventions, chosen by the world (World::Phase).
         *
         * Sine phase puts every harmonic a quarter turn along, which is the
         * basis in which saw, square, pulse and triangle are all exact — the
         * measured correlation against an ideal band-limited saw is 1.0000,
         * against 0.79 for the random phase everything used to get. A world
         * that wants recognisable waveforms asks for this one.
         *
         * Seed 0 means zero phase, a cosine stack, which is peaky: crest 5.26
         * against 2.02 for sine phase. Kept because the lattice format can
         * carry it, not because anything should choose it.
         *
         * Otherwise a fixed random phase per harmonic, quantised to the sine
         * table so the module and the desktop agree bit for bit. */
        Rng rng;
        /* The override wins where it is on, so a cosine twin of any world —
           including one somebody imported — is reachable without doubling the
           world list. */
        const World::Phase conv = EffectivePhase();
        const bool     sine = conv == World::Phase::Sine;
        const bool     cosn = conv == World::Phase::Cosine;
        const uint32_t seed = world_ ? world_->PhaseSeed() : 0u;
        rng.Seed(seed);
        for(int k = 0; k < kMaxK; k++)
        {
            const int idx = sine ? (kTableSize / 4)
                          : cosn ? 0
                                 : (seed ? (int)(rng.Next() & (uint32_t)(kTableSize - 1)) : 0);
            sph_[k]       = kSinTable[idx];
            cph_[k]       = kSinTable[(idx + kTableSize / 4) & (kTableSize - 1)];
        }
    }

    /* The band limit f0 implies, given the space's K. */
    int KcutFor(float f0) const
    {
        if(!world_ || !world_->Ready()) return 0;
        const int K = world_->K();
        if(f0 <= 0.f) return K;
        const float nyq   = 0.5f * sr_;
        const float ratio = nyq / f0;
        int         kmax  = ratio >= (float)K ? K : (int)ratio;
        if(kmax > 0 && (float)kmax * f0 >= nyq) kmax--;
        return kmax < K ? kmax : K;
    }

    static constexpr int kKcutHold = 64;   /* blocks, so about 32 ms */

    void Bandlimit()
    {
        const int K = world_->K();
        int       kmax;
        if(f0_ <= 0.f) kmax = K;
        else
        {
            const float nyq   = 0.5f * sr_;
            const float ratio = nyq / f0_;
            kmax              = ratio >= (float)K ? K : (int)ratio;
            /* strictly below Nyquist: a harmonic landing exactly on it is out */
            if(kmax > 0 && (float)kmax * f0_ >= nyq) kmax--;
        }
        kcut_ = kmax < K ? kmax : K;
        if(kcut_want_ < kcut_) kcut_ = kcut_want_;   /* honour the held limit */
        kcut_want_ = kcut_;
        /* The shaper's reduction comes *after* the hold state is settled, and
         * never feeds back into it.
         *
         * A world with a frame shaper needs headroom above the harmonics it
         * asks for, because a memoryless nonlinearity multiplies bandwidth and
         * the frame it is handed is band-limited to exactly Nyquist. But the
         * held limit exists to stop an audio-rate pitch from re-rendering
         * every block, and it is a property of the pitch, not of the shaper.
         * Writing the reduced value back into it — which the first version did
         * — leaves the held limit pinned low, so winding a folder back down
         * left the tone dull until the slow climb caught up, and the two
         * mechanisms fought each other on every block. A mitigation, not a
         * cure: the residual aliasing is measured in docs/m3-notes.md. */
        /* The shaper's cutoff is *fractional*, and the taper below is what
         * makes that mean anything.
         *
         * The first version truncated it to an integer, which drops a whole
         * harmonic the instant the knob crosses a boundary. At full band that
         * is one part in 64 and nobody notices; with a folder pulling the
         * limit down to 16 it is one part in 16, and measured it put a step of
         * 0.46 into the rendered cycle — against 0.002 for the smooth axes of
         * the same world. That is a click, and turning the fold knob walks
         * through a series of them.
         *
         * So the cut is a smooth window instead. A harmonic fades out over
         * three bins of travel rather than vanishing, and by the time the
         * integer limit drops it the bin is already at zero, which makes the
         * remaining step harmless. */
        shape_fc_ = 0.f;
        if(world_->HasShaper())
        {
            const float sc = world_->BandScale(p_);
            if(sc > 1.f)
            {
                float fc = (float)kcut_ / sc;
                if(fc < 4.f) fc = 4.f;
                if(fc < (float)kcut_)
                {
                    shape_fc_ = fc;
                    int lim = (int)(fc + 1.f);
                    if(lim < kcut_) kcut_ = lim;
                }
            }
        }
        for(int k = 0; k < kcut_; k++) mags_bl_[k] = mags_[k];
        for(int k = kcut_; k < K; k++) mags_bl_[k] = 0.f;
        const int rb = rolloff_bins < kcut_ ? rolloff_bins : kcut_;
        for(int i = 0; i < rb; i++)
        {
            /* bins kcut-rb .. kcut-1 taper 1 → ~0 with a raised cosine */
            const float t   = (float)(i + 1) / (float)(rb + 1);
            const int   idx = ((int)(t * (float)(kTableSize / 2)) + kTableSize / 4) & (kTableSize - 1);
            const float w   = 0.5f * (1.f + kSinTable[idx]);   /* 0.5(1+cos πt) */
            mags_bl_[kcut_ - rb + i] *= w;
        }
        /* the shaper's smooth cutoff, applied last */
        if(shape_fc_ > 0.f)
        {
            const float W = 3.f;
            for(int k = 0; k < kcut_; k++)
            {
                const float h = (float)(k + 1);
                const float u = (h - (shape_fc_ - W)) / W;
                if(u <= 0.f) continue;
                if(u >= 1.f) { mags_bl_[k] = 0.f; continue; }
                const int   idx = ((int)(u * (float)(kTableSize / 2)) + kTableSize / 4) & (kTableSize - 1);
                mags_bl_[k] *= 0.5f * (1.f + kSinTable[idx]);
            }
        }
    }

    const World* world_ = nullptr;
    int            rcap_ = 0;       /* voices lent (LendVoices); 0, no resonator plays here */
    ResonatorVoice* rvoices_ = nullptr; /* the resonate path; idle for every other kind */
    int            ractive_ = 0;    /* the voice the last strike took, which follows the pitch */
    int            rmember_ = 0;    /* the instrument of a family the voice is built from */
    int            rpoly_ = 1;
    ResonatorWorld rtuned_;         /* the member playing, attached once per member (Tuned()) */
    const World*   rtuned_for_ = nullptr;
    int            rtuned_member_ = -1;
    float*         rvnote_ = nullptr;   /* the note each voice was built at; 1e9 for not yet */
    bool*          rvdirty_ = nullptr;  /* the voice is to be rebuilt at that note: the tune or the member changed */
    uint32_t*      rvstruck_ = nullptr; /* the strike count when each voice was last struck: the oldest is the one taken */
    uint32_t       rstrikes_ = 0;
    uint32_t       at_count_ = 0;
    float          rtune_[3] = {0.f, 1.f, 1.f};   /* voicing (widths), decay (x), coil (x) */
    bool           rframe_ = false;  /* a resonate world's one silent frame has been rendered */
    bool           tune_from_control_ = false;
    bool           pitch_lock_ = true;   /* the nearest semitone, taken at the strike; off, a bend */
    float          vtrack_ = 0.f;        /* how much harder the strike gets with fast playing, 0..1 */
    float          rdens_ = 0.f;         /* strikes a second, as a leaky count with a one-second time constant */
    float          rlast_v_ = 0.f;       /* the velocity the last strike was made at */
    bool           rstriking_ = false;
    uint32_t       rsince_ = 0xFFFFFFu;  /* samples since the last strike, for the late-CV window */
    int            rbend_ = 64;          /* blocks since the last bend */
    bool           rdid_ = false;        /* a voice was built this block */
    int            rsettle_ = 64;        /* blocks since the last rebuild that was not a strike */
    bool           rlate_ = false;       /* the late-CV retune has been taken since the last strike */
    int            rlate_n_ = 0;         /* blocks a late CV has stood, no strike arriving */
    float          rforce_ = 1e9f;
    ResExciter     rexc_ = ResExciter::Recorded;
    float          rexc_timbre_ = 0.5f, rexc_pos_ = 0.5f, rexc_noise_ = 0.5f, rexc_mass_ = 0.5f;   /* the centre of each: a trained exciter as trained */
    int            rcv_ = -1;            /* the voice a coupled contact is driving, -1 none */
    float          rcg_ = 1.f;           /* the trained level the coupled voice is heard at, applied to its ring when the contact lets go */
    Hammer         rham_;
    Bow            rbow_;
    Reed           rreed_;
    Lips           rlips_;
    int            rsv_ = -1;            /* the voice the sustained exciter is driving, -1 none */
    float          rsw_[ResonatorBank::kMax];   /* its weights on that voice's modes */
    float          rsg_ = 1.f, rslim_ = 1.f, rsf1_ = 110.f, rsbeta_ = 0.2f, rsreg_[4] = {1.f, 2.f, 3.f, 4.f}, rsq1_ = 0.f;   /* its output's scale, its limiter, the note's fundamental, the bow's place (a fraction of the string), the lips' four registers (harmonic numbers, as this voice has them), the fundamental's Q (the bow's losses) */
    Pluck          rplk_;
    ContactNoise   rcn_;
    float          rcw_[ResonatorBank::kMax];   /* the contact's weights on the coupled voice's modes */
    uint32_t       rhurried_ = 0u;       /* the note a strike takes from the staged voice, 1e9 for none */
    const float*   exciter_ = nullptr;   /* this block's drive, or null */
    float          exgain_ = 0.f;
    uint32_t       rdriven_ = 0;         /* samples the bank still counts as driven: something came in at J1 */
    bool           rhold_ = false;       /* a strike is waiting for the CV (HoldPitch) */
    float*         rout_ = nullptr;      /* this block's right channel, when a resonator is heard from two points (SetListen) */
    float          rlisten_s_ = 0.f, rlisten_c_ = 0.25f;
    float          (*rwl_)[ResonatorBank::kMax] = nullptr, (*rwr_)[ResonatorBank::kMax] = nullptr;   /* each voice's ears, kept while they have not moved */
    float*         rw_s_ = nullptr; float* rw_c_ = nullptr; float* rw_param_ = nullptr; float* rw_hz0_ = nullptr;
    int*           rw_n_ = nullptr;
    float          rrelease_ms_ = ResonatorDefaults::kReleaseMs;
#ifndef KYK_MEMBER_MORPH
#define KYK_MEMBER_MORPH 0
#endif
    bool           rmorph_ = KYK_MEMBER_MORPH != 0;   /* a family's body axis a morph, not a switch (SetMemberMorph) */
    int            rmorph_m_ = -1;
    float          rmorph_t_ = -1.f;
    /* the morph's room, lent by the shell (SetMorphScratch): two members and
       two voices, 13 KB — not the engine's own, which sits in the M7's
       internal SRAM with no 13 KB to spare, twice over for the stereo
       pair; on the module it is SDRAM. None lent, no morph */
    ResonatorWorld* rmw_ = nullptr;
    ResonatorVoice* rscr_ = nullptr;
    /* the staged strike: the plan's room (lent), the request the audio
       thread posts under its sequence number, the world generation */
    StrikePlan*    rplan_ = nullptr;
    StrikeKey      rq_ = {nullptr, 0u, 0, 0.f, 0, {0.f, 0.f, 0.f}, false};
    volatile uint32_t rq_seq_ = 0u;
    uint32_t       rgen_ = 0u;
    uint32_t       rplan_taken_ = 0u;
    const World*   rmw_for_[2] = {nullptr, nullptr};
    int            rmw_m_[2] = {-1, -1};

    const World*   morph_world_ = nullptr;

    World::Phase   ph_over_ = World::Phase::Random;

    bool           ph_over_on_ = false;
    bool           phase_dirty_ = false;

    float          morph_ = 0.f;

    float          pm_[kMaxN] = {0.f};
    float          pmo_[kMaxN] = {0.f};
    float          moff_[kMaxN] = {0.f};
    float        sr_    = 48000.f;
    Osc          osc_;
    FftScratch   sc_;
    Weights      wt_;
    float        cph_[kMaxK], sph_[kMaxK];
    float        mags_[kMaxK], mags_bl_[kMaxK];
    /* Somewhere for a shaper that reads the cycle out of order — phase
     * modulation runs the read pointer backwards where the warp does, so it
     * cannot work in place. A member, not a stack array: the audio callback
     * is not the place to put four kilobytes. */
    float        shape_[kFrame];
    float        shape_fc_ = 0.f;   /* fractional cutoff a frame shaper asked for */
    /* What the current frame was rendered with, so a blend that has not moved
       since does not ask for another one. 1e9 until the first render, like
       rendered_, so the first call always counts. */
    float        morph_r_ = 1e9f;
    uint32_t     renders_ = 0;
    float        payload_[kMaxP];
    float        c_[kMaxN], p_[kMaxN], rendered_[kMaxN];
    float        f0_    = 110.f;
    int          kcut_  = 0;
    int          kcut_want_ = 1 << 20;   /* the band limit the pitch is asking for */
    int          hold_  = 0;
    uint32_t     block_ = 0;
    bool         dirty_ = true;
};

/* the engine with its own four voices: every caller that is not the stereo
   pair (the suite, the desktop's tools) plays a resonator as it always did.
   A copy lends itself its own copy of the voices, not the original's */
class Engine : public EngineCore
{
public:
    Engine() { LendVoices(&own_); }
    Engine(const Engine& o) : EngineCore(o), own_(o.own_) { LendVoices(&own_, false); }
    Engine& operator=(const Engine& o) { if(this != &o) { EngineCore::operator=(o); own_ = o.own_; LendVoices(&own_, false); } return *this; }
private:
    ResonatorVoices own_;
};

} // namespace kyk
