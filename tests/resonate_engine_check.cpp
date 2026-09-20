/* resonate_engine_check — the resonate path inside the engine.
 *
 * Four things, each of which the design says must hold and each of which
 * fails if the wiring is wrong:
 *
 *   1. A resonate world through Engine::Process is the standalone
 *      ResonatorVoice bit for bit: same world, same note, same velocity,
 *      same samples (the engine's gain divided out). Nothing in the path
 *      but the voice.
 *   2. Every built-in world renders exactly as it did: a frame from a
 *      wavetable world before and after a resonate world existed in the
 *      build is the same frame. (The path is behind the world's kind.)
 *   3. Arriving at a resonate world from a wavetable world gives the same
 *      samples as starting in it — SetWorld rebuilds the voice, and only
 *      SetWorld does. This is the rule the repo's five bugs were about.
 *   4. A strike at a new pitch plays the new note; a strike at the same
 *      pitch adds to what rings rather than cutting it.
 */
#include "kyk_stereo.h"
#include "kyk_worlds.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace kyk;

static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while(0)

static std::vector<uint8_t> slurp(const char* path)
{
    std::vector<uint8_t> b;
    FILE* f = fopen(path, "rb");
    if(!f) return b;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    b.resize(n);
    if(fread(b.data(), 1, n, f) != (size_t)n) b.clear();
    fclose(f);
    return b;
}

static void Run(Engine& e, std::vector<float>& out, int blocks, int n = 48)
{
    out.assign((size_t)blocks * n, 0.f);
    for(int b = 0; b < blocks; b++) e.Process(out.data() + (size_t)b * n, n);
}

