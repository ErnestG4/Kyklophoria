/* worldfill — every semitone of a note world a point of its own (format 8).
 *
 *   worldfill in.kykm out.kykm
 *
 * A note between two recorded points is built at every strike from both
 * (the runtime's At(): each point transposed to the note, partials paired,
 * blended by where the note lies), and on the module that rebuild ran the
 * strike block over — a pop at every strike on the pianos whose points are
 * two or three semitones apart, and none on the Iowa grand, which has a point
 * at nearly every note (Combust, 2026-09-27). Here the runtime's own At()
 * builds each missing semitone once, and the result is written as a point:
 * the module then plays every locked note by the cheap path, the nearest
 * point alone, which is now the note itself.
 *
 * A written point refers to the nearer recorded point's attack (a burst count
 * of 0xFFFF, then that point's index) rather than carrying a copy: the
 * attacks are most of a world, a copy a semitone would not fit the module's
 * 4 MB slot, and At() reads a referenced attack at the note's own pitch as it
 * read the nearer point's before. A family is filled member by member; a row
 * of bodies (an index world) is copied as it is.
 *
 * The encoders are the runtime decoders' inverses (Tables in kyk_resonate.h;
 * export.py's cents/decay8/level8/phase8). After writing, every semitone of
 * the old world and the new is built and compared: frequency, decay and
 * level mode for mode within the bytes' own resolution, or it fails. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "../../runtime/kyk_resonate.h"

using namespace kyk;
typedef std::vector<uint8_t> Bytes;

static void put(Bytes& b, const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
template <class T> static void put(Bytes& b, T v) { put(b, &v, sizeof v); }
template <class T> static T get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

static uint16_t cents(float hz) { const double c = std::round(6000.0 * std::log2(std::max(hz, 20.f) / 20.0)); return (uint16_t)std::min(65535.0, std::max(0.0, c)); }
static uint8_t decay8(float zeta) { const double d = std::round(-10.0 * std::log(std::max((double)zeta, 1e-11))); return (uint8_t)std::min(255.0, std::max(0.0, d)); }
static uint8_t level8(float g, float loudest)
{
    if(g == 0.f || loudest <= 0.f) return 255;
    const double db = 20.0 * std::log10(std::max((double)std::fabs(g), 1e-12) / loudest);
    return (uint8_t)std::min(254.0, std::max(0.0, std::round(-db * 4.0)));
}
static uint8_t phase8(float ph)
{
    double x = std::fmod((double)ph, 2 * M_PI); if(x < 0) x += 2 * M_PI;
    return (uint8_t)((int)std::lround(x / (2 * M_PI) * 256.0) % 256);
}

struct Pt { float param; const uint8_t* fixed; uint32_t fixed_n; const uint8_t* noise; uint32_t noise_n; const uint8_t* bursts; uint32_t bursts_n; };

/* the points of a plain world, as byte ranges */
static bool Walk(const uint8_t* blob, uint32_t size, std::vector<Pt>& pts, uint32_t& head, uint16_t& ver, uint16_t& N)
{
    ver = get<uint16_t>(blob + 4); N = get<uint16_t>(blob + 6);
    const uint16_t P = get<uint16_t>(blob + 8);
    head = 20 + (ver >= 7 ? 32u : 0u);
    const uint32_t fixed = 4 + 32 + 5u * N;
    const uint32_t bh = ver >= 6 ? 12u : 10u;
    uint32_t at = head;
    for(int i = 0; i < P; i++)
    {
        Pt p; p.param = get<float>(blob + at); p.fixed = blob + at; p.fixed_n = fixed; at += fixed;
        p.noise = blob + at; p.noise_n = 1 + 8u * blob[at]; at += p.noise_n;
        p.bursts = blob + at;
        const uint16_t nb = get<uint16_t>(blob + at);
        uint32_t q = at + 2;
        if(nb == 0xFFFFu) q = at + 4;
        else for(uint16_t k = 0; k < nb; k++) { const uint16_t n = get<uint16_t>(blob + q + 8); q += bh + 2u * n; }
        p.bursts_n = q - at; at = q;
        if(at > size) return false;
        pts.push_back(p);
    }
    return true;
}

/* one plain note world filled; returns the new blob (or the old one if there
   is nothing to fill or it is not a note world) */
