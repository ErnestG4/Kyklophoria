/* kyk_fft.h — the fixed-size inverse FFT behind frame rendering.
 *
 * RenderFrame(): frame[n] = Σ_{k=1..kcut} mag[k-1] · cos(2π k n / kFrame + φ_k)
 *
 * Two implementations, numerically equivalent to about 1e-5:
 *
 *   RenderFrameComplex  a kFrame-point complex radix-2 transform over the
 *                       positive bins, whose real part is the sum above. The
 *                       obvious way, and what M0 shipped. It computes an
 *                       imaginary half that is then thrown away, so it does
 *                       roughly twice the necessary work.
 *
 *   RenderFrame         the real-valued version: build the conjugate-symmetric
 *                       half-spectrum, fold it into a kFrame/2-point complex
 *                       transform, and unpack even and odd samples from the
 *                       real and imaginary parts of the result. Half as many
 *                       butterflies over half the length: 2.0x faster measured
 *                       on the desktop, and the arithmetic is still
 *                       table-driven so desktop and module stay bit-identical.
 *
 * The imaginary half the complex version discards is not junk — it is the
 * Hilbert transform of the output, i.e. the quadrature signal. If we ever want
 * frequency shifting, single-sideband work or a quadrature output, that is
 * where it comes from, and RenderFrameComplex already computes it.
 *
 * Twiddles come from kSinTable. Phases arrive as (cosφ, sinφ) pairs, already
 * table-quantised by the caller.
 *
 * Not yet done, and the next lever if the module is still tight: CMSIS
 * arm_rfft_fast_f32 on the ARM side behind KYK_FFT_CMSIS. That one cannot be
 * verified from here, so it needs a bench measurement before it ships, and it
 * ends desktop/module bit-identity (docs/spec.md §2 allows for that).
 */
#pragma once
#include "kyk_types.h"
#include "kyk_tables.h"

