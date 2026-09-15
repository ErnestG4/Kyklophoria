/* kyk_world.h — what the engine indexes, and the two ways of answering.
 *
 * A world answers one question: given a folded coordinate in [0,1]^N, what is
 * the spectrum and the payload there? There are two ways to answer it, and
 * which one a world uses is a property of the world, not of the engine.
 *
 *   Analytic   the world is a formula. The Braids eigenspace is a mean log
 *              spectrum plus N component vectors: about 1.3 KB describes the
 *              whole thing, and any coordinate — on a grid line, between two,
 *              mid-rotation — evaluates in closed form.
 *
 *   Lattice    the world is a table of samples, because it has no formula.
 *              A correlated random field is defined by its samples; so is an
 *              imported corpus. Multilinear interpolation between 2^N corners.
 *
 * The engine used to assume the second unconditionally, which meant baking
 * the eigenspace's formula out to 4096 grid points, discarding the formula,
 * and then interpolating between the samples to approximate what it had just
 * thrown away. Measured, that cost 1.18 MB per world, a pause to expand it,
 * and up to 4% error against the formula between grid points, in exchange for
 * saving 0.7 µs per block against a transform that costs several times that.
 *
 * So: analytic where a formula exists, tabulated where one does not.
 */
#pragma once
#include "kyk_interp.h"
#include "kyk_eigen_basis.h"
#include "kyk_fm.h"
#include "kyk_formant.h"
#include "kyk_shapes.h"
#include "kyk_lock.h"
#include "kyk_userworld.h"
#include "kyk_unison.h"
#include "kyk_modal.h"
#include "kyk_bend.h"

namespace kyk {

/* A mean log-spectrum plus N component vectors, each pre-scaled by its
 * standard deviation so a coordinate of 1 means one whitened deviation out. */
struct EigenBasis
{
    const float* mean;              /* [k] */
    const float* comp;              /* [n][k], row-major: comp[a*k + i] */
    int          n = 0, k = 0;
    float        extent = 1.f;      /* [0,1] on an axis maps to ±extent */
    float        floor_ = 0.f;      /* subtracted after exp; see kykeigen */
};

/* A set of waveforms placed at points in the space, weighted by distance.
 * Unlike every multiplicative family, distance couples all axes at once, so
 * this is the one legible construction that is not separable. */
struct VertexField
{
    const float* pos   = nullptr;   /* [count][kMaxN] */
    const float* spec  = nullptr;   /* [count][kMaxK] */
    int          count = 0, n = 0, k = 0;
    float        sigma = 0.3f;      /* lock tightness; small locks hard */
};

class World
{
public:
    enum class Kind : uint8_t { None = 0, Lattice = 1, Analytic = 2, Vertices = 3, Fm = 4, Formant = 5, Table = 6, Lock = 7, Unison = 8, Modal = 9, Bend = 10 };
    /* Which phase spectrum the engine should render this world's coefficients
     * against. This is not a detail — it decides whether the instrument can
     * produce a recognisable waveform at all.
     *
     *   Random   a fixed random phase per harmonic. Two spectra can be blended
     *            freely and the result never clicks, but a saw's magnitudes
     *            rendered at random phase are not a saw: measured, the best
     *            circular correlation against an ideal band-limited saw is
     *            0.79. The edges are gone. This is why the instrument has
     *            never locked onto a hard square or saw — it renders stacks of
     *            sines that happen to have the right spectrum.
     *
     *   Sine     every harmonic at a quarter turn. Saw, square, pulse and
     *            triangle are all odd-symmetric, so all of them are exact in
     *            this one basis: measured 1.0000 against the ideal, with a
     *            crest factor of 2.02, slightly *better* than the random
     *            phase we ship. Blending stays linear in the coefficients, so
     *            it is exactly as click-free as Random.
     *
     * The catch is that Sine only pays off with *signed* coefficients: a
     * triangle alternates sign and a pulse needs sin(h·pi·w), which goes
     * negative. Taking magnitudes puts a 25% pulse at 0.83 against the ideal.
     * Signed coefficients render correctly and keep blending linear, since a
     * negative coefficient is just a half-turn of phase. */
    /* Cosine is every harmonic at zero phase — the peaky one. It is offered
     * because it is a genuinely different waveform from the same spectrum, and
     * shape is what a folder or a ring modulator downstream actually chews on.
     * It costs headroom: see Engine::PhaseTrim. */
    enum class Phase : uint8_t { Random = 0, Sine = 1, Cosine = 2 };
    static constexpr int kMaxVerts = 32;

