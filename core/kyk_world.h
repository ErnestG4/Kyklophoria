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

class World
{
public:
    enum class Kind : uint8_t { None = 0, Lattice = 1, Analytic = 2 };

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

    Kind Which() const { return kind_; }
    bool Ready() const { return kind_ != Kind::None; }
    int  N() const { return kind_ == Kind::Lattice ? space_->N() : basis_.n; }
    int  K() const { return kind_ == Kind::Lattice ? space_->K() : basis_.k; }
    int  P() const { return kind_ == Kind::Lattice ? space_->P() : p_; }
    Topo TopoOf(int a) const { return kind_ == Kind::Lattice ? space_->TopoOf(a) : (Topo)topo_[a]; }
    uint32_t PhaseSeed() const { return kind_ == Kind::Lattice ? space_->Header().phase_seed : 1u; }
    const Space*      SpacePtr() const { return kind_ == Kind::Lattice ? space_ : nullptr; }
    const EigenBasis& Basis() const { return basis_; }

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
        if(kind_ != Kind::Analytic) { for(int k = 0; k < kMaxK; k++) mags[k] = 0.f; return; }
        EvalAnalytic(p01, mags, payload);
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

    Kind         kind_  = Kind::None;
    const Space* space_ = nullptr;
    EigenBasis   basis_;
    int          p_ = 0;
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