static Bytes Fill(const uint8_t* blob, uint32_t size, int& added, std::string& err)
{
    added = 0;
    ResonatorWorld R; R.Init();
    if(!R.Attach(blob, size)) { err = "not a world"; return Bytes(); }
    if(R.kind != 0) return Bytes(blob, blob + size);
    std::vector<Pt> pts; uint32_t head; uint16_t ver, N;
    if(!Walk(blob, size, pts, head, ver, N)) { err = "points run past the end"; return Bytes(); }
    R.voicing = 0.f; R.decay = 1.f; R.coil = 1.f;
    const float bh = ver >= 6 ? 12.f : 10.f; (void)bh;
    if(ver < 6) { err = "version under 6: no fade in the bursts; export it again"; return Bytes(); }

    /* how many modes a blend here holds: two points' partials, paired or
       not, can be more than either had (a Wurlitzer's 39 made 44), and the
       world's slot count is widened to it rather than the quietest dropped —
       every point then carries the extra slots silent, five bytes each */
    int need = N;
    for(int s = (int)std::ceil(R.lo - 1e-3f); s <= (int)std::floor(R.hi + 1e-3f); s++)
    {
        ResonatorVoice v; v.Init(); v.cap = 0; R.At((float)s, v, 48000.f);
        int c = 0; for(int k = 0; k < ResonatorBank::kMax; k++) if(v.gain[k] != 0.f && v.hz[k] > 0.f) c++;
        need = std::max(need, c);
    }
    const uint16_t N2 = (uint16_t)std::min(need, (int)ResonatorBank::kMax);
    /* the new list: every recorded point, and a new one at every semitone
       none sits on */
    struct Out { float param; int old; int ref_old; Bytes fixed, noise; };
    std::vector<Out> outs;
    for(size_t i = 0; i < pts.size(); i++) outs.push_back({pts[i].param, (int)i, -1, Bytes(), Bytes()});
    const int lo = (int)std::ceil(R.lo - 1e-3f), hi = (int)std::floor(R.hi + 1e-3f);
    for(int s = lo; s <= hi; s++)
    {
        bool have = false;
        for(const Pt& p : pts) if(std::fabs(p.param - (float)s) < 0.01f) { have = true; break; }
        if(have) continue;
        ResonatorVoice v; v.Init(); v.cap = 0;
        R.At((float)s, v, 48000.f);
        /* the point whose attack plays here: the nearer of the two either
           side, as At() takes it, followed through a reference */
        int a = 0; while(a + 1 < R.P && R.Param(a + 1) <= (float)s) a++;
        const int b = a + 1 < R.P ? a + 1 : a;
        const float pa = R.Param(a), pb = R.Param(b);
        const float t = pb > pa ? std::fmin(1.f, std::fmax(0.f, ((float)s - pa) / (pb - pa))) : 0.f;
        const int near = t < 0.5f ? a : b;
        Out o; o.param = (float)s; o.old = -1; o.ref_old = R.BurstPoint(near);
        /* the modes: the voice's, loudest N by the energy they carry (the
           export's own rank), in frequency order */
        std::vector<int> idx;
        for(int k = 0; k < ResonatorBank::kMax; k++) if(v.gain[k] != 0.f && v.hz[k] > 0.f) idx.push_back(k);
        std::sort(idx.begin(), idx.end(), [&](int x, int y) {
            const float ex = v.gain[x] * v.gain[x] / std::max(v.zeta[x] * v.hz[x], 1e-9f), ey = v.gain[y] * v.gain[y] / std::max(v.zeta[y] * v.hz[y], 1e-9f);
            return ex > ey; });
        if((int)idx.size() > N2) idx.resize(N2);
        std::sort(idx.begin(), idx.end(), [&](int x, int y) { return v.hz[x] < v.hz[y]; });
        float loudest = 0.f; for(int k : idx) loudest = std::max(loudest, std::fabs(v.gain[k]));
        if(loudest <= 0.f) loudest = 1.f;
        put(o.fixed, (float)s);
        for(int k = 0; k < 7; k++) put(o.fixed, v.stage[k]);
        put(o.fixed, loudest);
        for(int j = 0; j < N2; j++)
        {
            if(j < (int)idx.size())
            {
                const int k = idx[j];
                put(o.fixed, cents(v.hz[k])); put(o.fixed, decay8(v.zeta[k])); put(o.fixed, level8(v.gain[k], loudest));
                /* a negative gain is a phase turned by half: the level byte has no sign */
                put(o.fixed, phase8(v.gain[k] < 0.f ? v.phase[k] + (float)M_PI : v.phase[k]));
            }
            else { put(o.fixed, cents(20.f)); put(o.fixed, (uint8_t)0); put(o.fixed, (uint8_t)255); put(o.fixed, (uint8_t)0); }
        }
        const Pt& np = pts[near];
        o.noise.assign(np.noise, np.noise + np.noise_n);
        outs.push_back(o);
        added++;
    }
    if(!added) return Bytes(blob, blob + size);
    std::stable_sort(outs.begin(), outs.end(), [](const Out& x, const Out& y) { return x.param < y.param; });
    std::vector<int> newof(pts.size());
    for(size_t j = 0; j < outs.size(); j++) if(outs[j].old >= 0) newof[outs[j].old] = (int)j;

    Bytes w(blob, blob + head);
    /* a world from before version 7 has no body curve, and from 7 on the
       header is followed by one: a flat one (0 dB everywhere), which leaves
       every note as it was. Stamping 8 without it read the first point as
       the curve (tine-vel, version 6) */
    if(ver < 7) { const float flat[8] = {0}; put(w, flat, sizeof flat); }
    const uint16_t v8 = 8, P2 = (uint16_t)outs.size();
    std::memcpy(w.data() + 4, &v8, 2); std::memcpy(w.data() + 6, &N2, 2); std::memcpy(w.data() + 8, &P2, 2);
    for(const Out& o : outs)
    {
        if(o.old >= 0)
        {
            const Pt& p = pts[o.old];
            put(w, p.fixed, p.fixed_n);
            for(int j = N; j < N2; j++) { put(w, cents(20.f)); put(w, (uint8_t)0); put(w, (uint8_t)255); put(w, (uint8_t)0); }   /* the widened slots, silent */
            put(w, p.noise, p.noise_n);
            Bytes bb(p.bursts, p.bursts + p.bursts_n);
            if(get<uint16_t>(bb.data()) == 0xFFFFu) { const uint16_t r = (uint16_t)newof[get<uint16_t>(bb.data() + 2)]; std::memcpy(bb.data() + 2, &r, 2); }
            put(w, bb.data(), bb.size());
        }
        else
        {
            put(w, o.fixed.data(), o.fixed.size()); put(w, o.noise.data(), o.noise.size());
            put(w, (uint16_t)0xFFFFu); put(w, (uint16_t)newof[o.ref_old]);
        }
    }
    return w;
}

