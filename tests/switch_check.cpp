/* switch_check — arriving at a world must be the same as starting there.
 *
 * Four bugs this week were one sentence: state derived from a world, not
 * invalidated when the world changed. The phase spectrum, the payload cache,
 * the band-limit hold, the morph aim offset. Each was found by hand, after it
 * had shipped, and each could have been found here.
 *
 * They survived because of a blind spot rather than bad luck: every golden
 * renders a single world from boot and never switches, so nothing in the suite
 * had ever asked whether switching leaves the same instrument behind. This
 * asks, for every ordered pair of worlds.
 *
 * What must match is everything observable from outside: the rendered frame,
 * the spectrum, the band limit and the payload. What is allowed to differ is
 * nothing at all — a world switch is meant to be a change of world and not of
 * anything else, so the test is deliberately absolute rather than tolerant.
 */
#include "kyk_worlds.h"
#include "kyk_stereo.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace kyk;

static int fails = 0;

struct Shot { double frame[kFrame]; float mags[kMaxK]; float pay[kMaxP]; int kcut, n, k; };

static void Settle(StereoEngine& e, const float* c, int n)
{
    e.SetControl(c, n);
    float lo[64], ro[64];
    for(int i = 0; i < 12; i++) e.Process(lo, ro, 24);
}

static void Grab(StereoEngine& e, Shot& s)
{
    const float* f = e.L.Frame();
    for(int i = 0; i < kFrame; i++) s.frame[i] = f[i];
    s.k = e.L.WorldPtr() ? e.L.WorldPtr()->K() : 0;
    s.n = e.L.WorldPtr() ? e.L.WorldPtr()->N() : 0;
    for(int i = 0; i < s.k && i < kMaxK; i++) s.mags[i] = e.L.Mags()[i];
    for(int j = 0; j < kMaxP; j++) s.pay[j] = e.Payload()[j];
    s.kcut = e.L.Kcut();
}

static bool Build(uint8_t wi, World& w, Space& sp, solids::VertexTable& tbl,
                  std::vector<uint8_t>& blob)
{
    const worlds::Entry& e = worlds::Get(wi);
    if(e.kind == World::Kind::Lattice)
    {
        blob.assign(1u << 22, 0);
        const size_t n = worlds::Expand(wi, 4, 8, 64, 8, blob.data(), blob.size());
        if(!n || sp.Attach(blob.data(), n) != SpaceError::Ok) return false;
        w.UseLattice(&sp);
    }
    else if(!worlds::Point(wi, w, 8, nullptr, &tbl)) return false;
    return w.Ready();
}

int main()
{
    const float c[kMaxN] = {0.42f, 0.61f, 0.35f, 0.55f, 0.5f, 0.5f};
    std::vector<World> ws(worlds::kCount);
    std::vector<Space> sps(worlds::kCount);
    std::vector<solids::VertexTable> tbs(worlds::kCount);
    std::vector<std::vector<uint8_t>> blobs(worlds::kCount);
    std::vector<bool> ok(worlds::kCount, false);
    for(uint8_t i = 0; i < worlds::kCount; i++)
        ok[i] = Build(i, ws[i], sps[i], tbs[i], blobs[i]);

    /* what each world is when you start there */
    std::vector<Shot> fresh(worlds::kCount);
    for(uint8_t i = 0; i < worlds::kCount; i++)
    {
        if(!ok[i]) continue;
        StereoEngine e;
        e.Init(&ws[i], 48000.f);
        e.spread = 0.05f; e.slew_ms = 0.f;
        Settle(e, c, 4);
        Grab(e, fresh[i]);
    }

    int pairs = 0;
    long worstBin = 0;
    for(uint8_t a = 0; a < worlds::kCount; a++)
    {
        if(!ok[a]) continue;
        for(uint8_t b = 0; b < worlds::kCount; b++)
        {
            if(!ok[b] || a == b) continue;
            StereoEngine e;
            e.Init(&ws[a], 48000.f);
            e.spread = 0.05f; e.slew_ms = 0.f;
            Settle(e, c, 4);
            e.SetWorld(&ws[b]);
            Settle(e, c, 4);
            Shot got;
            Grab(e, got);
            pairs++;

            const Shot& want = fresh[b];
            double df = 0, dm = 0, dp = 0;
            for(int i = 0; i < kFrame; i++) df = std::fmax(df, std::fabs(got.frame[i] - want.frame[i]));
            for(int i = 0; i < want.k && i < kMaxK; i++) dm = std::fmax(dm, std::fabs((double)got.mags[i] - want.mags[i]));
            for(int j = 0; j < kMaxP; j++) dp = std::fmax(dp, std::fabs((double)got.pay[j] - want.pay[j]));
            const bool bad = df > 1e-6 || dm > 1e-6 || dp > 1e-6
                             || got.kcut != want.kcut || got.n != want.n || got.k != want.k;
            if(bad)
            {
                if(fails < 12)
                    printf("  %-10s -> %-10s frame %.5f  mags %.5f  payload %.5f  kcut %d/%d\n",
                           worlds::Get(a).name, worlds::Get(b).name, df, dm, dp, got.kcut, want.kcut);
                fails++;
            }
            (void)worstBin;
        }
    }
    printf("\n  %d ordered pairs checked\n", pairs);
    if(fails) printf("switch_check: %d pairs do not arrive where they should\n", fails);
    else      printf("switch_check: every world is the same arrived at as started in\n");
    return fails ? 1 : 0;
}
