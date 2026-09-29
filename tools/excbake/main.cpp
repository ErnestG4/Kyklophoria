/* excbake — a world with its trained exciters: format 9 (Kyklophoria
 * core/kyk_resonate.h, ResonatorWorld::Exciter).
 *
 *   excbake <world.kykm> <results.tsv> <out.kykm>
 *
 * results.tsv is what excfit appends (EXCFIT_RESULTS): a line a note — midi,
 * k, alpha, mu, mass, noise, noise band, speed pp/mf/ff, gain pp/mf/ff, the
 * mode count, and a weight a mode in the point's order. Each point of the
 * world is copied byte for byte and its exciter appended: a hammer where a
 * result has its note (to a hundredth of a semitone) and the same mode count,
 * none where not — that point plays its recorded attack. The header's version
 * becomes 9; a version 8 world is the input (a 9 is refused, a family too:
 * one member at a time). The runtime's own walk finds the points, so the
 * layout is the module's and not a second reading of it.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include "kyk_resonate.h"

using namespace kyk;

struct Result { float midi; float f[12]; int n; std::vector<float> w; };

int main(int argc, char** argv)
{
    if(argc != 4) { std::fprintf(stderr, "excbake <world.kykm> <results.tsv> <out.kykm>\n"); return 2; }
    std::vector<uint8_t> blob;
    { std::ifstream f(argv[1], std::ios::binary); blob.assign(std::istreambuf_iterator<char>(f), {}); }
    ResonatorWorld R; R.Init();
    if(!R.Attach(blob.data(), (uint32_t)blob.size())) { std::fprintf(stderr, "not a world: %s\n", argv[1]); return 1; }
    if(R.ver != 8 || R.kind == 2) { std::fprintf(stderr, "a version 8 note or index world is the input (this is version %d, kind %d)\n", R.ver, R.kind); return 1; }

    std::vector<Result> res;
    {
        std::ifstream f(argv[2]); std::string line;
        while(std::getline(f, line))
        {
            if(line.empty() || line[0] == '#') continue;
            std::istringstream ss(line); Result r;
            ss >> r.midi; for(float& x : r.f) ss >> x; ss >> r.n;
            r.w.resize(r.n > 0 ? r.n : 0); for(float& x : r.w) ss >> x;
            if(ss.fail()) { std::fprintf(stderr, "a malformed line in %s\n", argv[2]); return 1; }
            res.push_back(std::move(r));
        }
    }

    std::vector<uint8_t> out(blob.begin(), blob.begin() + R.HeaderBytes());
    const uint16_t nine = 9; std::memcpy(out.data() + 4, &nine, 2);
    int with = 0;
    for(int i = 0; i < R.P; i++)
    {
        const uint8_t* a = R.Point(i);
        const uint8_t* e = R.BurstEnd(R.NoiseEnd(a + R.FixedBytes()));
        out.insert(out.end(), a, e);
        const float midi = R.Param(i);
        const Result* hit = nullptr;
        for(auto& r : res) if(std::fabs(r.midi - midi) < 0.01f) hit = &r;       /* the last line for a note wins: a retrain appends */
        if(!hit || hit->n != R.N)
        {
            if(hit) std::fprintf(stderr, "  note %.2f: %d modes in the result, %d in the world — left with its recorded attack\n", midi, hit->n, R.N);
            out.push_back(0);
            continue;
        }
        out.push_back(1);
        const uint8_t* fb = (const uint8_t*)hit->f; out.insert(out.end(), fb, fb + sizeof hit->f);
        for(int k = 0; k < R.N; k++)
        {
            const float w = hit->w[k];
            int b = w > 0.f ? (int)std::lround(255.0 + 40.0 * std::log10(w)) : 0;
            out.push_back((uint8_t)(b < 1 ? (w > 0.f ? 1 : 0) : b > 255 ? 255 : b));
        }
        with++;
    }
    ResonatorWorld C; C.Init();
    if(!C.Attach(out.data(), (uint32_t)out.size())) { std::fprintf(stderr, "the result does not attach — not written\n"); return 1; }
    std::ofstream(argv[3], std::ios::binary).write((const char*)out.data(), (std::streamsize)out.size());
    std::printf("%s: %d of %d points with a trained hammer, %zu -> %zu bytes\n", argv[3], with, R.P, blob.size(), out.size());
    return 0;
}
