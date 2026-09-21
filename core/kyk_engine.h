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
        rvoice_.Init();
        rnote_ = 1e9f;
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
        Retune();
        rvoice_.Strike(velocity01);
    }
    static float NoteOf(float hz) { return 69.f + 12.f * std::log2(hz > 1.f ? hz / 440.f : 1.f / 440.f); }
    /* Where on its axis the world is played: a note world at the pitch, an
     * index world — a row of bodies, gong to woodblock — where position 0
     * puts it, lo to hi. The second is the world's one position axis doing
     * what a position does everywhere else here: choosing the timbre. */
    float ResParam() const
    {
        const ResonatorWorld& r = world_->Res();
        return r.kind == 1 ? r.lo + (r.hi - r.lo) * (c_[0] < 0.f ? 0.f : c_[0] > 1.f ? 1.f : c_[0]) : NoteOf(f0_);
    }
    /* the voice follows its parameter with its state ringing on; an index
       world follows the pot every block, a note world its pitch */
    void Retune()
    {
        const float p = ResParam();
        if(std::fabs(p - rnote_) > 0.01f) { Tuned().At(p, rvoice_, sr_, true); rnote_ = p; }
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
        if(t == v) return;
        t = v;
        rnote_ = 1e9f;                            /* re-read at the next block or strike */
    }
    float GetTune(Tune which) const { return rtune_[(int)which]; }
    ResonatorWorld Tuned() const
    {
        ResonatorWorld r = world_->Res();
        r.voicing = rtune_[0]; r.decay = rtune_[1]; r.coil = rtune_[2];
        return r;
    }

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
        }
        osc_.SetFreq(f0_, sr_);
        osc_.Process(out, n, render);
        if(world_ && world_->IsResonate())
        {
            if(world_->Res().kind == 1 || rnote_ == 1e9f) Retune();
            float tmp[48];
            for(int i = 0; i < n; i += 48)
            {
                const int m = n - i < 48 ? n - i : 48;
                rvoice_.Process(tmp, m);
                for(int k = 0; k < m; k++) out[i + k] += tmp[k];
            }
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
        rvoice_.Init();
        rnote_ = 1e9f;
        if(w && w->IsResonate()) { rnote_ = ResParam(); Tuned().At(rnote_, rvoice_, sr_); }
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
    ResonatorVoice rvoice_;         /* the resonate path; idle for every other kind */
    float          rnote_ = 1e9f;   /* the note the voice was built at */
    float          rtune_[3] = {0.f, 1.f, 1.f};   /* voicing (widths), decay (x), coil (x) */

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
