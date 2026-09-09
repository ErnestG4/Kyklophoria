/* rotate_check — spec §7 rotation tests plus the stereo pair.
 *   trig:     SinCosTurns exact at quarter turns, close to libm elsewhere
 *   rotation: all-zero angles is the identity and the engine skips it
 *             (bit-identical to the mono Engine); a quarter turn in plane
 *             (i,j) permutes axes exactly (pivot 0) and to 1 ulp (pivot 0.5);
 *             R·Rᵀ = I for random angles; plane enumeration
 *   stereo:   δ=0 → L, R and the mono Engine are bit-identical; δ≠0 → L≠R;
 *             turning δ on has no step; telemetry encodes to its declared size
 */
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include "kyk_stereo.h"
#include "kyk_world.h"
#include "kyk_telemetry.h"
#include "kyk_gen.h"

using namespace kyk;

static World gWorld;   /* the tests all drive a lattice world */

static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static void TestTrig()
{
    printf("trig\n");
    float s, c;
    SinCosTurns(0.f, s, c);    CHECK(s == 0.f && c == 1.f, "0 turns");
    SinCosTurns(0.25f, s, c);  CHECK(s == 1.f && c == 0.f, "1/4 turn: %g %g", s, c);
    SinCosTurns(0.5f, s, c);   CHECK(s == 0.f && c == -1.f, "1/2 turn");
    SinCosTurns(0.75f, s, c);  CHECK(s == -1.f && c == 0.f, "3/4 turn");
    SinCosTurns(1.f, s, c);    CHECK(s == 0.f && c == 1.f, "1 turn wraps");
    SinCosTurns(-0.25f, s, c); CHECK(s == -1.f && c == 0.f, "-1/4 turn");
    double maxerr = 0;
    for(int i = 0; i < 10000; i++)
    {
        const float t = (float)i / 10000.f * 3.f - 1.f;
        SinCosTurns(t, s, c);
        maxerr = std::fmax(maxerr, std::fabs(s - std::sin(2 * M_PI * t)));
        maxerr = std::fmax(maxerr, std::fabs(c - std::cos(2 * M_PI * t)));
    }
    printf("  interpolated table vs libm max err %.2e\n", maxerr);
    CHECK(maxerr < 2e-6, "table error %g", maxerr);
}

static void TestRotation()
{
    printf("rotation\n");
    int i, j;
    Rotation::PlaneAxes(4, 0, i, j); CHECK(i == 0 && j == 1, "plane 0");
    Rotation::PlaneAxes(4, 3, i, j); CHECK(i == 1 && j == 2, "plane 3");
    Rotation::PlaneAxes(4, 5, i, j); CHECK(i == 2 && j == 3, "plane 5");
    CHECK(Rotation::PlaneCount(4) == 6 && Rotation::PlaneCount(6) == 15, "plane counts");

    /* quarter turn permutes: (x,y) → (−y, x) about pivot 0, exactly */
    for(int n = 2; n <= kMaxN; n++)
        for(int p = 0; p < Rotation::PlaneCount(n); p++)
        {
            Rotation r;
            r.Init(n);
            r.SetAngle(p, 0.25f);
            r.Update();
            Rotation::PlaneAxes(n, p, i, j);
            float c[kMaxN], out[kMaxN];
            for(int a = 0; a < n; a++) c[a] = 0.1f * (float)(a + 1);
            r.Apply(c, out, 0.f);
            for(int a = 0; a < n; a++)
            {
                const float want = a == i ? -c[j] : (a == j ? c[i] : c[a]);
                CHECK(out[a] == want, "N=%d plane %d axis %d: %g want %g", n, p, a, out[a], want);
            }
            r.Apply(c, out, 0.5f);
            for(int a = 0; a < n; a++)
            {
                const float want = a == i ? 0.5f - (c[j] - 0.5f) : (a == j ? 0.5f + (c[i] - 0.5f) : c[a]);
                CHECK(std::fabs(out[a] - want) < 1e-7f, "pivot 0.5 N=%d plane %d axis %d: %g want %g", n, p, a, out[a], want);
            }
        }
    /* orthonormal for random angles */
    Rng rng;
    rng.Seed(5);
    for(int trial = 0; trial < 20; trial++)
    {
        const int n = 2 + (int)(rng.Next() % (kMaxN - 1));
        Rotation  r;
        r.Init(n);
        for(int p = 0; p < r.Planes(); p++) r.SetAngle(p, rng.Uniform());
        r.Update();
        for(int a = 0; a < n; a++)
            for(int b = 0; b < n; b++)
            {
                double dot = 0;
                for(int k = 0; k < n; k++) dot += (double)r.Row(a)[k] * r.Row(b)[k];
                CHECK(std::fabs(dot - (a == b ? 1.0 : 0.0)) < 1e-5, "R·Rᵀ N=%d (%d,%d) = %g", n, a, b, dot);
            }
        CHECK(!r.IsIdentity(), "random angles not identity");
    }
    /* angles wrap in turns; 1.0 is identity again */
    Rotation r;
    r.Init(4);
    r.SetAngle(2, 1.f);
    CHECK(r.IsIdentity(), "a whole turn is the identity");
    r.SetAngle(2, 0.3f);
    r.AddAngle(2, 0.9f);
    CHECK(std::fabs(r.Angle(2) - 0.2f) < 1e-6f, "AddAngle wraps: %g", r.Angle(2));
}

