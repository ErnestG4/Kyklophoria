/* armcost.cpp — the prototype's per-sample loops as the module's compiler
 * emits them (make -C proto armcost). The desktop build's std::complex and
 * the strike-time design are not here: on the module those run once a
 * strike, and README.md counts them separately. Kyklophoria's own
 * `make armcost` counts whole functions; armcount.py also finds each loop's
 * body, which is what a sample costs. */
#include <cstdint>
#include <cstring>
#include <cmath>

namespace probe {

/* the noise bands, one sample: as NoiseBands::Run with nothing arriving */
struct Noise
{
    float b0[12], a1[12], a2[12], norm[12], y1[12], y2[12], x1, x2, ef[12], es[12], df[12], ds[12];
    uint32_t rng;
};

/* the waveguide's loop, as Waveguide::Run: gain, the two-pole tilt, the
   input high-pass, the delay, the Thiran, M allpasses, the loss, the
   fourth-order in-loop high-pass, the output high-pass, the choke fade */
struct Bq { float b0, b1, b2, a1, a2, z1, z2; };
static inline float Run(Bq& q, float x) { const float y = q.b0 * x + q.z1; q.z1 = q.b1 * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }
struct WG
{
    float buf[4096];
    int w, D, M;
    float fa, fx1, fy1, ad, ax1[48], ay1[48], lb, la, ly1, ta, tz, tz2, gin, gout, dout;
    Bq hpl0, hpl1, hpi, hpo;
};

/* the resonator bank's pair loop, for scale (kyk_resonate.h, free ring) */
struct Bank { float c1[48], c2[48], y1[48], y2[48]; int n; };

} // namespace probe

using namespace probe;

extern "C" void c_noise(Noise* s, float* out, int n)
{
    for(int i = 0; i < n; i++)
    {
        s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5;
        const float x = (float)(int32_t)s->rng * (1.7320508f / 2147483648.f);
        float acc = 0.f;
        for(int k = 0; k < 12; k++)
        {
            const float y = s->b0[k] * (x - s->x2) - s->a1[k] * s->y1[k] - s->a2[k] * s->y2[k];
            s->y2[k] = s->y1[k]; s->y1[k] = y;
            acc += (s->ef[k] + s->es[k]) * s->norm[k] * y;
            s->ef[k] *= s->df[k]; s->es[k] *= s->ds[k];
        }
        s->x2 = s->x1; s->x1 = x;
        out[i] += acc;
    }
}

/* the same, arranged as the bank is: the block's noise drawn once, then a
   band at a time over the block with its state in registers */
extern "C" void c_noise_block(Noise* s, float* out, int n)
{
    float xb[26];
    xb[0] = s->x2; xb[1] = s->x1;
    for(int i = 0; i < n; i++)
    {
        s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5;
        xb[i + 2] = (float)(int32_t)s->rng * (1.7320508f / 2147483648.f);
    }
    for(int k = 0; k < 12; k++)
    {
        const float b0 = s->b0[k], a1 = s->a1[k], a2 = s->a2[k], nm = s->norm[k], df = s->df[k], ds = s->ds[k];
        float y1 = s->y1[k], y2 = s->y2[k], ef = s->ef[k], es = s->es[k];
        for(int i = 0; i < n; i++)
        {
            const float y = b0 * (xb[i + 2] - xb[i]) - a1 * y1 - a2 * y2;
            y2 = y1; y1 = y;
            out[i] += (ef + es) * nm * y;
            ef *= df; es *= ds;
        }
        s->y1[k] = y1; s->y2[k] = y2; s->ef[k] = ef; s->es[k] = es;
    }
    s->x2 = xb[n]; s->x1 = xb[n + 1];
}

extern "C" void c_wg(WG* g, const float* in, float* out, int n)
{
    for(int i = 0; i < n; i++)
    {
        float v0 = g->gin * in[i];
        g->tz += g->ta * (v0 - g->tz); g->tz2 += g->ta * (g->tz - g->tz2); v0 = g->tz2;
        v0 = Run(g->hpi, v0);
        const float y = g->buf[(g->w - g->D) & 4095];
        float v = g->fa * (y - g->fy1) + g->fx1; g->fx1 = y; g->fy1 = v;
        for(int m = 0; m < g->M; m++) { const float u = g->ad * (v - g->ay1[m]) + g->ax1[m]; g->ax1[m] = v; g->ay1[m] = u; v = u; }
        g->ly1 = g->lb * v - g->la * g->ly1;
        v = Run(g->hpl1, Run(g->hpl0, g->ly1));
        const float s = v0 + v;
        g->buf[g->w] = s; g->w = (g->w + 1) & 4095;
        float o = Run(g->hpo, s) * g->gout; g->gout *= g->dout;
        out[i] += o;
    }
}

extern "C" void c_bank(Bank* b, float* out, int m)
{
    int i = 0;
    for(; i + 1 < b->n; i += 2)
    {
        const float a0 = b->c1[i], b0 = b->c2[i], a1 = b->c1[i + 1], b1 = b->c2[i + 1];
        float p1 = b->y1[i], p2 = b->y2[i], q1 = b->y1[i + 1], q2 = b->y2[i + 1];
        for(int k = 0; k < m; k++)
        {
            const float yp = a0 * p1 + b0 * p2, yq = a1 * q1 + b1 * q2;
            p2 = p1; p1 = yp; q2 = q1; q1 = yq;
            float o = out[k]; o += yp; o += yq; out[k] = o;
        }
        b->y1[i] = p1; b->y2[i] = p2; b->y1[i + 1] = q1; b->y2[i + 1] = q2;
    }
}

/* the strike on the module: the contact is the stored Hertz shape stretched
   to T(v) = T1 v^-(p-1)/(p+1) (the pure Hertz pulse is self-similar), and
   each mode's gain is the felt's word, 1 / (1 + (f T)^2), against the
   reference strike's — a divide a mode */
extern "C" void c_strike(const float* shape, int ns, float* pulse, int len, float* gains, const float* hz, int nm, float T, float T1, float s)
{
    for(int n = 0; n < len; n++)
    {
        const float u = (float)n * (float)(ns - 1) / (float)(len - 1);
        const int i0 = (int)u; const float fr = u - (float)i0;
        pulse[n] = shape[i0] + fr * (shape[i0 + 1 < ns ? i0 + 1 : i0] - shape[i0]);
    }
    for(int k = 0; k < nm; k++)
    {
        const float x = hz[k] * T, x1 = hz[k] * T1;
        gains[k] = s * (1.f + x1 * x1) / (1.f + x * x);
    }
}

/* today's runtime, for scale: the same functions Kyklophoria's own
   `make armcost` counts, from ModalBake's verbatim copy */
#include "../runtime/kyk_resonate.h"
extern "C" void c_resonate(kyk::ResonatorBank* b, float* d, int n) { b->Process(d, n); }
extern "C" void c_burstplay(kyk::BurstPlayer* b, float* d, int n) { b->Process(d, n); }