/* every semitone of a (plain) world struck in the old and in the new and
   heard: 1 s through the voice (modes with their phases, the attack), the
   loudness in 50 ms windows and the ring's spectrum (0.1-0.6 s) in octave
   bands, both in dB. Quantisation is all that may differ — a fifth of a
   cent, a tenth of a nat of zeta, a quarter dB of level, 1.4 degrees of
   phase; the decay byte's 10 % steps are most of it, and every recorded
   point carries the same — so the two must agree within 1.5 dB. (Comparing the modes
   one by one paired a piano's unison pairs, a fraction of a cent apart,
   crosswise, and failed worlds that play identically.) */
static void Hear(const ResonatorWorld& R, int s, std::vector<float>& y)
{
    ResonatorVoice v; v.Init(); R.At((float)s, v, 48000.f); v.Strike(0.8f);
    y.assign(48000, 0.f);
    for(size_t i = 0; i < y.size(); i += 48) v.Process(y.data() + i, 48);
}
static bool Verify(const uint8_t* a, uint32_t an, const uint8_t* b, uint32_t bn, std::string& why)
{
    ResonatorWorld A, B; A.Init(); B.Init();
    if(!A.Attach(a, an) || !B.Attach(b, bn)) { why = "does not attach"; return false; }
    if(A.kind != 0) return true;
    A.decay = B.decay = 1.f; A.coil = B.coil = 1.f; A.voicing = B.voicing = 0.f;
    double worst_env = 0, worst_spec = 0; int wn_env = 0, wn_spec = 0;
    std::vector<float> ya, yb;
    for(int s = (int)std::ceil(A.lo - 1e-3f); s <= (int)std::floor(A.hi + 1e-3f); s++)
    {
        Hear(A, s, ya); Hear(B, s, yb);
        double peak = 0; for(float x : ya) peak = std::max(peak, (double)std::fabs(x));
        if(peak <= 0) continue;
        double env = 0; int nw = 0;
        for(int w = 0; w + 2400 <= 48000; w += 2400)
        {
            double ea = 0, eb = 0; for(int i = w; i < w + 2400; i++) { ea += ya[i] * ya[i]; eb += yb[i] * yb[i]; }
            if(ea < 2400 * peak * peak * 1e-6) continue;                  /* under -60 dB of the peak */
            env += std::fabs(10 * std::log10((eb + 1e-30) / (ea + 1e-30))); nw++;
        }
        env = nw ? env / nw : 0;
        /* octave bands of the ring by a plain DFT at the band centres' partials is
           overkill: energy in octave bands via a bank of one-pole-pair filters would
           do, but a direct look is simpler — the difference signal's energy against
           the signal's over the ring, in dB (-inf when identical) */
        double ed = 0, es = 0; for(int i = 4800; i < 28800; i++) { const double d = ya[i] - yb[i]; ed += d * d; es += (double)ya[i] * ya[i]; }
        const double spec = 10 * std::log10((ed + 1e-30) / (es + 1e-30));
        if(env > worst_env) { worst_env = env; wn_env = s; }
        if(spec > worst_spec || s == (int)std::ceil(A.lo - 1e-3f)) { worst_spec = std::max(worst_spec, spec); wn_spec = s; }
        if(s == (int)std::ceil(A.lo - 1e-3f)) worst_spec = spec;
    }
    printf("  heard every semitone: loudness within %.2f dB (worst at note %d); the ring differs by %.1f dB of its own energy at worst (note %d)\n",
           worst_env, wn_env, worst_spec, wn_spec);
    if(worst_env > 1.5) { why = "the new world does not play what the old one blended"; return false; }
    return true;
}

