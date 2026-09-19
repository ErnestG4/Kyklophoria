/* epigen — Epi's physical models as a generator of labelled notes.
 *
 * Epi (github.com/DatanoiseTV/epi, GPL-3.0) computes a tine piano, a reed
 * piano, an electric grand, a clavinet and an acoustic grand from physics,
 * with the transducer's nonlinearity in the loop: the harmonics come from
 * the pickup's field, and playing harder growls. That is the thing a linear
 * modal fit cannot learn from one velocity of recording, and here every
 * note can be had at every velocity, at every pickup height, in eight
 * metals. What it teaches is Epi's physics, not a Rhodes's; it is the bench
 * on which the fitter's nonlinear stage is built and checked before it meets
 * the recordings, which stay the truth.
 *
 * One note per file, `<out>/<dyn>/<note>.wav` — the folders layout fitset
 * reads (C4 = 60). Mono, 48 kHz, 24-bit. The engine's absolute level is
 * volts at a coil; every file in a set is scaled by the same gain, taken
 * from the loudest, so that velocity keeps its level across the set — the
 * one thing a per-file normalisation would destroy, and the one thing the
 * nonlinear fit needs. Dry: no room, no cabinet, no tremolo, no phaser.
 *
 * Build (from ModalBake): make build/epigen — links ../epi/src/epi/dsp.
 * Run:   build/epigen out/gen/tine --instrument 0 --notes 28 100 4
 *            --velocities 0.15,0.3,0.5,0.7,0.85,1.0 --seconds 6
 *            [--set pickupPos=-0.5] [--set material=2] [--set transducer=0] ...
 */
#include "epi/dsp/EpiEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace epi;

static const char* kNames[12] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };

static void writeWav(const std::string& path, const std::vector<float>& m, int sr)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if(!f) { std::printf("cannot write %s\n", path.c_str()); std::exit(1); }
    auto u32 = [f](unsigned v) { unsigned char b[4] { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) }; std::fwrite(b, 1, 4, f); };
    auto u16 = [f](unsigned v) { unsigned char b[2] { (unsigned char)v, (unsigned char)(v >> 8) }; std::fwrite(b, 1, 2, f); };
    const int n = (int)m.size(), bytes = n * 3;
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(sr); u32(sr * 3); u16(3); u16(24);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for(float v : m)
    {
        const int s = (int)(std::clamp(v, -1.0f, 1.0f) * 8388607.0f);
        unsigned char b[3] { (unsigned char)(s & 0xff), (unsigned char)((s >> 8) & 0xff), (unsigned char)((s >> 16) & 0xff) };
        std::fwrite(b, 1, 3, f);
    }
    std::fclose(f);
}

/* Every EngineParams field a sweep might want, by name. */
static bool set(EngineParams& p, const std::string& k, double v)
{
    std::map<std::string, float*> f = {
        { "tuneCents", &p.tuneCents }, { "velCurve", &p.velCurve }, { "hammerHard", &p.hammerHard },
        { "hammerMass", &p.hammerMass }, { "escapement", &p.escapement }, { "strikeNoise", &p.strikeNoise },
        { "damperGrip", &p.damperGrip }, { "tipMass", &p.tipMass }, { "resDamp", &p.resDamp },
        { "barCouple", &p.barCouple }, { "barTune", &p.barTune }, { "bodyMix", &p.bodyMix },
        { "nonlinAmt", &p.nonlinAmt }, { "pickupPos", &p.pickupPos }, { "pickupDist", &p.pickupDist },
        { "coilFreq", &p.coilFreq }, { "coilQ", &p.coilQ }, { "coilSat", &p.coilSat },
        { "preampDrive", &p.preampDrive }, { "bassDb", &p.bassDb }, { "trebleDb", &p.trebleDb },
        { "clarityDb", &p.clarityDb }, { "bodySize", &p.bodySize }, { "wearAmount", &p.wearAmount },
        { "cabMix", &p.cabMix }, { "spaceMix", &p.spaceMix }, { "spaceSize", &p.spaceSize } };
    std::map<std::string, int*> i = {
        { "instrument", &p.instrument }, { "pickupSel", &p.pickupSel }, { "transducer", &p.transducer },
        { "material", &p.material }, { "clavSwitch", &p.clavSwitch }, { "bodyMat", &p.bodyMat },
        { "damperFelt", &p.damperFelt }, { "keyBed", &p.keyBed }, { "hammerMat", &p.hammerMat },
        { "roomProfile", &p.roomProfile }, { "softMode", &p.softMode } };
    if(f.count(k)) { *f[k] = (float)v; return true; }
    if(i.count(k)) { *i[k] = (int)v; return true; }
    return false;
}

