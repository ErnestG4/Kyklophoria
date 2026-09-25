/* worldaudit — where a world's recorded points disagree with each other, read
 * through the runtime the module plays (runtime/kyk_resonate.h).
 *
 *   worldaudit world.kykm [--tsv out.tsv] [--walk]
 *
 * For every point of a note world (every member of a family):
 *
 *   tune   the loudest partial within a whole tone of the labelled
 *          fundamental, in cents off the label. "weak" where that partial is
 *          more than 20 dB under the point's loudest (a piano's bottom octave
 *          barely has a fundamental, and the nearest partial then is not it).
 *   shape  the ring's energy at 150 ms in bands of HARMONIC NUMBER — h 1, 2,
 *          3, 4-5, 6-7, 8-10, 11-14, 15-20, 21-28, 29-40 — as dB, so walking
 *          up the keyboard moves no partial across a band edge (bands in Hz
 *          put a 20 dB "jump" wherever a fundamental crossed one).
 *   level  the ring's total energy at 150 ms, dB.
 *   ring   the energy-weighted mean T60 of the modes, seconds.
 *
 * and against its neighbours: what the points either side predict for it,
 * interpolated at its note — shape (RMS dB with the mean removed, bands
 * within 40 dB), level (dB) and ring (log2 ratio). A point far from its
 * neighbours is either a real instrument's quirk or a fit gone wrong, and the
 * refit list is these, the worst first. The ends of the range have one
 * neighbour and are scored against it alone.
 *
 * --walk adds every semitone as played (between points the runtime blends
 * the two either side) and the step from each to the next, in the same terms.
 *
 * Combust, 2026-09-23: "a few notes are off and the jumps are bigger near
 * them ... We can do better with fits across the board". */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "../../runtime/kyk_resonate.h"

using namespace kyk;

static constexpr int kBands = 10;
static const float kEdge[kBands + 1] = {0.75f, 1.5f, 2.5f, 3.5f, 5.5f, 7.5f, 10.5f, 14.5f, 20.5f, 28.5f, 40.5f};

struct Prof { float note; float band[kBands]; float level; float ring; float cents; bool weak; float t60; };

static Prof Measure(const ResonatorWorld& R, float note)
{
    Prof p{}; p.note = note;
    ResonatorVoice v; v.Init(); R.At(note, v, 48000.f);
    const double f0 = 440.0 * std::pow(2.0, (note - 69.0) / 12.0), t = 0.15;
    double e[kBands] = {0}, tot = 0, lt = 0, wt = 0, emax = 0, efund = 0, hfund = 0;
    for(int k = 0; k < ResonatorBank::kMax; k++)
    {
        if(v.gain[k] == 0.f || v.hz[k] <= 0.f) continue;
        const double w = 2 * M_PI * v.hz[k];
        const double a = std::fabs(v.gain[k]) * std::exp(-v.zeta[k] * w * t), en = a * a;
        const double h = v.hz[k] / f0;
        tot += en; if(en > emax) emax = en;
        const double t60 = v.zeta[k] > 0.f ? 6.91 / (v.zeta[k] * w) : 60.0;
        lt += en * std::log2(t60); wt += en;
        for(int b = 0; b < kBands; b++) if(h >= kEdge[b] && h < kEdge[b + 1]) { e[b] += en; break; }
        if(h > 0.89 && h < 1.12 && en > efund) { efund = en; hfund = h; }
    }
    /* and how the whole ring falls: its energy (every mode, no phases) at
       150 and 750 ms, as a T60 — the number a recording's own decay can be
       held against (worldaudit's ring is a mean over modes, and a loud
       short mode beside a long one reads as a short ring) */
    {
        double e1 = 0, e2 = 0;
        for(int k = 0; k < ResonatorBank::kMax; k++)
        {
            if(v.gain[k] == 0.f || v.hz[k] <= 0.f) continue;
            const double w = 2 * M_PI * v.hz[k], g2 = (double)v.gain[k] * v.gain[k];
            e1 += g2 * std::exp(-2 * v.zeta[k] * w * 0.15); e2 += g2 * std::exp(-2 * v.zeta[k] * w * 0.75);
        }
        const double drop = 10 * std::log10((e1 + 1e-30) / (e2 + 1e-30));
        p.t60 = drop > 0.1 ? (float)(60.0 * 0.6 / drop) : 99.f;
    }
    for(int b = 0; b < kBands; b++) p.band[b] = (float)(10 * std::log10(e[b] + 1e-30));
    p.level = (float)(10 * std::log10(tot + 1e-30));
    p.ring = wt > 0 ? (float)std::exp2(lt / wt) : 0.f;
    p.weak = hfund == 0 || efund < emax * 0.01;
    p.cents = hfund > 0 ? (float)(1200 * std::log2(hfund)) : 0.f;
    return p;
}

