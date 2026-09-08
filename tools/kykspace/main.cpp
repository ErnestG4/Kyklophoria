/* kykspace — generate and inspect space files (docs/space-format.md).
 *
 *   kykspace gen out.kyk [--N 4 --side 4 --K 64 --P 8 --seed 1 --name harmonic --wrap <axis>]
 *   kykspace info file.kyk
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include "kyk_space.h"
#include "kyk_gen.h"

using namespace kyk;

static int Info(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); return 1; }
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)), {});
    Space                s;
    const SpaceError     e = s.Attach(blob.data(), blob.size());
    if(e != SpaceError::Ok) { printf("%s: INVALID (%s)\n", path.c_str(), Space::ErrorName(e)); return 1; }
    const SpaceHeader& h = s.Header();
    char               name[33];
    std::memcpy(name, h.name, 32);
    name[32] = 0;
    printf("%s\n  name      %s\n  N=%d side=%d points=%u  K=%d P=%d  phases=%s seed=%u\n", path.c_str(), name, s.N(),
           s.Side(), s.PointCount(), s.K(), s.P(), s.HasPhases() ? "stored" : "derived", h.phase_seed);
    printf("  topology  ");
    for(int a = 0; a < s.N(); a++) printf("%s%s", a ? "," : "", s.TopoOf(a) == Topo::Clamp ? "clamp" : s.TopoOf(a) == Topo::Wrap ? "wrap" : "sphere");
    printf("\n  bytes     %zu (%zu header + %u × %zu floats)\n", blob.size(), sizeof(SpaceHeader), s.PointCount(), s.Stride());
    /* sanity: finite values, spectral centroid range */
    double cmin = 1e9, cmax = 0;
    size_t bad = 0;
    for(uint32_t i = 0; i < s.PointCount(); i++)
    {
        const float* m = s.Mags(i);
        double       num = 0, den = 0;
        for(int k = 0; k < s.K(); k++)
        {
            if(!std::isfinite(m[k]) || m[k] < 0.f) bad++;
            num += (k + 1) * (double)m[k] * m[k];
            den += (double)m[k] * m[k];
        }
        const double c = den > 0 ? num / den : 0;
        cmin = std::fmin(cmin, c); cmax = std::fmax(cmax, c);
        for(int j = 0; j < s.P(); j++) if(!std::isfinite(s.Payload(i)[j])) bad++;
    }
    printf("  centroid  %.2f .. %.2f harmonics\n  bad values %zu\n", cmin, cmax, bad);
    return bad ? 1 : 0;
}

int main(int argc, char** argv)
{
    if(argc < 3) { fprintf(stderr, "usage: kykspace gen out.kyk [opts] | kykspace info file.kyk\n"); return 2; }
    const std::string cmd = argv[1], path = argv[2];
    if(cmd == "info") return Info(path);
    if(cmd != "gen") { fprintf(stderr, "unknown command %s\n", cmd.c_str()); return 2; }
    GenParams gp;
    std::string name = "harmonic";
    for(int i = 3; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if(a == "--N") gp.n = atoi(next());
        else if(a == "--side") gp.side = atoi(next());
        else if(a == "--K") gp.k = atoi(next());
        else if(a == "--P") gp.p = atoi(next());
        else if(a == "--seed") gp.seed = (uint32_t)strtoul(next(), nullptr, 0);
        else if(a == "--name") name = next();
        else if(a == "--wrap") { int ax = atoi(next()); if(ax >= 0 && ax < kMaxN) gp.topo[ax] = (uint8_t)Topo::Wrap; }
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    gp.name = name.c_str();
    std::vector<uint8_t> blob(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
    const size_t n = BuildLattice(gp, blob.data(), blob.size());
    if(!n) { fprintf(stderr, "generator refused the params (N≤%d, K≤%d, P≤%d, side≥2)\n", kMaxN, kMaxK, kMaxP); return 1; }
    std::ofstream out(path, std::ios::binary);
    out.write((const char*)blob.data(), (std::streamsize)n);
    if(!out) { fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
    printf("wrote %s (%zu bytes)\n", path.c_str(), n);
    return Info(path);
}
