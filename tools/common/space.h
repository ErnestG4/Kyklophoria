/* space.h — a modal operator as a vector, and a baked space as a file.
 *
 * Three ways of turning (Lambda, G) into something PCA can be done on, which
 * are the three ablations of the brief:
 *
 *   full      log Lambda, the Grassmann log map of G's column frame at the
 *             reference, and G's coefficients in the exp-mapped frame. The
 *             proposal.
 *   lambda    log Lambda alone; G is the reference's, frozen. A frequency list
 *             being interpolated, which is what the proposal has to beat.
 *   linear    Lambda in Hz and G as they are. No manifold at all, which is what
 *             the machinery has to earn its keep against.
 *   gonly     the other half of `lambda`: the shapes move and the frequencies
 *             are the reference's, frozen. Not one of the brief's three; it is
 *             here because the go/no-go is whether the shapes add anything,
 *             and the cleanest way to see what they add is to move nothing else.
 *
 * ── the maps, for `full` ────────────────────────────────────────────────
 *
 * Frequencies: y = log(hz) - log(hz_ref). Reconstruction is exp, so a
 * frequency is positive wherever the space is evaluated and moves
 * geometrically — a straight line in the space is a glide in pitch.
 *
 * Shapes: G is N x P. Its P columns — one per strike position, each a vector
 * over the N modes — span a P-dimensional subspace of R^N, a point on the
 * Grassmann manifold Gr(P, N). G = Q R with Q an orthonormal frame of that
 * subspace, and Q is what the log map acts on. With principal angles from the
 * thin SVD  Q_r^T Q = U cos(Theta) V^T :
 *
 *   Log_{Q_r}(Q) = U~ Theta U^T,   U~ = (Q V - Q_r U cos Theta) sin(Theta)^-1
 *   Exp_{Q_r}(D) = Q_r V' cos(S') V'^T + U' sin(S') V'^T,   D = U' S' V'^T
 *
 * which is the form that needs no inverse of cos Theta, so a subspace nearly
 * orthogonal to the reference's is a large angle and not a blow-up. The exp
 * map returns a frame Q~ of the same subspace rotated by V U^T relative to Q,
 * so G's coefficients are taken in *that* frame, R~ = Q~^T G, and the vector
 * carries R~ rather than R. At a corpus point G = Q~ R~ exactly; between
 * points the subspace moves along a geodesic and the coefficients linearly.
 * "G stays orthonormal" in the brief means Q~ does, and it does by
 * construction — the check in the reconstruction report measures it anyway.
 *
 * Reference: Amsallem, Cortial, Carlberg & Farhat, IJNME 80(9), 2009, for the
 * tangent-space interpolation of reduced bases; the principal-angle form of
 * the maps is Absil, Mahony & Sepulchre's.
 *
 * ── the vector ──────────────────────────────────────────────────────────
 *
 *   full     [ y (N) ][ vec Delta (N*P) ][ vec R~ (P*P) ]
 *   lambda   [ y (N) ]
 *   linear   [ hz (N) ][ vec G (N*P) ]
 *   gonly    [ vec Delta (N*P) ][ vec R~ (P*P) ]
 *
 * Each block is scaled by one number before PCA — its standard deviation over
 * the corpus — so that no block wins by having bigger units. Log frequencies
 * are tenths, tangent entries are radians, coefficients are whatever the
 * mass normalisation made them; without this the PCA would be a PCA of
 * whichever block was loudest.
 */
#pragma once
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include "linalg.h"
#include "corpus.h"