int main()
{
    const float sr = 48000.f;
    auto blob = slurp("tests/data/ep-vel.kykm");
    CHECK(!blob.empty(), "tests/data/ep-vel.kykm missing");
    World rw; rw.UseResonate(blob.data(), (uint32_t)blob.size());
    auto wblob = slurp("tests/data/wurli.kykm");
    World wurli; wurli.UseResonate(wblob.data(), (uint32_t)wblob.size());
    CHECK(rw.Ready() && rw.IsResonate() && rw.N() == 1, "the world did not attach as Resonate");

    /* 1. engine == voice */
    {
        Engine e; e.Init(&rw, sr); e.gain = 1.f; e.SetF0(130.81f);
        e.Strike(0.6f);
        std::vector<float> ye; Run(e, ye, 100);
        ResonatorVoice v; v.Init(); rw.Res().At(Engine::NoteOf(130.81f), v, sr); v.Strike(0.6f);
        std::vector<float> yv(4800); for(int b = 0; b < 100; b++) v.Process(yv.data() + b * 48, 48);
        double d = 0, en = 0;
        for(int i = 0; i < 4800; i++) { d += (ye[i] / e.PhaseTrim() - yv[i]) * (ye[i] / e.PhaseTrim() - yv[i]); en += yv[i] * yv[i]; }
        CHECK(en > 0 && d < 1e-10 * en, "engine differs from the voice: %.3g of the energy", d / en);
        printf("  engine == voice over 4800 samples (energy %.3g, difference %.3g)\n", en, d);
    }

    /* 2. a wavetable world is untouched: its frame does not depend on the
       resonate path existing. Two engines on the same built-in world, one
       of which visited the resonate world first. */
    {
        World ww; solids::VertexTable tbl;
        bool ok = worlds::Point(worlds::kSaw, ww, 8, nullptr, &tbl);
        CHECK(ok, "could not build Saw");
        Engine a; a.Init(&ww, sr);
        Engine b; b.Init(&rw, sr); b.Strike(0.9f); std::vector<float> junk; Run(b, junk, 10); b.SetWorld(&ww);
        const float c[kMaxN] = {0.42f, 0.61f, 0.35f, 0.55f, 0.5f, 0.5f};
        a.SetPosition(c, kMaxN); b.SetPosition(c, kMaxN);
        std::vector<float> ya, yb; Run(a, ya, 40); Run(b, yb, 40);
        /* the frame, as switch_check compares: raw samples differ by the
           oscillator's phase and its crossfade, which is the engine's own
           click-free behaviour and not this path's */
        double d = 0; for(int i = 0; i < kFrame; i++) d = std::fmax(d, std::fabs((double)a.Frame()[i] - b.Frame()[i]));
        CHECK(d == 0.0, "Saw's frame after a resonate world differs from Saw fresh by %.3g", d);
        printf("  Saw's frame after the resonate world: max difference %.3g\n", d);
    }

    /* 3. arriving == starting */
    {
        World ww; solids::VertexTable tbl; worlds::Point(worlds::kSaw, ww, 8, nullptr, &tbl);
        Engine fresh; fresh.Init(&rw, sr); fresh.SetF0(196.f); fresh.Strike(0.7f);
        Engine arrived; arrived.Init(&ww, sr); arrived.SetF0(196.f); std::vector<float> junk; Run(arrived, junk, 30);
        arrived.SetWorld(&rw); arrived.Strike(0.7f);
        std::vector<float> yf, ya; Run(fresh, yf, 60); Run(arrived, ya, 60);
        /* past the oscillator's crossfade out of the Saw frame (one cycle at
           196 Hz is 245 samples; the first 20 blocks are 960) */
        double d = 0; for(size_t i = 960; i < yf.size(); i++) d = std::fmax(d, std::fabs(yf[i] - ya[i]));
        CHECK(d == 0.0, "arrived at the resonate world differs from started in it by %.3g after the crossfade", d);
        printf("  arrived == started after the crossfade: max difference %.3g\n", d);
    }

    /* 4. pitch, retrigger, and a ringing note following the pitch */
    {
        /* a fresh strike at C4 is a C4: a C4 series within 20 dB of its peak
           (a reed piano's fundamental can sit under its second harmonic), no
           C3 fundamental. By plain DFT over 0.2 s */
        auto level_at = [&](const std::vector<float>& y, double hz) {
            const int N = 9600; double best = 0;
            for(double fr = hz * 0.97; fr <= hz * 1.03; fr += sr / N / 2)
            {
                double re = 0, im = 0;
                for(int i = 0; i < N; i++) { const double w = 0.5 - 0.5 * std::cos(6.283185307 * i / N); re += y[i] * w * std::cos(6.283185307 * fr * i / sr); im -= y[i] * w * std::sin(6.283185307 * fr * i / sr); }
                best = std::fmax(best, std::sqrt(re * re + im * im));
            }
            return 20 * std::log10(best + 1e-12);
        };
        Engine e; e.Init(&wurli, sr); e.gain = 1.f; e.SetF0(261.63f); e.Strike(0.7f);
        std::vector<float> y2; Run(e, y2, 200);
        const double l131 = level_at(y2, 130.81), top = std::fmax(level_at(y2, 261.63), level_at(y2, 523.25));
        CHECK(l131 < top - 30.0, "a C4 strike carries a C3 fundamental: %.1f dB against the C4 series at %.1f", l131, top);
        /* a pitch change under the ringing note keeps it ringing (retuned),
           and a re-strike adds rather than restarting from silence */
        Engine g; g.Init(&wurli, sr); g.gain = 1.f; g.SetF0(130.81f); g.Strike(0.7f);
        std::vector<float> a1; Run(g, a1, 100);
        g.SetF0(146.83f); g.Strike(0.f);          /* a retune with no strike: velocity 0 is swing 0 on a world without takes */
        std::vector<float> a2; Run(g, a2, 5);
        double e_before = 0, e_after = 0;
        for(int i = 4560; i < 4800; i++) e_before += a1[i] * a1[i];
        for(int i = 0; i < 240; i++) e_after += a2[i] * a2[i];
        CHECK(e_after > 0.25 * e_before, "a pitch change cut the ringing note: %.3g before, %.3g after", e_before, e_after);
        /* a re-strike adds: over the next 20 ms, more energy than the same
           engine ringing on without it (two copies, the bank being linear
           and the burst a sum) */
        Engine h = g;
        g.Strike(0.7f);
        std::vector<float> a3, a4; Run(g, a3, 20); Run(h, a4, 20);
        double e_re = 0, e_on = 0; for(size_t i = 0; i < a3.size(); i++) { e_re += a3[i] * a3[i]; e_on += a4[i] * a4[i]; }
        CHECK(e_re > e_on, "a re-strike did not add to the ring: %.3g without, %.3g with", e_on, e_re);
        printf("  a C4 strike is a C4 (C3 fundamental %.0f dB under its series); a retune keeps the ring (%.3g -> %.3g); a re-strike adds (%.3g -> %.3g over 20 ms)\n", top - l131, e_before, e_after, e_on, e_re);
    }

    printf(fails ? "resonate_engine_check: %d FAILED\n" : "resonate_engine_check: ok\n", fails);
    return fails ? 1 : 0;
}
