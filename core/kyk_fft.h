/* kyk_fft.h — the fixed-size inverse FFT shim behind frame rendering.
 *
 * RenderFrame(): frame[n] = Σ_{k=1..kcut} mag[k-1] · cos(2π k n / kFrame + φ_k)
 *
 * Implementation: a kFrame-point complex radix-2 DIT transform over the
 * positive bins only (negative bins stay zero) whose real part is the sum
 * above. Twiddles come from kSinTable, so the desktop and the module run
 * the same arithmetic on the same constants. Phases are given as
 * (cosφ, sinφ) pairs, already table-quantised by the caller.
 *
 * M1 may swap the ARM side for CMSIS arm_rfft_fast_f32 behind this same
 * signature (KYK_FFT_CMSIS); when it does, docs/m1-notes.md records the
 * desktop-vs-module drift, because that build is no longer bit-identical.
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
} // namespace detail

/* mags[kcut], cph/sph[kcut] (phase cos/sin per harmonic 1..kcut). */
inline void RenderFrame(const float* mags, const float* cph, const float* sph, int kcut, float* frame,
                        FftScratch& sc)
{
    const int F = kFrame;
    /* bins in bit-reversed order straight into the work arrays */
    for(int i = 0; i < F; i++) { sc.re[i] = 0.f; sc.im[i] = 0.f; }
    for(int k = 1; k <= kcut; k++)
    {
        /* bit-reverse k over kFrameLog2 bits */
        unsigned r = 0, v = (unsigned)k;
        for(int b = 0; b < kFrameLog2; b++) { r = (r << 1) | (v & 1u); v >>= 1; }
        sc.re[r] = mags[k - 1] * cph[k - 1];
        sc.im[r] = mags[k - 1] * sph[k - 1];
    }
    /* iterative DIT butterflies, e^{+i…} (inverse) */
    for(int len = 2; len <= F; len <<= 1)
    {
        const int half = len >> 1;
        for(int start = 0; start < F; start += len)
        {
            for(int j = 0; j < half; j++)
            {
                float wc, ws;
                detail::Twiddle(j, len, wc, ws);
                const int   a  = start + j, b = a + half;
                const float xr = sc.re[b] * wc - sc.im[b] * ws;
                const float xi = sc.re[b] * ws + sc.im[b] * wc;
                sc.re[b]       = sc.re[a] - xr;
                sc.im[b]       = sc.im[a] - xi;
                sc.re[a]       = sc.re[a] + xr;
                sc.im[a]       = sc.im[a] + xi;
            }
        }
    }
    for(int i = 0; i < F; i++) frame[i] = sc.re[i];
}

} // namespace kyk