    void UseLattice(const Space* s)
    {
        kind_  = s && s->Attached() ? Kind::Lattice : Kind::None;
        space_ = s;
    }
    void UseAnalytic(const EigenBasis& b, int p, const uint8_t* topo)
    {
        kind_  = (b.n >= 1 && b.n <= kMaxN && b.k >= 1 && b.k <= kMaxK) ? Kind::Analytic : Kind::None;
        basis_ = b;
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    void UseVertices(const VertexField& v, int p, const uint8_t* topo)
    {
        kind_ = (v.count > 0 && v.count <= kMaxVerts && v.n >= 1 && v.n <= kMaxN
                 && v.k >= 1 && v.k <= kMaxK) ? Kind::Vertices : Kind::None;
        verts_ = v;
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    void UseFm(const FmField& f, int p, const uint8_t* topo)
    {
        kind_ = (f.n >= 1 && f.n <= kMaxN && f.k >= 1 && f.k <= kMaxK) ? Kind::Fm : Kind::None;
        fm_   = f;
        p_    = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    void UseFormant(const FormantField& f, int p, const uint8_t* topo)
    {
        kind_ = (f.n >= 1 && f.n <= kMaxN && f.k >= 1 && f.k <= kMaxK) ? Kind::Formant : Kind::None;
        form_ = f;
        p_    = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    /* Takes the parameters rather than a whole ShapeField, and builds the node
     * table straight into its own copy. A ShapeField carries 4 KB of node
     * table, so passing one by value would put that on the caller's stack and
     * keeping one as a file static would add a guarded initialiser — neither
     * belongs on a control thread that also has to serve HostLink. */
    void UseShapes(int n, int k, Shaper a2, Shaper a3, float pulse_min,
                   int p, const uint8_t* topo)
    {
        kind_ = (n >= 1 && n <= kMaxN && k >= 1 && k <= kShapeK) ? Kind::Table : Kind::None;
        if(kind_ == Kind::None) return;
        shapes_.cols = 4;
        shapes_.rows = 4;
        shapes_.n = n;
        shapes_.k = k;
        shapes_.axis2 = a2;
        shapes_.axis3 = a3;
        shapes_.pulse_min = pulse_min;
        detail::BuildShapeNodes(shapes_, nodes_);
        phase_ = Phase::Sine;       /* the entire point: real waveforms */
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    /* Real waveforms on the 24-cell's vertices, one family per Givens plane. */
    void UseLock(int n, int k, float sigma, int p, const uint8_t* topo)
    {
        kind_ = (n >= 1 && n <= kMaxN && k >= 1 && k <= kShapeK) ? Kind::Lock : Kind::None;
        if(kind_ == Kind::None) return;
        lock_ = LockField();
        lock_.n = n; lock_.k = k; lock_.sigma = sigma;
        detail::BuildLockNodes(lock_, nodes_);
        phase_ = Phase::Sine;
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    /* A world somebody else wrote.
     *
     * Deliberately Kind::Lock and not a kind of its own. The evaluation, the
     * basin narrowing under Morph, the click-freedom and the continuity
     * guarantee are all properties of the Lock path, and a parallel path would
     * be a second place for all four to go wrong. A user world differs from
     * the built-in twenty-four-cell only in where its numbers came from, so
     * that is the only thing that differs in the code.
     *
     * Returns the parse error; on anything but Ok the world is left None
     * rather than half-loaded, so a corrupt file is silent and not a fault. */
    UserError UseUserWorld(const uint8_t* blob, size_t len, int p, const uint8_t* topo,
                           char* name_out = nullptr)
    {
        LockField f;
        const UserError e = ParseUserWorld(blob, len, f, nodes_, name_out);
        if(e != UserError::Ok) { kind_ = Kind::None; return e; }
        lock_  = f;
        kind_  = Kind::Lock;
        phase_ = (blob[9] & 1u) ? Phase::Sine : Phase::Random;
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
        return UserError::Ok;
    }

    /* Serialise whatever Lock-shaped world is loaded, so a built-in can be
     * exported, edited and loaded back as a starting point. */
    size_t SaveUserWorld(const char* name, uint8_t* out, size_t cap) const
    {
        if(kind_ != Kind::Lock) return 0;
        return WriteUserWorld(lock_, nodes_, name, out, cap, phase_ == Phase::Sine);
    }

    void UseUnison(int n, int k, int p, const uint8_t* topo)
    {
        kind_ = (n >= 1 && n <= kMaxN && k >= 1 && k <= kShapeK) ? Kind::Unison : Kind::None;
        if(kind_ == Kind::None) return;
        uni_ = UnisonField();
        uni_.n = n; uni_.k = k;
        phase_ = Phase::Sine;
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    void UseModal(Body b, int n, int k, int p, const uint8_t* topo)
    {
        kind_ = (n >= 1 && n <= kMaxN && k >= 1 && k <= kShapeK) ? Kind::Modal : Kind::None;
        if(kind_ == Kind::None) return;
        modal_ = ModalField();
        modal_.body = b; modal_.n = n; modal_.k = k;
        /* The one-dimensional bodies stretch their ratio set rather than
         * reshaping a grid, so they need a wider reach to spread modes up the
         * spectrum at all — a drum's modes are packed into two octaves and
         * without this the world cannot be bright anywhere. */
        if(b == Body::Bar)  { modal_.geom_min = 0.62f; modal_.geom_max = 1.24f; }
        if(b == Body::Drum) { modal_.geom_min = 0.50f; modal_.geom_max = 3.20f; }
        phase_ = Phase::Sine;
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    void UseBend(Base b, int n, int k, int p, const uint8_t* topo)
    {
        kind_ = (n >= 1 && n <= kMaxN && k >= 1 && k <= kShapeK) ? Kind::Bend : Kind::None;
        if(kind_ == Kind::None) return;
        bend_ = BendField();
        bend_.base = b; bend_.n = n; bend_.k = k;
        phase_ = Phase::Sine;
        p_     = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    Kind Which() const { return kind_; }
    bool Ready() const { return kind_ != Kind::None; }
    /* A switch, not a ternary chain. This was seven levels of nested `?:`
     * and adding a world to it silently missed — the new kind fell through to
     * the eigenbasis and reported zero dimensions, which the engine reads as a
     * band limit of zero, which is silence. A switch makes the compiler
     * complain instead. */
    int N() const
    {
        switch(kind_)
        {
            case Kind::Lattice:  return space_->N();
            case Kind::Vertices: return verts_.n;
            case Kind::Fm:       return fm_.n;
            case Kind::Formant:  return form_.n;
            case Kind::Table:    return shapes_.n;
            case Kind::Lock:     return lock_.n;
            case Kind::Unison:   return uni_.n;
            case Kind::Modal:    return modal_.n;
            case Kind::Bend:     return bend_.n;
            case Kind::Analytic: return basis_.n;
            default:             return 0;
        }
    }
    int K() const
    {
        switch(kind_)
        {
            case Kind::Lattice:  return space_->K();
            case Kind::Vertices: return verts_.k;
            case Kind::Fm:       return fm_.k;
            case Kind::Formant:  return form_.k;
            case Kind::Table:    return shapes_.k;
            case Kind::Lock:     return lock_.k;
            case Kind::Unison:   return uni_.k;
            case Kind::Modal:    return modal_.k;
            case Kind::Bend:     return bend_.k;
            case Kind::Analytic: return basis_.k;
            default:             return 0;
        }
    }
    const VertexField& Verts() const { return verts_; }
    int  P() const { return kind_ == Kind::Lattice ? space_->P() : p_; }
    Topo TopoOf(int a) const { return kind_ == Kind::Lattice ? space_->TopoOf(a) : (Topo)topo_[a]; }
    uint32_t PhaseSeed() const { return kind_ == Kind::Lattice ? space_->Header().phase_seed : 1u; }
    Phase    PhaseMode() const { return phase_; }
    void     SetPhase(Phase p) { phase_ = p; }
    const Space*      SpacePtr() const { return kind_ == Kind::Lattice ? space_ : nullptr; }
    const EigenBasis&   Basis() const { return basis_; }
    const FmField&      Fm() const { return fm_; }
    const FormantField& Formant() const { return form_; }
    const ShapeField&   Shapes() const { return shapes_; }
    const LockField&    Lock() const { return lock_; }
    const UnisonField&  Unison() const { return uni_; }
    const ModalField&   Modal() const { return modal_; }
    const BendField&    Bend() const { return bend_; }

    /* ── the frame shapers ───────────────────────────────────────────────
     * A wavefolder has no closed form in the harmonics, so these run on the
     * rendered single cycle. The engine calls Shape() straight after the
     * transform; BandScale() tells it how much to pull the band limit in
     * first, because a memoryless nonlinearity multiplies bandwidth and the
     * frame arrives band-limited to exactly Nyquist. */
    bool HasShaper() const
    {
        return kind_ == Kind::Table
               || (kind_ == Kind::Lock && (lock_.fx[0] != Shaper::None || lock_.fx[1] != Shaper::None));
    }
    /* Which shapers this world wants on the rendered cycle, and how deep at
     * this position. A shape table puts them on axes 2 and 3 by construction;
     * a world somebody wrote names its own axes in the file
     * (core/kyk_userworld.h). One list, so the stage below and the headroom
     * rule cannot disagree about what is running.
     *
     * Returns every *declared* shaper including ones at zero depth, because the
     * band limit has to move continuously through zero — filtering here is the
     * caller's job and only Shape() wants it. */
    int Fx(const float* p01, Shaper* s, float* d) const
    {
        int m = 0;
        if(kind_ == Kind::Table)
        {
            const Shaper ax[2] = {shapes_.axis2, shapes_.axis3};
            for(int q = 0; q < 2; q++)
            {
                if(ax[q] == Shaper::None) continue;
                s[m] = ax[q];
                d[m] = shapes_.n > 2 + q ? p01[2 + q] : 0.f;
                m++;
            }
        }
        else if(kind_ == Kind::Lock)
        {
            for(int q = 0; q < 2; q++)
            {
                if(lock_.fx[q] == Shaper::None) continue;
                const int a = lock_.fx_axis[q];
                s[m] = lock_.fx[q];
                d[m] = a < lock_.n ? p01[a] : 0.f;
                m++;
            }
        }
        return m;
    }
    /* The declaration without a position, for whoever writes the world down:
     * effect q is this shaper on this axis. A shape table's two live on axes 2
     * and 3 by construction, which is why it has no axis field to read. */
    Shaper FxShaper(int q) const
    {
        if(q < 0 || q > 1) return Shaper::None;
        if(kind_ == Kind::Table) return q ? shapes_.axis3 : shapes_.axis2;
        if(kind_ == Kind::Lock) return lock_.fx[q];
        return Shaper::None;
    }
    uint8_t FxAxis(int q) const
    {
        if(q < 0 || q > 1) return 0u;
        if(kind_ == Kind::Table) return (uint8_t)(2 + q);
        if(kind_ == Kind::Lock) return lock_.fx_axis[q];
        return 0u;
    }
    float BandScale(const float* p01) const
    {
        Shaper s[2]; float d[2];
        const int m  = Fx(p01, s, d);
        float     sc = 1.f;
        for(int q = 0; q < m; q++) sc += ShaperBandScale(s[q], d[q]);
        return sc;
    }
    /* The frame is rendered into `scratch`; this writes the finished cycle
     * into `dst`. Staging it that way rather than shaping in place means the
     * out-of-order shaper gets a separate source for nothing, so no stage ever
     * copies a buffer — 1024 floats per render that used to be spent moving
     * memory around. */
    void Shape(float* dst, float* scratch, int n, const float* p01) const
    {
        Shaper sd[2]; float dd[2];
        const int  decl = Fx(p01, sd, dd);
        Shaper s[2]; float d[2]; int m = 0;
        for(int q = 0; q < decl; q++)
            if(dd[q] > 1e-4f) { s[m] = sd[q]; d[m] = dd[q]; m++; }
        if(m == 0) { CopyFrame(dst, scratch, n); return; }
        if(m == 1) { Apply(s[0], dst, scratch, n, d[0]); return; }
        if(s[0] != Shaper::Warp)
        {
            Apply(s[0], scratch, scratch, n, d[0]);      /* in place */
            Apply(s[1], dst, scratch, n, d[1]);
            return;
        }
        Apply(s[0], dst, scratch, n, d[0]);              /* warp cannot be in place */
        if(s[1] != Shaper::Warp) { Apply(s[1], dst, dst, n, d[1]); return; }
        CopyFrame(scratch, dst, n);
        Apply(s[1], dst, scratch, n, d[1]);
    }

    void Fold(const float* c, float* p) const
    {
        for(int a = 0; a < N(); a++) p[a] = FoldAxis(c[a], TopoOf(a));
    }

    /* p01: folded, each axis in [0,1]. `sharp` only means anything to the
     * lattice backend, where it biases the interpolation toward the nearest
     * corner; an analytic world has no corners to lean on. `wt` is filled for
     * the lattice backend so telemetry can show the contributing cells, and
     * left empty otherwise. */
    void Evaluate(const float* p01, float sharp, float* mags, float* payload, Weights& wt) const
    {
        if(kind_ == Kind::Lattice)
        {
            LatticeWeights(*space_, p01, wt);
            SharpenWeights(wt, sharp);
            Blend(*space_, wt, mags, payload);
            return;
        }
        wt.n_corners = 0;
        if(kind_ == Kind::Analytic) { EvalAnalytic(p01, mags, payload); return; }
        if(kind_ == Kind::Vertices) { EvalVertices(p01, sharp, mags, payload); return; }
        if(kind_ == Kind::Fm) { EvalFm(p01, mags, payload); return; }
        if(kind_ == Kind::Formant) { EvalFormant(p01, mags, payload); return; }
        if(kind_ == Kind::Table) { EvalShapes(p01, sharp, mags, payload); return; }
        if(kind_ == Kind::Lock) { EvalLock(p01, sharp, mags, payload); return; }
        if(kind_ == Kind::Unison) { EvalUnison(p01, mags, payload); return; }
        if(kind_ == Kind::Modal) { EvalModal(p01, mags, payload); return; }
        if(kind_ == Kind::Bend) { EvalBend(p01, mags, payload); return; }
        for(int k = 0; k < kMaxK; k++) mags[k] = 0.f;
    }

private:
    void EvalAnalytic(const float* p01, float* mags, float* payload) const
    {
        const int n = basis_.n, k = basis_.k;
        float     coord[kMaxN];
        for(int a = 0; a < n; a++) coord[a] = (2.f * p01[a] - 1.f) * basis_.extent;

        float sum = 0.f, sum2 = 0.f, kw = 0.f, hf = 0.f;
        for(int i = 0; i < k; i++)
        {
            float y = basis_.mean[i];
            for(int a = 0; a < n; a++) y += coord[a] * basis_.comp[(size_t)a * k + i];
            float v = detail::Exp(y) - basis_.floor_;
            if(v < 0.f) v = 0.f;
            mags[i] = v;
            const float p2 = v * v;
            sum += v;
            sum2 += p2;
            kw += (float)(i + 1) * p2;
            if(i >= k / 4) hf += p2;
        }
        const float g = sum2 > 0.f ? Sqrt(2.f) / Sqrt(sum2) : 0.f;
        for(int i = 0; i < k; i++) mags[i] *= g;

        /* Payload measured from the spectrum, so the filter and drive follow
         * the wave. Flatness is (Σm)² / (K·Σm²), which is 1 for a flat
         * spectrum and 1/K for a single partial — the same shape as the
         * geometric-over-arithmetic definition without K logarithms per
         * block. */
        const float centroid = sum2 > 0.f ? (kw / sum2 - 1.f) / (float)(k - 1) : 0.f;
        const float bright   = sum2 > 0.f ? hf / sum2 : 0.f;
        const float flat     = sum2 > 0.f ? (sum * sum) / ((float)k * sum2) : 0.f;
        float       rad      = 0.f;
        for(int a = 0; a < n; a++) rad += coord[a] * coord[a];
        rad = Sqrt(rad) / (basis_.extent * Sqrt((float)n));
        auto sat = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };
        const float pl[kMaxP] = {
            sat(Sqrt(centroid)), sat(1.f - flat), sat(bright), sat(0.3f + 0.7f * bright),
            sat(rad), sat(0.5f + 0.5f * coord[0] / basis_.extent), sat(centroid), sat(flat),
        };
        for(int j = 0; j < p_; j++) payload[j] = pl[j];
    }

    /* The four axes map straight onto the four FM controls. `sharp` is not
     * used: there is nothing discrete here to lean toward, the same reason
     * the eigenspace ignores it. */
    void EvalFm(const float* p01, float* mags, float* payload) const
    {
        const int     k = fm_.k;
        const FmPoint q = FmAt(fm_, p01);
        FmSpectrum(q.index, q.ratio, q.carrier, q.second, k, mags);

        float acc = 0.f, lin = 0.f, kw = 0.f, hf = 0.f;
        for(int i = 0; i < k; i++)
        {
            const float p2 = mags[i] * mags[i];
            acc += p2; lin += mags[i];
            kw += (float)(i + 1) * p2;
            if(i >= k / 4) hf += p2;
        }
        const float g = acc > 0.f ? Sqrt(2.f) / Sqrt(acc) : 0.f;
        for(int i = 0; i < k; i++) mags[i] *= g;

        const float centroid = acc > 0.f ? (kw / acc - 1.f) / (float)(k - 1) : 0.f;
        const float bright   = acc > 0.f ? hf / acc : 0.f;
        const float flat     = acc > 0.f ? (lin * lin) / ((float)k * acc) : 0.f;
        auto        sat      = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };
        /* How near this ratio is to a whole number, 1 on it and 0 halfway
         * between: the CV out fires when the spectrum turns harmonic, which
         * is the event you can hear. */
        const float fr = q.ratio - (float)(int)q.ratio;
        const float har = 1.f - 2.f * (fr < 0.5f ? fr : 1.f - fr);
        const float pl[kMaxP] = {
            sat(Sqrt(centroid)), sat(1.f - flat), sat(bright), sat(0.3f + 0.7f * bright),
            sat(har), sat(q.index / (fm_.index_max > 0.f ? fm_.index_max : 1.f)),
            sat(centroid), sat(flat),
        };
        for(int j = 0; j < p_; j++) payload[j] = pl[j];
    }

    void EvalFormant(const float* p01, float* mags, float* payload) const
    {
        const int          k = form_.k;
        const FormantPoint q = FormantAt(form_, p01);
        FormantSpectrum(form_, q.f1, q.r2, q.r3, q.q, k, mags);

        float acc = 0.f, lin = 0.f, kw = 0.f, hf = 0.f;
        for(int i = 0; i < k; i++)
        {
            const float p2 = mags[i] * mags[i];
            acc += p2; lin += mags[i];
            kw += (float)(i + 1) * p2;
            if(i >= k / 4) hf += p2;
        }
        const float g = acc > 0.f ? Sqrt(2.f) / Sqrt(acc) : 0.f;
        for(int i = 0; i < k; i++) mags[i] *= g;

        const float centroid = acc > 0.f ? (kw / acc - 1.f) / (float)(k - 1) : 0.f;
        const float bright   = acc > 0.f ? hf / acc : 0.f;
        const float flat     = acc > 0.f ? (lin * lin) / ((float)k * acc) : 0.f;
        auto        sat      = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };
        /* lane 4 is how narrow the peaks are, which is the one control that
         * turns this from a tone colour into a resonance you can hear ring */
        const float narrow = form_.q_max > form_.q_min
                             ? (form_.q_max - q.q) / (form_.q_max - form_.q_min) : 0.f;
        const float pl[kMaxP] = {
            sat(Sqrt(centroid)), sat(1.f - flat), sat(bright), sat(0.3f + 0.7f * bright),
            sat(narrow), sat(q.r2 / (form_.r2_max > 0.f ? form_.r2_max : 1.f)),
            sat(centroid), sat(flat),
        };
        for(int j = 0; j < p_; j++) payload[j] = pl[j];
    }

    /* Softmax over minus the squared distance to each vertex, blending the
     * stored spectra. Same weighting as the older vertex worlds; the
     * difference is entirely that these spectra are signed and get rendered at
     * sine phase, so a vertex is the waveform rather than its spectrum. */
    void EvalLock(const float* p01, float sharp, float* mags, float* payload) const
    {
        const int n = lock_.n, k = lock_.k, m = lock_.count;
        if(sharp < 0.f) sharp = 0.f;
        if(sharp > 1.f) sharp = 1.f;
        const float sigma  = lock_.sigma * (1.f - 0.7f * sharp);
        const float inv2s2 = 1.f / (2.f * sigma * sigma);
        float       w[kWorldNodes];
        float       best = -1e30f;
        for(int v = 0; v < m; v++)
        {
            float d2 = 0.f;
            for(int a = 0; a < n; a++)
            {
                const float dd = p01[a] - lock_.pos[v][a];
                d2 += dd * dd;
            }
            w[v] = -d2 * inv2s2;
            if(w[v] > best) best = w[v];
        }
        float sum = 0.f;
        for(int v = 0; v < m; v++) { w[v] = detail::Exp(w[v] - best); sum += w[v]; }
        const float inv = sum > 0.f ? 1.f / sum : 0.f;
        for(int i = 0; i < k; i++) mags[i] = 0.f;
        float top = 0.f;
        for(int v = 0; v < m; v++)
        {
            const float wv = w[v] * inv;
            if(wv > top) top = wv;
            if(wv < 1e-4f) continue;
            const float* sv = nodes_[v];
            for(int i = 0; i < k; i++) mags[i] += wv * sv[i];
        }
        Finish(mags, k, top, 1.f - top, payload);
    }

    void EvalBend(const float* p01, float* mags, float* payload) const
    {
        const int k = bend_.k;
        BendSpectrum(bend_, p01[0], p01[1],
                     bend_.n > 2 ? p01[2] : 0.5f,
                     bend_.n > 3 ? p01[3] : 1.f, k, mags);
        /* lane 4 is how far up the fold point sits, lane 5 the first axis */
        Finish(mags, k, bend_.n > 3 ? p01[3] : 1.f, p01[0], payload);
    }

    void EvalModal(const float* p01, float* mags, float* payload) const
    {
        const ModalPoint q = ModalAt(modal_, p01);
        ModalSpectrum(modal_, q.strike, q.geom, q.time, q.damp, modal_.k, mags);
        /* lane 4 counts down as the strike decays, lane 5 is the geometry */
        Finish(mags, modal_.k, 1.f - q.time, q.geom, payload);
    }

    void EvalUnison(const float* p01, float* mags, float* payload) const
    {
        const UnisonPoint q = UnisonAt(uni_, p01);
        UnisonSpectrum(uni_, q.voices, q.span, q.detune, q.wave, uni_.k, mags);
        /* how near the roots are to whole numbers, which is how harmonic the
         * stack currently is — the CV out fires when it locks up */
        const float off = q.detune - (float)(int)q.detune;
        Finish(mags, uni_.k, 1.f - 2.f * (off < 0.5f ? off : 1.f - off),
               q.voices / uni_.voices_max, payload);
    }

    /* Normalise and fill the payload lanes. Shared by the worlds whose
     * coefficients are signed, so the flatness sum takes magnitudes. */
    void Finish(float* mags, int k, float lane4, float lane5, float* payload) const
    {
        float acc = 0.f, lin = 0.f, kw = 0.f, hf = 0.f;
        for(int i = 0; i < k; i++)
        {
            const float a  = mags[i] < 0.f ? -mags[i] : mags[i];
            const float p2 = mags[i] * mags[i];
            acc += p2; lin += a;
            kw += (float)(i + 1) * p2;
            if(i >= k / 4) hf += p2;
        }
        const float g = acc > 0.f ? Sqrt(2.f) / Sqrt(acc) : 0.f;
        for(int i = 0; i < k; i++) mags[i] *= g;
        const float centroid = acc > 0.f ? (kw / acc - 1.f) / (float)(k - 1) : 0.f;
        const float bright   = acc > 0.f ? hf / acc : 0.f;
        const float flat     = acc > 0.f ? (lin * lin) / ((float)k * acc) : 0.f;
        auto        sat      = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };
        const float pl[kMaxP] = {
            sat(Sqrt(centroid)), sat(1.f - flat), sat(bright), sat(0.3f + 0.7f * bright),
            sat(lane4), sat(lane5), sat(centroid), sat(flat),
        };
        for(int j = 0; j < p_; j++) payload[j] = pl[j];
    }

    static void Apply(Shaper s, float* dst, const float* src, int n, float d)
    {
        switch(s)
        {
            case Shaper::Fold: FoldFrame(dst, src, n, d); break;
            case Shaper::Ring: RingFrame(dst, src, n, d); break;
            case Shaper::Warp: WarpFrame(dst, src, n, d); break;
            case Shaper::Crush: CrushFrame(dst, src, n, d); break;
            case Shaper::Drop: DropFrame(dst, src, n, d); break;
            default: CopyFrame(dst, src, n); break;
        }
    }

    /* Bilinear over a grid of real waveforms. `sharp` biases each fractional
     * coordinate toward the nearer node, so the Morph knob decides how much of
     * the travel you spend sitting on a recognisable shape rather than between
     * two of them. Coefficients are signed and rendered at sine phase, which
     * is what makes a node an actual square rather than a spectrum that
     * measures like one. */
    void EvalShapes(const float* p01, float sharp, float* mags, float* payload) const
    {
        const int k = shapes_.k, C = shapes_.cols, R = shapes_.rows;
        if(sharp < 0.f) sharp = 0.f;
        if(sharp > 1.f) sharp = 1.f;
        const float gx = p01[0] * (float)(C - 1), gy = p01[1] * (float)(R - 1);
        int   c0 = (int)gx, r0 = (int)gy;
        if(c0 > C - 2) c0 = C - 2;
        if(c0 < 0) c0 = 0;
        if(r0 > R - 2) r0 = R - 2;
        if(r0 < 0) r0 = 0;
        const float u = detail::SnapTo(gx - (float)c0, sharp);
        const float v = detail::SnapTo(gy - (float)r0, sharp);

        /* The four corners are already built; this is a bilinear blend of
         * stored vectors and nothing else. */
        const float w[4] = {(1.f - u) * (1.f - v), u * (1.f - v), (1.f - u) * v, u * v};
        const int   id[4] = {r0 * C + c0, r0 * C + c0 + 1, (r0 + 1) * C + c0, (r0 + 1) * C + c0 + 1};
        for(int i = 0; i < k; i++) mags[i] = 0.f;
        for(int q = 0; q < 4; q++)
        {
            if(w[q] <= 0.f) continue;
            const float* node = nodes_[id[q]];
            for(int i = 0; i < k; i++) mags[i] += w[q] * node[i];
        }

        float acc = 0.f, lin = 0.f, kw = 0.f, hf = 0.f;
        for(int i = 0; i < k; i++)
        {
            const float a  = mags[i] < 0.f ? -mags[i] : mags[i];   /* signed now */
            const float p2 = mags[i] * mags[i];
            acc += p2; lin += a;
            kw += (float)(i + 1) * p2;
            if(i >= k / 4) hf += p2;
        }
        const float g = acc > 0.f ? Sqrt(2.f) / Sqrt(acc) : 0.f;
        for(int i = 0; i < k; i++) mags[i] *= g;

        const float centroid = acc > 0.f ? (kw / acc - 1.f) / (float)(k - 1) : 0.f;
        const float bright   = acc > 0.f ? hf / acc : 0.f;
        const float flat     = acc > 0.f ? (lin * lin) / ((float)k * acc) : 0.f;
        auto        sat      = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };
        /* lane 4 is how near a node we are, so the CV out fires when the
         * waveform locks onto a shape you can name */
        const float du = u < 0.5f ? u : 1.f - u, dv = v < 0.5f ? v : 1.f - v;
        const float onnode = 1.f - 2.f * (du > dv ? du : dv);
        const float pl[kMaxP] = {
            sat(Sqrt(centroid)), sat(1.f - flat), sat(bright), sat(0.3f + 0.7f * bright),
            sat(onnode), sat(shapes_.n > 2 ? p01[2] : 0.f), sat(centroid), sat(flat),
        };
        for(int j = 0; j < p_; j++) payload[j] = pl[j];
    }

    /* Softmax over minus the squared distance to each vertex: the Gaussian
     * mixture weight. At a vertex one term dominates and you hear that
     * waveform; step off and the neighbours crowd in. `sigma` decides how
     * abruptly the first becomes the second. */
    void EvalVertices(const float* p01, float sharp, float* mags, float* payload) const
    {
        const int n = verts_.n, k = verts_.k, m = verts_.count;
        /* `sharp` means the same thing here as it does to the lattice: how
         * discrete is this space. Tightening sigma narrows each vertex's
         * basin, so the waveforms lock harder and the ground between them
         * gets murkier. Measured on the 24-cell at K=64: the fraction of the
         * space sitting within 0.05 of a vertex waveform runs 0.3% at sigma
         * 0.20, 35% at 0.12 and 81% at 0.06, while the largest spectral step
         * along a path stays under 0.02 down to sigma 0.10 and only starts
         * reading as switching below that. */
        if(sharp < 0.f) sharp = 0.f;
        if(sharp > 1.f) sharp = 1.f;
        const float sigma  = verts_.sigma * (1.f - 0.7f * sharp);
        const float inv2s2 = 1.f / (2.f * sigma * sigma);
        float       w[kMaxVerts];
        float       best = -1e30f;
        for(int v = 0; v < m; v++)
        {
            float d2 = 0.f;
            for(int a = 0; a < n; a++)
            {
                const float dd = p01[a] - verts_.pos[(size_t)v * kMaxN + a];
                d2 += dd * dd;
            }
            w[v] = -d2 * inv2s2;
            if(w[v] > best) best = w[v];
        }
        float sum = 0.f;
        for(int v = 0; v < m; v++) { w[v] = detail::Exp(w[v] - best); sum += w[v]; }
        const float inv = sum > 0.f ? 1.f / sum : 0.f;

        for(int i = 0; i < k; i++) mags[i] = 0.f;
        float top = 0.f;
        for(int v = 0; v < m; v++)
        {
            const float wv = w[v] * inv;
            if(wv > top) top = wv;                   /* how locked are we? */
            if(wv < 1e-4f) continue;
            const float* sv = verts_.spec + (size_t)v * kMaxK;
            for(int i = 0; i < k; i++) mags[i] += wv * sv[i];
        }
        float acc = 0.f, kw = 0.f, hf = 0.f, lin = 0.f;
        for(int i = 0; i < k; i++)
        {
            const float p2 = mags[i] * mags[i];
            acc += p2;
            lin += mags[i];
            kw += (float)(i + 1) * p2;
            if(i >= k / 4) hf += p2;
        }
        const float g = acc > 0.f ? Sqrt(2.f) / Sqrt(acc) : 0.f;
        for(int i = 0; i < k; i++) mags[i] *= g;

        const float centroid = acc > 0.f ? (kw / acc - 1.f) / (float)(k - 1) : 0.f;
        const float bright   = acc > 0.f ? hf / acc : 0.f;
        const float flat     = acc > 0.f ? (lin * lin) / ((float)k * acc) : 0.f;
        auto        sat      = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };
        /* lane 4 is proximity to a vertex, so the CV out fires when the
         * waveform locks — the event you can hear */
        const float pl[kMaxP] = {
            sat(Sqrt(centroid)), sat(1.f - flat), sat(bright), sat(0.3f + 0.7f * bright),
            sat(top), sat(1.f - top), sat(centroid), sat(flat),
        };
        for(int j = 0; j < p_; j++) payload[j] = pl[j];
    }

    Kind         kind_  = Kind::None;
    const Space* space_ = nullptr;
    EigenBasis   basis_;
    VertexField  verts_;
    FmField      fm_;
    FormantField form_;
    ShapeField   shapes_;
    LockField    lock_;
    UnisonField  uni_;
    ModalField   modal_;
    BendField    bend_;
    /* One node buffer, lent to whichever table-shaped world is live. 24 rows
     * because that is the 24-cell's vertex count; a shape table uses 16 of
     * them. Six kilobytes, in DTCM with the World. */
    float        nodes_[kWorldNodes][kShapeK];
    int          p_ = 0;
    Phase        phase_ = Phase::Random;
    uint8_t      topo_[kMaxN] = {0, 0, 0, 0, 0, 0};
};

/* The eigenspace baked into the firmware by tools/kykeigen. */
inline EigenBasis BraidsBasis()
{
    EigenBasis b;
    b.mean   = worlds::kEigenMean;
    b.comp   = &worlds::kEigenComp[0][0];
    b.n      = worlds::kEigenN;
    b.k      = worlds::kEigenK;
    b.extent = worlds::kEigenExtent;
    b.floor_ = worlds::kEigenFloor;
    return b;
}

} // namespace kyk
