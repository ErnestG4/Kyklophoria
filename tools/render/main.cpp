/* render — the listening set: a path through the space, struck as it goes.
 *
 *   render space.msp corpus.mdb align.bin out.wav --from bar00 --to bar11
 *          [--via plate01] [--strikes 20] [--interval 0.25] [--strike 0]
 *          [--listen 0] [--rayleigh 5 3e-8] [--gain 0.5] [--pitch-normalise]
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
        r.hz.resize(N); r.G = Mat(N, c.P);
        for(int i = 0; i < N; i++)
        {
            r.hz[i] = c.m[k].hz[perm[i]];
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
    std::string from, to, via;
    int    strikes = 20, pos_s = 0, pos_l = 0;
    double interval = 0.25, alpha = 5.0, beta = 3e-8, gain = 0.5;
    bool   pitchnorm = false;
    for(int i = 5; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if(a == "--from") from = next();
        else if(a == "--to") to = next();
        else if(a == "--via") via = next();
        else if(a == "--strikes") strikes = atoi(next());
        else if(a == "--interval") interval = atof(next());
        else if(a == "--strike") pos_s = atoi(next());
        else if(a == "--listen") pos_l = atoi(next());
        else if(a == "--rayleigh") { alpha = atof(next()); beta = atof(next()); }
        else if(a == "--gain") gain = atof(next());
        else if(a == "--pitch-normalise") pitchnorm = true;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Space sp;
    if(!sp.Read(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    Corpus c;
    if(!ReadCorpus(argv[2], c)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    std::vector<std::vector<double>> vecs;
    if(!LoadAlignedVectors(argv[3], c, sp.chart, vecs)) { fprintf(stderr, "cannot read %s\n", argv[3]); return 1; }
    auto find = [&](const std::string& id) { for(size_t k = 0; k < c.m.size(); k++) if(c.m[k].id == id) return (int)k; return -1; };
    const int a = find(from), b = find(to), m = via.empty() ? -1 : find(via);
    if(a < 0 || b < 0 || (!via.empty() && m < 0)) { fprintf(stderr, "unknown model\n"); return 1; }
    double pa[8], pb[8], pm[8];
    sp.Coord(vecs[a], pa); sp.Coord(vecs[b], pb);
    if(m >= 0) sp.Coord(vecs[m], pm);

    const int    sr   = 48000;
    const double tail = 2.0;
    std::vector<float> out((size_t)(sr * (strikes * interval + tail)), 0.f);
    double peak = 0;
    for(int s = 0; s < strikes; s++)
    {
        const double t = strikes > 1 ? (double)s / (strikes - 1) : 0.0;
        double p[8];
        for(int k = 0; k < sp.K; k++)
        {
            if(m < 0) p[k] = pa[k] + (pb[k] - pa[k]) * t;
            else p[k] = t < 0.5 ? pa[k] + (pm[k] - pa[k]) * (2 * t) : pm[k] + (pb[k] - pm[k]) * (2 * t - 1);
        }
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
        for(size_t i = 0; i < r.hz.size(); i++)
        {
            const double hz = r.hz[i] * shift;
            if(!(hz > 20.0) || hz > 20000.0) continue;
            const double w    = 2 * M_PI * hz;
            const double amp  = r.G((int)i, pos_s) * r.G((int)i, pos_l) / w;
            const double zeta = 0.5 * (alpha / w + beta * w);
            const double rate = zeta * w;
            const size_t len  = std::min(out.size() - at, (size_t)(sr * std::fmin(6.0, 6.9 / std::fmax(rate, 0.5))));
            for(size_t n = 0; n < len; n++)
            {
                const double tt = (double)n / sr;
                out[at + n] += (float)(amp * std::sin(w * tt) * std::exp(-rate * tt));
            }
        }
    }
    for(float v : out) peak = std::fmax(peak, std::fabs(v));
    if(peak > 0) for(float& v : out) v = (float)(v / peak * gain);
    WriteWav(argv[4], out, sr);
    printf("%s: %s -> %s%s, %d strikes, peak normalised\n", argv[4], from.c_str(), to.c_str(),
           via.empty() ? "" : (" via " + via).c_str(), strikes);
    return 0;
}