/* The firmware writes the static angle from its pot every single block while
 * an orbit is running. The first orbit build stored both in one variable, so
 * each write wiped the motion and the orbit did nothing on the module — while
 * the desktop script, which sets an angle once, worked fine and the golden
 * passed. This is that case. */
static void TestOrbit()
{
    printf("orbit\n");
    Rotation r;
    r.Init(4);
    r.SetRate(0, 0.25f);                    /* a quarter turn per second */
    const float dt = 24.f / 48000.f;
    for(int block = 0; block < 2000; block++)
    {
        r.SetAngle(0, 0.1f);                /* the pot, rewritten every block */
        r.Advance(dt);
    }
    const float expected = Fract(0.1f + 0.25f * 2000.f * dt);   /* 0.1 + 0.25 turn */
    printf("  after 1 s at 0.25 turn/s with the pot rewritten each block: %.4f (want %.4f)\n",
           r.Angle(0), expected);
    CHECK(std::fabs(r.Angle(0) - expected) < 1e-3f,
          "orbit does not accumulate under a repeated SetAngle (%g vs %g)", r.Angle(0), expected);
    CHECK(std::fabs(r.BaseAngle(0) - 0.1f) < 1e-6f, "base angle should still be the pot value");
    CHECK(r.Orbiting(), "should report orbiting");

    /* a zero rate must leave the angle exactly where the pot puts it */
    Rotation q;
    q.Init(4);
    q.SetAngle(2, 0.37f);
    for(int b = 0; b < 100; b++) { q.SetAngle(2, 0.37f); q.Advance(dt); }
    CHECK(q.Angle(2) == 0.37f, "a stopped orbit must not drift (%g)", q.Angle(2));
    CHECK(!q.Orbiting(), "no rates set, so not orbiting");

    /* ResetOrbit parks the motion without disturbing the pot */
    r.ResetOrbit();
    CHECK(std::fabs(r.Angle(0) - 0.1f) < 1e-6f, "ResetOrbit should leave the base angle (%g)", r.Angle(0));
}

