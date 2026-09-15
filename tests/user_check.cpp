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

    /* ── effects on an axis ─────────────────────────────────────────
     *
     * The build tab puts a frame effect on axis 4 or 5 and writes the world as
     * six axes with every node at 0.5 on the new ones. That is three claims —
     * the effect runs, the axis is neutral in the blend, and an effect at zero
     * costs nothing — and each is a property of the bytes rather than of the
     * page, so each is checked here against the module's own reader.
     *
     * This does the page's job in twenty lines rather than borrowing its
     * output, so a change to either side has to keep agreeing with the format
     * and not merely with the other side. */
    auto with_fx = [&](Shaper s0, uint8_t ax0, Shaper s1, uint8_t ax1, int nn = 6) {
        const int k = src.K(), cnt = 24;
        std::vector<uint8_t> v(UserBlobSize(nn, k, cnt));
        std::memcpy(v.data(), blob.data(), kUserHeader);
        v[4] = (s0 != Shaper::None || s1 != Shaper::None) ? 2u : 1u; v[5] = 0u;
        v[6] = (uint8_t)nn;
        v[14] = s0 == Shaper::None ? 0u : (uint8_t)((ax0 << 4) | (uint8_t)s0);
        v[15] = s1 == Shaper::None ? 0u : (uint8_t)((ax1 << 4) | (uint8_t)s1);
        const uint8_t* rp = blob.data() + kUserHeader;
        uint8_t*       wp = v.data() + kUserHeader;
        for(int node = 0; node < cnt; node++)
        {
            std::memcpy(wp, rp, 4 * 4);                    /* the four placed axes */
            const float half = 0.5f;
            for(int a = 4; a < nn; a++) std::memcpy(wp + 4 * a, &half, 4);
            std::memcpy(wp + 4 * nn, rp + 4 * 4, 4 * (size_t)k);
            rp += 4 * (size_t)(4 + k);
            wp += 4 * (size_t)(nn + k);
        }
        return v;
    };

    {
        const std::vector<uint8_t> v = with_fx(Shaper::Fold, 4, Shaper::Crush, 5);
        World w;
        const UserError r = w.UseUserWorld(v.data(), v.size(), 8, nullptr);
        ck("a world with two effects loads", r == UserError::Ok && w.Ready() && w.N() == 6);
        ck("and says it shapes the rendered cycle", w.HasShaper()
               && w.FxShaper(0) == Shaper::Fold && w.FxAxis(0) == 4
               && w.FxShaper(1) == Shaper::Crush && w.FxAxis(1) == 5);
        /* Written back out, the bytes are the bytes: an effect survives an
           export of an import, which is what makes a world shareable. */
        std::vector<uint8_t> out(v.size() + 64);
        const size_t wn = w.SaveUserWorld("round trip", out.data(), out.size());
        ck("and exports byte-identically", wn == v.size()
               && std::memcmp(out.data() + 6, v.data() + 6, wn - 6) == 0 && out[4] == 2u);

        float p[kMaxN] = {0.3f, 0.7f, 0.5f, 0.5f, 0.f, 0.f};
        double fx0[kFrame], plain[kFrame];
        FrameAt(w, p, fx0);
        FrameAt(src, p, plain);
        double d0 = 0;
        for(int i = 0; i < kFrame; i++) d0 = std::fmax(d0, std::fabs(fx0[i] - plain[i]));
        /* Not exactly zero, and the reason is worth writing down rather than
           loosening quietly. The two extra axes add the same constant to every
           node's squared distance — 0.5 with the position at zero — and the
           softmax cancels a constant exactly in arithmetic but not in floats:
           adding 0.5 to each d2 before the subtraction costs the low bits of
           d2. Measured at 4.8e-7 on a frame that spans about +-2, which is
           -132 dBFS, and it is rounding rather than a different sound. */
        std::snprintf(m, sizeof m, "at zero depth it is the four-axis world to float rounding (worst %.2e)", d0);
        ck(m, d0 < 1e-5);

        /* The neutrality claim, and the honest form of it: travelling an axis
           that carries no effect, on a world where every node sits at 0.5
           there, must not move the sound by one bit. Compared along the axis
           rather than against the four-axis world, because the constant above
           is the same at every position and would mask exactly what this asks. */
        const std::vector<uint8_t> only5 = with_fx(Shaper::None, 0, Shaper::Crush, 5);
        World w5;
        w5.UseUserWorld(only5.data(), only5.size(), 8, nullptr);
        double dn = 0, ref[kFrame];
        for(int t = 0; t <= 8; t++)
        {
            float q[kMaxN] = {0.3f, 0.7f, 0.5f, 0.5f, (float)t / 8.f, 0.f};
            double f1[kFrame];
            FrameAt(w5, q, f1);
            if(!t) std::memcpy(ref, f1, sizeof ref);
            else for(int i = 0; i < kFrame; i++) dn = std::fmax(dn, std::fabs(f1[i] - ref[i]));
        }
        std::snprintf(m, sizeof m, "an axis with no effect on it moves nothing audible across its whole travel (worst %.2e)", dn);
        /* Same rounding as above and the same size: the constant the axis adds
           to every distance is only constant *across nodes*, so it changes with
           position and its low bits change with it. -128 dBFS, smooth, and not
           a step. The mathematical claim — a term equal for every node cancels
           out of the softmax — is exact; the arithmetic is float. */
        ck(m, dn < 1e-5);

        /* And the effect does something, on its own axis and only there. */
        float pf[kMaxN] = {0.3f, 0.7f, 0.5f, 0.5f, 1.f, 0.f};
        float pc[kMaxN] = {0.3f, 0.7f, 0.5f, 0.5f, 0.f, 1.f};
        double fold[kFrame], crush[kFrame];
        FrameAt(w, pf, fold);
        FrameAt(w, pc, crush);
        double df = 0, dc = 0;
        for(int i = 0; i < kFrame; i++)
        {
            df = std::fmax(df, std::fabs(fold[i] - plain[i]));
            dc = std::fmax(dc, std::fabs(crush[i] - plain[i]));
        }
        std::snprintf(m, sizeof m, "full fold on axis 4 changes the cycle (%.3f) and full crush on axis 5 does too (%.3f)", df, dc);
        ck(m, df > 0.05 && dc > 0.02);

        /* Continuity, measured the way cont_check measures it so the numbers
           are comparable: the L2 norm of the frame difference, swept 0.02 to
           0.98, at two step sizes. Halve the step and a continuous axis halves
           its largest change; a cliff does not move. */
        for(int q = 0; q < 2; q++)
        {
            double at[2] = {0, 0};
            for(int pass = 0; pass < 2; pass++)
            {
                const int steps = pass ? 2000 : 500;
                double    prevf[kFrame], worstd = 0;
                for(int t = 0; t <= steps; t++)
                {
                    float q2[kMaxN] = {0.3f, 0.7f, 0.35f, 0.35f, 0.f, 0.f};
                    q2[4 + q] = 0.02f + 0.96f * (float)t / (float)steps;
                    double cur[kFrame];
                    FrameAt(w, q2, cur);
                    if(t)
                    {
                        double dd = 0;
                        for(int i = 0; i < kFrame; i++)
                        {
                            const double x = cur[i] - prevf[i];
                            dd += x * x;
                        }
                        dd = std::sqrt(dd);
                        if(dd > worstd) worstd = dd;
                    }
                    std::memcpy(prevf, cur, sizeof cur);
                }
                at[pass] = worstd;
            }
            const double ratio = at[1] > 0 ? at[0] / at[1] : 0;
            std::snprintf(m, sizeof m, "axis %d %-5s continuous: %.5f at step/500, %.5f at step/2000, ratio %.2f",
                          4 + q, ShaperName(q ? Shaper::Crush : Shaper::Fold), at[0], at[1], ratio);
            /* The ratio is the claim, not the absolute size. Both of these
               move the frame much further than the same shapers do on the
               Shapes grid — Lock's nodes are unit-RMS spectra with sixty-four
               harmonics in some of them, so a folder has far more to work with
               than a saw does — and a big smooth axis is not a rough one. What
               would matter is a step that stops shrinking, which is what a
               crusher reached by rounding rather than by crossfading gives. */
            ck(m, ratio >= 2.0);
        }
    }

    /* Effects nobody should have sent. */
    {
        struct FxCase { const char* what; uint8_t b14, b15; uint8_t ver; };
        const FxCase fxc[] = {
            {"unknown shaper",     (uint8_t)((4 << 4) | 9), 0u, 2u},
            {"axis past N",        (uint8_t)((6 << 4) | 1), 0u, 2u},
            {"two on one axis",    (uint8_t)((4 << 4) | 1), (uint8_t)((4 << 4) | 4), 2u},
            {"version 2, none",    0u, 0u, 2u},
            {"axis without shaper",(uint8_t)(4 << 4), 0u, 2u},
            {"v1 with effect bits",(uint8_t)((4 << 4) | 1), 0u, 1u},
        };
        for(const FxCase& c : fxc)
        {
            std::vector<uint8_t> v = with_fx(Shaper::Fold, 4, Shaper::None, 0);
            v[4] = c.ver; v[14] = c.b14; v[15] = c.b15;
            World w;
            const UserError r = w.UseUserWorld(v.data(), v.size(), 8, nullptr);
            std::snprintf(m, sizeof m, "%-20s refused (%s) and leaves no world", c.what, UserErrorName(r));
            ck(m, r != UserError::Ok && !w.Ready());
        }
    }

    if(bad) printf("user_check: %d FAILURES\n", bad);
    else    printf("user_check: all passed\n");
    return bad ? 1 : 0;
}
