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
#include "kyk_world.h"
#include "kyk_resonate.h"
#include "kyk_fft.h"
#include "kyk_osc.h"

namespace kyk {

class Engine
{
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
        for(int v = 0; v < kPoly; v++) { rvoices_[v].Init(); rvoices_[v].release_ms = rrelease_ms_; }
        rtuned_.Init(); rtuned_for_ = nullptr; rtuned_member_ = -1;   /* the member is state derived from the world: rebuilt here and only here */
        ractive_ = 0; rpoly_ = 1; rmember_ = 0; rdriven_ = 0; rhold_ = false;
        for(int v = 0; v < kPoly; v++) { rvnote_[v] = 1e9f; rvdirty_[v] = false; rvstruck_[v] = 0u; }
        rframe_ = false;
        if(world && world->IsResonate()) { rmember_ = ResMemberOf(c_[0]); rvnote_[0] = ResParam(); Tuned().At(rvnote_[0], rvoices_[0], sr_); }
    }

    /* ── the resonate path ───────────────────────────────────────────────
     * A resonate world is a ResonatorVoice after the oscillator, whose
     * frame is silent for that kind. The voice is built from the world at
     * the current pitch in SetWorld; a strike where the pitch has moved
     * retunes the bank with its state ringing on, as the oscillator follows
     * the pitch, and every strike adds to what rings, as a hammer does. Who strikes is the shell's business (a host action, a
     * gate, a pot for the velocity — docs/modal-mode.md). */
    void Strike(float velocity01)
    {
        if(!world_ || !world_->IsResonate()) return;
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
        rstriking_ = false; rsince_ = 0;
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
        rvoices_[ractive_].Strike(v);
    }
    /* 1, 2 or 4 voices. Changing it cuts nothing: a voice past the new
       count rings out and is then skipped, and the round goes on from the
       last voice within the count. On the module this is a pot, and
       silencing four voices at the turn of it was a click */
    void SetPolyphony(int n)
    {
        n = n < 1 ? 1 : n > kPoly ? kPoly : n;
        if(n == rpoly_) return;
        rpoly_ = n;
        if(ractive_ >= n) ractive_ = n - 1;
        /* Rings' rule: the bank's modes shared out among the voices, so
           four voices stacked are as rich as one and cost the same —
           Combust: "white room talk". Each voice keeps its loudest 48 / n
           modes, rebuilt one a block */
        for(int v = 0; v < kPoly; v++) { rvoices_[v].cap = n > 1 ? ResonatorBank::kMax / n : 0; rvdirty_[v] = true; }
    }
    int Polyphony() const { return rpoly_; }
    /* Rings' external exciter: an audio block driven into the voice that
       follows the pitch, g x a sample into every mode — the world as a
       resonant filter bank for whatever is patched in. Set per block; the
       pointer is read by the Process that follows and then dropped. */
    void SetExciter(const float* x, float gain) { exciter_ = x; exgain_ = gain; }
    /* a strike is waiting (the module holds one for the CV to settle): the
       pitch is not taken by the voice still ringing meanwhile */
    void HoldPitch(bool on) { rhold_ = on; }
    /* how long a stolen voice's last note takes to fall 60 dB (ms, 5..1000;
       ResonatorDefaults::kReleaseMs by default) */
    void SetReleaseMs(float ms)
    {
        rrelease_ms_ = ms < 5.f ? 5.f : ms > 1000.f ? 1000.f : ms;
        for(int v = 0; v < kPoly; v++) rvoices_[v].release_ms = rrelease_ms_;
    }
    float ReleaseMs() const { return rrelease_ms_; }
    static float NoteOf(float hz) { return 69.f + 12.f * std::log2(hz > 1.f ? hz / 440.f : 1.f / 440.f); }
    /* Where on its axis the world is played: a note world at the pitch, an
     * index world — a row of bodies, gong to woodblock — where position 0
     * puts it, lo to hi. The second is the world's one position axis doing
     * what a position does everywhere else here: choosing the timbre. */
    float ResParam() const
    {
        const ResonatorWorld& r = world_->Res();
        if(r.kind == 1) return r.lo + (r.hi - r.lo) * (c_[0] < 0.f ? 0.f : c_[0] > 1.f ? 1.f : c_[0]);
        const float note = NoteOf(f0_);
        if(!pitch_lock_) return note;
        /* locked: the nearest semitone, with a tenth of a semitone of
           hysteresis between strikes so a CV on a boundary does not
           chatter; a strike takes the nearest outright, since a strike is
           a moment and the CV is what it is then */
        const float held = rvnote_[ractive_];
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
    void SetPitchLock(bool on) { if(pitch_lock_ != on) { pitch_lock_ = on; rvnote_[ractive_] = 1e9f; } }   /* re-read: locked, the nearest semitone; free, the cent */
    void SetVelocityTrack(float amount) { vtrack_ = amount; }   /* 0..1: how much harder the strike gets with fast playing (0.5 of velocity at full, at six strikes a second) */
    float VelocityTrack() const { return vtrack_; }
    float LastStrikeVelocity() const { return rlast_v_; }
    bool PitchLock() const { return pitch_lock_; }
    /* the voice follows its parameter with its state ringing on; an index
       world follows the pot every block, a note world its pitch */
    void Retune()
    {
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
        if(m != rmember_) { rmember_ = m; for(int v = 0; v < kPoly; v++) rvdirty_[v] = true; }   /* another instrument: every voice rebuilt, its ring carried */
        /* does the active voice take the pitch? Not within the deadband;
           and locked, not at all unless this is the strike, the bank is
           being driven (the pitch is then the only thing playing it), or
           the pitch jumps a semitone or more within 30 ms of the strike —
           a sequencer whose CV lands after its gate: the note belongs to
           the strike it followed, and a strike that held the old note
           would be the wrong note for as long as it rang */
        bool move;
        if(note == 1e9f) move = true;
        else if(std::fabs(p - note) <= eps) move = false;
        else
        {
            const bool late = rsince_ < 0.03f * sr_ && std::fabs(p - note) > 0.4f;
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
            /* a tune change under the lock rebuilds the voice at the note
               it holds, not at wherever the pitch has gone since */
            Tuned().At(move ? p : note, rvoices_[ractive_], sr_, true, rstriking_); at_count_++; rdid_ = true;
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
        if(rdid_) return;
        for(int k = 1; k < kPoly; k++)
        {
            const int v = (ractive_ + k) % kPoly;
            if(!rvdirty_[v]) continue;
            if(rvnote_[v] != 1e9f && rvoices_[v].Active()) { Tuned().At(rvnote_[v], rvoices_[v], sr_, true); at_count_++; rdid_ = true; rvdirty_[v] = false; return; }
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
        for(int i = 0; i < kPoly; i++) rvdirty_[i] = true;   /* every voice rebuilt at the note it holds, one a block */
    }
    float GetTune(Tune which) const { return rtune_[(int)which]; }
    const ResonatorVoice& Voice() const { return rvoices_[ractive_]; }
    const ResonatorVoice& VoiceAt(int v) const { return rvoices_[v < 0 ? 0 : v >= kPoly ? kPoly - 1 : v]; }
    float ResParamNow() const { return rvnote_[ractive_]; }
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
        if(world_ && world_->IsResonate() && rframe_) dirty_ = false;
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
        if(world_ && world_->IsResonate())
        {
            if(tune_from_control_)
            {
                if(world_->Res().kind == 0) SetTune(Tune::Voicing, VoicingOf(c_[0]));   /* on a family or a row, axis 0 is the instrument */
                SetTune(Tune::Decay, DecayOf(c_[2]));
                SetTune(Tune::Coil, CoilOf(c_[3]));
            }
            rdid_ = false;                 /* one voice built a block at most (see Retune) */
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
            if(rsince_ < 0xFFFFFFu) rsince_ += (uint32_t)n;
            rdens_ *= 1.f - (float)n / sr_;                   /* the strike count leaks with a one-second time constant */
            float tmp[48];
            for(int v = 0; v < kPoly; v++)
            {
                /* a voice past the count rings out and is then skipped */
                if(v >= rpoly_ && !rvoices_[v].Active()) continue;
                for(int i = 0; i < n; i += 48)
                {
                    const int m = n - i < 48 ? n - i : 48;
                    const bool drive = v == ractive_ && exciter_ && exgain_ > 0.f;
                    rvoices_[v].Process(tmp, m, drive ? exciter_ + i : nullptr, exgain_);
                    /* a voice that has gone to infinity or NaN stays there —
                       a linear bank's state is fed back forever — so it is
                       reset, not played: silent until its next strike builds
                       it, where a NaN left in reached the codec every block */
                    float sum = 0.f;
                    for(int k = 0; k < m; k++) sum += tmp[k];
                    if(!(sum - sum == 0.f))
                    {
                        rvoices_[v].Init();
                        rvoices_[v].cap = rpoly_ > 1 ? ResonatorBank::kMax / rpoly_ : 0;
                        rvoices_[v].release_ms = rrelease_ms_;
                        rvnote_[v] = 1e9f; rvdirty_[v] = false;
                        continue;
                    }
                    for(int k = 0; k < m; k++) out[i + k] += tmp[k];
                }
            }
            exciter_ = nullptr;
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
        for(int v = 0; v < kPoly; v++) { rvoices_[v].Init(); rvoices_[v].release_ms = rrelease_ms_; }
        rtuned_.Init(); rtuned_for_ = nullptr; rtuned_member_ = -1;   /* the member is state derived from the world: rebuilt here and only here */
        ractive_ = 0; rmember_ = 0;
        for(int v = 0; v < kPoly; v++) { rvnote_[v] = 1e9f; rvdirty_[v] = false; rvstruck_[v] = 0u; }
        rframe_ = false;
        if(w && w->IsResonate()) { rmember_ = ResMemberOf(c_[0]); rvnote_[0] = ResParam(); Tuned().At(rvnote_[0], rvoices_[0], sr_); }
    }

    /* ── pairing (kyk_stereo.h) ──────────────────────────────────────────── */
    /* Match another voice's phase and block count without rendering. */
    void FollowPhase(const Engine& o)
    {
        osc_.ResetPhase(o.osc_.Phase());
        block_ = o.block_;
    }
    /* Take the other voice's current frame as our own, so the next render
     * crossfades from it instead of from stale content. */
    void AdoptFrame(const Engine& o)
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
    static constexpr int kPoly = 4;
    ResonatorVoice rvoices_[kPoly]; /* the resonate path; idle for every other kind */
    int            ractive_ = 0;    /* the voice the last strike took, which follows the pitch */
    int            rmember_ = 0;    /* the instrument of a family the voice is built from */
    int            rpoly_ = 1;
    ResonatorWorld rtuned_;         /* the member playing, attached once per member (Tuned()) */
    const World*   rtuned_for_ = nullptr;
    int            rtuned_member_ = -1;
    float          rvnote_[kPoly];  /* the note each voice was built at; 1e9 for not yet */
    bool           rvdirty_[kPoly]; /* the voice is to be rebuilt at that note: the tune or the member changed */
    uint32_t       rvstruck_[kPoly] = {};   /* the strike count when each voice was last struck: the oldest is the one taken */
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
    const float*   exciter_ = nullptr;   /* this block's drive, or null */
    float          exgain_ = 0.f;
    uint32_t       rdriven_ = 0;         /* samples the bank still counts as driven: something came in at J1 */
    bool           rhold_ = false;       /* a strike is waiting for the CV (HoldPitch) */
    float          rrelease_ms_ = ResonatorDefaults::kReleaseMs;

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

} // namespace kyk
