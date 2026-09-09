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
    enum class Kind : uint8_t { None = 0, Lattice = 1, Analytic = 2, Vertices = 3, Fm = 4, Formant = 5, Table = 6 };
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
    enum class Phase : uint8_t { Random = 0, Sine = 1 };
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

    void UseShapes(const ShapeField& f, int p, const uint8_t* topo)
    {
        kind_   = (f.n >= 1 && f.n <= kMaxN && f.k >= 1 && f.k <= kMaxK
                   && f.cols >= 2 && f.rows >= 2) ? Kind::Table : Kind::None;
        shapes_ = f;
        phase_  = Phase::Sine;      /* the entire point: real waveforms */
        p_      = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
        for(int a = 0; a < kMaxN; a++) topo_[a] = topo ? topo[a] : 0u;
    }

    Kind Which() const { return kind_; }
    bool Ready() const { return kind_ != Kind::None; }
    int  N() const { return kind_ == Kind::Lattice ? space_->N() : (kind_ == Kind::Vertices ? verts_.n : (kind_ == Kind::Fm ? fm_.n : (kind_ == Kind::Formant ? form_.n : (kind_ == Kind::Table ? shapes_.n : basis_.n)))); }
    int  K() const { return kind_ == Kind::Lattice ? space_->K() : (kind_ == Kind::Vertices ? verts_.k : (kind_ == Kind::Fm ? fm_.k : (kind_ == Kind::Formant ? form_.k : (kind_ == Kind::Table ? shapes_.k : basis_.k)))); }
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

    /* ── the frame shapers ───────────────────────────────────────────────
     * A wavefolder has no closed form in the harmonics, so these run on the
     * rendered single cycle. The engine calls Shape() straight after the
     * transform; BandScale() tells it how much to pull the band limit in
     * first, because a memoryless nonlinearity multiplies bandwidth and the
     * frame arrives band-limited to exactly Nyquist. */
    bool HasShaper() const { return kind_ == Kind::Table; }
    float BandScale(const float* p01) const
    {
        if(kind_ != Kind::Table) return 1.f;
        const float a2 = shapes_.n > 2 ? p01[2] : 0.f;
        const float a3 = shapes_.n > 3 ? p01[3] : 0.f;
        return ShapeBandScale(shapes_, a2, a3);
    }
    void Shape(float* frame, float* scratch, int n, const float* p01) const
    {
        if(kind_ != Kind::Table) return;
        const float a2 = shapes_.n > 2 ? p01[2] : 0.f;
        const float a3 = shapes_.n > 3 ? p01[3] : 0.f;
        Apply(shapes_.axis2, frame, scratch, n, a2);
        Apply(shapes_.axis3, frame, scratch, n, a3);
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

    static void Apply(Shaper s, float* frame, float* scratch, int n, float d)
    {
        switch(s)
        {
            case Shaper::Fold: FoldFrame(frame, n, d); break;
            case Shaper::Ring: RingFrame(frame, n, d); break;
            case Shaper::Warp: WarpFrame(frame, scratch, n, d); break;
            default: break;
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

        float node[kMaxK];
        const float w[4] = {(1.f - u) * (1.f - v), u * (1.f - v), (1.f - u) * v, u * v};
        const int   cc[4] = {c0, c0 + 1, c0, c0 + 1};
        const int   rr[4] = {r0, r0, r0 + 1, r0 + 1};
        for(int i = 0; i < k; i++) mags[i] = 0.f;
        for(int q = 0; q < 4; q++)
        {
            if(w[q] <= 0.f) continue;
            detail::ShapeNode(shapes_, cc[q], rr[q], k, node);
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
