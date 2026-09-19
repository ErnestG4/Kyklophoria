/* render — the listening set: a path through the space, struck as it goes.
 *
 *   render space.msp corpus.mdb align.bin out.wav --from bar00 --to bar11
 *          [--via plate01] [--strikes 20] [--interval 0.25] [--strike 0]
 *          [--listen 0] [--rayleigh 5 3e-8] [--gain 0.5] [--pitch-normalise]
 *          [--mode strikes|glide] [--walk seed --seconds 12]
 *
 * Two modes. `strikes` is a row of separate hits along the path, each a model
 * struck once and left to ring — the corpus as a keyboard. `glide` is one bank
 * of oscillators that keeps ringing while the point moves: each mode's phase
 * accumulates at whatever its frequency is *now*, its envelope decays at
 * whatever its damping is now, its weight is whatever the gain pattern says
 * now, and a strike adds to the envelope rather than resetting it. That is the
 * sound Kyklophoria makes of a space — motion while sounding — and it is what
 * the listening set was missing. Nothing physical happens to an object whose
 * shape changes while it rings; this is the instrument the space would be.
 *
 * `--walk seed` replaces the straight path with a smooth wander through the
 * cube — each axis a sum of three slow sines, like an orbit — for --seconds.
 *
 * A straight line in the space from one corpus model's coordinates to
 * another's (through a third, if asked), sampled at --strikes points; at each,
 * the modal model there is struck once and rings over the strikes that follow.
 * A strike is a sum of decaying sines, mode i at amplitude
 * G[i][strike] G[i][listen] / omega_i, which is the displacement impulse
 * response of a mass-normalised mode heard at a point.
 *
 * Damping here is Rayleigh with the corpus material's coefficients, because a
 * listening set with every mode decaying at the same rate sounds like a
 * synthesiser and the point of listening is to hear whether it sounds like a
 * thing. The grade uses uniform damping and says so; this is the one place
 * the choice is different, and it is a flag.
 *
 * Every strike is injected at the same energy — the amplitude vector at the
 * moment of the strike is normalised, weighted by each mode's decay so that
 * the energy over the ring is equal and not the energy at the instant —
 * because mass-normalised gains make a light object tens of times louder than
 * a heavy one and Rayleigh damping makes a bell at 4 kHz die five times faster
 * than a bar at 1 kHz, and a listening set that is one loud bell and fifteen
 * seconds of whisper is a listening set of one bell. Level is not what is
 * being listened for. Modes below 40 Hz are not
 * rendered, for the same reason the grader starts its bands there.
 *
 * The file is levelled to about -20 dBFS RMS with a soft knee above 0.6, since
 * a strike's peak is ten times its RMS on a fast-decaying model.
 *
 * 48 kHz, 16-bit, mono. Nothing about this is fast and nothing needs to be.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include "../common/corpus.h"
#include "../common/space.h"

using namespace mb;

static bool LoadAlignedVectors(const std::string& path, const Corpus& c, const Chart& chart, std::vector<std::vector<double>>& vecs)
{
    FILE* f = fopen(path.c_str(), "rb");
    if(!f) return false;
    char magic[4]; uint32_t hdr[3];
    if(fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MALN", 4) != 0 || fread(hdr, 4, 3, f) != 3) { fclose(f); return false; }
    const int M = (int)hdr[0], N = (int)hdr[1];
    vecs.resize(M);
    for(int k = 0; k < M; k++)
    {
        std::vector<int32_t> perm(N), sg(N);
        if(fread(perm.data(), 4, N, f) != (size_t)N || fread(sg.data(), 4, N, f) != (size_t)N) { fclose(f); return false; }
        Rep r;
        r.hz.resize(N); r.zeta.resize(N); r.G = Mat(N, c.P);
        for(int i = 0; i < N; i++)
        {
            r.hz[i] = c.m[k].hz[perm[i]];
            r.zeta[i] = c.m[k].zeta[perm[i]];
            for(int p = 0; p < c.P; p++) r.G(i, p) = sg[i] * c.m[k].G(perm[i], p, c.P);
        }
        chart.ToVector(r, vecs[k]);
    }
    fclose(f);
    return true;
}

static void WriteWav(const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = fopen(path.c_str(), "wb");
    if(!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    const uint32_t data = (uint32_t)x.size() * 2, riff = 36 + data;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(riff); fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(1);
    u32((uint32_t)sr); u32((uint32_t)sr * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(data);
    for(float v : x)
    {
        const int s = (int)std::lround(std::fmax(-1.f, std::fmin(1.f, v)) * 32767.f);
        u16((uint16_t)(int16_t)s);
    }
    fclose(f);
}

int main(int argc, char** argv)
{
    if(argc < 5) { fprintf(stderr, "render space.msp corpus.mdb align.bin out.wav --from id --to id [options]\n"); return 2; }
    std::string from, to, via, family;
    int    strikes = 20, pos_s = 0, pos_l = 0;
    double interval = 0.25, alpha = 5.0, beta = 3e-8, gain = 0.5;
    bool   pitchnorm = false, glide = false;
    int    walk = -1;
    double seconds = 12.0;
    for(int i = 5; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if(a == "--from") from = next();
        else if(a == "--to") to = next();
        else if(a == "--via") via = next();
        else if(a == "--family") family = next();   /* lowest to highest parameter of that family */
        else if(a == "--strikes") strikes = atoi(next());
        else if(a == "--interval") interval = atof(next());
        else if(a == "--strike") pos_s = atoi(next());
        else if(a == "--listen") pos_l = atoi(next());
        else if(a == "--rayleigh") { alpha = atof(next()); beta = atof(next()); }
        else if(a == "--gain") gain = atof(next());
        else if(a == "--pitch-normalise") pitchnorm = true;
        else if(a == "--mode") glide = std::string(next()) == "glide";
        else if(a == "--walk") walk = atoi(next());
        else if(a == "--seconds") seconds = atof(next());
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Space sp;
    if(!sp.Read(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    Corpus c;
    if(!ReadCorpus(argv[2], c)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    std::vector<std::vector<double>> vecs;
    if(!LoadAlignedVectors(argv[3], c, sp.chart, vecs)) { fprintf(stderr, "cannot read %s\n", argv[3]); return 1; }
    if(!family.empty())
    {
        const int fi = c.FamilyIndex(family);
        int lo = -1, hi = -1;
        for(size_t k = 0; k < c.m.size(); k++)
            if(c.m[k].family == fi)
            {
                if(lo < 0 || c.m[k].param < c.m[lo].param) lo = (int)k;
                if(hi < 0 || c.m[k].param > c.m[hi].param) hi = (int)k;
            }
        if(lo < 0) { fprintf(stderr, "no family %s\n", family.c_str()); return 1; }
        from = c.m[lo].id; to = c.m[hi].id;
    }
    auto find = [&](const std::string& id) { for(size_t k = 0; k < c.m.size(); k++) if(c.m[k].id == id) return (int)k; return -1; };
    const int a = walk >= 0 ? -2 : find(from), b = walk >= 0 ? -2 : find(to), m = via.empty() ? -1 : find(via);
    if(walk < 0 && (a < 0 || b < 0 || (!via.empty() && m < 0))) { fprintf(stderr, "unknown model\n"); return 1; }
    double pa[8] = {0}, pb[8] = {0}, pm[8] = {0};
    if(walk < 0) { sp.Coord(vecs[a], pa); sp.Coord(vecs[b], pb); if(m >= 0) sp.Coord(vecs[m], pm); }
    /* the walk: three slow sines an axis, phases and rates from the seed */
    double wf[8][3], wp[8][3];
    {
        uint32_t s = (uint32_t)(walk < 0 ? 1 : walk) * 2654435761u + 7u;
        auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0 / 16777216.0); };
        for(int k = 0; k < 8; k++)
            for(int j = 0; j < 3; j++) { wf[k][j] = (0.03 + 0.12 * rnd()) * (j + 1); wp[k][j] = 2 * M_PI * rnd(); }
    }
    /* where the path is at t in [0,1] */
    auto place = [&](double t, double* p) {
        for(int k = 0; k < sp.K; k++)
        {
            if(walk >= 0)
            {
                double x = 0.5;
                for(int j = 0; j < 3; j++) x += 0.14 * std::sin(2 * M_PI * wf[k][j] * t * seconds + wp[k][j]);
                p[k] = std::fmin(1.0, std::fmax(0.0, x));
            }
            else if(m < 0) p[k] = pa[k] + (pb[k] - pa[k]) * t;
            else p[k] = t < 0.5 ? pa[k] + (pm[k] - pa[k]) * (2 * t) : pm[k] + (pb[k] - pm[k]) * (2 * t - 1);
        }
    };

    const int    sr   = 48000;
    const double tail = 2.0;
    const double total = walk >= 0 ? seconds : strikes * interval;
    std::vector<float> out((size_t)(sr * (total + tail)), 0.f);
    double peak = 0;
    if(glide)
    {
        /* one oscillator bank, parameters refreshed every block */
        const int N = sp.chart.N, block = 96;
        std::vector<double> phase(N, 0.0), env(N, 0.0), w(N, 0.0), amp(N, 0.0), rate(N, 0.0);
        const double dt = 1.0 / sr;
        for(size_t at = 0; at + block <= out.size(); at += block)
        {
            const double tt = (double)at / sr;
            const double t  = std::fmin(1.0, tt / total);
            double p[8];
            place(t, p);
            Rep r;
            sp.At(p, r);
            double shift = 1.0;
            if(pitchnorm)
            {
                double lowest = 0;
                for(int i = 0; i < N; i++) if(r.hz[i] > 20 && r.hz[i] < 20000 && (lowest == 0 || r.hz[i] < lowest)) lowest = r.hz[i];
                if(lowest > 0) shift = 440.0 / lowest;
            }
            double e2 = 0;
            for(int i = 0; i < N; i++)
            {
                const double hz = r.hz[i] * shift;
                if(!(hz > 40.0) || hz > 20000.0) { amp[i] = 0.0; w[i] = 0.0; rate[i] = 50.0; continue; }
                w[i]    = 2 * M_PI * hz;
                amp[i]  = r.G(i, pos_s) * r.G(i, pos_l) / w[i];
                /* the space's own decay where it carries one, Rayleigh where not */
                rate[i] = (i < (int)r.zeta.size() ? r.zeta[i] : 0.5 * (alpha / w[i] + beta * w[i])) * w[i];
                e2 += amp[i] * amp[i] / (2.0 * rate[i]);
            }
            /* a strike on every interval, adding to whatever is still ringing,
               at unit energy */
            const long this_strike = (long)std::floor(tt / interval), prev_strike = (long)std::floor((tt - block * dt) / interval);
            if(tt < total && this_strike != prev_strike && e2 > 0)
                for(int i = 0; i < N; i++) env[i] += 1.0 / std::sqrt(e2);
            for(int n = 0; n < block; n++)
            {
                double y = 0;
                for(int i = 0; i < N; i++)
                {
                    if(amp[i] == 0.0) continue;
                    phase[i] += w[i] * dt;
                    if(phase[i] > 2 * M_PI) phase[i] -= 2 * M_PI;
                    env[i] *= std::exp(-rate[i] * dt);
                    y += amp[i] * env[i] * std::sin(phase[i]);
                }
                out[at + n] = (float)y;
            }
        }
    }
    else for(int s = 0; s < strikes; s++)
    {
        const double t = strikes > 1 ? (double)s / (strikes - 1) : 0.0;
        double p[8];
        place(t, p);
        Rep r;
        sp.At(p, r);
        /* pitch normalisation for listening: the lowest audible mode to 440 */
        double shift = 1.0;
        if(pitchnorm)
        {
            double lowest = 0;
            for(size_t i = 0; i < r.hz.size(); i++) if(r.hz[i] > 20 && r.hz[i] < 20000 && (lowest == 0 || r.hz[i] < lowest)) lowest = r.hz[i];
            if(lowest > 0) shift = 440.0 / lowest;
        }
        const size_t at = (size_t)(s * interval * sr);
        double e2 = 0;
        for(size_t i = 0; i < r.hz.size(); i++)
        {
            const double hz = r.hz[i] * shift;
            if(!(hz > 40.0) || hz > 20000.0) continue;
            const double w2 = 2 * M_PI * hz;
            const double aa = r.G((int)i, pos_s) * r.G((int)i, pos_l) / w2;
            const double z2 = i < r.zeta.size() ? r.zeta[i] : 0.5 * (alpha / w2 + beta * w2);
            e2 += aa * aa / (2.0 * z2 * w2);
        }
        const double norm = e2 > 0 ? 1.0 / std::sqrt(e2) : 0.0;
        for(size_t i = 0; i < r.hz.size(); i++)
        {
            const double hz = r.hz[i] * shift;
            if(!(hz > 40.0) || hz > 20000.0) continue;
            const double w    = 2 * M_PI * hz;
            const double amp  = norm * r.G((int)i, pos_s) * r.G((int)i, pos_l) / w;
            const double zeta = i < r.zeta.size() ? r.zeta[i] : 0.5 * (alpha / w + beta * w);
            const double rate = zeta * w;
            const size_t len  = std::min(out.size() - at, (size_t)(sr * std::fmin(6.0, 6.9 / std::fmax(rate, 0.5))));
            for(size_t n = 0; n < len; n++)
            {
                const double tt = (double)n / sr;
                out[at + n] += (float)(amp * std::sin(w * tt) * std::exp(-rate * tt));
            }
        }
    }
    /* Level: the whole file to -20 dBFS RMS, then a soft knee for the strike
       peaks, which are ten times the RMS on a fast-decaying model. Peak
       normalisation left the files at -30 dB and quiet is not what anyone is
       listening for either. */
    double e = 0;
    for(float v : out) e += (double)v * v;
    const double rms = std::sqrt(e / (double)out.size());
    const double g   = rms > 0 ? 0.1 * gain * 2.0 / rms : 1.0;
    for(float& v : out)
    {
        const double y = v * g;
        v = (float)(std::fabs(y) < 0.6 ? y : (y > 0 ? 0.6 + 0.4 * std::tanh((y - 0.6) / 0.4) : -0.6 - 0.4 * std::tanh((-y - 0.6) / 0.4)));
        peak = std::fmax(peak, std::fabs(v));
    }
    WriteWav(argv[4], out, sr);
    if(walk >= 0) printf("%s: walk %d, %.0f s, %s, peak normalised\n", argv[4], walk, seconds, glide ? "glide" : "strikes");
    else printf("%s: %s -> %s%s, %s, peak normalised\n", argv[4], from.c_str(), to.c_str(),
                via.empty() ? "" : (" via " + via).c_str(), glide ? "glide" : "strikes");
    return 0;
}