/* how far b is from a: spectral shape (RMS dB, mean removed, bands within
   40 dB of the louder, floored there), level dB, ring log2 */
static void Compare(const Prof& a, const Prof& b, float& shape, float& level, float& ring)
{
    float mx = -1e30f; for(int k = 0; k < kBands; k++) mx = std::max(mx, std::max(a.band[k], b.band[k]));
    float d[kBands]; int n = 0; float m = 0.f;
    for(int k = 0; k < kBands; k++) if(std::max(a.band[k], b.band[k]) > mx - 40.f)
    { d[n] = std::max(b.band[k], mx - 40.f) - std::max(a.band[k], mx - 40.f); m += d[n]; n++; }
    m = n ? m / n : 0.f; float s = 0.f; for(int k = 0; k < n; k++) s += (d[k] - m) * (d[k] - m);
    shape = n ? std::sqrt(s / n) : 0.f;
    level = b.level - a.level;
    ring = (a.ring > 0.f && b.ring > 0.f) ? std::log2(b.ring / a.ring) : 0.f;
}

static Prof Lerp(const Prof& a, const Prof& b, float t)
{
    Prof p{}; p.note = a.note + t * (b.note - a.note);
    for(int k = 0; k < kBands; k++) p.band[k] = a.band[k] + t * (b.band[k] - a.band[k]);
    p.level = a.level + t * (b.level - a.level);
    p.ring = (a.ring > 0.f && b.ring > 0.f) ? std::exp2(std::log2(a.ring) + t * (std::log2(b.ring) - std::log2(a.ring))) : 0.f;
    return p;
}

static const char* NoteName(float n, char* buf)
{
    static const char* nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int i = (int)std::floor(n + 0.5f);
    snprintf(buf, 8, "%s%d", nm[((i % 12) + 12) % 12], (i / 12 - 1) % 100);
    return buf;
}

