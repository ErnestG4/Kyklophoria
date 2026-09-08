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

namespace kyk {

class StereoEngine
{
public:
    float spread       = 0.f;    /* turns; 0 = mono */
    int   spread_plane = 0;      /* plane index, see Rotation::PlaneAxes */
    float pivot        = 0.5f;   /* rotation centre on every axis */

    void Init(const Space* space, float sr)
    {
        space_ = space;
        L.Init(space, sr);
        R.Init(space, sr);
        rot.Init(space ? space->N() : 4);
        for(int a = 0; a < kMaxN; a++) { c_[a] = 0.5f; pc_[a] = 0.5f; }
        for(int j = 0; j < kMaxP; j++) payload_[j] = 0.f;
        stereo_ = false;
    }

    void SetControl(const float* c, int n)
    {
        for(int a = 0; a < n && a < kMaxN; a++) c_[a] = c[a];
    }
    void SetF0(float f0) { L.SetF0(f0); R.SetF0(f0); }

    /* Engine tunables, applied to both voices. */
    void SetGain(float g) { L.gain = g; R.gain = g; }
    void SetRenderDiv(int d) { L.render_div = d; R.render_div = d; }
    void SetRolloff(int b) { L.rolloff_bins = b; R.rolloff_bins = b; }

    void Process(float* outL, float* outR, int n)
    {
        if(!space_) { for(int i = 0; i < n; i++) { outL[i] = 0.f; outR[i] = 0.f; } return; }
        const int N = space_->N();
        rot.Update();
        if(rot.IsIdentity()) for(int a = 0; a < N; a++) pc_[a] = c_[a];
        else rot.Apply(c_, pc_, pivot);

        const bool stereo = spread != 0.f && N >= 2;
        if(!stereo)
        {
            L.SetPosition(pc_, N);
            L.Process(outL, n);
            for(int i = 0; i < n; i++) outR[i] = outL[i];
            for(int j = 0; j < space_->P(); j++) payload_[j] = L.Payload()[j];
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
            L.SetPosition(pl, N);
            R.SetPosition(pr, N);
            L.Process(outL, n);
            R.Process(outR, n);
            /* payload at the centre */
            float   pf[kMaxN];
            Fold(*space_, pc_, pf);
            Weights w;
            LatticeWeights(*space_, pf, w);
            BlendPayload(*space_, w, payload_);
        }
        stereo_ = stereo;
    }

    /* ── telemetry ──────────────────────────────────────────────────────── */
    const float* Control() const { return c_; }
    const float* Centre() const { return pc_; }     /* rotated, unfolded */
    const float* Payload() const { return payload_; }
    bool         IsStereo() const { return stereo_; }
    const Space* SpacePtr() const { return space_; }

    Engine   L, R;
    Rotation rot;

private:
    static void BlendPayload(const Space& s, const Weights& wt, float* payload)
    {
        const int P = s.P();
        for(int j = 0; j < P; j++) payload[j] = 0.f;
        for(int c = 0; c < wt.n_corners; c++)
        {
            const float w = wt.w[c];
            if(w == 0.f) continue;
            const float* pl = s.Payload(wt.idx[c]);
            for(int j = 0; j < P; j++) payload[j] += w * pl[j];
        }
    }

    const Space* space_ = nullptr;
    float        c_[kMaxN], pc_[kMaxN];
    float        payload_[kMaxP];
    bool         stereo_ = false;
};

} // namespace kyk
