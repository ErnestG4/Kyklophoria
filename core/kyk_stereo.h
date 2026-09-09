/* kyk_stereo.h — the stereo pair: one control frame, one rotation, two ears.
 *
 *   c ─► R·c (about pivot) ─► centre pc
 *        pc rotated by −δ in the stereo plane ─► L voice
 *        pc rotated by +δ                     ─► R voice
 *        pc folded                            ─► payload (single-valued)
 *
 * δ = 0 is the mono path: one voice renders and the right output is a copy,
 * bit-identical to the M0 Engine (golden). The right voice's phase and frame
 * are kept in step so switching δ on has no step. Both voices share f0; FM
 * (M3) feeds both.
 */
#pragma once
#include "kyk_engine.h"
#include "kyk_rotate.h"
#include "kyk_kepler.h"

namespace kyk {

class StereoEngine
{
public:
    float spread       = 0.f;    /* turns; 0 = mono */
    int   spread_plane = 0;      /* plane index, see Rotation::PlaneAxes */
    float pivot        = 0.5f;   /* rotation centre on every axis */

    /* Milliseconds for the control frame to cross the whole space; 0 disables.
     * A rate limiter, not a filter: motion slower than the limit passes
     * through untouched, so sweeps keep their shape and only jumps are ramped.
     *
     * This is a smoothing control, not a click fix. An instant full-range jump
     * does *not* click even with the slew off — measured curvature at the jump
     * is 1.4x the 99.99th percentile of the same render, where a click is 50x
     * or more (tests/morph_check). What the slew buys is halving that figure
     * and turning a one-block timbre switch into a short morph, which is what
     * a stepped sequencer CV wants. Vital crossfades its wavetable changes
     * over about 7 ms for the same reason.
     *
     * Known wart: the limit runs in control space, so under Wrap a jump across
     * the seam travels the long way round. Seam-aware slew is an M2 job. */
    float slew_ms = 5.f;

    /* 0 smooth multilinear morph, 1 nearly hard stepping between cells. */
    float sharp = 0.f;

    /* Kepler mode: the position falls through a central potential rather than
     * being carried round at constant speed. The control frame becomes the
     * centre of attraction and the body orbits it, so a still hand still
     * moves — unevenly, which is the point. */
    Kepler kepler;

    void Init(const World* world, float sr)
    {
        world_ = world;
        sr_    = sr;
        L.Init(world, sr);
        R.Init(world, sr);
        rot.Init(world ? world->N() : 4);
        for(int a = 0; a < kMaxN; a++) { c_[a] = 0.5f; target_[a] = 0.5f; pc_[a] = 0.5f; payAt_[a] = 1e9f; }
        for(int j = 0; j < kMaxP; j++) payload_[j] = 0.f;
        stereo_ = false;
    }

    void SetControl(const float* c, int n)
    {
        for(int a = 0; a < n && a < kMaxN; a++) target_[a] = c[a];
    }

    /* Jump the control frame to its target, skipping the slew. For a preset
     * load or a space change, where a ramp would be a swoop, not a morph. */
    void SnapControl()
    {
        for(int a = 0; a < kMaxN; a++) c_[a] = target_[a];
    }
    void SetF0(float f0) { L.SetF0(f0); R.SetF0(f0); }

    /* Change worlds under a running voice. Build the World fully first. */
    void SetWorld(const World* w)
    {
        world_ = w;
        L.SetWorld(w);
        R.SetWorld(w);
    }

    /* Engine tunables, applied to both voices. */
    void SetGain(float g) { L.gain = g; R.gain = g; }
    void SetRenderDiv(int d)
    {
        L.render_div = d;
        R.render_div = d;
        L.render_phase = 0;
        R.render_phase = d > 1 ? d / 2 : 0;   /* keep the two transforms apart */
    }
    void SetRolloff(int b) { L.rolloff_bins = b; R.rolloff_bins = b; }

