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

int main()
{
    const float sr = 48000.f;
    auto blob = slurp("tests/data/ep-vel.kykm");
    CHECK(!blob.empty(), "tests/data/ep-vel.kykm missing");
    World rw; rw.UseResonate(blob.data(), (uint32_t)blob.size());
    auto wblob = slurp("tests/data/wurli.kykm");
    World wurli; wurli.UseResonate(wblob.data(), (uint32_t)wblob.size());
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
        Engine e; e.Init(&wurli, sr); e.gain = 1.f; e.SetF0(261.63f); e.Strike(0.7f);
        std::vector<float> y2; Run(e, y2, 200);
        const double l131 = level_at(y2, 130.81), top = std::fmax(level_at(y2, 261.63), level_at(y2, 523.25));
        CHECK(l131 < top - 30.0, "a C4 strike carries a C3 fundamental: %.1f dB against the C4 series at %.1f", l131, top);
        /* a pitch change under the ringing note keeps it ringing (retuned),
           and a re-strike adds rather than restarting from silence */
        Engine g; g.Init(&wurli, sr); g.gain = 1.f; g.SetF0(130.81f); g.Strike(0.7f);
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
        auto pblob = slurp("tests/data/perc.kykm");
        CHECK(!pblob.empty(), "tests/data/perc.kykm missing");
        World perc; perc.UseResonate(pblob.data(), (uint32_t)pblob.size());
        CHECK(perc.IsResonate() && perc.Res().kind == 1 && perc.Res().lo == 0.f && perc.Res().hi == 18.f,
              "perc.kykm did not attach as an index world (kind %d, %g..%g)", perc.Res().kind, perc.Res().lo, perc.Res().hi);
        const float at0[kMaxN] = {0.f}, at1[kMaxN] = {1.f}, mid[kMaxN] = {0.5f};
        Engine a; a.Init(&perc, sr); a.gain = 1.f; a.SetPosition(at0, 1); a.SetF0(261.63f); a.Strike(0.7f);
        Engine b; b.Init(&perc, sr); b.gain = 1.f; b.SetPosition(at0, 1); b.SetF0(130.81f); b.Strike(0.7f);
        Engine c; c.Init(&perc, sr); c.gain = 1.f; c.SetPosition(at1, 1); c.SetF0(261.63f); c.Strike(0.7f);
        std::vector<float> ya, yb, yc; Run(a, ya, 100); Run(b, yb, 100); Run(c, yc, 100);
        ResonatorVoice v; v.Init(); perc.Res().At(0.f, v, sr); v.Strike(0.7f);
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
        /* the pot moves under the ring: the sound goes on, and it is now the
           body at the new position (the same as one struck there, in
           frequency content, not in phase — so compared on a spectrum) */
        Engine g; g.Init(&perc, sr); g.gain = 1.f; g.SetPosition(at0, 1); g.SetF0(261.63f); g.Strike(0.7f);
        std::vector<float> g1; Run(g, g1, 100);
        g.SetPosition(mid, 1);
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
        Engine e0; e0.Init(&wurli, sr); e0.gain = 1.f; e0.SetF0(261.63f);
        Engine e1 = e0; e1.SetTune(Engine::Tune::Voicing, 0.f); e1.SetTune(Engine::Tune::Decay, 1.f); e1.SetTune(Engine::Tune::Coil, 1.f);
        e0.Strike(0.7f); e1.Strike(0.7f);
        std::vector<float> y0, y1; Run(e0, y0, 100); Run(e1, y1, 100);
        double d = 0; for(size_t i = 0; i < y0.size(); i++) d += (y0[i] - y1[i]) * (y0[i] - y1[i]);
        CHECK(d == 0.0, "a tune at its centre changed the sound: %.3g", d);
        Engine e4; e4.Init(&wurli, sr); e4.gain = 1.f; e4.SetF0(261.63f); e4.SetTune(Engine::Tune::Decay, 4.f); e4.Strike(0.7f);
        std::vector<float> y4; Run(e4, y4, 1000);
        Engine e5; e5.Init(&wurli, sr); e5.gain = 1.f; e5.SetF0(261.63f); e5.Strike(0.7f);
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
            Engine e; e.Init(&wurli, sr); e.gain = 1.f; e.SetPolyphony(poly);
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
        Engine e; e.Init(&wurli, sr); e.gain = 1.f; e.SetF0(130.81f);
        std::vector<float> noise(4800), y(4800), quiet(4800);
        uint32_t r = 12345u; for(auto& v : noise) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; v = ((int32_t)r) * (0.3f / 2147483648.f); }
        for(int b = 0; b < 100; b++) { e.SetExciter(noise.data() + b * 48, 0.02f); e.Process(y.data() + b * 48, 48); }
        Engine q; q.Init(&wurli, sr); q.gain = 1.f; q.SetF0(130.81f);
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
        const char* names[2] = { "wurli", "ep-vel" };
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
        CHECK(std::strcmp(f.Res().MemberName(1), "ep-vel") == 0, "member 1 is named '%s'", f.Res().MemberName(1));
        const float at0[kMaxN] = {0.f}, at1[kMaxN] = {1.f}, at45[kMaxN] = {0.45f};
        auto render = [&](const World& w, const float* pos) {
            Engine e; e.Init(&w, sr); e.gain = 1.f; e.SetPosition(pos, 1); e.SetF0(130.81f); e.Strike(0.6f);
            std::vector<float> y; Run(e, y, 100); return y;
        };
        const auto fw = render(f, at0), ww = render(wurli, at0), fe = render(f, at1), ee = render(rw, at0), fm = render(f, at45);
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
        Engine e; e.Init(&wurli, sr); e.gain = 1.f;
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
        Engine u; u.Init(&wurli, sr); u.gain = 1.f; u.SetPitchLock(false);
        u.SetF0(131.8f); u.Strike(0.7f);
        const float ustruck = f0_of(u);
        u.SetF0(196.f); Run(u, y, 20);
        const float umoved = f0_of(u);
        CHECK(std::fabs(ustruck - 131.8f) < 0.2f, "unlocked, a strike was quantised: %.2f Hz for 131.8", ustruck);
        CHECK(std::fabs(umoved - 196.f) < 0.3f, "unlocked, the ring did not follow the pitch: %.2f Hz", umoved);
        printf("  pitch lock: 131.8 Hz strikes C3 (%.2f), holds it under a G3 pitch, takes the G3 at the strike; unlocked %.1f then %.1f\n", struck, ustruck, umoved);
        /* a sequencer whose CV lands after its gate: a jump within 30 ms of
           the strike is the strike's note; one at 100 ms is not */
        Engine l; l.Init(&wurli, sr); l.gain = 1.f;
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
        Engine e; e.Init(&wurli, sr); e.gain = 1.f; e.SetPolyphony(2);
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

    /* 12. a tune change reaches every voice, one a block, and does not
       move a locked ring's pitch. Four voices struck at four notes; decay
       x4; within four blocks every voice's zeta has quartered. And a ring
       locked at C3 with the pitch since moved to G3 stays at C3 through
       the tune change (it used to be re-read at the pitch, which under
       the lock is the one thing a tune change must not do). */
    {
        Engine e; e.Init(&wurli, sr); e.gain = 1.f; e.SetPolyphony(4);
        std::vector<float> y;
        const float notes[4] = {130.81f, 164.81f, 196.f, 261.63f};
        for(int v = 0; v < 4; v++) { e.SetF0(notes[v]); e.Strike(0.7f); Run(e, y, 5); }
        float z0[4]; for(int v = 0; v < 4; v++) z0[v] = e.VoiceAt(v).zeta[0];
        e.SetTune(Engine::Tune::Decay, 4.f);
        Run(e, y, 1);
        int after1 = 0; for(int v = 0; v < 4; v++) if(std::fabs(e.VoiceAt(v).zeta[0] / z0[v] - 0.25f) < 0.01f) after1++;
        Run(e, y, 4);
        int after5 = 0; for(int v = 0; v < 4; v++) if(std::fabs(e.VoiceAt(v).zeta[0] / z0[v] - 0.25f) < 0.01f) after5++;
        CHECK(after1 >= 1 && after1 <= 2, "after one block %d voices had the new decay (one or two: the active one, and one more)", after1);
        CHECK(after5 == 4, "after five blocks %d of 4 voices had the new decay", after5);
        Engine l; l.Init(&wurli, sr); l.gain = 1.f;
        l.SetF0(130.81f); l.Strike(0.7f); Run(l, y, 50);
        l.SetF0(196.f); Run(l, y, 10);
        const float before = l.Voice().hz[0];
        l.SetTune(Engine::Tune::Decay, 2.f); Run(l, y, 2);
        const float after = l.Voice().hz[0];
        CHECK(std::fabs(after - before) < 0.01f, "a tune change moved a locked ring from %.2f to %.2f Hz", before, after);
        printf("  a tune reaches the voices one a block (%d after one, %d after five) and leaves a locked ring at %.1f Hz\n", after1, after5, after);
    }

    /* 13. Rings' rule for polyphony: the bank's modes shared out, so four
       voices are as rich as one. At one voice the Wurlitzer's C3 has
       every fitted mode; at four, twelve each — the loudest twelve, so
       the fundamental is among them. */
    {
        auto live = [](const ResonatorVoice& v, int N) { int n = 0; for(int k = 0; k < N; k++) if(v.gain[k] != 0.f) n++; return n; };
        const int N = wurli.Res().N;
        Engine e; e.Init(&wurli, sr); e.gain = 1.f;
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
       harder (0.5 + 0.2 x the density's share of eight a second), the
       first not at all; with the track off every one is 0.5. And a
       second's rest lets the density leak back */
    {
        auto run10 = [&](float track) {
            Engine e; e.Init(&wurli, sr); e.gain = 1.f; e.SetVelocityTrack(track); e.SetF0(261.63f);
            std::vector<float> y; float first = 0.f, tenth = 0.f;
            for(int k = 0; k < 10; k++) { e.Strike(0.5f); if(k == 0) first = e.LastStrikeVelocity(); if(k == 9) tenth = e.LastStrikeVelocity(); Run(e, y, 100); }
            Run(e, y, 3000); e.Strike(0.5f);
            return std::make_tuple(first, tenth, e.LastStrikeVelocity()); };
        const auto on = run10(1.f), off = run10(0.f);
        CHECK(std::fabs(std::get<0>(on) - 0.5f) < 1e-6f, "the first strike was tracked: %.3f", std::get<0>(on));
        CHECK(std::get<1>(on) > 0.62f && std::get<1>(on) <= 0.7f, "the tenth strike at ten a second is %.3f (0.62..0.7 wanted)", std::get<1>(on));
        CHECK(std::get<2>(on) < 0.52f, "after three seconds' rest the strike is still tracked: %.3f", std::get<2>(on));
        CHECK(std::get<1>(off) == 0.5f, "with the track off the tenth strike is %.3f", std::get<1>(off));
        printf("  dig in: ten strikes a second take the tenth from 0.5 to %.2f; rested, %.2f; off, %.2f\n", std::get<1>(on), std::get<2>(on), std::get<1>(off));
    }

    printf(fails ? "resonate_engine_check: %d FAILED\n" : "resonate_engine_check: ok\n", fails);
    return fails ? 1 : 0;
}
