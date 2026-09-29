/* exclevel — a world's trained exciters checked for level before anyone hears
 * them (29 September; the night before, a trained re-strike ran 300-690x and
 * "C1 may have just deafened me").
 *
 *   exclevel <world.kykm> [ratio]      exit 1 if any note fails
 *
 * Every point, through the engine the module runs (Kyklophoria core), at
 * velocity 0.1 to 1.0: struck from silence, and struck again 100 ms later
 * while it rings — with the Trained exciter and with the Recorded attack.
 * A note fails if its trained peak is over `ratio` (2 by default) times its
 * recorded peak, or over 2 in the voice's units (a struck note peaks under 1),
 * or not finite. Prints the worst ten and a line a failure.
 */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <fstream>
#include <algorithm>
#include "kyk_stereo.h"

using namespace kyk;

static float Peak(const World& w, ResExciter x, float param, float vel, bool& finite)
{
    Engine e; e.Init(&w, 48000.f); e.gain = 1.f; e.SetPolyphony(4); e.SetExciterType(x);
    e.SetF0(440.f * std::exp2((param - 69.f) / 12.f));
    std::vector<float> y(48);
    float pk = 0.f; finite = true;
    e.Strike(vel);
    for(int b = 0; b < 100; b++) { e.Process(y.data(), 48); for(float s : y) { finite = finite && std::isfinite(s); pk = std::fmax(pk, std::fabs(s)); } }
    e.Strike(vel);                                   /* again, while it rings */
    for(int b = 0; b < 300; b++) { e.Process(y.data(), 48); for(float s : y) { finite = finite && std::isfinite(s); pk = std::fmax(pk, std::fabs(s)); } }
    return pk / e.PhaseTrim();
}

int main(int argc, char** argv)
{
    if(argc < 2) { std::fprintf(stderr, "exclevel <world.kykm> [ratio]\n"); return 2; }
    const float ratio = argc > 2 ? (float)std::atof(argv[2]) : 2.f;
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    World W; W.UseResonate(b.data(), (uint32_t)b.size());
    if(!W.IsResonate()) { std::fprintf(stderr, "not a resonate world: %s\n", argv[1]); return 1; }
    const ResonatorWorld& R = W.Res();
    struct Row { float param, vel, trained, recorded; bool finite; };
    std::vector<Row> rows; int fails = 0, notes = 0;
    for(int i = 0; i < R.P; i++)
    {
        static ResonatorVoice v; v.Init(); R.At(R.Param(i), v, 48000.f);
        if(!v.exc_type) continue;                    /* no trained exciter: it plays its recorded attack */
        notes++;
        for(int vi = 1; vi <= 10; vi++)
        {
            const float vel = vi / 10.f; bool f1, f2;
            const float pt = Peak(W, ResExciter::Trained, R.Param(i), vel, f1), pr = Peak(W, ResExciter::Recorded, R.Param(i), vel, f2);
            rows.push_back({R.Param(i), vel, pt, pr, f1 && f2});
            const bool bad = !(f1 && f2) || pt > ratio * pr || pt > 2.f;
            if(bad) { fails++; std::printf("  FAIL note %.1f velocity %.1f: trained peak %.3f, recorded %.3f%s\n", R.Param(i), vel, pt, pr, f1 ? "" : " (not finite)"); }
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& c) { return a.trained / std::max(a.recorded, 1e-6f) > c.trained / std::max(c.recorded, 1e-6f); });
    std::printf("the loudest ten, trained against recorded (a strike and a re-strike, every velocity):\n");
    for(size_t k = 0; k < rows.size() && k < 10; k++)
        std::printf("  note %5.1f velocity %.1f: %.3f vs %.3f (x%.2f)\n", rows[k].param, rows[k].vel, rows[k].trained, rows[k].recorded, rows[k].trained / std::max(rows[k].recorded, 1e-6f));
    std::printf("%s: %d notes with a trained exciter, %d of %zu strikes over x%.1f or 2.0 — %s\n", argv[1], notes, fails, rows.size(), ratio, fails ? "FAILS" : "safe");
    return fails ? 1 : 0;
}