static std::vector<uint8_t> MakeSpace()
{
    GenParams gp;
    std::vector<uint8_t> b(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
    BuildLattice(gp, b.data(), b.size());
    return b;
}

static void TestStereo()
{
    printf("stereo\n");
    auto  b = MakeSpace();
    Space s;
    s.Attach(b.data(), b.size());
    static StereoEngine st;
    static Engine       mono;
    gWorld.UseLattice(&s);
    st.Init(&gWorld, 48000.f);
    st.slew_ms = 0.f;   /* comparing against the bare Engine, which has no slew */
    gWorld.UseLattice(&s);
    mono.Init(&gWorld, 48000.f);
    static float l[24], r[24], m[24];
    Rng rng;
    rng.Seed(11);
    bool same = true;
    /* δ = 0, identity rotation: L == R == mono, bit for bit, over a wander */
    for(int blk = 0; blk < 1000; blk++)
    {
        float c[4];
        for(int a = 0; a < 4; a++) c[a] = rng.Uniform() * 1.2f - 0.1f;
        const float f0 = 50.f + rng.Uniform() * 1000.f;
        st.SetControl(c, 4); st.SetF0(f0); st.Process(l, r, 24);
        mono.SetPosition(c, 4); mono.SetF0(f0); mono.Process(m, 24);
        if(std::memcmp(l, m, sizeof(l)) || std::memcmp(r, m, sizeof(r))) same = false;
    }
    CHECK(same, "δ=0 identity path diverged from the mono engine");
    CHECK(!st.IsStereo(), "reports mono");
    /* rotation set then cleared: still identical to mono afterwards */
    st.rot.SetAngle(0, 0.1f);
    const float c0[4] = {0.3f, 0.6f, 0.2f, 0.8f};
    st.SetControl(c0, 4); st.Process(l, r, 24);
    mono.SetPosition(c0, 4); mono.Process(m, 24);
    CHECK(std::memcmp(l, m, sizeof(l)) != 0, "a rotation changes the sound");
    st.rot.SetAngle(0, 0.f);
    same = true;
    for(int blk = 0; blk < 10; blk++)
    {
        float c[4];
        for(int a = 0; a < 4; a++) c[a] = rng.Uniform();
        st.SetControl(c, 4); st.Process(l, r, 24);
        mono.SetPosition(c, 4); mono.Process(m, 24);
        if(blk > 0 && std::memcmp(l, m, sizeof(l))) same = false;   /* block 0 still fades from the rotated frame */
    }
    CHECK(same, "back to identity is not bit-identical to mono");

    /* δ ≠ 0: L ≠ R, no step at the switch, payload stays finite */
    st.SetControl(c0, 4);
    for(int blk = 0; blk < 4; blk++) st.Process(l, r, 24);
    const float lastL = l[23], lastR = r[23];
    st.spread = 0.02f;
    st.Process(l, r, 24);
    CHECK(st.IsStereo(), "reports stereo");
    CHECK(std::fabs(l[0] - lastL) < 0.05f && std::fabs(r[0] - lastR) < 0.05f, "step at δ on: L %g→%g R %g→%g", lastL, l[0], lastR, r[0]);
    for(int blk = 0; blk < 20; blk++) st.Process(l, r, 24);
    CHECK(std::memcmp(l, r, sizeof(l)) != 0, "δ≠0 but L == R");
    double diff = 0;
    for(int k = 0; k < 24; k++) diff += std::fabs(l[k] - r[k]);
    printf("  δ=0.02 turns: mean |L−R| %.4f\n", diff / 24);
    for(int j = 0; j < s.P(); j++) CHECK(std::isfinite(st.Payload()[j]) && st.Payload()[j] >= 0.f && st.Payload()[j] <= 1.f, "payload lane %d", j);
    /* back to mono: R follows L again with no step */
    st.spread = 0.f;
    st.Process(l, r, 24);
    CHECK(std::memcmp(l, r, sizeof(l)) == 0, "δ back to 0: R must copy L");

    /* telemetry */
    static uint8_t buf[2048];
    const int want = TelemetrySize(s.N(), s.K(), s.P(), kTelSpectrum | kTelFrame);
    const int got  = EncodeTelemetry(st, kTelSpectrum | kTelFrame, buf, sizeof(buf));
    printf("  telemetry %d bytes\n", got);
    CHECK(got == want, "telemetry size %d vs declared %d", got, want);
    CHECK(EncodeTelemetry(st, kTelSpectrum | kTelFrame, buf, 40) == 0, "small cap must refuse");
    uint32_t blk;
    std::memcpy(&blk, buf, 4);
    CHECK(blk == st.L.Block(), "block field");
    CHECK(buf[8] == 4 && buf[9] == 64 && buf[11] == 8 && buf[12] == 6, "header fields %d %d %d %d", buf[8], buf[9], buf[11], buf[12]);
    /* dB scale: the byte spans −96 to +6 dB, so unity is 15 steps below the
     * top and the six dB of headroom is there because the engine normalises
     * to a sum of squares of two — one partial can legitimately exceed unity
     * and a peaky world's tallest one does. */
    CHECK(std::abs((int)detail::MagToU8(1.f) - 240) <= 1, "0 dB → %d", detail::MagToU8(1.f));
    CHECK(std::abs((int)detail::MagToU8(0.5f) - 225) <= 2, "-6 dB → %d", detail::MagToU8(0.5f));
    CHECK(detail::MagToU8(2.f) == 255, "+6 dB tops out");
    CHECK(std::abs((int)detail::MagToU8(1.4142f) - 248) <= 2, "+3 dB fits → %d", detail::MagToU8(1.4142f));
    CHECK(detail::MagToU8(1e-6f) == 0, "floor");
}

int main()
{
    TestTrig();
    TestRotation();
    TestOrbit();
    TestStereo();
    printf(fails ? "rotate_check: %d FAILURES\n" : "rotate_check: all passed\n", fails);
    return fails ? 1 : 0;
}
