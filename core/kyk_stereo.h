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

/* The last thing before the codec: a stereo-linked peak limiter and a hard
 * clamp. Transparent below its ceiling — the gain is exactly 1 there, so
 * every world that fitted inside full scale renders bit for bit as it did
 * (the wavetable worlds' headroom is still the gain's job; see Engine). It
 * exists for what does not fit: a resonator struck at audio rate is a
 * hammer three hundred times a second, and the ring piles up past full
 * scale — 1.6 on the cello, measured with J4 and v/oct both at audio rate —
 * which the module turned into a scream "at very unsafe levels" (Combust).
 * Instant attack (the gain falls to the ceiling over the sample that would
 * cross it, so nothing overshoots), 80 ms release; a sample that is not a
 * number is silence. */
struct Limiter
{
    float g, rel, ceil;
    void Init(float sr) { g = 1.f; rel = 1.f - std::exp(-1.f / (0.08f * (sr > 0.f ? sr : 48000.f))); ceil = 0.98f; }
    void Process(float* l, float* r, int n)
    {
        for(int i = 0; i < n; i++)
        {
            float a = l[i], b = r[i];
            if(!(a == a) || !(b == b)) { l[i] = r[i] = 0.f; continue; }   /* NaN */
            const float fa = std::fabs(a), fb = std::fabs(b), pk = fa > fb ? fa : fb;
            if(g < 1.f) { g += (1.f - g) * rel; if(g > 1.f - 1e-6f) g = 1.f; }
            if(pk * g > ceil) g = ceil / pk;
            if(g < 1.f) { a *= g; b *= g; }
            l[i] = a > 1.f ? 1.f : a < -1.f ? -1.f : a;
            r[i] = b > 1.f ? 1.f : b < -1.f ? -1.f : b;
        }
    }
    float Reduction() const { return g; }   /* 1 = untouched */
};

class StereoEngine
{
public:
    float spread       = 0.f;    /* turns; 0 = mono */
    float listen_at    = 0.25f;  /* a resonator's ears: where along the string their centre sits (0..0.5; Bongs' Space page), an orbit on the stereo plane swinging it 0.2 either way */
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
#if KYK_RESONATOR
        L.LendVoices(&lvoices_);   /* the resonator is L's */
#endif
        L.Init(world, sr);
        R.Init(world, sr);
        rot.Init(world ? world->N() : 4);
        rot.BaseOff(world && world->IsResonate());   /* see SetWorld */
        for(int a = 0; a < kMaxN; a++) { c_[a] = 0.5f; target_[a] = 0.5f; pc_[a] = 0.5f; payAt_[a] = 1e9f; }
        for(int j = 0; j < kMaxP; j++) payload_[j] = 0.f;
        stereo_ = false;
        lim.Init(sr);
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
    /* a resonate world's strike: L's only. Its voices are L's and R copies
       them (Process below), so R's were built on every strike and never
       heard — a second whole voice build in the strike's block, which on
       the module doubled what a note-on cost. Every other kind ignores a
       strike, so there is nothing for R to do on any world. */
    void Strike(float velocity01, bool at_plan = false) { L.Strike(velocity01, at_plan); }
    void SetTune(Engine::Tune which, float v) { L.SetTune(which, v); R.SetTune(which, v); }
    void TuneFromControl(bool on) { L.TuneFromControl(on); R.TuneFromControl(on); }
    void SetPolyphony(int n) { L.SetPolyphony(n); R.SetPolyphony(n); }
    void SetExciter(const float* x, float gain) { L.SetExciter(x, gain); }   /* the voice is L's; R copies */
    void SetPitchLock(bool on) { L.SetPitchLock(on); R.SetPitchLock(on); }
    void SetVelocityTrack(float per_octave) { L.SetVelocityTrack(per_octave); R.SetVelocityTrack(per_octave); }
    void SetMemberMorph(bool on) { L.SetMemberMorph(on); R.SetMemberMorph(on); }
    void SetReleaseMs(float ms) { L.SetReleaseMs(ms); R.SetReleaseMs(ms); }

    /* Change worlds under a running voice. Build the World fully first. */
    void SetWorld(const World* w)
    {
        world_ = w;
        L.SetWorld(w);
        R.SetWorld(w);
        /* a resonate world's knobs go straight to its axes: body, velocity,
           decay and coil, pot plus CV. The Rotate page's angles are left
           out (Rotation::BaseOff) and an orbit's phase from a wavetable
           world is not carried in, so arriving is as straight as starting;
           an orbit or Kepler run on a resonator moves its axes as ever */
        const bool res = w && w->IsResonate();
        if(res && !rot.IsBaseOff()) rot.ResetOrbit();
        rot.BaseOff(res);
        /* The payload cache is keyed on the position, not the world, so a
         * switch with a still hand left it holding the previous world's
         * numbers — and the payload drives CV out A and the page's lanes, so
         * the module went on reporting a world it was no longer playing until
         * something moved. Measured: 0.50 of stale payload on an FM to Drum
         * switch, where the correct answer was to change by exactly that.
         *
         * Same shape as the phase bug directly above this one: derived state
         * that did not follow the world it was derived from. */
        for(int a = 0; a < kMaxN; a++) payAt_[a] = 1e9f;
    }

