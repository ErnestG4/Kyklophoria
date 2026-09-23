/* proto_api.cpp — the prototype as a shared library, for the fitter and the
 * renderer (proto/threeway.py through ctypes). One implementation of the
 * DSP: what fits the parameters is what plays them.
 *
 *   make -C proto            -> ../build/proto/libkykproto.so
 *
 * Every render is at 48 kHz and at record scale times the world's own
 * headroom scale (no engine gain): the numbers the fitter compares.
 */
#include "kyk_exciter.h"
#include <vector>
#include <cstdlib>

using namespace kykp;

namespace {

struct Handle
{
    std::vector<uint8_t> blob;
    kyk::ResonatorWorld whole, w;        /* the file, and the member played (itself for a plain world) */
    std::vector<float> P;                /* P_COUNT a point */
    std::vector<float> K;                /* the hammer's stiffness a point */
    float sr;
};

void SetK(Handle* h, int i)
{
    const float* p = &h->P[(size_t)i * P_COUNT];
    h->K[i] = (int)p[P_KIND] == 0 ? Contact::HammerK(p[P_TC], p[P_EXP], h->sr) : 1.f;
}

} // namespace

extern "C" {

int kp_pcount() { return P_COUNT; }
int kp_bands() { return kBands; }
float kp_band_edge(int k) { return BandEdge(k); }

void* kp_open(const uint8_t* data, int size, int member)
{
    Handle* h = new Handle();
    h->blob.assign(data, data + size);
    h->sr = 48000.f;
    h->whole.Init();
    if(!h->whole.Attach(h->blob.data(), (uint32_t)h->blob.size())) { delete h; return nullptr; }
    if(!h->whole.Member(member, h->w)) { delete h; return nullptr; }
    h->P.assign((size_t)h->w.P * P_COUNT, 0.f);
    h->K.assign(h->w.P, 1.f);
    for(int i = 0; i < h->w.P; i++) { float* p = &h->P[(size_t)i * P_COUNT]; p[P_TC] = 0.002f; p[P_EXP] = 2.5f; p[P_FELT] = 1.f; p[P_RAMP] = 0.003f; p[P_ORDER] = 2; p[P_HPK] = 1.f; SetK(h, i); }
    return h;
}
void kp_close(void* hh) { delete (Handle*)hh; }
int kp_npoints(void* hh) { return ((Handle*)hh)->w.P; }
float kp_param(void* hh, int i) { return ((Handle*)hh)->w.Param(i); }
float kp_lo(void* hh) { return ((Handle*)hh)->w.lo; }
float kp_hi(void* hh) { return ((Handle*)hh)->w.hi; }

/* the voice as At() builds it at a note (cap modes, 0 for all): the live
   modes' hz, zeta, gain (absolute) — what the bank will ring */
int kp_voice_modes(void* hh, float note, int cap, float* hz, float* zeta, float* gain)
{
    Handle* h = (Handle*)hh;
    static kyk::ResonatorVoice v; v.Init(); v.cap = cap;
    h->w.At(note, v, h->sr);
    int n = 0;
    for(int k = 0; k < kyk::ResonatorBank::kMax; k++)
        if(v.gain[k] != 0.f) { hz[n] = v.hz[k]; zeta[n] = v.zeta[k]; gain[n] = v.gain[k]; n++; }
    return n;
}

void kp_set(void* hh, int i, const float* p)
{
    Handle* h = (Handle*)hh;
    std::memcpy(&h->P[(size_t)i * P_COUNT], p, sizeof(float) * P_COUNT);
    SetK(h, i);
}
void kp_get(void* hh, int i, float* p) { Handle* h = (Handle*)hh; std::memcpy(p, &h->P[(size_t)i * P_COUNT], sizeof(float) * P_COUNT); }

/* one strike at a note: variant 0 today's runtime, 1 synthesised exciter, 2
   and the waveguide. out is the voice; modes, noise, wg its parts (any may
   be null). Returns the contact's length in samples (0 for variant 0). */
int kp_render(void* hh, float note, float vel, int variant, int cap, int n, float* out, float* modes, float* noise, float* wg)
{
    Handle* h = (Handle*)hh;
    static ProtoVoice v;
    v.Init(h->sr);
    v.variant = variant;
    v.rv.cap = cap;
    v.Build(h->w, h->P.data(), h->K.data(), note, false, false);
    v.Strike(vel);
    const int clen = variant ? v.contact.len : 0;
    for(int i = 0; i < n; i++) { out[i] = 0.f; if(modes) modes[i] = 0.f; if(noise) noise[i] = 0.f; if(wg) wg[i] = 0.f; }
    for(int i = 0; i < n; i += 24)
    {
        const int m = n - i < 24 ? n - i : 24;
        v.Process(out + i, m, modes ? modes + i : nullptr, noise ? noise + i : nullptr, wg ? wg + i : nullptr);
    }
    return clen;
}

/* a passage through a four-voice engine that does what Engine::Strike does:
   the next voice round-robin, built at the note with its ring carried and
   choked if the note moved (At with keep and strike), then struck; every
   voice within the count processed every 24-sample block; strikes land on
   the first block that starts at or after their sample (at, computed by the
   caller in double, as kykdesk does). cap = 48 / poly. */
int kp_passage(void* hh, int nev, const int* at, const float* note, const float* vel, int variant, int poly, int n, float* out)
{
    Handle* h = (Handle*)hh;
    static ProtoVoice vs[4];
    for(int i = 0; i < 4; i++) { vs[i].Init(h->sr); vs[i].variant = variant; vs[i].rv.cap = poly > 1 ? kyk::ResonatorBank::kMax / poly : 0; }
    int active = 0, k = 0;
    float tmp[24];
    for(int i = 0; i < n; i++) out[i] = 0.f;
    for(int b = 0; b * 24 < n; b++)
    {
        while(k < nev && at[k] <= b * 24)
        {
            if(poly > 1) active = (active + 1) % poly;
            ProtoVoice& v = vs[active];
            const bool fresh = v.note == 0.f;
            v.Build(h->w, h->P.data(), h->K.data(), note[k], !fresh, true);
            v.Strike(vel[k]);
            k++;
        }
        const int m = n - b * 24 < 24 ? n - b * 24 : 24;
        for(int j = 0; j < poly && j < 4; j++)
        {
            for(int q = 0; q < m; q++) tmp[q] = 0.f;
            vs[j].Process(tmp, m);
            for(int q = 0; q < m; q++) out[b * 24 + q] += tmp[q];
        }
    }
    return 0;
}

/* the contact's pulse at a swing, for a look */
int kp_contact(const float* p, float swing, float* out, int max)
{
    static Contact c;
    const float sr = 48000.f;
    const float K = (int)p[P_KIND] == 0 ? Contact::HammerK(p[P_TC], p[P_EXP], sr) : 1.f;
    c.Make(p, K, swing, sr);
    const int n = c.len < max ? c.len : max;
    for(int i = 0; i < n; i++) out[i] = c.f[i];
    return c.len;
}

/* the waveguide as a strike would set it at ratio: its resonances near the
   given guesses (Hz) for partial numbers ks, and the loop gain there.
   Returns D (the integer delay), or -1 if the design is off */
int kp_wg(const float* p, float ratio, int nk, const int* ks, const float* guess, float* f_out, float* mag_out)
{
    static Waveguide g;
    const float sr = 48000.f;
    g.Init();
    g.Set(p, ratio, sr);
    if(!g.on) return -1;
    for(int i = 0; i < nk; i++)
    {
        const float w = g.Partial(6.2831853f * guess[i] / sr, ks[i]);
        f_out[i] = w * sr / 6.2831853f;
        mag_out[i] = g.LoopMag(w);
    }
    return g.D;
}

} // extern "C"