int main(int argc, char** argv)
{
    if(argc < 3) { printf("usage: worldfill in.kykm out.kykm\n"); return 1; }
    FILE* f = fopen(argv[1], "rb"); if(!f) { printf("cannot read %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    Bytes in((size_t)n); if(fread(in.data(), 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; } fclose(f);
    if(n < 20 || std::memcmp(in.data(), "KYKM", 4) != 0) { printf("%s: not a world\n", argv[1]); return 1; }
    const uint8_t kind = in[11];
    Bytes out; std::string err; int total = 0;
    if(kind == 2)
    {
        /* a family: the container rebuilt around filled members */
        const uint8_t M = in[20];
        std::vector<std::pair<std::string, Bytes>> mem;
        for(int m = 0; m < M; m++)
        {
            const uint32_t off = get<uint32_t>(in.data() + 21 + 24 * m), len = get<uint32_t>(in.data() + 21 + 24 * m + 4);
            char nm[17] = {0}; std::memcpy(nm, in.data() + 21 + 24 * m + 8, 16);
            int added = 0;
            Bytes b = Fill(in.data() + off, len, added, err);
            if(b.empty()) { printf("%s member %d: %s\n", argv[1], m, err.c_str()); return 1; }
            std::string why;
            if(!Verify(in.data() + off, len, b.data(), (uint32_t)b.size(), why)) { printf("%s member %s: %s\n", argv[1], nm, why.c_str()); return 1; }
            printf("  member %-8s %d semitones added\n", nm, added);
            total += added;
            mem.push_back({nm, b});
        }
        out.assign(in.begin(), in.begin() + 20);
        out.push_back(M);
        uint32_t at = (uint32_t)(21 + 24 * M); at = (at + 3) & ~3u;
        Bytes table, body;
        for(auto& m : mem)
        {
            put(table, (uint32_t)(at + body.size())); put(table, (uint32_t)m.second.size());
            char nm[16] = {0}; std::strncpy(nm, m.first.c_str(), 15); put(table, nm, 16);
            put(body, m.second.data(), m.second.size());
            while(body.size() & 3) body.push_back(0);
        }
        put(out, table.data(), table.size());
        while(out.size() < at) out.push_back(0);
        put(out, body.data(), body.size());
    }
    else
    {
        out = Fill(in.data(), (uint32_t)n, total, err);
        if(out.empty()) { printf("%s: %s\n", argv[1], err.c_str()); return 1; }
        std::string why;
        if(!Verify(in.data(), (uint32_t)n, out.data(), (uint32_t)out.size(), why)) { printf("%s: %s\n", argv[1], why.c_str()); return 1; }
    }
    ResonatorWorld chk; chk.Init();
    if(!chk.Attach(out.data(), (uint32_t)out.size())) { printf("%s: the filled world does not attach\n", argv[2]); return 1; }
    FILE* o = fopen(argv[2], "wb"); if(!o) return 1;
    fwrite(out.data(), 1, out.size(), o); fclose(o);
    printf("%s: %d semitones added, %ld -> %zu bytes\n", argv[2], total, n, out.size());
    return 0;
}