namespace mb {

enum class Variant : int { Full = 0, Lambda = 1, Linear = 2, GOnly = 3 };
inline const char* VariantName(Variant v)
{
    return v == Variant::Full ? "full" : v == Variant::Lambda ? "lambda" : v == Variant::Linear ? "linear" : "gonly";
}

/* One model, aligned to the reference: hz[k] and row k of G are the mode that
 * matched reference mode k, sign resolved. */
struct Rep
{
    std::vector<double> hz;   /* N */
    Mat                 G;    /* N x P */
};

/* Grassmann log map at Qr of Q. */
inline Mat GrassLog(const Mat& Qr, const Mat& Q)
{
    const int N = Qr.rows, P = Qr.cols;
    Mat M = Mul(Tr(Qr), Q);              /* P x P */
    Mat U, V;
    std::vector<double> S;
    SVD(M, U, S, V);
    /* singular values are cos(theta); clamp the rounding */
    Mat QV = Mul(Q, V), QrU = Mul(Qr, U);
    Mat Ut(N, P);
    std::vector<double> theta(P);
    for(int k = 0; k < P; k++)
    {
        const double c  = S[k] > 1.0 ? 1.0 : S[k];
        theta[k]        = std::acos(c);
        const double sn = std::sin(theta[k]);
        for(int i = 0; i < N; i++)
            Ut(i, k) = sn > 1e-9 ? (QV(i, k) - QrU(i, k) * c) / sn : 0.0;
    }
    /* Delta = Ut diag(theta) U^T */
    return Mul(Mul(Ut, Diag(theta)), Tr(U));
}

/* Grassmann exp map at Qr of a tangent Delta. */
inline Mat GrassExp(const Mat& Qr, const Mat& D)
{
    const int P = Qr.cols;
    Mat U, V;
    std::vector<double> S;
    SVD(D, U, S, V);
    std::vector<double> cs(P), sn(P);
    for(int k = 0; k < P; k++) { cs[k] = std::cos(S[k]); sn[k] = std::sin(S[k]); }
    Mat a = Mul(Mul(Mul(Qr, V), Diag(cs)), Tr(V));
    Mat b = Mul(Mul(U, Diag(sn)), Tr(V));
    return Add(a, b);
}

struct Chart
{
    Variant             variant = Variant::Full;
    int                 N = 0, P = 0;
    std::vector<double> lam_ref;   /* log hz of the reference */
    Mat                 Qr;        /* N x P, the reference frame */
    Mat                 Gref;      /* N x P, for the lambda-only variant */

    int D() const
    {
        switch(variant)
        {
            case Variant::Full:   return N + N * P + P * P;
            case Variant::Lambda: return N;
            case Variant::GOnly:  return N * P + P * P;
            default:              return N + N * P;
        }
    }
    int Blocks() const { return variant == Variant::Full ? 3 : variant == Variant::Lambda ? 1 : 2; }
    /* which block each vector entry belongs to */
    int BlockOf(int i) const
    {
        if(variant == Variant::GOnly) return i < N * P ? 0 : 1;
        if(i < N) return 0;
        if(variant == Variant::Full && i >= N + N * P) return 2;
        return 1;
    }

    void SetReference(const Rep& r)
    {
        N = (int)r.hz.size(); P = r.G.cols;
        lam_ref.resize(N);
        for(int i = 0; i < N; i++) lam_ref[i] = std::log(r.hz[i]);
        Mat R;
        QR(r.G, Qr, R);
        Gref = r.G;
        /* A reference whose G has dependent columns has no frame, and the maps
         * at a frame that is not one are garbage for every model. The first
         * corpus did this: padding rows that were all one pattern gave a bar of
         * ten real modes a G of rank eleven. */
        double rmin = 1e300;
        for(int i = 0; i < P; i++) rmin = std::min(rmin, R(i, i));
        frame_ok = rmin > 1e-9;
    }
    bool frame_ok = true;

    /* log map: model -> vector */
    void ToVector(const Rep& r, std::vector<double>& v) const
    {
        v.assign(D(), 0.0);
        if(variant == Variant::Linear)
        {
            for(int i = 0; i < N; i++) v[i] = r.hz[i];
            for(int i = 0; i < N * P; i++) v[N + i] = r.G.a[i];
            return;
        }
        const int off = variant == Variant::GOnly ? 0 : N;
        if(variant != Variant::GOnly) for(int i = 0; i < N; i++) v[i] = std::log(r.hz[i]) - lam_ref[i];
        if(variant == Variant::Lambda) return;
        Mat Q, R;
        QR(r.G, Q, R);
        Mat Dl = GrassLog(Qr, Q);
        Mat Qt = GrassExp(Qr, Dl);
        Mat Rt = Mul(Tr(Qt), r.G);
        for(int i = 0; i < N * P; i++) v[off + i] = Dl.a[i];
        for(int i = 0; i < P * P; i++) v[off + N * P + i] = Rt.a[i];
    }

