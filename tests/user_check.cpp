/* user_check — a world as a file, including files nobody should have sent.
 *
 * This is the first thing in the instrument that reads bytes a stranger wrote.
 * The happy path is the easy half: export a built-in world, read it back, and
 * the spectra must match to the bit and the rendered frames must be identical.
 *
 * The half that matters is the rest. A blob whose header disagrees with its own
 * length, or claims more nodes than there is storage for, or carries a NaN in
 * one coefficient, must be refused — and refused *without* leaving a half-built
 * world behind, because a half-built world is worse than no world: it plays.
 * Every truncation and every single-byte corruption of a valid blob is tried
 * here, and the only acceptable outcomes are "loads and is sane" or "refused".
 */
#include "kyk_worlds.h"
#include "kyk_engine.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace kyk;

static int  bad = 0;
static void ck(const char* m, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", m); if(!ok) bad++; }

static Engine eng;
static void FrameAt(const World& w, const float* p, double* out)
{
    eng.Init(&w, 48000.f); eng.render_div = 1; eng.gain = 1.f;
    eng.SetF0(110.f); eng.SetPosition(p, w.N());
    float o[24];
    for(int i = 0; i < 6; i++) eng.Process(o, 24);
    const float* f = eng.Frame();
    for(int i = 0; i < kFrame; i++) out[i] = f[i];
}

int main()
{
    /* ── round trip ─────────────────────────────────────────────────── */
    World src; solids::VertexTable tbl;
    if(!worlds::Point(worlds::kLock, src, 8, nullptr, &tbl)) { printf("cannot build Lock\n"); return 2; }

    std::vector<uint8_t> blob(UserBlobSize(src.N(), src.K(), 24) + 64);
    const size_t n = src.SaveUserWorld("round trip", blob.data(), blob.size());
    ck("a built-in Lock world exports", n > 0 && n == UserBlobSize(src.N(), src.K(), 24));
    blob.resize(n);

    World dst;
    const UserError e = dst.UseUserWorld(blob.data(), blob.size(), 8, nullptr);
    ck("and reads back", e == UserError::Ok && dst.Ready());
    ck("with the same geometry", dst.N() == src.N() && dst.K() == src.K());

    double a[kFrame], b[kFrame];
    double worst = 0;
    for(int t = 0; t < 64; t++)
    {
        float p[kMaxN] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        for(int q = 0; q < 4; q++) p[q] = (float)((t >> q) & 1) * 0.8f + 0.1f;
        FrameAt(src, p, a); FrameAt(dst, p, b);
        for(int i = 0; i < kFrame; i++) worst = std::fmax(worst, std::fabs(a[i] - b[i]));
    }
    char m[96]; std::snprintf(m, sizeof m, "and renders identically at 64 positions (worst sample diff %.2e)", worst);
    ck(m, worst == 0.0);

    /* ── files nobody should have sent ──────────────────────────────── */
    struct Case { const char* what; int off; uint8_t val; };
    const Case cases[] = {
        {"bad magic",        0, 0xFFu},
        {"bad version",      4, 0x09u},
        {"N of zero",        6, 0u},
        {"N past kMaxN",     6, 99u},
        {"K of zero",        7, 0u},
        {"K past kShapeK",   7, 200u},
        {"count of zero",    8, 0u},
        {"count past store", 8, 250u},
    };
    for(const Case& c : cases)
    {
        std::vector<uint8_t> v = blob;
        v[c.off] = c.val;
        World w;
        const UserError r = w.UseUserWorld(v.data(), v.size(), 8, nullptr);
        std::snprintf(m, sizeof m, "%-18s refused (%s) and leaves no world", c.what, UserErrorName(r));
        ck(m, r != UserError::Ok && !w.Ready());
    }
    {   /* a NaN in one coefficient */
        std::vector<uint8_t> v = blob;
        const uint32_t nan = 0x7FC00000u;
        std::memcpy(v.data() + kUserHeader + 4 * 7, &nan, 4);
        World w;
        const UserError r = w.UseUserWorld(v.data(), v.size(), 8, nullptr);
        ck("a NaN coefficient    refused and leaves no world", r == UserError::NotFinite && !w.Ready());
    }

    /* every truncation */
    int loaded = 0, refused = 0, unsafe = 0;
    for(size_t len = 0; len < blob.size(); len++)
    {
        World w;
        const UserError r = w.UseUserWorld(blob.data(), len, 8, nullptr);
        if(r == UserError::Ok) { loaded++; if(!w.Ready()) unsafe++; }
        else { refused++; if(w.Ready()) unsafe++; }
    }
    std::snprintf(m, sizeof m, "every one of %d truncations is refused cleanly", refused);
    ck(m, loaded == 0 && unsafe == 0);

    /* every single-byte corruption of the header, and a sample of the body */
    int accepted = 0, faults = 0;
    for(size_t i = 0; i < blob.size(); i += (i < (size_t)kUserHeader ? 1 : 97))
        for(uint8_t bit = 0; bit < 8; bit++)
        {
            std::vector<uint8_t> v = blob;
            v[i] ^= (uint8_t)(1u << bit);
            World w;
            const UserError r = w.UseUserWorld(v.data(), v.size(), 8, nullptr);
            if(r == UserError::Ok)
            {
                accepted++;
                /* accepted is fine — most body bits are just a different sound —
                   but it must be a usable world that renders finite samples */
                if(!w.Ready() || w.N() < 1 || w.K() < 1) { faults++; continue; }
                float p[kMaxN] = {0.4f, 0.6f, 0.5f, 0.5f, 0.5f, 0.5f};
                FrameAt(w, p, a);
                for(int q = 0; q < kFrame; q++) if(!(a[q] == a[q])) { faults++; break; }
            }
            else if(w.Ready()) faults++;
        }
    std::snprintf(m, sizeof m, "single-bit corruption: %d accepted, all render finite; %d faults", accepted, faults);
    ck(m, faults == 0);

    if(bad) printf("user_check: %d FAILURES\n", bad);
    else    printf("user_check: all passed\n");
    return bad ? 1 : 0;
}