namespace kyk {

struct FftScratch
{
    float re[kFrame];
    float im[kFrame];
};

namespace detail {
/* twiddle e^{+i·2π·j/len}: cos from the sine table a quarter period on */
inline void Twiddle(int j, int len, float& c, float& s)
{
    const int step = kTableSize / len;
    const int idx  = (j * step) & (kTableSize - 1);
    s              = kSinTable[idx];
    c              = kSinTable[(idx + kTableSize / 4) & (kTableSize - 1)];
}

inline unsigned BitRev(unsigned v, int bits)
{
    unsigned r = 0;
    for(int b = 0; b < bits; b++) { r = (r << 1) | (v & 1u); v >>= 1; }
    return r;
}

/* In-place iterative DIT butterflies over n points already in bit-reversed
 * order, with the e^{+i…} (inverse) sign convention. */
inline void Butterflies(float* re, float* im, int n)
{
    for(int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        for(int start = 0; start < n; start += len)
            for(int j = 0; j < half; j++)
            {
                float wc, ws;
                Twiddle(j, len, wc, ws);
                const int   a  = start + j, b = a + half;
                const float xr = re[b] * wc - im[b] * ws;
                const float xi = re[b] * ws + im[b] * wc;
                re[b]          = re[a] - xr;
                im[b]          = im[a] - xi;
                re[a]          = re[a] + xr;
                im[a]          = im[a] + xi;
            }
    }
}
} // namespace detail

/* The M0 implementation, kept as the reference the real version is tested
 * against (tests/morph_check). */
inline void RenderFrameComplex(const float* mags, const float* cph, const float* sph, int kcut, float* frame,
                               FftScratch& sc)
{
    const int F = kFrame;
    for(int i = 0; i < F; i++) { sc.re[i] = 0.f; sc.im[i] = 0.f; }
    for(int k = 1; k <= kcut; k++)
    {
        const unsigned r = detail::BitRev((unsigned)k, kFrameLog2);
        sc.re[r]         = mags[k - 1] * cph[k - 1];
        sc.im[r]         = mags[k - 1] * sph[k - 1];
    }
    detail::Butterflies(sc.re, sc.im, F);
    for(int i = 0; i < F; i++) frame[i] = sc.re[i];
}

/* mags[kcut], cph/sph[kcut] (phase cos/sin per harmonic 1..kcut). */
inline void RenderFrame(const float* mags, const float* cph, const float* sph, int kcut, float* frame,
                        FftScratch& sc)
{
    const int M = kFrame / 2;            /* half-length complex transform */
    const int B = kFrameLog2 - 1;

    /* A real signal's spectrum is conjugate symmetric, so half of it says
     * everything: H[k] = (m_k/2)·e^{iφ_k} for k = 1..kcut, zero elsewhere,
     * with H[0] and H[M] real (both zero here — no DC, and kcut < M always
     * because K ≤ kFrame/4).
     *
     * Fold it into a length-M complex sequence whose transform interleaves
     * the even and odd output samples:
     *   Xe[k] = H[k] + conj(H[M−k])                      even samples
     *   Xo[k] = (H[k] − conj(H[M−k])) · e^{+i2πk/kFrame}  odd samples
     *   Z[k]  = Xe[k] + i·Xo[k]
     * Then z = IDFT_M(Z) gives frame[2n] = Re z[n], frame[2n+1] = Im z[n].
     * H is sparse, so the fold reads at most kcut+1 non-zero entries. */
    for(int i = 0; i < M; i++) { sc.re[i] = 0.f; sc.im[i] = 0.f; }
    /* Only two short runs of k can be non-zero: k in [1,kcut] where H[k] is
     * live, and k in [M-kcut, M-1] where H[M-k] is. Everything between them
     * folds to zero, so walking all M would be mostly wasted branches. */
    if(kcut < 1)
    {
        for(int n = 0; n < kFrame; n++) frame[n] = 0.f;   /* silence, not stale content */
        return;
    }
    int lo2 = M - kcut;
    if(lo2 <= kcut) lo2 = kcut + 1;                   /* the runs would overlap */
    for(int pass = 0; pass < 2; pass++)
    {
        const int kb = pass ? lo2 : 1;
        const int ke = pass ? M - 1 : (kcut < M - 1 ? kcut : M - 1);
        for(int k = kb; k <= ke; k++)
        {
        float ar = 0.f, ai = 0.f, br = 0.f, bi = 0.f;
        if(k >= 1 && k <= kcut) { ar = 0.5f * mags[k - 1] * cph[k - 1]; ai = 0.5f * mags[k - 1] * sph[k - 1]; }
        const int mk = M - k;
        if(mk >= 1 && mk <= kcut) { br = 0.5f * mags[mk - 1] * cph[mk - 1]; bi = 0.5f * mags[mk - 1] * sph[mk - 1]; }
        bi = -bi;                                     /* conj(H[M-k]) */
        /* E[k] = H[k] + conj(H[M−k]) and O[k] = (H[k] − conj(H[M−k]))·e^{+i2πk/F};
         * no halving here — the factor of two already lives in H. */
        const float er = ar + br, ei = ai + bi;
        const float orr = ar - br, oi = ai - bi;
        float       wc, ws;
        detail::Twiddle(k, kFrame, wc, ws);           /* e^{+i2πk/kFrame} */
        const float tr = orr * wc - oi * ws;
        const float ti = orr * ws + oi * wc;
        /* Z[k] = (er + i·ei) + i·(tr + i·ti) = (er − ti) + i·(ei + tr) */
        const unsigned r = detail::BitRev((unsigned)k, B);
        sc.re[r]         = er - ti;
        sc.im[r]         = ei + tr;
        }
    }
    detail::Butterflies(sc.re, sc.im, M);
    for(int n = 0; n < M; n++)
    {
        frame[2 * n]     = sc.re[n];
        frame[2 * n + 1] = sc.im[n];
    }
}

} // namespace kyk
