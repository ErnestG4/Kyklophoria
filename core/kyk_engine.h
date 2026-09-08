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
#include "kyk_space.h"
#include "kyk_interp.h"
#include "kyk_fft.h"
#include "kyk_osc.h"

namespace kyk {

class Engine
{
public:
    /* Tunables the shell may set between blocks. */
    float gain         = 0.5f;   /* unit-RMS spectra peak near 1.7; keep the output sane */
    int   render_div   = 1;      /* render a new frame every render_div blocks */
    int   rolloff_bins = 0;      /* raised-cosine taper over the top bins below the cutoff */

    void Init(const Space* space, float sr)
    {
        space_ = space;
        sr_    = sr;
        osc_.Init();
        block_ = 0;
        f0_    = 110.f;
        kcut_  = 0;
        for(int a = 0; a < kMaxN; a++) { c_[a] = 0.5f; p_[a] = 0.5f; }
        for(int k = 0; k < kMaxK; k++) { mags_[k] = 0.f; mags_bl_[k] = 0.f; }
        for(int j = 0; j < kMaxP; j++) payload_[j] = 0.f;
        DerivePhases();
        dirty_ = true;
    }

    /* Control-frame position, n ≤ kMaxN (axes beyond the space's N ignored). */
    void SetPosition(const float* c, int n)
    {
        for(int a = 0; a < n && a < kMaxN; a++) c_[a] = c[a];
        dirty_ = true;
    }
    void SetF0(float f0)
    {
        if(f0 != f0_) dirty_ = true;
        f0_ = f0;
    }

    void Process(float* out, int n)
    {
        const bool due    = (block_ % (uint32_t)(render_div < 1 ? 1 : render_div)) == 0u;
        const bool render = due && dirty_ && space_ && space_->Attached();
        if(render)
        {
            Fold(*space_, c_, p_);
            LatticeWeights(*space_, p_, wt_);
            Blend(*space_, wt_, mags_, payload_);
            Bandlimit();
            RenderFrame(mags_bl_, cph_, sph_, kcut_, osc_.Back(), sc_);
            dirty_ = false;
        }
        osc_.SetFreq(f0_, sr_);
        osc_.Process(out, n, render);
        for(int i = 0; i < n; i++) out[i] *= gain;
        block_++;
    }

    void ResetPhase(uint32_t p = 0) { osc_.ResetPhase(p); }

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
    const Space*   SpacePtr() const { return space_; }

private:
    void DerivePhases()
    {
        /* seed 0: zero phases (every cell a cosine stack, peaky). Otherwise a
         * fixed random phase per harmonic, quantised to the sine table so
         * the module and the desktop agree bit for bit. */
        Rng rng;
        const uint32_t seed = space_ ? space_->Header().phase_seed : 0u;
        rng.Seed(seed);
        for(int k = 0; k < kMaxK; k++)
        {
            const int idx = seed ? (int)(rng.Next() & (uint32_t)(kTableSize - 1)) : 0;
            sph_[k]       = kSinTable[idx];
            cph_[k]       = kSinTable[(idx + kTableSize / 4) & (kTableSize - 1)];
        }
    }

    void Bandlimit()
    {
        const int K = space_->K();
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

    const Space* space_ = nullptr;
    float        sr_    = 48000.f;
    Osc          osc_;
    FftScratch   sc_;
    Weights      wt_;
    float        cph_[kMaxK], sph_[kMaxK];
    float        mags_[kMaxK], mags_bl_[kMaxK];
    float        payload_[kMaxP];
    float        c_[kMaxN], p_[kMaxN];
    float        f0_    = 110.f;
    int          kcut_  = 0;
    uint32_t     block_ = 0;
    bool         dirty_ = true;
};

} // namespace kyk