int main(int argc, char** argv)
{
    if(argc < 2) { std::printf("usage: epigen <outdir> [--instrument n] [--notes lo hi step] [--velocities a,b,..] [--seconds s] [--hold s] [--set k=v]...\n"); return 1; }
    const std::string out = argv[1];
    int lo = 28, hi = 100, step = 4;
    std::vector<double> vels = { 0.15, 0.3, 0.5, 0.7, 0.85, 1.0 };
    double seconds = 6.0, hold = -1.0;
    EngineParams p;
    /* dry */
    p.cabMix = 0.0f; p.spaceMix = 0.0f; p.tremDepth = 0.0f; p.phaserMix = 0.0f;
    for(int a = 2; a < argc; a++)
    {
        std::string s = argv[a];
        auto next = [&]() { if(a + 1 >= argc) { std::printf("%s needs a value\n", s.c_str()); std::exit(1); } return std::string(argv[++a]); };
        if(s == "--instrument") p.instrument = atoi(next().c_str());
        else if(s == "--notes") { lo = atoi(next().c_str()); hi = atoi(next().c_str()); step = atoi(next().c_str()); }
        else if(s == "--seconds") seconds = atof(next().c_str());
        else if(s == "--hold") hold = atof(next().c_str());
        else if(s == "--velocities") { vels.clear(); std::string v = next(); size_t i = 0; while(i < v.size()) { size_t j = v.find(',', i); if(j == std::string::npos) j = v.size(); vels.push_back(atof(v.substr(i, j - i).c_str())); i = j + 1; } }
        else if(s == "--set") { std::string kv = next(); size_t e = kv.find('='); if(e == std::string::npos || !set(p, kv.substr(0, e), atof(kv.substr(e + 1).c_str()))) { std::printf("unknown parameter %s\n", kv.c_str()); return 1; } }
        else { std::printf("unknown flag %s\n", s.c_str()); return 1; }
    }
    if(hold < 0) hold = seconds;   /* the key stays down: the note rings, no damper */
    const int sr = 48000, total = (int)(seconds * sr);
    for(size_t i = 1; i <= out.size(); i++)          /* mkdir -p */
        if(i == out.size() || out[i] == '/') mkdir(out.substr(0, i).c_str(), 0755);

    /* render everything first, then one gain for the set */
    struct Take { std::string path; std::vector<float> m; };
    std::vector<Take> takes;
    float peak = 0.0f;
    for(double v : vels)
    {
        char dyn[32]; std::snprintf(dyn, sizeof dyn, "v%03d", (int)std::lround(v * 100));
        mkdir((out + "/" + dyn).c_str(), 0755);
        for(int n = lo; n <= hi; n += step)
        {
            EpiEngine engine;
            engine.prepare(sr, 512);
            std::vector<float> L(total, 0.0f), R(total, 0.0f);
            NoteEvent on { 0, NoteEvent::noteOn, n, (float)v };
            NoteEvent off { (int)(hold * sr), NoteEvent::noteOff, n, 0.0f };
            const int block = 128;
            for(int pos = 0; pos < total; pos += block)
            {
                const int k = std::min(block, total - pos);
                std::vector<NoteEvent> ev;
                if(pos == 0) ev.push_back(on);
                if(off.offset >= pos && off.offset < pos + k) { NoteEvent e = off; e.offset -= pos; ev.push_back(e); }
                engine.process(L.data() + pos, R.data() + pos, k, p, ev.data(), (int)ev.size());
            }
            std::vector<float> m(total);
            for(int i = 0; i < total; i++) { m[i] = 0.5f * (L[i] + R[i]); peak = std::max(peak, std::fabs(m[i])); }
            char name[64]; std::snprintf(name, sizeof name, "%s%d.wav", kNames[n % 12], n / 12 - 1);
            takes.push_back({ out + "/" + dyn + "/" + name, std::move(m) });
        }
    }
    const float g = peak > 1e-9f ? 0.891f / peak : 1.0f;   /* -1 dBFS on the loudest */
    for(auto& t : takes)
    {
        for(float& v : t.m) v *= g;
        writeWav(t.path, t.m, sr);
    }
    std::printf("%s: %zu notes, %zu velocities, %.1f s each, instrument %d, set peak %.4g scaled by %.3g\n",
                out.c_str(), takes.size() / vels.size(), vels.size(), seconds, p.instrument, peak, g);
    return 0;
}