    void Process(float* outL, float* outR, int n)
    {
        if(!world_ || !world_->Ready()) { for(int i = 0; i < n; i++) { outL[i] = 0.f; outR[i] = 0.f; } return; }
        const int N = world_->N();
        /* rate-limit the control frame toward its target */
        if(slew_ms > 0.f && sr_ > 0.f)
        {
            const float block_ms = 1000.f * (float)n / sr_;
            const float step     = block_ms / slew_ms;    /* fraction of the range per block */
            for(int a = 0; a < N; a++)
            {
                const float d = target_[a] - c_[a];
                if(d > step) c_[a] += step;
                else if(d < -step) c_[a] -= step;
                else c_[a] = target_[a];
            }
        }
        else for(int a = 0; a < N; a++) c_[a] = target_[a];
        if(sr_ > 0.f)
        {
            const float dt = (float)n / sr_;
            rot.Advance(dt);
            if(kepler.Running())
            {
                /* Gravity should match the space it lives in: if the orbit
                 * plane's axes wrap, so does the attractor. Derived rather
                 * than switched, because a toroidal potential in a clamped
                 * space would pull toward an image that is not there. */
                int ki, kj;
                Rotation::PlaneAxes(N, kepler.plane, ki, kj);
                kepler.wrap = world_->TopoOf(ki) == Topo::Wrap || world_->TopoOf(kj) == Topo::Wrap;
                kepler.Step(dt);
                kepler.Apply(c_, N);      /* the control frame is the centre */
            }
        }
        rot.Update();
        if(rot.IsIdentity()) for(int a = 0; a < N; a++) pc_[a] = c_[a];
        else rot.Apply(c_, pc_, pivot);

        const bool stereo = spread != 0.f && N >= 2;
        if(!stereo)
        {
            L.sharp = sharp;
            L.SetPosition(pc_, N);
            L.Process(outL, n);
            for(int i = 0; i < n; i++) outR[i] = outL[i];
            for(int j = 0; j < world_->P(); j++) payload_[j] = L.Payload()[j];
            R.FollowPhase(L);   /* keep the idle ear in step */
        }
        else
        {
            if(!stereo_) R.AdoptFrame(L);   /* no step when δ leaves zero */
            int i, j;
            Rotation::PlaneAxes(N, spread_plane, i, j);
            float pl[kMaxN], pr[kMaxN];
            for(int a = 0; a < N; a++) { pl[a] = pc_[a]; pr[a] = pc_[a]; }
            Rotation::RotatePlane(i, j, -spread, pl, pivot);
            Rotation::RotatePlane(i, j, spread, pr, pivot);
            L.sharp = sharp;
            R.sharp = sharp;
            L.SetPosition(pl, N);
            R.SetPosition(pr, N);
            L.Process(outL, n);
            R.Process(outR, n);
            /* Payload at the centre, single-valued whichever backend answers.
             * Only when the centre has actually moved: deriving eight payload
             * numbers costs a whole spectrum evaluation, and doing that every
             * block was a third of the stereo cost for a signal that drives a
             * filter and a CV output and could not care less about 2 kHz. */
            bool moved = false;
            for(int a = 0; a < N; a++)
            {
                const float d = pc_[a] - payAt_[a];
                if(d > 1e-3f || d < -1e-3f) { moved = true; break; }
            }
            if(moved)
            {
                for(int a = 0; a < kMaxN; a++) payAt_[a] = pc_[a];
                float   pf[kMaxN], scratch[kMaxK];
                Weights w;
                world_->Fold(pc_, pf);
                world_->Evaluate(pf, sharp, scratch, payload_, w);
            }
        }
        stereo_ = stereo;
    }

    /* ── telemetry ──────────────────────────────────────────────────────── */
    const float* Control() const { return c_; }          /* after the slew */
    const float* ControlTarget() const { return target_; }
    const float* Centre() const { return pc_; }     /* rotated, unfolded */
    const float* Payload() const { return payload_; }
    bool         IsStereo() const { return stereo_; }
    const World* WorldPtr() const { return world_; }
    const Space* SpacePtr() const { return world_ ? world_->SpacePtr() : nullptr; }

    Engine   L, R;
    Rotation rot;

private:
    const World* world_ = nullptr;
    float        sr_    = 48000.f;
    float        c_[kMaxN], target_[kMaxN], pc_[kMaxN], payAt_[kMaxN];
    float        payload_[kMaxP];
    bool         stereo_ = false;
};

} // namespace kyk