    /* Engine tunables, applied to both voices. */
    /* Both voices morph together: the stereo pair is one timbre read at two
     * points, and letting the ears disagree about which world they are in
     * would be a different instrument. */
    void SetMorph(const World* w, float amount) { L.SetMorph(w, amount); R.SetMorph(w, amount); }
    float Morph() const { return L.Morph(); }
    const World* MorphWorld() const { return L.MorphWorld(); }

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
        /* The frame the rotation reads: the control frame plus wherever the
         * falling body currently is. Built fresh every block and never written
         * back into c_.
         *
         * It used to add the body straight into c_, which is persistent state
         * that only the slew pulls back — and the slew is a rate limiter that
         * moves at most block_ms/slew_ms of the range per block, 0.1 at the
         * shipping default. So a fixed point existed only while the orbit
         * radius stayed under 0.1, and the radius knob goes to 0.55. Measured
         * at gravity 0.3, radius 0.30: ctl reached 85 after a quarter second
         * and -156 after four, with the position pinned in a cube corner
         * flickering between 0 and 1. Every position the module reported while
         * Kepler was running was noise, which is a thing you cannot see in a
         * desktop test that leaves the slew at zero. */
        for(int a = 0; a < N; a++) kc_[a] = c_[a];
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
                kepler.Apply(kc_, N);     /* the control frame is the attractor */
            }
        }
        rot.Update();
        if(rot.IsIdentity()) for(int a = 0; a < N; a++) pc_[a] = kc_[a];
        else rot.Apply(kc_, pc_, pivot);

        /* a resonate world's voices are L's and R copies, whatever the
           spread: a second set of voices would be twice the cost for a
           stereo image the pickup does not have */
        const bool stereo = spread != 0.f && N >= 2 && !world_->IsResonate();
        if(!stereo)
        {
            L.sharp = sharp;
            L.SetPosition(pc_, N);
            /* a resonator with a spread is heard from two points along its
               string, the pair's centre where the stereo plane's angle puts
               it — an orbit on that plane spins them — and the spread their
               distance and depth (Engine::SetListen). Without one it is mono,
               as it was: both ears the one output */
            const bool listen = world_->IsResonate() && spread > 0.f;
            if(listen)
            {
                float sn, cs; SinCosTurns(rot.Angle(spread_plane), sn, cs);
                L.SetListen(outR, spread / 0.1f, listen_at + 0.2f * sn);
            }
            L.Process(outL, n);
            if(!listen) for(int i = 0; i < n; i++) outR[i] = outL[i];
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
        /* a resonator sits 6 dB up: its peaks are a struck note's, 0.2 to
           0.4 at the wavetables' gain, some 10 dB under them ("a bit quiet
           usually compared to other modules" — Combust), and what a pile of
           strikes pushes past full scale the limiter now takes */
        if(world_->IsResonate()) for(int i = 0; i < n; i++) { outL[i] *= kResTrim; outR[i] *= kResTrim; }
        lim.Process(outL, outR, n);
    }

    /* ── telemetry ──────────────────────────────────────────────────────── */
    const float* Control() const { return c_; }          /* after the slew */
    const float* ControlTarget() const { return target_; }
    const float* Centre() const { return pc_; }     /* rotated, unfolded */
    const float* Payload() const { return payload_; }
    bool         IsStereo() const { return stereo_; }
    const World* WorldPtr() const { return world_; }
    const Space* SpacePtr() const { return world_ ? world_->SpacePtr() : nullptr; }

    EngineCore      L, R;
#if KYK_RESONATOR
    /* L's voices; R has none — it never plays a resonator (ResonatorVoices) */
    ResonatorVoices lvoices_;
#endif
    Rotation rot;

private:
    const World* world_ = nullptr;
    float        sr_    = 48000.f;
    float        c_[kMaxN], target_[kMaxN], pc_[kMaxN], payAt_[kMaxN];
    float        kc_[kMaxN] = {0,0,0,0,0,0};   /* c_ plus the falling body */
    float        payload_[kMaxP];
    bool         stereo_ = false;
    Limiter      lim;
public:
    static constexpr float kResTrim = 2.f;
    float LimiterGain() const { return lim.Reduction(); }
private:
};

} // namespace kyk
