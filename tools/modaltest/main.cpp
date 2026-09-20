/* modaltest — a shaped record through the runtime prototype, to a WAV, and
 * against the fitter's own model of the same note.
 *
 *   build/modaltest out/fit/tine-vel/tine007.mmr out/wav/rt-tine007.wav [--velocities 6]
 *
 * Reads the record (modes, shaper, takes), strikes the voice at rising swings
 * from the softest take's to the hardest's, writes the file, and prints the
 * h2..h4 of each strike against h1 so that the table can be set beside the
 * fit's. The same chain as tools/playvel.py, in the form the module runs.
 */
#include "../../runtime/modal_bank.h"
#include "../../runtime/world.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void wav(const char* path, const std::vector<float>& x, int sr)
{
    FILE* f = fopen(path, "wb");
    auto u32 = [f](unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); };
    auto u16 = [f](unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + x.size() * 2); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(x.size() * 2);
    for(float v : x) { int s = (int)(std::fmax(-1.f, std::fmin(1.f, v)) * 32767); u16((unsigned)(s & 0xffff)); }
    fclose(f);
}

/* a .kykm world across its keyboard through the runtime: every point struck
   once at the given velocity, then the file; and a decode check against
   the first point's hertz printed for the eye */
static int world(const char* path, const char* out_path, float vel, int sr)
{
    FILE* f = fopen(path, "rb");
    if(!f) { printf("cannot read %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> blob(n);
    if(fread(blob.data(), 1, n, f) != (size_t)n) return 1;
    fclose(f);
    mb::World w;
    if(!w.Attach(blob.data(), (uint32_t)n)) { printf("not a world: %s\n", path); return 1; }
    printf("%s: %d points, %d modes, form %d, param %.0f..%.0f\n", path, w.P, w.N, w.form, w.lo, w.hi);
    float hz[mb::ModalBank::kMax], z[mb::ModalBank::kMax], g[mb::ModalBank::kMax];
    w.Decode(0, hz, z, g);
    printf("  point 0 (param %.0f): %.1f Hz zeta %.4g gain %.3f | %.1f Hz | %.1f Hz\n", w.Param(0), hz[0], z[0], g[0], hz[1], hz[2]);
    const double interval = 0.3, ring = 3.0;
    std::vector<float> out((size_t)((w.P * interval + ring) * sr), 0.0f);
    std::vector<mb::ModalVoice> voices(w.P);
    for(int i = 0; i < w.P; i++) w.At(w.Param(i), voices[i], (float)sr);
    const int block = 48;
    for(size_t pos = 0; pos < out.size(); pos += block)
    {
        const int nn = (int)std::min((size_t)block, out.size() - pos);
        float tmp[48];
        for(int i = 0; i < w.P; i++)
        {
            const size_t at = (size_t)(i * interval * sr);
            if(pos + nn <= at) continue;
            if(pos <= at && at < pos + nn) voices[i].Strike(vel);
            voices[i].Process(tmp, nn);
            for(int k = 0; k < nn; k++) out[pos + k] += tmp[k];
        }
    }
    float rms = 0; for(float s : out) rms += s * s; rms = std::sqrt(rms / out.size());
    if(rms > 0) for(float& s : out) s = std::tanh(s * 0.1f / rms);
    wav(out_path, out, sr);
    printf("  %s: %d strikes at velocity %.2f\n", out_path, w.P, vel);
    return 0;
}

int main(int argc, char** argv)
{
    if(argc < 3) { printf("usage: modaltest record.mmr out.wav [--velocities n] [--sr 48000]\n       modaltest world.kykm out.wav [--velocity v]\n"); return 1; }
    if(strstr(argv[1], ".kykm"))
    {
        float vel = 0.8f;
        for(int i = 3; i + 1 < argc; i++) if(!strcmp(argv[i], "--velocity")) vel = (float)atof(argv[++i]);
        return world(argv[1], argv[2], vel, 48000);
    }
    int vels = 6, sr = 48000;
    for(int i = 3; i < argc; i++)
    {
        if(!strcmp(argv[i], "--velocities") && i + 1 < argc) vels = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--sr") && i + 1 < argc) sr = atoi(argv[++i]);
    }
    std::vector<float> hz, zeta, gain;
    float sh[5] = { 0, 1, 1, 4000, 1 }; bool shaped = false;
    float soft = 1e9, hard = 0;
    FILE* f = fopen(argv[1], "r");
    if(!f) { printf("cannot read %s\n", argv[1]); return 1; }
    char line[4096];
    while(fgets(line, sizeof line, f))
    {
        if(!strncmp(line, "mode ", 5))
        {
            int k; float h, z, g;
            if(sscanf(line, "mode %d hz %f zeta %f gains %f", &k, &h, &z, &g) == 4) { hz.push_back(h); zeta.push_back(z); gain.push_back(g); }
        }
        else if(!strncmp(line, "shaper bell", 11))
        {
            if(sscanf(line, "shaper bell %f %f %f %f %f", &sh[0], &sh[1], &sh[2], &sh[3], &sh[4]) == 5) shaped = true;
        }
        else if(!strncmp(line, "take ", 5))
        {
            char name[64]; float g, l;
            if(sscanf(line, "take %63s swing %f level %f", name, &g, &l) == 3) { soft = std::fmin(soft, g); hard = std::fmax(hard, g); }
        }
    }
    fclose(f);
    if(hz.empty()) { printf("no modes in %s\n", argv[1]); return 1; }
    if(hard <= 0) { soft = hard = 1.0f; }

    mb::ModalVoice v;
    v.bank.Set(hz.data(), zeta.data(), gain.data(), (int)hz.size(), (float)sr);
    if(shaped) v.pickup.Set(sh[0], sh[1], sh[2], sh[3], sh[4], (float)sr);
    v.swing_soft = soft; v.swing_hard = hard;

    const double interval = 0.35, ring = 2.5;
    std::vector<float> out((size_t)((vels * interval + ring) * sr), 0.0f);
    const int block = 48;
    size_t pos = 0;
    int next = 0;
    while(pos < out.size())
    {
        if(next < vels && pos >= (size_t)(next * interval * sr)) { v.Strike(vels > 1 ? (float)next / (vels - 1) : 1.0f); next++; }
        const int n = (int)std::min((size_t)block, out.size() - pos);
        float tmp[48];
        v.Process(tmp, n);
        for(int i = 0; i < n; i++) out[pos + i] += tmp[i];
        pos += n;
    }
    float peak = 0; for(float s : out) peak = std::fmax(peak, std::fabs(s));
    if(peak > 0) for(float& s : out) s *= 0.5f / peak;
    wav(argv[2], out, sr);

    /* the bark table: h1..h4 of each strike, each rendered alone in a fresh
       voice (a tail of 28 s would otherwise sit under the next strike), from
       a 16384-point DFT by plain summation — this is a test, not a tool */
    const float f0 = hz[0];
    for(int s = 0; s < vels; s++)
    {
        mb::ModalVoice one;
        one.bank.Set(hz.data(), zeta.data(), gain.data(), (int)hz.size(), (float)sr);
        if(shaped) one.pickup.Set(sh[0], sh[1], sh[2], sh[3], sh[4], (float)sr);
        one.swing_soft = soft; one.swing_hard = hard;
        one.Strike(vels > 1 ? (float)s / (vels - 1) : 1.0f);
        const int N = 16384;
        std::vector<float> iso(N);
        one.Process(iso.data(), N);
        const size_t at = 0;
        std::vector<float>& out = iso;
        double h[4];
        for(int k = 1; k <= 4; k++)
        {
            double best = 0;
            for(double fr = k * f0 * 0.95; fr <= k * f0 * 1.05; fr += (double)sr / N / 2)
            {
                double re = 0, im = 0;
                for(int i = 0; i < N && at + i < out.size(); i++)
                {
                    const double w = 0.5 - 0.5 * std::cos(6.283185307 * i / N);
                    re += out[at + i] * w * std::cos(6.283185307 * fr * i / sr);
                    im -= out[at + i] * w * std::sin(6.283185307 * fr * i / sr);
                }
                best = std::fmax(best, std::sqrt(re * re + im * im));
            }
            h[k - 1] = 20 * std::log10(best + 1e-12);
        }
        printf("strike %d  swing %.2f  h2 %+.0f h3 %+.0f h4 %+.0f dB re h1\n", s, soft * std::pow(hard / soft, vels > 1 ? (float)s / (vels - 1) : 1.0f), h[1] - h[0], h[2] - h[0], h[3] - h[0]);
    }
    printf("%s: %zu modes, %s, %d strikes\n", argv[2], hz.size(), shaped ? "pickup" : "no pickup", vels);
    return 0;
}