    /* exp map: vector -> model. `frame_error` reports how far the reconstructed
     * frame is from orthonormal, for the report. */
    void FromVector(const std::vector<double>& v, Rep& r, double* frame_error = nullptr) const
    {
        r.hz.assign(N, 0.0);
        if(variant == Variant::Linear)
        {
            for(int i = 0; i < N; i++) r.hz[i] = v[i];
            r.G = Mat(N, P);
            for(int i = 0; i < N * P; i++) r.G.a[i] = v[N + i];
            if(frame_error) *frame_error = 0.0;
            return;
        }
        const int off = variant == Variant::GOnly ? 0 : N;
        for(int i = 0; i < N; i++) r.hz[i] = std::exp(lam_ref[i] + (variant == Variant::GOnly ? 0.0 : v[i]));
        if(variant == Variant::Lambda) { r.G = Gref; if(frame_error) *frame_error = 0.0; return; }
        Mat Dl(N, P), Rt(P, P);
        for(int i = 0; i < N * P; i++) Dl.a[i] = v[off + i];
        for(int i = 0; i < P * P; i++) Rt.a[i] = v[off + N * P + i];
        Mat Qt = GrassExp(Qr, Dl);
        if(frame_error)
        {
            Mat I(P, P);
            for(int i = 0; i < P; i++) I(i, i) = 1.0;
            *frame_error = Frob(Sub(Mul(Tr(Qt), Qt), I));
        }
        r.G = Mul(Qt, Rt);
    }
};

/* The baked space: a chart, block scales, a mean, K whitened components. */
struct Space
{
    Chart               chart;
    int                 K = 4;
    double              extent = 2.2;
    std::vector<double> block_scale;    /* per block */
    std::vector<double> mean;           /* D, unscaled units */
    std::vector<std::vector<double>> comp;   /* K x D, unit vectors in scaled units */
    std::vector<double> sdev;           /* K, standard deviation of the scores */

    /* [0,1]^K -> the vector, unscaled */
    void VectorAt(const double* p01, std::vector<double>& v) const
    {
        const int D = chart.D();
        v = mean;
        for(int k = 0; k < K; k++)
        {
            const double c = (p01[k] - 0.5) * 2.0 * extent * sdev[k];
            for(int i = 0; i < D; i++) v[i] += c * comp[k][i] * block_scale[chart.BlockOf(i)];
        }
    }
    void At(const double* p01, Rep& r, double* frame_error = nullptr) const
    {
        std::vector<double> v;
        VectorAt(p01, v);
        chart.FromVector(v, r, frame_error);
    }
    /* Where a model's vector lands in the cube: its whitened coordinates,
     * mapped through the extent so that the cube is [0,1]^K. A model outside
     * the cube gets coordinates outside [0,1], which is information. */
    void Coord(const std::vector<double>& v, double* p01) const
    {
        const int D = chart.D();
        for(int k = 0; k < K; k++)
        {
            double s = 0;
            for(int i = 0; i < D; i++) s += (v[i] - mean[i]) / block_scale[chart.BlockOf(i)] * comp[k][i];
            const double c = sdev[k] > 0 ? s / sdev[k] : 0.0;
            p01[k] = 0.5 + c / (2.0 * extent);
        }
    }

    bool Write(const std::string& path) const
    {
        FILE* f = fopen(path.c_str(), "wb");
        if(!f) return false;
        auto w32 = [&](uint32_t x) { fwrite(&x, 4, 1, f); };
        auto wd  = [&](const std::vector<double>& d) { fwrite(d.data(), 8, d.size(), f); };
        fwrite("MSPC", 1, 4, f);
        w32(1u); w32((uint32_t)chart.variant); w32((uint32_t)chart.N); w32((uint32_t)chart.P);
        w32((uint32_t)K); w32((uint32_t)chart.D()); w32((uint32_t)chart.Blocks());
        fwrite(&extent, 8, 1, f);
        wd(chart.lam_ref); wd(chart.Qr.a); wd(chart.Gref.a);
        wd(block_scale); wd(mean);
        for(int k = 0; k < K; k++) wd(comp[k]);
        wd(sdev);
        fclose(f);
        return true;
    }
    bool Read(const std::string& path)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if(!f) return false;
        char magic[4];
        if(fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MSPC", 4) != 0) { fclose(f); return false; }
        uint32_t ver, var, N, P, k, D, B;
        if(fread(&ver, 4, 1, f) != 1 || ver != 1u) { fclose(f); return false; }
        fread(&var, 4, 1, f); fread(&N, 4, 1, f); fread(&P, 4, 1, f); fread(&k, 4, 1, f); fread(&D, 4, 1, f); fread(&B, 4, 1, f);
        fread(&extent, 8, 1, f);
        chart.variant = (Variant)var; chart.N = (int)N; chart.P = (int)P; K = (int)k;
        auto rd = [&](std::vector<double>& d, size_t n) { d.resize(n); return fread(d.data(), 8, n, f) == n; };
        bool ok = true;
        ok &= rd(chart.lam_ref, N);
        chart.Qr = Mat((int)N, (int)P); ok &= rd(chart.Qr.a, (size_t)N * P);
        chart.Gref = Mat((int)N, (int)P); ok &= rd(chart.Gref.a, (size_t)N * P);
        ok &= rd(block_scale, B);
        ok &= rd(mean, D);
        comp.assign(K, {});
        for(int q = 0; q < K; q++) ok &= rd(comp[q], D);
        ok &= rd(sdev, K);
        fclose(f);
        return ok && (int)D == chart.D();
    }
};

} // namespace mb