int main(int argc, char** argv)
{
    if(argc < 2) { printf("usage: worldaudit world.kykm [--tsv out.tsv] [--walk] [--render NOTE out.raw [velocity] [member]]\n"); return 1; }
    const char* path = argv[1]; const char* tsv = nullptr; bool walk = false;
    const char* rnote = nullptr; const char* rout = nullptr; float rvel = 1.f; int rmem = 0;
    for(int i = 2; i < argc; i++)
    {
        if(!strcmp(argv[i], "--tsv") && i + 1 < argc) tsv = argv[++i];
        else if(!strcmp(argv[i], "--walk")) walk = true;
        else if(!strcmp(argv[i], "--render") && i + 2 < argc)
        {
            rnote = argv[++i]; rout = argv[++i];
            if(i + 1 < argc && argv[i + 1][0] != '-') rvel = (float)atof(argv[++i]);
            if(i + 1 < argc && argv[i + 1][0] != '-') rmem = atoi(argv[++i]);
        }
    }
    FILE* f = fopen(path, "rb"); if(!f) { printf("cannot read %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> blob((size_t)n); if(fread(blob.data(), 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; } fclose(f);
    ResonatorWorld W; if(!W.Attach(blob.data(), (uint32_t)n)) { printf("not a world: %s\n", path); return 1; }
    const char* base = strrchr(path, '/'); base = base ? base + 1 : path;
    /* --render: one note through the voice as the module plays it — modes
       with their phases, the burst, the pickup — 4 s of float32 at 48 kHz,
       raw, for a comparison against its recording (tools/fitcheck.py
       --worlds). An energy sum over modes ignores the pairs that cancel,
       which is how a piano's two-stage decay is built */
    if(rnote)
    {
        ResonatorWorld R = W; if(W.kind == 2) W.Member(rmem, R);
        R.voicing = 0.f; R.decay = 1.f; R.coil = 1.f;
        ResonatorVoice v; v.Init(); R.At((float)atof(rnote), v, 48000.f); v.Strike(rvel);
        std::vector<float> y(192000);
        for(size_t i = 0; i < y.size(); i += 48) v.Process(y.data() + i, 48);
        FILE* o = fopen(rout, "wb"); if(!o) return 1;
        fwrite(y.data(), sizeof(float), y.size(), o); fclose(o);
        return 0;
    }
    FILE* out = tsv ? fopen(tsv, "w") : nullptr;
    if(out) fprintf(out, "world\tmember\tnote\tname\tcents\tweak\tshape_vs_neighbours\tlevel_vs_neighbours\tring_vs_neighbours\tscore\tt60\n");
    const int M = W.kind == 2 ? W.M : 1;
    for(int m = 0; m < M; m++)
    {
        ResonatorWorld R = W; if(W.kind == 2) W.Member(m, R);
        R.voicing = 0.f; R.decay = 1.f; R.coil = 1.f;      /* the world as fitted: the spin at its centre */
        if(R.kind == 1) { printf("%s: an index world (a row of bodies), not audited\n", base); continue; }
        std::vector<Prof> pts; for(int i = 0; i < R.P; i++) pts.push_back(Measure(R, R.Param(i)));
        struct Row { int i; float shape, level, ring, score; };
        std::vector<Row> rows; int off = 0; float worstc = 0.f;
        for(int i = 0; i < (int)pts.size(); i++)
        {
            Prof pred;
            if(i > 0 && i + 1 < (int)pts.size()) pred = Lerp(pts[i - 1], pts[i + 1], (pts[i].note - pts[i - 1].note) / (pts[i + 1].note - pts[i - 1].note));
            else if(pts.size() > 1) pred = pts[i > 0 ? i - 1 : 1];
            else pred = pts[i];
            float s, l, r; Compare(pred, pts[i], s, l, r);
            /* one number to rank by: shape in dB, level beyond 3 dB, ring
               beyond a factor of 1.4, tuning beyond 15 cents (in 10s) */
            const float c = pts[i].weak ? 0.f : std::fabs(pts[i].cents);
            const float score = s + std::max(0.f, std::fabs(l) - 3.f) + 6.f * std::max(0.f, std::fabs(r) - 0.5f) + std::max(0.f, c - 15.f) / 10.f;
            rows.push_back({i, s, l, r, score});
            if(!pts[i].weak && std::fabs(pts[i].cents) > 25.f) off++;
            if(!pts[i].weak && std::fabs(pts[i].cents) > std::fabs(worstc)) worstc = pts[i].cents;
            if(out) { char nb[8]; fprintf(out, "%s\t%d\t%.2f\t%s\t%.1f\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\n", base, m, pts[i].note, NoteName(pts[i].note, nb), pts[i].cents, pts[i].weak ? 1 : 0, s, l, r, score, pts[i].t60); }
        }
        std::vector<Row> srt = rows; std::sort(srt.begin(), srt.end(), [](const Row& a, const Row& b) { return a.score > b.score; });
        std::vector<float> sh; for(const Row& r : rows) sh.push_back(r.shape);
        std::sort(sh.begin(), sh.end());
        char mn[24] = ""; if(M > 1) snprintf(mn, sizeof(mn), " member %d", m);
        printf("%s%s: %zu points, %d off pitch >25c (worst %+.0fc), shape vs neighbours median %.1f dB p90 %.1f | worst:",
               base, mn, pts.size(), off, worstc, sh.empty() ? 0.f : sh[sh.size() / 2], sh.empty() ? 0.f : sh[(size_t)(0.9 * (sh.size() - 1))]);
        for(size_t k = 0; k < srt.size() && k < 6; k++)
        {
            const Row& r = srt[k]; const Prof& p = pts[r.i]; char nb[8];
            printf(" %s(%.1f%s%s)", NoteName(p.note, nb), r.score, p.weak ? "" : "", std::fabs(p.cents) > 25.f && !p.weak ? (std::string(" ") + std::to_string((int)std::lround(p.cents)) + "c").c_str() : "");
        }
        printf("\n");
        if(walk)
        {
            Prof prev{}; bool have = false;
            for(int nt = (int)std::ceil(R.lo); nt <= (int)std::floor(R.hi); nt++)
            {
                const Prof p = Measure(R, (float)nt);
                if(have) { float s, l, r; Compare(prev, p, s, l, r); char nb[8], nb2[8]; printf("  %s->%s shape %5.2f level %+5.1f ring %+5.2f\n", NoteName(prev.note, nb), NoteName(p.note, nb2), s, l, r); }
                prev = p; have = true;
            }
        }
    }
    if(out) fclose(out);
    return 0;
}
