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
        f0_    = 110.f;
        kcut_  = 0;
        kcut_want_ = 1 << 20;
        hold_  = 0;
        for(int a = 0; a < kMaxN; a++) { c_[a] = 0.5f; p_[a] = 0.5f; rendered_[a] = 1e9f; }
        for(int k = 0; k < kMaxK; k++) { mags_[k] = 0.f; mags_bl_[k] = 0.f; }
        for(int j = 0; j < kMaxP; j++) payload_[j] = 0.f;
        DerivePhases();
        dirty_ = true;
    }

    /* Position moves that are smaller than this are not worth a re-render.
     * The pots and CVs are read through a 16-bit ADC, so a perfectly still
     * knob still jitters by a few counts and every block was being marked
     * dirty. At side 8 a cell spans 1/7 of the axis, so this deadband is
     * about a third of a percent of a cell: far below anything audible, and
     * it takes a held, unmodulated note from rendering forever down to
     * rendering once. */
    float move_eps = 5e-4f;

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
            for(int a = 0; a < kMaxN; a++) rendered_[a] = c_[a];
            world_->Fold(c_, p_);
            world_->Evaluate(p_, sharp, mags_, payload_, wt_);
            Bandlimit();
            RenderFrame(mags_bl_, cph_, sph_, kcut_, osc_.Back(), sc_);
            dirty_ = false;
        }
        osc_.SetFreq(f0_, sr_);
        osc_.Process(out, n, render);
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
        for(int i = 0; i < n; i++) out[i] *= gain;
        block_++;
    }

    void ResetPhase(uint32_t p = 0) { osc_.ResetPhase(p); }

    /* Swap the world under a running voice. One pointer write, so the audio
     * thread either sees the old world or the new one and never a mixture —
     * provided the caller finished building the new World before calling. */
    void SetWorld(const World* w)
    {
        world_ = w;
        dirty_ = true;
        for(int a = 0; a < kMaxN; a++) rendered_[a] = 1e9f;   /* force a re-render */
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
        /* seed 0: zero phases (every cell a cosine stack, peaky). Otherwise a
         * fixed random phase per harmonic, quantised to the sine table so
         * the module and the desktop agree bit for bit. */
        Rng rng;
        const uint32_t seed = world_ ? world_->PhaseSeed() : 0u;
        rng.Seed(seed);
        for(int k = 0; k < kMaxK; k++)
        {
            const int idx = seed ? (int)(rng.Next() & (uint32_t)(kTableSize - 1)) : 0;
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
    }

    const World* world_ = nullptr;
    float        sr_    = 48000.f;
    Osc          osc_;
    FftScratch   sc_;
    Weights      wt_;
    float        cph_[kMaxK], sph_[kMaxK];
    float        mags_[kMaxK], mags_bl_[kMaxK];
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
