/* linalg.h — the little dense algebra the bake needs, in doubles.
 *
 * Jacobi eigendecomposition is kykeigen's, copied; the rest is what the
 * Grassmann log and exp maps need — thin SVD of a tall matrix through the
 * eigendecomposition of its small Gram matrix, and a Gram–Schmidt QR. The
 * matrices are 48 x 12, so nothing here has to be fast.
 */
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace mb {

/* Cyclic Jacobi eigendecomposition of a symmetric n x n matrix (row-major,
 * destroyed). Eigenvalues descending; vec is column-major eigenvectors, i.e.
 * vec[i*n + k] is component i of eigenvector k. From tools/kykeigen. */
inline void Jacobi(std::vector<double>& a, int n, std::vector<double>& val, std::vector<double>& vec)
{
    vec.assign((size_t)n * n, 0.0);
    for(int i = 0; i < n; i++) vec[(size_t)i * n + i] = 1.0;
    for(int sweep = 0; sweep < 200; sweep++)
    {
        double off = 0;
        for(int p = 0; p < n; p++)
            for(int q = p + 1; q < n; q++) off += a[(size_t)p * n + q] * a[(size_t)p * n + q];
        if(off < 1e-24) break;
        for(int p = 0; p < n; p++)
            for(int q = p + 1; q < n; q++)
            {
                const double apq = a[(size_t)p * n + q];
                if(std::fabs(apq) < 1e-300) continue;
                const double app = a[(size_t)p * n + p], aqq = a[(size_t)q * n + q];
                const double th  = 0.5 * std::atan2(2 * apq, aqq - app);
                const double c = std::cos(th), s = std::sin(th);
                for(int k = 0; k < n; k++)
                {
                    const double akp = a[(size_t)k * n + p], akq = a[(size_t)k * n + q];
                    a[(size_t)k * n + p] = c * akp - s * akq;
                    a[(size_t)k * n + q] = s * akp + c * akq;
                }
                for(int k = 0; k < n; k++)
                {
                    const double apk = a[(size_t)p * n + k], aqk = a[(size_t)q * n + k];
                    a[(size_t)p * n + k] = c * apk - s * aqk;
                    a[(size_t)q * n + k] = s * apk + c * aqk;
                }
                for(int k = 0; k < n; k++)
                {
                    const double vkp = vec[(size_t)k * n + p], vkq = vec[(size_t)k * n + q];
                    vec[(size_t)k * n + p] = c * vkp - s * vkq;
                    vec[(size_t)k * n + q] = s * vkp + c * vkq;
                }
            }
    }
    val.resize(n);
    for(int i = 0; i < n; i++) val[i] = a[(size_t)i * n + i];
    /* sort descending, carrying the columns */
    std::vector<int> order(n);
    for(int i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int x, int y) { return val[x] > val[y]; });
    std::vector<double> v2(val), e2(vec);
    for(int k = 0; k < n; k++)
    {
        val[k] = v2[order[k]];
        for(int i = 0; i < n; i++) vec[(size_t)i * n + k] = e2[(size_t)i * n + order[k]];
    }
}

/* Row-major m x n matrices as flat vectors. */
struct Mat
{
    int rows = 0, cols = 0;
    std::vector<double> a;
    Mat() {}
    Mat(int r, int c) : rows(r), cols(c), a((size_t)r * c, 0.0) {}
    double&       operator()(int i, int j) { return a[(size_t)i * cols + j]; }
    const double& operator()(int i, int j) const { return a[(size_t)i * cols + j]; }
};

inline Mat Mul(const Mat& x, const Mat& y)
{
    Mat r(x.rows, y.cols);
    for(int i = 0; i < x.rows; i++)
        for(int k = 0; k < x.cols; k++)
        {
            const double v = x(i, k);
            if(v == 0.0) continue;
            for(int j = 0; j < y.cols; j++) r(i, j) += v * y(k, j);
        }
    return r;
}
inline Mat Tr(const Mat& x)
{
    Mat r(x.cols, x.rows);
    for(int i = 0; i < x.rows; i++) for(int j = 0; j < x.cols; j++) r(j, i) = x(i, j);
    return r;
}
inline Mat Sub(const Mat& x, const Mat& y)
{
    Mat r(x.rows, x.cols);
    for(size_t i = 0; i < r.a.size(); i++) r.a[i] = x.a[i] - y.a[i];
    return r;
}
inline Mat Add(const Mat& x, const Mat& y)
{
    Mat r(x.rows, x.cols);
    for(size_t i = 0; i < r.a.size(); i++) r.a[i] = x.a[i] + y.a[i];
    return r;
}
inline Mat Diag(const std::vector<double>& d)
{
    Mat r((int)d.size(), (int)d.size());
    for(size_t i = 0; i < d.size(); i++) r((int)i, (int)i) = d[i];
    return r;
}
inline double Frob(const Mat& x) { double s = 0; for(double v : x.a) s += v * v; return std::sqrt(s); }

/* Modified Gram–Schmidt. Q is m x n with orthonormal columns, R is n x n upper
 * triangular with a non-negative diagonal, so the frame is sign-fixed. A
 * column that is dependent on the ones before it gets a zero diagonal and a
 * zero column in Q rather than a division by nothing. */
inline void QR(const Mat& A, Mat& Q, Mat& R)
{
    const int m = A.rows, n = A.cols;
    Q = Mat(m, n); R = Mat(n, n);
    for(int j = 0; j < n; j++)
    {
        std::vector<double> v(m);
        for(int i = 0; i < m; i++) v[i] = A(i, j);
        for(int k = 0; k < j; k++)
        {
            double d = 0;
            for(int i = 0; i < m; i++) d += Q(i, k) * v[i];
            R(k, j) = d;
            for(int i = 0; i < m; i++) v[i] -= d * Q(i, k);
        }
        double nrm = 0;
        for(int i = 0; i < m; i++) nrm += v[i] * v[i];
        nrm = std::sqrt(nrm);
        R(j, j) = nrm;
        if(nrm > 1e-12) for(int i = 0; i < m; i++) Q(i, j) = v[i] / nrm;
    }
}

/* Thin SVD of a tall m x n matrix (m >= n): A = U S V^T with U m x n, S n, V
 * n x n, through the eigendecomposition of A^T A. Adequate for n = 12; a
 * singular value below eps gets a zero column in U, which is what every use
 * here wants (that direction contributes nothing). */
inline void SVD(const Mat& A, Mat& U, std::vector<double>& S, Mat& V, double eps = 1e-12)
{
    const int m = A.rows, n = A.cols;
    std::vector<double> g((size_t)n * n, 0.0);
    for(int i = 0; i < n; i++)
        for(int j = 0; j < n; j++)
        {
            double s = 0;
            for(int k = 0; k < m; k++) s += A(k, i) * A(k, j);
            g[(size_t)i * n + j] = s;
        }
    std::vector<double> val, vec;
    Jacobi(g, n, val, vec);
    V = Mat(n, n);
    for(int i = 0; i < n; i++) for(int k = 0; k < n; k++) V(i, k) = vec[(size_t)i * n + k];
    S.resize(n);
    U = Mat(m, n);
    for(int k = 0; k < n; k++)
    {
        S[k] = val[k] > 0 ? std::sqrt(val[k]) : 0.0;
        if(S[k] <= eps) { S[k] = 0.0; continue; }
        for(int i = 0; i < m; i++)
        {
            double s = 0;
            for(int j = 0; j < n; j++) s += A(i, j) * V(j, k);
            U(i, k) = s / S[k];
        }
    }
}

} // namespace mb
