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
#include <tuple>

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
/* as Run, onto the end of what is there: for a check that compares a whole
   performance, not its last call */
static void RunOn(Engine& e, std::vector<float>& out, int blocks, int n = 48)
{
    const size_t at = out.size();
    out.resize(at + (size_t)blocks * n, 0.f);
    for(int b = 0; b < blocks; b++) e.Process(out.data() + at + (size_t)b * n, n);
}

int main()
{
    const float sr = 48000.f;
    auto blob = slurp("tests/data/tine.kykm");
    CHECK(!blob.empty(), "tests/data/tine.kykm missing");
    World rw; rw.UseResonate(blob.data(), (uint32_t)blob.size());
    auto wblob = slurp("tests/data/piano.kykm");
    World piano; piano.UseResonate(wblob.data(), (uint32_t)wblob.size());
    CHECK(rw.Ready() && rw.IsResonate() && rw.N() == 4, "the world did not attach as Resonate with its four axes");

    /* 1. engine == voice */
    {
        Engine e; e.Init(&rw, sr); e.gain = 1.f; e.SetF0(130.81f);
        e.Strike(0.6f);
        std::vector<float> ye; Run(e, ye, 100);
        /* the engine's pitch is locked to the semitone: C3 exactly, not 130.81 */
        ResonatorVoice v; v.Init(); rw.Res().At(std::floor(Engine::NoteOf(130.81f) + 0.5f), v, sr); v.Strike(0.6f);
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
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetF0(261.63f); e.Strike(0.7f);
        std::vector<float> y2; Run(e, y2, 200);
        const double l131 = level_at(y2, 130.81), top = std::fmax(level_at(y2, 261.63), level_at(y2, 523.25));
        CHECK(l131 < top - 30.0, "a C4 strike carries a C3 fundamental: %.1f dB against the C4 series at %.1f", l131, top);
        /* a pitch change under the ringing note keeps it ringing (retuned),
           and a re-strike adds rather than restarting from silence */
        Engine g; g.Init(&piano, sr); g.gain = 1.f; g.SetF0(130.81f); g.Strike(0.7f);
        std::vector<float> a1; Run(g, a1, 100);
        g.SetPitchLock(false); g.SetF0(146.83f);  /* a retune with no strike: the pitch moved under a free ring (a strike at another note chokes it) */
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

    /* 5. an index world — a row of bodies — is played by position 0, not by
       pitch: the same position at two pitches is the same sound, bit for
       bit, and equal to the standalone voice built at that index; two
       positions are two bodies; and the position moving under a ringing
       body retunes it rather than cutting it */
    {
        auto pblob = slurp("tests/data/bodies.kykm");
        CHECK(!pblob.empty(), "tests/data/bodies.kykm missing");
        World bodies; bodies.UseResonate(pblob.data(), (uint32_t)pblob.size());
        CHECK(bodies.IsResonate() && bodies.Res().kind == 1 && bodies.Res().lo == 0.f && bodies.Res().hi == 3.f,
              "bodies.kykm did not attach as an index world (kind %d, %g..%g)", bodies.Res().kind, bodies.Res().lo, bodies.Res().hi);
        const float at0[kMaxN] = {0.f}, at1[kMaxN] = {1.f}, nudge[kMaxN] = {0.05f};
        Engine a; a.Init(&bodies, sr); a.gain = 1.f; a.SetPosition(at0, 1); a.SetF0(261.63f); a.Strike(0.7f);
        Engine b; b.Init(&bodies, sr); b.gain = 1.f; b.SetPosition(at0, 1); b.SetF0(130.81f); b.Strike(0.7f);
        Engine c; c.Init(&bodies, sr); c.gain = 1.f; c.SetPosition(at1, 1); c.SetF0(261.63f); c.Strike(0.7f);
        std::vector<float> ya, yb, yc; Run(a, ya, 100); Run(b, yb, 100); Run(c, yc, 100);
        ResonatorVoice v; v.Init(); bodies.Res().At(0.f, v, sr); v.Strike(0.7f);
        std::vector<float> yv(4800); for(int k = 0; k < 100; k++) v.Process(yv.data() + k * 48, 48);
        double dab = 0, dav = 0, dac = 0, en = 0;
        for(int i = 0; i < 4800; i++)
        {
            dab += (ya[i] - yb[i]) * (ya[i] - yb[i]);
            dav += (ya[i] / a.PhaseTrim() - yv[i]) * (ya[i] / a.PhaseTrim() - yv[i]);
            dac += (ya[i] - yc[i]) * (ya[i] - yc[i]);
            en += ya[i] * ya[i];
        }
        CHECK(en > 0 && dab == 0.0, "an index world changed with the pitch: %.3g of the energy", dab / en);
        CHECK(dav < 1e-10 * en, "an index world at position 0 differs from the voice at index 0: %.3g of the energy", dav / en);
        CHECK(dac > 0.1 * en, "position 0 and position 1 are the same body: %.3g of the energy apart", dac / en);
        /* the pot moves under the ring: the sound goes on. A twentieth of
           the travel, which is ten times the deadband and stays inside one
           body's neighbourhood — a row folds its points mode for mode and
           slides each one geometrically, so a nudge moves every mode a few
           per cent and the ring follows it. Crossing to the next body is
           not this question and does not keep the ring: two bodies an
           octave apart in their partials have nothing to carry between
           them, and the carry is capped at the level the new mode's own
           strike would give it, which for a bell taking a bass marimba's
           ring is near zero. That is the cap working. */
        Engine g; g.Init(&bodies, sr); g.gain = 1.f; g.SetPosition(at0, 1); g.SetF0(261.63f); g.Strike(0.7f);
        std::vector<float> g1; Run(g, g1, 100);
        g.SetPosition(nudge, 1);
        std::vector<float> g2; Run(g, g2, 5);
        double e_before = 0, e_after = 0;
        for(int i = 4560; i < 4800; i++) e_before += g1[i] * g1[i];
        for(int i = 0; i < 240; i++) e_after += g2[i] * g2[i];
        CHECK(e_after > 0.1 * e_before, "moving the position cut the ringing body: %.3g before, %.3g after", e_before, e_after);
        printf("  an index world: position, not pitch (0 apart at two pitches; %.3g of the energy apart at two positions); the pot moving keeps the ring (%.3g -> %.3g)\n", dac / en, e_before, e_after);
    }

    /* 6. the spin: a tune at its centre is the fitted world bit for bit;
       decay x4 rings longer; voicing and the coil change the EP's spectrum
       (it has a pickup), and the change lands with the state ringing on */
    {
        auto energy = [](const std::vector<float>& v, size_t a, size_t b) { double e = 0; for(size_t i = a; i < b; i++) e += v[i] * v[i]; return e; };
        Engine e0; e0.Init(&piano, sr); e0.gain = 1.f; e0.SetF0(261.63f);
        Engine e1 = e0; e1.SetTune(Engine::Tune::Voicing, 0.f); e1.SetTune(Engine::Tune::Decay, 1.f); e1.SetTune(Engine::Tune::Coil, 1.f);
        e0.Strike(0.7f); e1.Strike(0.7f);
        std::vector<float> y0, y1; Run(e0, y0, 100); Run(e1, y1, 100);
        double d = 0; for(size_t i = 0; i < y0.size(); i++) d += (y0[i] - y1[i]) * (y0[i] - y1[i]);
        CHECK(d == 0.0, "a tune at its centre changed the sound: %.3g", d);
        Engine e4; e4.Init(&piano, sr); e4.gain = 1.f; e4.SetF0(261.63f); e4.SetTune(Engine::Tune::Decay, 4.f); e4.Strike(0.7f);
        std::vector<float> y4; Run(e4, y4, 1000);
        Engine e5; e5.Init(&piano, sr); e5.gain = 1.f; e5.SetF0(261.63f); e5.Strike(0.7f);
        std::vector<float> y5; Run(e5, y5, 1000);
        const double late4 = energy(y4, 40000, 48000), late1 = energy(y5, 40000, 48000);
        CHECK(late4 > 2.0 * late1, "decay x4 did not ring longer: %.3g against %.3g at 0.9 s", late4, late1);
        /* the EP: voicing moves the pole, the coil moves its resonance;
           the tune arriving under a ring is heard on the next block */
        Engine p0; p0.Init(&rw, sr); p0.gain = 1.f; p0.SetF0(261.63f); p0.Strike(0.8f);
        Engine pv = p0, pc = p0;
        std::vector<float> a0; Run(p0, a0, 100);
        pv.SetTune(Engine::Tune::Voicing, 1.5f); std::vector<float> av; Run(pv, av, 100);
        pc.SetTune(Engine::Tune::Coil, 0.5f); std::vector<float> ac; Run(pc, ac, 100);
        double dv = 0, dc = 0, ea = 0;
        for(size_t i = 0; i < a0.size(); i++) { dv += (a0[i] - av[i]) * (a0[i] - av[i]); dc += (a0[i] - ac[i]) * (a0[i] - ac[i]); ea += a0[i] * a0[i]; }
        CHECK(dv > 0.01 * ea, "voicing did nothing on the EP: %.3g of the energy", dv / ea);
        CHECK(dc > 0.01 * ea, "the coil did nothing on the EP: %.3g of the energy", dc / ea);
        CHECK(energy(av, 0, 240) > 0.1 * energy(a0, 0, 240), "voicing cut the ring: %.3g against %.3g", energy(av, 0, 240), energy(a0, 0, 240));
        printf("  the spin: centre is the fitted world (0 apart); decay x4 rings %.1fx at 0.9 s; voicing moves the EP %.2f of its energy, the coil %.2f, the ring kept\n",
               late4 / late1, dv / ea, dc / ea);
    }

    /* 7. polyphony: at two voices a C3 struck and then a G3 struck leaves
       both ringing, the C3 at its own pitch; at one voice the G3 strike
       retunes the C3 away, so only the G3 series is left. Measured on the
       spectrum 100 ms after the second strike. */
    {
        auto level_at = [](const std::vector<float>& y, float hz) {
            double best = 0; const int n = (int)y.size();
            for(float f = hz * 0.99f; f <= hz * 1.01f; f += hz * 0.001f)
            { double re = 0, im = 0; for(int i = 0; i < n; i++) { const double ph = 6.2831853 * f * i / 48000.0; re += y[i] * std::cos(ph); im -= y[i] * std::sin(ph); } best = std::fmax(best, std::sqrt(re * re + im * im) / n); }
            return 20 * std::log10(best + 1e-12);
        };
        /* measured 0.5 to 0.7 s after the second strike: past the C3's
           burst, which is the recording's first 390 ms and plays to its
           end whatever the voice count (a cut burst was a click) */
        auto two = [&](int poly) {
            Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(poly);
            e.SetF0(130.81f); e.Strike(0.7f);
            std::vector<float> a; Run(e, a, 50);
            e.SetF0(196.f); e.Strike(0.7f);
            std::vector<float> b; Run(e, b, 700);
            std::vector<float> tail(b.begin() + 24000, b.begin() + 33600);
            return std::make_pair(level_at(tail, 130.81f), level_at(tail, 196.f));
        };
        const auto p2 = two(2), p1 = two(1);
        CHECK(p2.first > p2.second - 12.0 && p2.first > -60.0, "at two voices the C3 did not ring on under the G3: C3 %.1f dB, G3 %.1f", p2.first, p2.second);
        CHECK(p1.first < p2.first - 15.0, "at one voice the C3 was not retuned away: %.1f dB against %.1f at two", p1.first, p2.first);
        printf("  polyphony: two voices hold C3 at %.1f dB under a G3 at %.1f; one voice leaves the C3 at %.1f\n", p2.first, p2.second, p1.first);
    }

    /* 8. the exciter: white noise driven into the Wurlitzer's C3 voice with
       no strike at all rings at the note — the fundamental's line stands
       20 dB over the noise's own level there — and nothing rings without
       the drive */
    {
        auto level_at = [](const std::vector<float>& y, float hz) {
            double re = 0, im = 0; const int n = (int)y.size();
            for(int i = 0; i < n; i++) { const double ph = 6.2831853 * hz * i / 48000.0; re += y[i] * std::cos(ph); im -= y[i] * std::sin(ph); }
            return 20 * std::log10(std::sqrt(re * re + im * im) / n + 1e-12);
        };
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetF0(130.81f);
        std::vector<float> noise(4800), y(4800), quiet(4800);
        uint32_t r = 12345u; for(auto& v : noise) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; v = ((int32_t)r) * (0.3f / 2147483648.f); }
        for(int b = 0; b < 100; b++) { e.SetExciter(noise.data() + b * 48, 0.02f); e.Process(y.data() + b * 48, 48); }
        Engine q; q.Init(&piano, sr); q.gain = 1.f; q.SetF0(130.81f);
        for(int b = 0; b < 100; b++) q.Process(quiet.data() + b * 48, 48);
        double eq = 0; for(float v : quiet) eq += v * v;
        const double at = level_at(y, 130.81f), off = level_at(y, 150.f);
        CHECK(at > off + 15.0, "a driven voice does not ring at its note: %.1f dB at C3, %.1f at 150 Hz", at, off);
        CHECK(eq == 0.0, "an undriven, unstruck voice made sound: %.3g", eq);
        printf("  exciter: noise in, C3 stands %.1f dB over 150 Hz; silent without the drive\n", at - off);
    }

    /* 9. a family: a container of the Wurlitzer and the EP built here, the
       way export.py builds one; position 0 chooses the instrument, the
       pitch the note. At position 0 the family plays exactly what the
       Wurlitzer alone plays; at 1, exactly the EP; between, one or the
       other with hysteresis, never a blend. */
    {
        std::vector<uint8_t> fam;
        auto u8 = [&](uint8_t v) { fam.push_back(v); };
        auto u16 = [&](uint16_t v) { u8(v & 255); u8(v >> 8); };
        auto u32 = [&](uint32_t v) { u16(v & 65535); u16(v >> 16); };
        auto f32 = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); };
        const std::vector<uint8_t>* mem[2] = { &wblob, &blob };
        const char* names[2] = { "piano", "tine" };
        uint16_t Nmax = 0; for(auto* b : mem) { uint16_t nn; std::memcpy(&nn, b->data() + 6, 2); Nmax = std::max(Nmax, nn); }
        fam.insert(fam.end(), {'K', 'Y', 'K', 'M'}); u16(6); u16(Nmax); u16(0); u8(0); u8(2); f32(0.f); f32(1.f);
        u8(2);
        uint32_t off = (uint32_t)(20 + 1 + 24 * 2); off = (off + 3) & ~3u;
        std::vector<uint8_t> body;
        for(int i = 0; i < 2; i++)
        {
            u32(off + (uint32_t)body.size()); u32((uint32_t)mem[i]->size());
            char nm[16] = {0}; std::snprintf(nm, 16, "%s", names[i]); for(char c : nm) u8((uint8_t)c);
            body.insert(body.end(), mem[i]->begin(), mem[i]->end());
            while(body.size() & 3) body.push_back(0);
        }
        while(fam.size() < off) fam.push_back(0);
        fam.insert(fam.end(), body.begin(), body.end());
        World f; f.UseResonate(fam.data(), (uint32_t)fam.size());
        CHECK(f.IsResonate() && f.Res().kind == 2 && f.Res().M == 2, "the family did not attach (kind %d, %d members)", f.Res().kind, f.Res().M);
        CHECK(std::strcmp(f.Res().MemberName(1), "tine") == 0, "member 1 is named '%s'", f.Res().MemberName(1));
        const float at0[kMaxN] = {0.f}, at1[kMaxN] = {1.f}, at45[kMaxN] = {0.45f};
        auto render = [&](const World& w, const float* pos) {
            Engine e; e.Init(&w, sr); e.gain = 1.f; e.SetPosition(pos, 1); e.SetF0(130.81f); e.Strike(0.6f);
            std::vector<float> y; Run(e, y, 100); return y;
        };
        const auto fw = render(f, at0), ww = render(piano, at0), fe = render(f, at1), ee = render(rw, at0), fm = render(f, at45);
        double d0 = 0, d1 = 0, dm = 0, en = 0;
        for(size_t i = 0; i < fw.size(); i++) { d0 += (fw[i] - ww[i]) * (fw[i] - ww[i]); d1 += (fe[i] - ee[i]) * (fe[i] - ee[i]); dm += (fm[i] - ww[i]) * (fm[i] - ww[i]); en += ww[i] * ww[i]; }
        CHECK(d0 == 0.0, "the family at position 0 is not the Wurlitzer: %.3g of the energy", d0 / en);
        CHECK(d1 == 0.0, "the family at position 1 is not the EP: %.3g of the energy", d1 / en);
        CHECK(dm == 0.0, "the family at 0.45 is not still the Wurlitzer (a blend?): %.3g of the energy", dm / en);
        printf("  a family of two: position 0 is the Wurlitzer, 1 the EP, 0.45 still the Wurlitzer, all bit for bit\n");
    }

    /* 10. the pitch lock, on by default: a strike lands on the semitone
       nearest the pitch (131.8 Hz, 13 cents sharp, is a C3, not a C3 and
       13 cents), a ring keeps its note when the pitch moves to the next
       (the tail does not bend up behind the strike), and the next strike
       takes the new pitch. Unlocked, the ring follows by the cent — a
       bend. Read from the voice the engine reports, which is what At()
       built it at. */
    {
        auto f0_of = [](const Engine& e) { return e.Voice().hz[0]; };
        Engine e; e.Init(&piano, sr); e.gain = 1.f;
        CHECK(e.PitchLock(), "the lock is not on by default");
        e.SetF0(131.8f); e.Strike(0.7f);
        const float struck = f0_of(e);
        std::vector<float> y; Run(e, y, 50);      /* past the 30 ms late-CV window */
        e.SetF0(196.f); Run(e, y, 20);
        const float moved = f0_of(e);
        e.Strike(0.7f); Run(e, y, 20);
        const float again = f0_of(e);
        CHECK(std::fabs(struck - 130.81f) < 0.2f, "a strike 13 cents sharp of C3 was built at %.2f Hz, not C3", struck);
        CHECK(moved == struck, "locked, the ring followed the pitch: %.2f Hz after C3 was struck", moved);
        CHECK(std::fabs(again - 196.f) < 0.3f, "the next strike did not take the new pitch: %.2f Hz", again);
        Engine u; u.Init(&piano, sr); u.gain = 1.f; u.SetPitchLock(false);
        u.SetF0(131.8f); u.Strike(0.7f);
        const float ustruck = f0_of(u);
        u.SetF0(196.f); Run(u, y, 20);
        const float umoved = f0_of(u);
        CHECK(std::fabs(ustruck - 131.8f) < 0.2f, "unlocked, a strike was quantised: %.2f Hz for 131.8", ustruck);
        CHECK(std::fabs(umoved - 196.f) < 0.3f, "unlocked, the ring did not follow the pitch: %.2f Hz", umoved);
        printf("  pitch lock: 131.8 Hz strikes C3 (%.2f), holds it under a G3 pitch, takes the G3 at the strike; unlocked %.1f then %.1f\n", struck, ustruck, umoved);
        /* a sequencer whose CV lands after its gate: a jump within 30 ms of
           the strike is the strike's note; one at 100 ms is not */
        Engine l; l.Init(&piano, sr); l.gain = 1.f;
        l.SetF0(130.81f); l.Strike(0.7f); Run(l, y, 10);
        l.SetF0(196.f); Run(l, y, 10);
        const float late = f0_of(l);
        Run(l, y, 100);
        l.SetF0(220.f); Run(l, y, 10);
        const float later = f0_of(l);
        CHECK(std::fabs(late - 196.f) < 0.3f, "a pitch landing 10 ms after the strike was not taken: %.2f Hz", late);
        CHECK(std::fabs(later - 196.f) < 0.3f, "a pitch moving 120 ms after the strike was taken: %.2f Hz", later);
        printf("  a late CV: 10 ms after the strike the ring goes to it (%.1f), 120 ms after it does not (%.1f)\n", late, later);
    }

    /* 11. nothing cuts: a strike past the voice count carries the voice it
       takes rather than silencing it, and turning the count down lets the
       voices past it ring out. The third strike is at velocity 0 — on the
       Wurlitzer that is a retune with no energy added — so the only step
       there could be is a cut of the C3's ring, and a ring's step from
       one sample to the next is what it was a block earlier. */
    {
        auto max_step = [](const std::vector<float>& y, size_t from, size_t to) {
            float m = 0.f; for(size_t i = from + 1; i < to && i < y.size(); i++) m = std::fmax(m, std::fabs(y[i] - y[i - 1])); return m; };
        auto more = [](Engine& e, std::vector<float>& y, int blocks) {
            std::vector<float> part; Run(e, part, blocks); y.insert(y.end(), part.begin(), part.end()); };
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(2);
        std::vector<float> y;
        /* half a second between, past the Wurlitzer's 390 ms burst, so
           what rings at the third strike is the bank and not the recording */
        e.SetF0(130.81f); e.Strike(0.8f); more(e, y, 500);
        e.SetF0(164.81f); e.Strike(0.8f); more(e, y, 500);
        const size_t before = y.size();
        e.SetF0(196.f);   e.Strike(0.f);                    /* takes the C3's voice: carried, not cut */
        more(e, y, 500);
        const size_t turn = y.size();
        e.SetPolyphony(1); more(e, y, 40);
        const float ringing  = max_step(y, before - 480, before - 1);
        const float at_reuse = max_step(y, before - 1, before + 48);
        const float at_turn  = max_step(y, turn - 1, turn + 48);
        /* a strike at another note chokes the old ring over 2 ms — a fade,
           steeper than the ring's own slope but no step: a cut measured
           6.6x the ring's step here, the choke 2.7x */
        CHECK(at_reuse < 4.f * ringing, "a strike past the count cut a voice: step %.3g where the ring stepped %.3g", at_reuse, ringing);
        CHECK(at_turn < 2.f * ringing, "turning the count down cut a voice: step %.3g where the ring stepped %.3g", at_turn, ringing);
        double after = 0; for(size_t i = turn; i < turn + 480; i++) after += y[i] * y[i];
        CHECK(after > 0, "the voices past the count fell silent at the turn");
        printf("  nothing cuts: a reused voice steps %.3g, the count turned down %.3g, the ring itself %.3g\n", at_reuse, at_turn, ringing);
    }

    /* 12. a tune change reaches every voice, one every four blocks, and does not
       move a locked ring's pitch. Four voices struck at four notes; decay
       x4; within four blocks every voice's zeta has quartered. And a ring
       locked at C3 with the pitch since moved to G3 stays at C3 through
       the tune change (it used to be re-read at the pitch, which under
       the lock is the one thing a tune change must not do). */
    {
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4);
        std::vector<float> y;
        const float notes[4] = {130.81f, 164.81f, 196.f, 261.63f};
        for(int v = 0; v < 4; v++) { e.SetF0(notes[v]); e.Strike(0.7f); Run(e, y, 5); }
        float z0[4]; for(int v = 0; v < 4; v++) z0[v] = e.VoiceAt(v).zeta[0];
        e.SetTune(Engine::Tune::Decay, 4.f);
        Run(e, y, 1);
        int after1 = 0; for(int v = 0; v < 4; v++) if(std::fabs(e.VoiceAt(v).zeta[0] / z0[v] - 0.25f) < 0.01f) after1++;
        /* one voice every four blocks since the rebuild cap (a CV at
           audio rate on the decay was a rebuild every block): the first at
           once, all four by the thirteenth block, 6.5 ms */
        Run(e, y, 4);
        int after5 = 0; for(int v = 0; v < 4; v++) if(std::fabs(e.VoiceAt(v).zeta[0] / z0[v] - 0.25f) < 0.01f) after5++;
        Run(e, y, 8);
        int after13 = 0; for(int v = 0; v < 4; v++) if(std::fabs(e.VoiceAt(v).zeta[0] / z0[v] - 0.25f) < 0.01f) after13++;
        CHECK(after1 == 1, "after one block %d voices had the new decay (the active one)", after1);
        CHECK(after5 == 2, "after five blocks %d of 4 voices had the new decay (two: a rebuild every four blocks)", after5);
        CHECK(after13 == 4, "after thirteen blocks %d of 4 voices had the new decay", after13);
        Engine l; l.Init(&piano, sr); l.gain = 1.f;
        l.SetF0(130.81f); l.Strike(0.7f); Run(l, y, 50);
        l.SetF0(196.f); Run(l, y, 10);
        const float before = l.Voice().hz[0];
        l.SetTune(Engine::Tune::Decay, 2.f); Run(l, y, 2);
        const float after = l.Voice().hz[0];
        CHECK(std::fabs(after - before) < 0.01f, "a tune change moved a locked ring from %.2f to %.2f Hz", before, after);
        printf("  a tune reaches the voices one every four blocks (%d after one, %d after five) and leaves a locked ring at %.1f Hz\n", after1, after5, after);
    }

    /* 13. Rings' rule for polyphony: the bank's modes shared out, so four
       voices are as rich as one. At one voice the Wurlitzer's C3 has
       every fitted mode; at four, twelve each — the loudest twelve, so
       the fundamental is among them. */
    {
        auto live = [](const ResonatorVoice& v, int N) { int n = 0; for(int k = 0; k < N; k++) if(v.gain[k] != 0.f) n++; return n; };
        const int N = piano.Res().N;
        Engine e; e.Init(&piano, sr); e.gain = 1.f;
        e.SetF0(130.81f); e.Strike(0.7f);
        const int one = live(e.Voice(), N);
        e.SetPolyphony(4);
        std::vector<float> y;
        const float notes[4] = {130.81f, 146.83f, 164.81f, 196.f};
        for(int v = 0; v < 4; v++) { e.SetF0(notes[v]); e.Strike(0.7f); Run(e, y, 3); }
        int most = 0, fund = 0;
        for(int v = 0; v < 4; v++) most = std::max(most, live(e.VoiceAt(v), N));
        for(int n = 0; n < 4; n++)          /* each note's fundamental is live on the voice that holds it, whichever the round gave it */
        {
            bool has = false;
            for(int v = 0; v < 4; v++) for(int k = 0; k < N; k++) if(e.VoiceAt(v).gain[k] != 0.f && std::fabs(e.VoiceAt(v).hz[k] / notes[n] - 1.f) < 0.01f) has = true;
            if(has) fund++;
        }
        CHECK(one > 12, "at one voice the C3 has only %d modes", one);
        CHECK(most <= 12, "at four voices a voice has %d modes (12 allowed)", most);
        CHECK(fund == 4, "the fundamental survived the cut on %d of 4 voices", fund);
        e.SetPolyphony(1); e.SetF0(130.81f); e.Strike(0.7f);
        CHECK(live(e.Voice(), N) == one, "back at one voice the C3 has %d modes, not %d", live(e.Voice(), N), one);
        printf("  polyphony shares the modes: %d at one voice, at most %d each at four, the fundamental kept on all\n", one, most);
    }

    /* 14. the strike harder with fast playing: ten strikes at ten a
       second at velocity 0.5 — with the track at full the tenth is made
       harder (0.5 + 0.5 x the density's share of six a second), the
       first not at all; with the track off every one is 0.5. And three
       seconds' rest lets the density leak back to a trace */
    {
        auto run10 = [&](float track) {
            Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetVelocityTrack(track); e.SetF0(261.63f);
            std::vector<float> y; float first = 0.f, tenth = 0.f;
            for(int k = 0; k < 10; k++) { e.Strike(0.5f); if(k == 0) first = e.LastStrikeVelocity(); if(k == 9) tenth = e.LastStrikeVelocity(); Run(e, y, 100); }
            Run(e, y, 3000); e.Strike(0.5f);
            return std::make_tuple(first, tenth, e.LastStrikeVelocity()); };
        const auto on = run10(1.f), off = run10(0.f);
        CHECK(std::fabs(std::get<0>(on) - 0.5f) < 1e-6f, "the first strike was tracked: %.3f", std::get<0>(on));
        CHECK(std::get<1>(on) > 0.9f && std::get<1>(on) <= 1.f, "the tenth strike at ten a second is %.3f (0.9..1 wanted)", std::get<1>(on));
        CHECK(std::get<2>(on) < 0.54f, "after three seconds' rest the strike is still tracked: %.3f", std::get<2>(on));
        CHECK(std::get<1>(off) == 0.5f, "with the track off the tenth strike is %.3f", std::get<1>(off));
        printf("  dig in: ten strikes a second take the tenth from 0.5 to %.2f; rested, %.2f; off, %.2f\n", std::get<1>(on), std::get<2>(on), std::get<1>(off));
    }

    /* 15. on a world with no pickup, axes 0 and 3 are the strike position
       and brightness. The Wurlitzer file here is form 0: voicing at the
       centre is the world as fitted (bit for bit); at the right end
       (position 0.5, the middle of the string) the second harmonic is
       cut against the fundamental; coil x2 lifts the top by 3 dB an
       octave, coil x1/2 drops it. */
    {
        auto lvl = [](const std::vector<float>& y, float hz) {
            double re = 0, im = 0; const int n = (int)y.size();
            for(int i = 0; i < n; i++) { const double ph = 6.2831853 * hz * i / 48000.0; re += y[i] * std::cos(ph); im -= y[i] * std::sin(ph); }
            return 20 * std::log10(std::sqrt(re * re + im * im) / n + 1e-12); };
        auto ring = [&](float voicing, float coil) {
            Engine e; e.Init(&piano, sr); e.gain = 1.f;
            e.SetTune(Engine::Tune::Voicing, voicing); e.SetTune(Engine::Tune::Coil, coil);
            e.SetF0(130.81f); e.Strike(0.7f);
            std::vector<float> y; Run(e, y, 200); return std::vector<float>(y.begin() + 4800, y.end()); };
        const auto c = ring(0.f, 1.f), c2 = ring(0.f, 1.f), mid = ring(2.f, 1.f), bright = ring(0.f, 2.f), dull = ring(0.f, 0.5f);
        CHECK(c == c2, "the centre is not repeatable");
        const double h2c = lvl(c, 261.6f) - lvl(c, 130.81f), h2m = lvl(mid, 261.6f) - lvl(mid, 130.81f);
        CHECK(h2m < h2c - 6.0, "position at the middle of the string did not cut the second harmonic: %.1f dB against %.1f at the centre", h2m, h2c);
        const double tiltb = (lvl(bright, 1046.f) - lvl(bright, 130.81f)) - (lvl(c, 1046.f) - lvl(c, 130.81f));
        const double tiltd = (lvl(dull, 1046.f) - lvl(dull, 130.81f)) - (lvl(c, 1046.f) - lvl(c, 130.81f));
        CHECK(tiltb > 6.0 && tiltb < 12.0, "brightness x2 tilted three octaves by %.1f dB (9 wanted)", tiltb);
        CHECK(tiltd < -6.0 && tiltd > -12.0, "brightness x1/2 tilted three octaves by %.1f dB (-9 wanted)", tiltd);
        printf("  no pickup: position at the middle cuts h2 by %.1f dB; brightness tilts three octaves %+.1f / %+.1f dB\n", h2c - h2m, tiltb, tiltd);
    }

    /* straight knobs on a resonator: the Rotate page's angles do not cross
       its axes (Combust: "knobs are crossed"), an orbit run on it does move
       them, and a wavetable world keeps its angles. A quarter turn in plane
       (1,2) swaps velocity and decay outright when it applies */
    {
        auto axes = [&](const World* w, float ang, float rate, int blocks, int plane = 3) {
            StereoEngine se; se.Init(w, sr);
            se.rot.SetAngle(plane, ang);             /* plane 3 of four axes is (1,2): velocity and decay */
            se.rot.SetRate(plane, rate);
            const float c[kMaxN] = {0.2f, 0.9f, 0.1f, 0.6f};
            se.SetControl(c, 4); se.SnapControl();
            std::vector<float> l(48), r(48);
            for(int b = 0; b < blocks; b++) se.Process(l.data(), r.data(), 48);
            return std::make_tuple(se.L.ControlAt(0), se.L.ControlAt(1), se.L.ControlAt(2), se.L.ControlAt(3));
        };
        const auto st = axes(&piano, 0.25f, 0.f, 4);
        CHECK(std::get<0>(st) == 0.2f && std::get<1>(st) == 0.9f && std::get<2>(st) == 0.1f && std::get<3>(st) == 0.6f,
              "a Rotate page angle crossed a resonator's axes: body %.3f velocity %.3f decay %.3f coil %.3f (0.2 0.9 0.1 0.6 set)",
              std::get<0>(st), std::get<1>(st), std::get<2>(st), std::get<3>(st));
        const auto orb = axes(&piano, 0.f, 1.f, 100);
        CHECK(std::fabs(std::get<1>(orb) - 0.9f) > 0.05f || std::fabs(std::get<2>(orb) - 0.1f) > 0.05f,
              "an orbit run on a resonator did not move its axes: velocity %.3f decay %.3f", std::get<1>(orb), std::get<2>(orb));
        World saw; solids::VertexTable stbl;
        CHECK(worlds::Point(worlds::kSaw, saw, 8, nullptr, &stbl), "could not build Saw");
        const auto wt = axes(&saw, 0.25f, 0.f, 4, 0);   /* plane 0, (0,1): Saw has fewer axes than four */
        CHECK(std::fabs(std::get<1>(wt) - 0.9f) > 0.05f, "a wavetable world lost its Rotate page angle: axis 1 at %.3f", std::get<1>(wt));
        /* a resonator reached from a wavetable world with an orbit phase on it
           starts straight, as one started in */
        StereoEngine se; se.Init(&saw, sr); se.rot.SetRate(3, 1.f);
        std::vector<float> l(48), r(48);
        for(int b = 0; b < 100; b++) se.Process(l.data(), r.data(), 48);
        se.rot.SetRate(3, 0.f); se.SetWorld(&piano);
        const float c[kMaxN] = {0.2f, 0.9f, 0.1f, 0.6f};
        se.SetControl(c, 4); se.SnapControl(); se.Process(l.data(), r.data(), 48);
        CHECK(se.L.ControlAt(1) == 0.9f && se.L.ControlAt(2) == 0.1f,
              "a resonator arrived at from an orbiting wavetable world is not straight: velocity %.3f decay %.3f", se.L.ControlAt(1), se.L.ControlAt(2));
        printf("  straight knobs: a quarter-turn Rotate angle leaves a resonator's axes as set; an orbit moves them; a wavetable keeps its angle\n");
    }

    /* 16. which voice a strike takes: a note struck again goes back to the
       voice ringing at it, and the other notes ring on; a new note takes a
       silent voice, and failing that the one struck longest ago */
    {
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4);
        std::vector<float> y;
        auto hit = [&](float hz) { e.SetF0(hz); e.Strike(0.8f); Run(e, y, 50); };
        auto slot = [&]() { for(int v = 0; v < 4; v++) if(&e.Voice() == &e.VoiceAt(v)) return v; return -1; };
        hit(130.81f); const int vc = slot();
        hit(164.81f); const int ve = slot();
        hit(196.f);   const int vg = slot();
        CHECK(vc != ve && ve != vg && vc != vg, "three notes did not take three voices: %d %d %d", vc, ve, vg);
        hit(130.81f); const int vc2 = slot(); hit(130.81f); hit(130.81f);       /* C3 three more times */
        CHECK(vc2 == vc && slot() == vc, "a C3 struck again did not go back to the C3's voice (%d, then %d; it was %d)", vc2, slot(), vc);
        CHECK(std::fabs(e.VoiceAt(ve).param - 52.f) < 0.01f && std::fabs(e.VoiceAt(vg).param - 55.f) < 0.01f,
              "a repeated C3 took the E3's or the G3's voice: they hold %.2f and %.2f", e.VoiceAt(ve).param, e.VoiceAt(vg).param);
        hit(220.f); const int va = slot();                      /* A3: the one silent voice */
        CHECK(va != vc && va != ve && va != vg && !(va < 0), "a new note did not take the silent voice: %d", va);
        hit(246.94f);                                           /* B3: all four ring; the oldest strike is the E3's */
        CHECK(slot() == ve, "a note past the count did not take the voice struck longest ago: %d, the E3's is %d", slot(), ve);
        printf("  voices: a repeated note keeps its voice and the others ring on; a new note takes a silent one, then the oldest\n");
    }

    /* 17. the exciter's amount up with nothing coming in is not a drive:
       the note still ringing keeps its pitch when the CV moves — the pot
       is the Stereo page's sixth, left wherever a wavetable world had it,
       and a lock that let go for it retuned the ringing note to the next
       one in the milliseconds a strike waits, a pop at every note start.
       With a signal in, the bank follows the pitch by the semitone, except
       while a strike is waiting (HoldPitch) */
    {
        auto run = [&](const float* drive, bool hold) {
            Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4);
            e.SetF0(130.81f); e.Strike(0.8f);
            std::vector<float> y(48);
            for(int b = 0; b < 250; b++) e.Process(y.data(), 48);
            e.SetF0(196.f); e.HoldPitch(hold);
            for(int b = 0; b < 8; b++) { e.SetExciter(drive ? drive + b * 48 : nullptr, 0.02f); e.Process(y.data(), 48); }
            return e.ResParamNow();
        };
        std::vector<float> silent(8 * 48, 0.f), noise(8 * 48);
        uint32_t r = 777u; for(auto& v : noise) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; v = ((int32_t)r) * (0.3f / 2147483648.f); }
        const float quiet = run(silent.data(), false), driven = run(noise.data(), false), held = run(noise.data(), true);
        CHECK(std::fabs(quiet - 48.f) < 0.01f, "an exciter amount with nothing in retuned the ringing C3 to %.2f", quiet);
        CHECK(std::fabs(driven - 55.f) < 0.01f, "a driven bank did not follow the pitch: %.2f", driven);
        CHECK(std::fabs(held - 48.f) < 0.01f, "a driven bank followed the pitch while a strike was waiting: %.2f", held);
        printf("  exciter: nothing in, the ring keeps C3 (%.0f); driven, it follows (%.0f); a strike waiting, it holds (%.0f)\n", quiet, driven, held);
    }

    /* 18. the output never passes the ceiling: struck at audio rate — J4
       and v/oct both driven by oscillators, a strike every five blocks at
       wherever the pitch is, the decay near its top — the ring piles up
       past full scale, and the limiter takes it (it has to: its gain falls
       below 1, so the check is not passing on a render that fitted anyway) */
    {
        StereoEngine se; se.Init(&piano, sr); se.L.TuneFromControl(true); se.L.SetPolyphony(4);
        const float c[kMaxN] = {0.5f, 1.f, 0.95f, 0.5f};
        se.SetControl(c, 4); se.SnapControl();
        std::vector<float> l(24), r(24);
        float pk = 0.f, gmin = 1.f; bool finite = true;
        for(int b = 0; b < 4000; b++)
        {
            se.SetF0(261.63f * std::exp2(2.f * std::sin(6.2831853f * 100.f * b * 24 / sr)));
            if(b % 5 == 0) se.Strike(1.f);
            se.Process(l.data(), r.data(), 24);
            for(int i = 0; i < 24; i++) { finite = finite && std::isfinite(l[i]); pk = std::fmax(pk, std::fabs(l[i])); }
            gmin = std::fmin(gmin, se.LimiterGain());
        }
        CHECK(finite && pk <= 0.98f + 1e-6f, "the output passed the ceiling under audio-rate strikes: %.3f", pk);
        CHECK(gmin < 0.9f, "the audio-rate strikes never reached the limiter (gain %.3f): the check proves nothing", gmin);
        printf("  limiter: audio-rate strikes peak %.3f with the gain down to %.2f\n", pk, gmin);
    }

    /* 19. nothing passes Nyquist: a note far over a world's top puts its
       partials past half the rate, where the pole's cos is a polynomial
       outside its range and the mode grew without end — 1e32 on the guitar
       from a 4 kHz fundamental, the codec railed. Every such mode is
       silent now: struck from C4 to 200 kHz (v/oct can ask for that), each note stays under four
       times the C4's own peak and finite */
    {
        auto peak_at = [&](const World& w, float hz) {
            Engine e; e.Init(&w, sr); e.gain = 1.f; e.SetF0(hz); e.Strike(1.f);
            std::vector<float> y; Run(e, y, 500);
            float pk = 0.f; for(float v : y) pk = std::isfinite(v) ? std::fmax(pk, std::fabs(v)) : 1e30f;
            return pk;
        };
        for(const World* w : {&piano, &rw})
        {
            const float ref = peak_at(*w, 261.63f);
            float worst = 0.f, at = 0.f;
            for(float hz = 523.25f; hz < 200000.f; hz *= 1.4142f) { const float p = peak_at(*w, hz); if(p > worst) { worst = p; at = hz; } }
            CHECK(worst < 4.f * ref, "a note over the world's top blew up: %.3g at %.0f Hz, against %.3g at C4", worst, at, ref);
            printf("  over the top: struck up to 200 kHz, the loudest %.2fx the C4 (at %.0f Hz)\n", worst / ref, at);
        }
    }

    /* 20. the recorded intro follows the velocity: at full the whole take
       plays, softer a share of it (a quarter at the bottom, 30 ms at least)
       sloping off, and the modes come in under the shorter seam */
    {
        auto playing_for = [&](float vel, uint32_t& len) {
            ResonatorVoice v; v.Init(); piano.Res().At(49.f, v, sr); v.Strike(vel);   /* a point: the take read at its own rate */
            len = v.burst_len;
            std::vector<float> y(48); int n = 0;
            while(v.burst.Playing() && n < 96000) { v.Process(y.data(), 48); n += 48; }
            return n;
        };
        uint32_t len = 0;
        const int full = playing_for(1.f, len), soft = playing_for(0.f, len);
        const float want = std::fmax(0.25f * (float)len, std::fmin(1440.f, (float)len));
        CHECK(std::abs(full - (int)len) <= 96, "a full-velocity strike did not play its whole intro: %d of %u samples", full, len);
        CHECK(std::fabs((float)soft - want) <= 96.f, "the softest strike's intro ran %d samples, not a quarter of %u", soft, len);
        printf("  intro by velocity: full plays %d of %u samples, the softest %d\n", full, len, soft);
    }

    /* 21. format 8: a point may play another point's attack. The export fills
       every semitone of a note world with a point built by At()'s own blend
       (ModalBake tools/worldfill) so the module plays every locked note by
       the cheap path — the blend at every strike ran the pianos' strike
       blocks over (Combust: "a lot of popping and overruns especially on the
       pianos") — and each new point refers to the nearer recorded point's
       attack instead of carrying a copy. Built here in memory from the
       fixture: point k's bytes again at param + 0.5 with its attack a
       reference to k, and a second one referring to the first */
    {
        const ResonatorWorld& W = piano.Res();
        const uint8_t* bl = wblob.data();
        auto u16 = [&](size_t at) { uint16_t v; std::memcpy(&v, bl + at, 2); return v; };
        const uint16_t ver = u16(4), N = u16(6), P = u16(8);
        const size_t head = 20 + (ver >= 7 ? 32 : 0), fixed = 4 + 32 + 5u * N, bh = ver >= 6 ? 12 : 10;
        std::vector<size_t> at{head};
        for(int i = 0; i < P; i++)
        {
            size_t q = at.back() + fixed; q += 1 + 8u * bl[q];
            const uint16_t nb = u16(q); q += 2;
            for(uint16_t k = 0; k < nb; k++) q += bh + 2u * u16(q + 8);
            at.push_back(q);
        }
        const int k = P / 2;
        float pk; std::memcpy(&pk, bl + at[k], 4);
        std::vector<uint8_t> v8(bl, bl + at[k + 1]);                 /* header and points 0..k */
        auto point_ref = [&](float param, uint16_t ref) {
            const size_t s0 = v8.size();
            v8.insert(v8.end(), bl + at[k], bl + at[k] + fixed + 1 + 8u * bl[at[k] + fixed]);   /* k's modes and wash */
            std::memcpy(v8.data() + s0, &param, 4);
            const uint16_t mark = 0xFFFF; v8.insert(v8.end(), (const uint8_t*)&mark, (const uint8_t*)&mark + 2);
            v8.insert(v8.end(), (const uint8_t*)&ref, (const uint8_t*)&ref + 2);
        };
        point_ref(pk + 0.25f, (uint16_t)k);                          /* index k + 1: plays k's attack */
        point_ref(pk + 0.5f, (uint16_t)(k + 1));                     /* index k + 2: a reference to a reference */
        v8.insert(v8.end(), bl + at[k + 1], bl + at[P]);             /* the rest, shifted two on */
        const uint16_t eight = 8, P2 = P + 2; std::memcpy(v8.data() + 4, &eight, 2); std::memcpy(v8.data() + 8, &P2, 2);
        ResonatorWorld V; V.Init();
        CHECK(V.Attach(v8.data(), (uint32_t)v8.size()) && V.P == P + 2, "a version 8 world with references did not attach");
        CHECK(V.BurstPoint(k + 1) == k && V.BurstPoint(k + 2) == k + 1 && V.BurstPoint(k) == k, "BurstPoint: %d %d %d", V.BurstPoint(k + 1), V.BurstPoint(k + 2), V.BurstPoint(k));
        CHECK(V.Bursts(k + 1) == V.Bursts(k), "a reference does not play the attack it names");
        uint16_t nb2; std::memcpy(&nb2, V.Bursts(k + 2), 2);
        CHECK(nb2 == 0, "a reference to a reference played something (%u attacks)", nb2);
        /* every point after the two is where it was: the same note, the same modes */
        bool same = true;
        for(int i = k + 1; i < P; i++)
        {
            ResonatorVoice a, b; a.Init(); b.Init();
            W.At(W.Param(i), a, sr); V.At(V.Param(i + 2), b, sr);
            same = same && W.Param(i) == V.Param(i + 2);
            for(int m = 0; m < ResonatorBank::kMax; m++) same = same && a.hz[m] == b.hz[m] && a.gain[m] == b.gain[m];
        }
        CHECK(same, "the points after a reference moved or changed");
        /* the referring note: k's attack, read at its own pitch */
        ResonatorVoice r; r.Init(); V.At(pk + 0.25f, r, sr);
        ResonatorVoice o; o.Init(); W.At(pk, o, sr);
        CHECK(r.bursts == V.Bursts(k) && std::fabs(r.burst_rate - std::exp2(0.25f / 12.f)) < 1e-5f && r.burst_len == o.burst_len,
              "the referring note's attack: rate %.5f (want %.5f), %u samples (k's %u)", r.burst_rate, std::exp2(0.25f / 12.f), r.burst_len, o.burst_len);
        printf("  format 8: a point plays another's attack at its own pitch (x%.4f); a reference to a reference plays nothing; the points after are untouched\n", r.burst_rate);
        /* and a world whose bytes run out is refused at Attach, never read
           past: cut short, or stamped with a version it was not written for
           (a version 6 world marked 8 reads its first point as a body curve),
           or a reference past the last point */
        ResonatorWorld T; T.Init();
        const bool cut = T.Attach(bl, (uint32_t)wblob.size() - 100);
        std::vector<uint8_t> restamped(wblob);
        const uint16_t six = 6; std::memcpy(restamped.data() + 4, &six, 2);       /* the fixture is 7: read as 6, its body curve is taken for its first point */
        ResonatorWorld T2; T2.Init();
        const bool wrongver = T2.Attach(restamped.data(), (uint32_t)restamped.size());
        std::vector<uint8_t> badref(v8);
        size_t rat = 0;
        for(size_t i = 0; i + 4 <= badref.size(); i++) { uint16_t m, x; std::memcpy(&m, &badref[i], 2); std::memcpy(&x, &badref[i + 2], 2); if(m == 0xFFFF && x == (uint16_t)k) { rat = i; break; } }
        const uint16_t far = 9999; if(rat) std::memcpy(&badref[rat + 2], &far, 2);
        ResonatorWorld T3; T3.Init();
        const bool farref = rat && T3.Attach(badref.data(), (uint32_t)badref.size());
        CHECK(!cut && !wrongver && !farref && rat, "a world whose bytes run out attached: cut short %d, read as another version %d, a reference past the end %d", cut, wrongver, farref);
        printf("  a world cut short, read as another version, or referring past its last point is refused at Attach\n");
    }

    /* 22. the release: how long a stolen voice's last note takes to fall is a
       setting (Engine::SetReleaseMs, 40 ms by default), and a tail still
       sounding when the voice is stolen again goes on rather than being cut —
       at 40 ms it had always fallen 60 dB by the next note, at 200 ms and
       notes 30 ms apart it has not, and dropping it was a step. On the bank:
       three notes of six modes each, a release after each */
    {
        auto bank_note = [&](ResonatorBank& b, float f0) {
            float hz[6], z[6], g[6], ph[6];
            for(int k = 0; k < 6; k++) { hz[k] = f0 * (k + 1); z[k] = 0.0005f; g[k] = 0.3f / (k + 1); ph[k] = 0.3f * k; }
            b.Set(hz, z, g, 6, sr, ph); b.Strike(1.f);
        };
        auto run = [&](ResonatorBank& b, int n, std::vector<float>& y) { const size_t s0 = y.size(); y.resize(s0 + n); for(int i = 0; i < n; i += 24) b.Process(y.data() + s0 + i, 24); };
        ResonatorBank b; b.Init();
        std::vector<float> y;
        bank_note(b, 130.81f); run(b, 4800, y);
        b.Release(200.f, sr);
        CHECK(b.tail_left == 9600 && std::fabs(std::pow((double)b.tail_c, 9600.0) - 1e-3) < 1e-4, "a 200 ms release: %d samples, falls to %.2e over them", b.tail_left, std::pow((double)b.tail_c, 9600.0));
        bank_note(b, 164.81f); run(b, 1440, y);                         /* 30 ms on, the first note's tail about 9 dB down */
        b.Release(200.f, sr);
        const int tails = b.tn;
        /* the step at the third release: the bank's output either side of
           it against its own step a sample before */
        run(b, 1440, y);
        const size_t at = y.size();
        const float before = std::fabs(y[at - 1] - y[at - 2]);
        b.Release(200.f, sr);
        bank_note(b, 196.f);
        std::vector<float> z2; run(b, 24, z2);
        /* the new note starts from its own state; what has to be continuous
           is the old notes, so the new one is subtracted: the same bank
           state struck alone */
        ResonatorBank lone; lone.Init(); bank_note(lone, 196.f); std::vector<float> zl; run(lone, 24, zl);
        const float across = std::fabs((z2[0] - zl[0]) - y[at - 1]);
        CHECK(tails == 12, "the second steal kept %d tail modes, not the first note's six and the second's six", tails);
        CHECK(across < 8.f * before + 1e-6f, "a steal cut the tails: a step of %.3g where the ring stepped %.3g", across, before);
        printf("  release: 200 ms sets a 200 ms fall; a steal keeps the tails still sounding (%d modes), no step (%.3g against the ring's %.3g)\n", tails, across, before);
    }

    /* 22d. the rebuilds under playing nobody plans for (Combust: "people
       smash notes together. roll their fingers across the keys. Call in
       v/oct with audio rate", and it overran). Four voices, a strike every
       four milliseconds (the module's shortest hold), the pitch random every
       block and the decay and coil axes with it: never two voices built in
       one block, and the rebuilds a second at most two a strike (the strike
       and its one late-CV retune) plus one every four blocks (500). It was 1 940 a second with the pitch alone —
       the late-CV rule rebuilding every block for 30 ms after each strike —
       and two in one block one block in eight with the axes moving */
    {
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.TuneFromControl(true); e.SetPolyphony(4);
        std::vector<float> y(24);
        uint32_t rs = 7; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.f; };
        float c[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        int most = 0, strikes = 0; const uint32_t a0 = e.AtCount();
        const int blocks = 4000;
        for(int b = 0; b < blocks; b++)
        {
            c[2] = rnd(); c[3] = rnd(); e.SetPosition(c, 4);
            e.SetF0(440.f * std::exp2((36.f + 48.f * rnd() - 69.f) / 12.f));
            const uint32_t before = e.AtCount();
            if(b % 8 == 0) { e.Strike(0.9f); strikes++; }
            e.Process(y.data(), 24);
            most = std::max(most, (int)(e.AtCount() - before));
        }
        const double secs = blocks * 24.0 / sr, per_s = (e.AtCount() - a0) / secs;
        CHECK(most == 1, "%d voices built in one block under audio-rate v/oct", most);
        CHECK(per_s <= 2.0 * strikes / secs + 500.0 + 1.0, "%.0f rebuilds a second under audio-rate v/oct at %.0f strikes a second: more than two a strike (the strike and its one late CV) and one every four blocks", per_s, strikes / secs);
        printf("  audio-rate v/oct with its axes moving and a strike every 4 ms: one build a block at most, %.0f a second (%.0f of them strikes)\n", per_s, strikes / secs);
    }

    /* 22g. a quiet main state is skipped (ResonatorBank::quiet): a note
       struck at another note starts from zeros and stays there until its
       strike folds in, and running the main loop over the zeros was a
       voice's whole cost through the lead and the fade. Held to exactness:
       the same banks — strikes at new notes (Release, fresh Set), a strike
       at the same note inside a fade (both strike banks), a bend, the
       exciter driving it for a while — once as they are and once with the
       skip forced off before every block, mono and two ears: identical, bit
       for bit, and the skip taken on some blocks */
    {
        auto hit = [&](ResonatorBank& b, float f0, float lead, bool fresh) {
            float hz[8], z[8], g[8], ph[8];
            for(int k = 0; k < 8; k++) { hz[k] = f0 * (k + 1) * (1.f + 0.001f * k * k); z[k] = 0.0008f; g[k] = 0.3f / (k + 1); ph[k] = 0.4f * k; }
            if(fresh) { b.Release(200.f, sr, 8); b.Set(hz, z, g, 8, sr, ph); }
            b.Strike(1.f, lead, 480.f);
        };
        auto run = [&](bool force, bool lr) {
            ResonatorBank b; b.Init();
            std::vector<float> L(24), R(24), x(24), out;
            const float wl[8] = {1, 0.5f, 1, 0.2f, 1, 1, 0.7f, 1}, wr[8] = {0.3f, 1, 1, 1, 0.6f, 1, 1, 0.1f};
            int skipped = 0;
            uint32_t rs = 3; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.f - 0.5f; };
            for(int blk = 0; blk < 2000; blk++)
            {
                if(blk == 0) hit(b, 110.f, 960.f, true);
                if(blk == 150) hit(b, 146.8f, 1440.f, true);
                if(blk == 170) hit(b, 146.8f, 0.f, false);               /* the same note inside the fade: the second strike bank */
                if(blk == 400) { float hz[8], z[8]; for(int k = 0; k < 8; k++) { hz[k] = 150.f * (k + 1); z[k] = 0.0008f; } b.Bend(hz, z, 8, sr); }
                if(blk == 600) hit(b, 196.f, 2400.f, true);
                if(blk == 1200) hit(b, 98.f, 480.f, true);
                const bool drive = blk >= 1210 && blk < 1300;
                for(int k = 0; k < 24; k++) x[k] = rnd();
                if(force) b.quiet = false; else if(b.quiet) skipped++;
                if(lr) { b.ProcessLR(L.data(), R.data(), 24, wl, wr, drive ? x.data() : nullptr, 0.01f); out.insert(out.end(), L.begin(), L.end()); out.insert(out.end(), R.begin(), R.end()); }
                else { b.Process(L.data(), 24, drive ? x.data() : nullptr, 0.01f); out.insert(out.end(), L.begin(), L.end()); }
            }
            return std::make_pair(out, skipped);
        };
        for(int lr = 0; lr < 2; lr++)
        {
            const auto a = run(false, lr), f = run(true, lr);
            bool same = a.first.size() == f.first.size();
            for(size_t i = 0; same && i < a.first.size(); i++) same = a.first[i] == f.first[i];
            CHECK(same, "%s: skipping the quiet main state changed the output", lr ? "two ears" : "mono");
            CHECK(a.second > 100, "%s: the quiet main state was skipped on %d blocks", lr ? "two ears" : "mono", a.second);
            printf("  a quiet main state skipped on %d blocks of 2000 (%s), bit for bit the bank run through\n", a.second, lr ? "two ears" : "mono");
        }
    }

    /* 22f. a roll faster than the late-CV window: C3 struck, and 10 ms later
       the pitch at E3 with the next strike waiting (HoldPitch, as the module
       holds every strike for its CV). The C3 keeps ringing at C3 on its
       voice and the E3 takes another. The late-CV rule took the E3 as the
       C3's late CV — the ringing note dragged to E3, and the strike then
       found E3 there and struck that voice again */
    {
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4);
        std::vector<float> y;
        e.SetF0(130.81f); e.Strike(0.8f); Run(e, y, 12);
        int cv = -1; for(int v = 0; v < 4; v++) if(std::fabs(e.VoiceAt(v).param - 48.f) < 0.01f) cv = v;
        e.HoldPitch(true); e.SetF0(164.81f); Run(e, y, 8);
        const float c3 = cv >= 0 ? e.VoiceAt(cv).param : -1.f;
        e.Strike(0.8f); e.HoldPitch(false); Run(e, y, 4);
        int at_c3 = 0, at_e3 = 0;
        for(int v = 0; v < 4; v++) { if(std::fabs(e.VoiceAt(v).param - 48.f) < 0.01f) at_c3++; if(std::fabs(e.VoiceAt(v).param - 52.f) < 0.01f) at_e3++; }
        CHECK(std::fabs(c3 - 48.f) < 0.01f && at_c3 == 1 && at_e3 == 1, "a roll 10 ms apart: the C3's voice went to note %.2f while the E3 waited; %d voices at C3, %d at E3", c3, at_c3, at_e3);
        /* the CV a block ahead of the gate, as a keyboard's: the pitch at E3
           a millisecond before the strike is waiting — still the E3's */
        {
            Engine f; f.Init(&piano, sr); f.gain = 1.f; f.SetPolyphony(4);
            std::vector<float> z;
            f.SetF0(130.81f); f.Strike(0.8f); Run(f, z, 12);
            int fv = -1; for(int v = 0; v < 4; v++) if(std::fabs(f.VoiceAt(v).param - 48.f) < 0.01f) fv = v;
            f.SetF0(164.81f); Run(f, z, 2);                            /* the CV first */
            f.HoldPitch(true); Run(f, z, 8); f.Strike(0.8f); f.HoldPitch(false); Run(f, z, 4);
            const float lead = fv >= 0 ? f.VoiceAt(fv).param : -1.f;
            CHECK(std::fabs(lead - 48.f) < 0.01f, "the CV a millisecond ahead of the gate dragged the ringing C3 to note %.2f", lead);
            /* and a CV that is late, with no strike after it, is still taken */
            Engine g; g.Init(&piano, sr); g.gain = 1.f; g.SetPolyphony(4);
            g.SetF0(130.81f); g.Strike(0.8f); Run(g, z, 4);
            g.SetF0(164.81f); Run(g, z, 8);
            CHECK(std::fabs(g.Voice().param - 52.f) < 0.01f, "a late CV with no strike after it was not taken: the voice is at %.2f", g.Voice().param);
        }
        printf("  a roll under the late-CV window: the ringing note keeps its pitch while the next strike waits, and the next takes its own voice\n");
    }

    /* 22e. the staged strike: the voice a waiting strike will need built
       before it, off the audio thread (PlanStrike, ServeStrikePlan), and the
       strike only taking it. Two engines play the same performance — four
       voices, notes from all over, a strike every 20 blocks, the decay moved
       twice, a strike at the note already ringing now and then — one building
       every voice inline, one served a plan before each strike: the output
       bit for bit the same, and the plan taken on the strikes it is for (at
       another note). A plan made stale by the pitch moving after it was built
       is refused. A pickup world (the fixture tine) takes them too, its coil
       crossed from the old voice's on the audio thread as Build crosses it */
    {
        auto play = [&](const World& w, bool staged, bool stale, uint32_t& taken) {
            Engine e; e.Init(&w, sr); e.gain = 1.f; e.SetPolyphony(4);
            static StrikePlan plan;
            if(staged) e.SetStrikePlan(&plan);
            std::vector<float> y;
            uint32_t rs = 11; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.f; };
            float note = 60.f;
            for(int b = 0; b < 3000; b++)
            {
                if(b == 1000) e.SetTune(Engine::Tune::Decay, 2.f);
                if(b == 2000) e.SetTune(Engine::Tune::Decay, 0.5f);
                if(b % 20 == 12) { if(rnd() > 0.15f) note = (float)(36 + (int)(48 * rnd())); e.SetF0(440.f * std::exp2((note - 69.f) / 12.f)); }
                e.HoldPitch(b % 20 >= 12);   /* the strike waiting from the pitch change on, as the module holds it */
                if(staged && b % 20 >= 13 && b % 20 < 19) { e.PlanStrike(); e.ServeStrikePlan(); }
                if(stale && b % 20 == 19) e.SetF0(440.f * std::exp2((note + 2.f - 69.f) / 12.f));   /* the pitch moves after the plan */
                if(b % 20 == 19) e.Strike(0.3f + 0.7f * rnd());
                RunOn(e, y, 1);
            }
            taken = e.PlansTaken();
            return y;
        };
        uint32_t t0, t1, t2, t3, t4;
        const auto inl = play(piano, false, false, t0), stg = play(piano, true, false, t1);
        float d = 0.f; for(size_t i = 0; i < inl.size(); i++) d = std::fmax(d, std::fabs(inl[i] - stg[i]));
        CHECK(d == 0.f, "a staged strike differs from one built inline by %.3g", d);
        CHECK(t1 >= 100, "only %u of 150 strikes took the staged voice", t1);
        const auto inl2 = play(piano, false, true, t2), stl = play(piano, true, true, t3);
        float d2 = 0.f; for(size_t i = 0; i < inl2.size(); i++) d2 = std::fmax(d2, std::fabs(inl2[i] - stl[i]));
        CHECK(d2 == 0.f && t3 == 0, "a stale plan: %u taken, output off by %.3g", t3, d2);
        uint32_t tp; const auto tin = play(rw, false, false, t4), tst = play(rw, true, false, tp);
        float d3 = 0.f; for(size_t i = 0; i < tin.size(); i++) d3 = std::fmax(d3, std::fabs(tin[i] - tst[i]));
        CHECK(d3 == 0.f && tp >= 100, "a pickup world (the tine): %u plans taken, output off by %.3g", tp, d3);
        /* a request posted, then the world changed before the control loop
           served it: nothing is built from the old world */
        {
            Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4);
            static StrikePlan plan; e.SetStrikePlan(&plan);
            std::vector<float> z;
            e.SetF0(196.f); e.HoldPitch(true); e.PlanStrike();
            e.SetWorld(&rw);
            const bool built = e.ServeStrikePlan();
            CHECK(!built && plan.state != 2u, "a request from before the world changed was built (state %u)", plan.state);
        }
        printf("  the staged strike: %u of 150 strikes took a voice built before them, %u on a pickup world, bit for bit the inline build; stale plans build inline\n", t1, tp);
    }

    /* 22i. the load governor (Engine::SetLoad): four voices of the Piano
       stolen at a 1 s release, so tails ring; a block reported at 0.9 of the
       budget brings every tail to its end within 2 ms, with no step — the
       output's jump across the hurry no bigger than the ring's own — and a
       block at 0.8 leaves them alone */
    {
        auto setup = [&](Engine& e, std::vector<float>& y) {
            e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4); e.SetReleaseMs(1000.f);
            const float notes[8] = {48, 55, 60, 64, 50, 57, 62, 66};
            for(int i = 0; i < 8; i++) { e.SetF0(440.f * std::exp2((notes[i] - 69.f) / 12.f)); e.Strike(0.9f); RunOn(e, y, 20); }
        };
        auto tails = [&](const Engine& e) { int t = 0; for(int v = 0; v < 4; v++) { const ResonatorBank& b = e.VoiceAt(v).bank; if(b.tn > 0 && b.tail_left > 0) t += b.tn; } return t; };
        Engine a; std::vector<float> ya; setup(a, ya);
        const int before = tails(a);
        const float ring = std::fabs(ya[ya.size() - 1] - ya[ya.size() - 2]);
        a.SetLoad(0.8f); RunOn(a, ya, 1);
        const int calm = tails(a);
        const size_t at = ya.size();
        a.SetLoad(0.9f); RunOn(a, ya, 5);
        const int after = tails(a);
        float jump = 0.f; for(size_t i = at; i < ya.size(); i++) jump = std::fmax(jump, std::fabs(ya[i] - ya[i - 1]));
        float ringmax = 0.f; for(size_t i = at - 480; i < at; i++) ringmax = std::fmax(ringmax, std::fabs(ya[i] - ya[i - 1]));
        CHECK(before > 0 && calm == before, "tails before the load %d, after a block at 0.8 %d (should be untouched)", before, calm);
        CHECK(after == 0, "a block at 0.9 of the budget left %d tail modes ringing 2.5 ms later", after);
        CHECK(jump <= 1.5f * ringmax + 1e-6f, "hurrying the tails stepped %.3g where the ring moves %.3g a sample", jump, ringmax);
        printf("  the load governor: %d tail modes ended within 2 ms at 0.9 of the budget (none at 0.8), no step (%.3g against the ring's %.3g)\n", before, jump, ringmax);
        (void)ring;
    }

    /* 22h. a strike whose pitch never settled (audio-rate v/oct: the module
       waited its 30 ms) strikes at the staged voice's note (Strike, at_plan)
       and takes it — the pitch a millisecond ago is as much the note as the
       one now, and it is built. The pitch random every block, a plan served
       every other block, a strike every 60: every strike after the first few
       takes the plan, at the plan's note. A settled strike (at_plan false)
       takes the pitch as it stands, which here matches no plan */
    {
        auto go = [&](bool at_plan, int& at_note) {
            Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4);
            static StrikePlan plan; e.SetStrikePlan(&plan);
            std::vector<float> y;
            uint32_t rs = 5; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.f; };
            at_note = 0;
            for(int b = 0; b < 3000; b++)
            {
                e.SetF0(440.f * std::exp2((36.f + 48.f * rnd() - 69.f) / 12.f));
                const bool strike = b % 60 == 59;
                e.HoldPitch(!strike);
                if(!strike) { e.PlanStrike(); if(b % 2) e.ServeStrikePlan(); }
                if(strike)
                {
                    const float want = plan.state == 2u ? plan.key.param : -1.f;
                    const uint32_t before = e.PlansTaken();
                    e.Strike(0.8f, at_plan);
                    if(e.PlansTaken() > before && std::fabs(e.Voice().param - want) < 1e-4f) at_note++;
                }
                Run(e, y, 1);
            }
            return e.PlansTaken();
        };
        int n1 = 0, n0 = 0;
        const uint32_t with = go(true, n1), without = go(false, n0);
        CHECK(with >= 40 && (int)with == n1, "unsettled strikes at audio-rate v/oct: %u of 50 took the plan, %d at its note", with, n1);
        CHECK(without <= 3, "settled strikes at audio-rate v/oct took %u plans: the pitch as it stands matches none", without);
        printf("  audio-rate v/oct: %u of 50 unsettled strikes took the voice built before them, at its note (settled, %u)\n", with, without);
    }

    /* 22j. the voices are lent (ResonatorVoices, LendVoices): the stereo
       pair's left engine plays the resonator with four and its right holds
       none — some 23 KB of AXI SRAM the right never used — and a strike on the
       right does nothing. A copied Engine plays its own voices: a strike on
       the copy leaves the original's voices as they were */
    {
        StereoEngine se; se.Init(&piano, sr);
        CHECK(se.L.Voices() == 4 && se.R.Voices() == 0, "the stereo pair lends L %d voices and R %d (4 and 0)", se.L.Voices(), se.R.Voices());
        se.R.SetF0(261.63f); se.R.Strike(0.9f); se.R.SetPolyphony(4);
        CHECK(!se.R.Voice().Active() && se.R.Voices() == 0, "a strike on the right engine sounded");
        Engine a; a.Init(&piano, sr); a.gain = 1.f; a.SetF0(130.81f); a.Strike(0.8f);
        std::vector<float> ya; Run(a, ya, 4);
        Engine b = a;
        const float before = a.Voice().bank.y1[0];
        b.SetF0(196.f); b.Strike(0.9f); std::vector<float> yb; Run(b, yb, 20);
        CHECK(a.Voice().bank.y1[0] == before && &a.Voice() != &b.Voice(), "a copied Engine played the original's voices");
        printf("  the voices lent: the stereo pair's L four, R none; a copied Engine plays its own\n");
    }

    /* 22c. the position a resonate world reports as heard (Position(), which
       telemetry sends as posL and the page's model sliders follow) moves with
       the axes. The skipped render was the only thing that folded it, so it
       sat where the world was loaded and the sliders never moved under the
       pots (Combust: "the decay and brightness etc in the web client aren't
       changing -still-") */
    {
        Engine e; e.Init(&piano, sr); e.gain = 1.f;
        std::vector<float> y(24);
        float worst = 0.f;
        const float at[3][4] = { {0.5f, 0.5f, 0.2f, 0.8f}, {0.3f, 0.9f, 0.95f, 0.1f}, {0.7f, 0.1f, 0.5f, 0.5f} };
        for(int s = 0; s < 3; s++)
        {
            e.SetPosition(at[s], 4);
            for(int b = 0; b < 4; b++) e.Process(y.data(), 24);
            for(int a = 0; a < 4; a++) worst = std::fmax(worst, std::fabs(e.Position()[a] - at[s][a]));
        }
        CHECK(worst < 1e-6f, "a resonate world's reported position is %.3f off its axes: the page's sliders would not follow the pots", worst);
        printf("  a resonate world reports the position it plays (within %.1g): the model sliders follow the pots\n", worst);
    }

    /* 22b. the tail is held to the voice's share. It kept the tail before it
       and took the new ring beside it up to all 48 modes, so four voices under
       overlapping strikes at a long release rang 192 tail modes over their own
       48 — five times the block Rings' rule promises, and the overruns heard
       only on strikes, only overlapping (Combust). (a) The engine: four
       voices, a 1 s release, a note every 25 ms for two seconds: no voice's
       tail ever holds more than its twelve. (b) The share keeps the loudest,
       wherever they came from: a loud note, then a quiet one stolen onto the
       same bank; capped at six the tail keeps nine tenths of what the whole
       twelve would ring — the six new modes first, as the tail used to take
       them, keep a hundredth */
    {
        Engine e; e.Init(&piano, sr); e.gain = 1.f; e.SetPolyphony(4); e.SetReleaseMs(1000.f);
        const float notes[] = {48, 55, 60, 64, 67, 72, 52, 59, 62, 65, 69, 74, 50, 57, 63, 70};
        std::vector<float> y(24);
        int most = 0;
        for(int blk = 0; blk < 4000; blk++)
        {
            if(blk % 50 == 0) { e.SetF0(440.f * std::exp2((notes[(blk / 50) % 16] - 69.f) / 12.f)); e.Strike(0.8f); }
            e.Process(y.data(), 24);
            for(int v = 0; v < 4; v++) { const ResonatorBank& b = e.VoiceAt(v).bank; if(b.tail_left > 0 && b.tn > most) most = b.tn; }
        }
        const int share = ResonatorBank::kMax / 4;
        CHECK(most > 0 && most <= share, "overlapping strikes at four voices: a tail of %d modes, the share is %d", most, share);

        auto note = [&](ResonatorBank& b, float f0, float lv) {
            float hz[6], z[6], g[6], ph[6];
            for(int k = 0; k < 6; k++) { hz[k] = f0 * (k + 1); z[k] = 0.0005f; g[k] = lv / (k + 1); ph[k] = 0.3f * k; }
            b.Set(hz, z, g, 6, sr, ph); b.Strike(1.f);
        };
        auto tail_energy = [&](int lim) {
            ResonatorBank b; b.Init();
            std::vector<float> y2(2400);
            note(b, 130.81f, 1.f); for(int i = 0; i < 2400; i += 24) b.Process(y2.data() + i, 24);
            b.Release(1000.f, sr);                                   /* the loud note to the tail */
            note(b, 196.f, 0.01f); for(int i = 0; i < 480; i += 24) b.Process(y2.data() + i, 24);
            b.Release(1000.f, sr, lim);                              /* the quiet one stolen: both compete */
            double en = 0.0;
            for(int i = 0; i < 2400; i += 24) { b.Process(y2.data() + i, 24); for(int k = 0; k < 24; k++) en += (double)y2[i + k] * y2[i + k]; }
            return std::make_pair(en, b.tn);
        };
        /* (c) the selection both use (TopScores, a quickselect) takes what
           picking the highest lim times over took, the first of equals each
           time: 20 000 draws of up to 96 scores from eight values, so ties
           at the cut are the rule, some under the floor */
        {
            uint32_t rs = 12345u; int bad = 0;
            auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return rs >> 8; };
            for(int t = 0; t < 20000 && !bad; t++)
            {
                const int m = 1 + (int)(rnd() % 96), lim = 1 + (int)(rnd() % 48);
                float sc[96]; for(int k = 0; k < m; k++) sc[k] = (float)(int)(rnd() % 8) - 1.f;   /* -1 is under the floor */
                int got[96]; const int ng = TopScores(sc, m, lim, got, -0.5f);
                bool take[96] = {false}; int nw = 0, want[96];
                for(int c = 0; c < lim; c++)
                {
                    int best = -1;
                    for(int k = 0; k < m; k++) if(!take[k] && (best < 0 || sc[k] > sc[best])) best = k;
                    if(best < 0 || sc[best] <= -0.5f) break;
                    take[best] = true;
                }
                for(int k = 0; k < m; k++) if(take[k]) want[nw++] = k;
                if(ng != nw) bad = 1; else for(int k = 0; k < nw; k++) if(got[k] != want[k]) bad = 1;
            }
            CHECK(!bad, "TopScores took another set than the greedy pick");
        }
        const auto whole = tail_energy(ResonatorBank::kMax), capped = tail_energy(6);
        CHECK(whole.second == 12 && capped.second == 6, "tails of %d and %d modes, not 12 and 6", whole.second, capped.second);
        CHECK(capped.first > 0.9 * whole.first, "the capped tail rings %.3g of the whole one's energy: it kept the quiet modes", capped.first / whole.first);
        printf("  the tail at four voices under overlapping strikes holds %d modes at most (the share %d); capped at six it keeps %.3f of the whole tail's ring\n", most, share, capped.first / whole.first);
    }

    /* 23. a resonator heard from two points (the fun that synthesis buys: a
       stereo image that is the string's own, and spins). (a) the bank: with
       complementary weights, left plus right is the mono output exactly.
       (b) the engine: no spread is both ears the one output; full spread is
       two different ears whose sum is within 3 dB of the mono sound. (c) an
       orbit on the stereo plane moves the points: the ears change with it */
    {
        auto six = [&](ResonatorBank& b) {
            float hz[6], z[6], g[6], ph[6];
            for(int k = 0; k < 6; k++) { hz[k] = 220.f * (k + 1); z[k] = 0.001f; g[k] = 0.3f / (k + 1); ph[k] = 0.2f * k; }
            b.Set(hz, z, g, 6, sr, ph); b.Strike(1.f);
        };
        ResonatorBank a, m; a.Init(); m.Init(); six(a); six(m);
        const float wl[6] = {1, 0, 1, 0, 0.25f, 0.5f}, wr[6] = {0, 1, 0, 1, 0.75f, 0.5f};
        std::vector<float> L(9600), R(9600), M(9600);
        for(int i = 0; i < 9600; i += 24) { a.ProcessLR(L.data() + i, R.data() + i, 24, wl, wr); m.Process(M.data() + i, 24); }
        float dsum = 0.f, dlr = 0.f, pk = 0.f;
        for(int i = 0; i < 9600; i++) { dsum = std::fmax(dsum, std::fabs(L[i] + R[i] - M[i])); dlr = std::fmax(dlr, std::fabs(L[i] - R[i])); pk = std::fmax(pk, std::fabs(M[i])); }
        CHECK(dsum < 1e-4f * pk && dlr > 0.1f * pk, "two ears on the bank: L+R differs from mono by %.3g, L from R by %.3g (peak %.3g)", dsum, dlr, pk);

        auto ears = [&](float spread, float rate, std::vector<float>& l, std::vector<float>& r) {
            StereoEngine se; se.Init(&piano, sr); se.L.gain = 1.f;
            se.spread = spread; se.spread_plane = 0; if(rate != 0.f) se.rot.SetRate(0, rate);
            se.SetF0(261.63f); se.Strike(0.8f);
            l.assign(24000, 0.f); r.assign(24000, 0.f);
            for(int i = 0; i < 24000; i += 24) se.Process(l.data() + i, r.data() + i, 24);
        };
        std::vector<float> l0, r0, l1, r1, l2, r2;
        ears(0.f, 0.f, l0, r0); ears(0.1f, 0.f, l1, r1); ears(0.1f, 2.f, l2, r2);
        double same = 0, e0 = 0, e1 = 0, dif = 0, spin = 0, el = 0;
        for(int i = 0; i < 24000; i++)
        {
            same = std::fmax(same, std::fabs(l0[i] - r0[i]));
            e0 += (double)l0[i] * l0[i]; e1 += 0.25 * (double)(l1[i] + r1[i]) * (l1[i] + r1[i]);
            dif += (double)(l1[i] - r1[i]) * (l1[i] - r1[i]); el += (double)l1[i] * l1[i];
            if(i > 12000) spin += (double)(l2[i] - l1[i]) * (l2[i] - l1[i]);
        }
        const double sumdb = 10 * std::log10(e1 / e0), lrdb = 10 * std::log10(dif / el);
        CHECK(same == 0.0, "no spread and the ears differ by %.3g", same);
        CHECK(std::fabs(sumdb) < 3.0 && lrdb > -30.0, "full spread: the sum %+.1f dB from mono, the ears %.1f dB apart (of the left)", sumdb, lrdb);
        CHECK(spin > 1e-3 * el, "an orbit on the stereo plane did not move the ears: %.3g of the left's energy", spin / el);
        printf("  two ears: L+R is mono on the bank; no spread is one output, full spread two ears (sum %+.1f dB, apart %.1f dB); an orbit spins them\n", sumdb, lrdb);
    }

    /* 24. a family's body axis as a morph (SetMemberMorph): between two
       members of the same kind the voice is both, paired and blended, and a
       sweep of the axis glides where the switch stepped. The fixture
       Wurlitzer and a copy of it 12 dB down (every point's level x 1/4):
       swept 0..1, a strike at each place, morph on — no step a fifth of the
       switch's — and off — one step, the whole difference. At either end the morph is the
       member itself. Two members of different kinds (the Wurlitzer and the
       pickup EP) switch as they did */
    {
        auto family = [&](const std::vector<uint8_t>& m0, const std::vector<uint8_t>& m1) {
            std::vector<uint8_t> fam;
            auto u8 = [&](uint8_t v) { fam.push_back(v); };
            auto u16 = [&](uint16_t v) { u8(v & 255); u8(v >> 8); };
            auto u32 = [&](uint32_t v) { u16(v & 65535); u16(v >> 16); };
            auto f32 = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); };
            const std::vector<uint8_t>* mem[2] = { &m0, &m1 };
            uint16_t Nmax = 0; for(auto* b : mem) { uint16_t nn; std::memcpy(&nn, b->data() + 6, 2); Nmax = std::max(Nmax, nn); }
            fam.insert(fam.end(), {'K', 'Y', 'K', 'M'}); u16(6); u16(Nmax); u16(0); u8(0); u8(2); f32(0.f); f32(1.f);
            u8(2);
            uint32_t off = (uint32_t)(20 + 1 + 24 * 2); off = (off + 3) & ~3u;
            std::vector<uint8_t> body;
            for(int i = 0; i < 2; i++)
            {
                u32(off + (uint32_t)body.size()); u32((uint32_t)mem[i]->size());
                char nm[16] = {0}; std::snprintf(nm, 16, "m%d", i); for(char c : nm) u8((uint8_t)c);
                body.insert(body.end(), mem[i]->begin(), mem[i]->end());
                while(body.size() & 3) body.push_back(0);
            }
            while(fam.size() < off) fam.push_back(0);
            fam.insert(fam.end(), body.begin(), body.end());
            return fam;
        };
        /* the quieter copy: every point's level (the stage's eighth float) x 1/4 */
        std::vector<uint8_t> dim(wblob);
        {
            const uint8_t* bl = dim.data();
            auto g16 = [&](size_t at) { uint16_t v; std::memcpy(&v, bl + at, 2); return v; };
            const uint16_t ver = g16(4), N = g16(6), P = g16(8);
            const size_t fixed = 4 + 32 + 5u * N, bh = ver >= 6 ? 12 : 10;
            size_t q = 20 + (ver >= 7 ? 32 : 0);
            for(int i = 0; i < P; i++)
            {
                float lv; std::memcpy(&lv, &dim[q + 4 + 28], 4); lv *= 0.25f; std::memcpy(&dim[q + 4 + 28], &lv, 4);
                q += fixed; q += 1 + 8u * dim[q];
                const uint16_t nb = g16(q); q += 2;
                for(uint16_t k = 0; k < nb; k++) q += bh + 2u * g16(q + 8);
            }
        }
        std::vector<uint8_t> fam = family(wblob, dim), mixed = family(wblob, blob);
        World f; f.UseResonate(fam.data(), (uint32_t)fam.size());
        World fm; fm.UseResonate(mixed.data(), (uint32_t)mixed.size());
        auto heard = [&](World& w, float c, bool morph, std::vector<float>* keep = nullptr) {
            static ResonatorVoice scv[2]; static ResonatorWorld scw[2];
            Engine e; e.Init(&w, sr); e.gain = 1.f; e.SetMemberMorph(morph); e.SetMorphScratch(scv, scw);
            float pos[kMaxN]; for(int k = 0; k < kMaxN; k++) pos[k] = 0.5f; pos[0] = c;
            e.SetPosition(pos, kMaxN); e.SetF0(261.63f); e.Strike(0.8f);
            std::vector<float> y; Run(e, y, 300);
            if(keep) *keep = y;
            double en = 0; for(size_t i = 2400; i < y.size(); i++) en += (double)y[i] * y[i];
            return 10 * std::log10(en + 1e-30);
        };
        double on_step = 0, off_step = 0, on_prev = 0, off_prev = 0, on0 = 0, on1 = 0;
        for(int k = 0; k <= 20; k++)
        {
            const float c = k / 20.f;
            const double a = heard(f, c, true), b = heard(f, c, false);
            if(k) { on_step = std::fmax(on_step, std::fabs(a - on_prev)); off_step = std::fmax(off_step, std::fabs(b - off_prev)); }
            if(k == 0) on0 = a;
            if(k == 20) on1 = a;
            on_prev = a; off_prev = b;
        }
        std::vector<float> ya, yb;
        heard(f, 0.f, true, &ya); heard(f, 0.f, false, &yb);
        const bool ends = ya == yb;
        std::vector<float> ma, mb;
        heard(fm, 0.5f, true, &ma); heard(fm, 0.5f, false, &mb);
        /* the attack carries its own level and is not scaled with the
           modes, so the two ends are 7-8 dB apart, not 12 */
        CHECK(on_step < 0.2 * off_step && std::fabs(on0 - on1) > 5.0, "the morph: largest step %.1f dB (the switch %.1f), %.1f dB end to end", on_step, off_step, on0 - on1);
        CHECK(off_step > 5.0, "the switch (morph off) stepped only %.1f dB: the check proves nothing", off_step);
        CHECK(ends, "at the end of the axis the morph is not the member itself");
        CHECK(ma == mb, "two members of different kinds did not switch as they did");
        printf("  morph: a sweep across two members steps %.1f dB at most (%.1f end to end) where the switch stepped %.1f; the ends are the members\n", on_step, on0 - on1, off_step);
    }

    printf(fails ? "resonate_engine_check: %d FAILED\n" : "resonate_engine_check: ok\n", fails);
    return fails ? 1 : 0;
}
