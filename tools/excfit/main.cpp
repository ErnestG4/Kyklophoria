/* excfit — train a coupled exciter to a note's recordings (the road to full
 * synthesis: Combust, 28 September, "we are trying to move to full synthesis
 * as well. We'll need to train exciters to match").
 *
 *   excfit <world.kykm> <midi> <out-prefix> <target.wav>:<layer> [...]
 *     layer: pp, mf or ff (or a number 0..1, the velocity the take stands for)
 *
 * A world's modes were fitted with the recorded attack playing beside them.
 * Here the attack is the hammer: the voice's modes, driven through the coupled
 * loop the module runs (Kyklophoria core/kyk_exciter.h ProcessStruck, the
 * weights EngineCore::StrikeCoupled uses), and the hammer's felt, mass and the
 * speed of each layer searched (Nelder-Mead, restarted) so the first 600 ms
 * match each take: a multi-resolution log-magnitude STFT and the 10 ms
 * envelope, the two summed. One felt and one mass for all the layers — the
 * velocity's brightness has to come out of the physics, not a fit per layer.
 *
 * Built against Kyklophoria's core itself, not a copy: what is trained is what
 * plays. Prints the loss of the trained hammer against the loss of today's
 * voice (the recorded attack) on the same takes, and writes both renders:
 * <out-prefix>-<layer>-hammer.wav, -recorded.wav, and the take, -target.wav.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include "kyk_exciter.h"
#include "wavio.h"

using namespace kyk;

static const float kSec = 0.6f;          /* how much of each take the loss sees */
static bool g_refweights = false;       /* EXCFIT_WEIGHTS=ref: the fitted gains over the synthetic felt's own spectrum */

/* ── a small radix-2 FFT for the loss ─────────────────────────────────── */
static void Fft(std::vector<double>& re, std::vector<double>& im)
{
    const size_t n = re.size();
    for(size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;
        for(; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if(i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for(size_t len = 2; len <= n; len <<= 1)
    {
        const double a = -2.0 * M_PI / (double)len;
        for(size_t i = 0; i < n; i += len)
            for(size_t k = 0; k < len / 2; k++)
            {
                const double c = std::cos(a * k), s = std::sin(a * k);
                const double xr = re[i + k + len / 2] * c - im[i + k + len / 2] * s;
                const double xi = re[i + k + len / 2] * s + im[i + k + len / 2] * c;
                re[i + k + len / 2] = re[i + k] - xr; im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr; im[i + k] += xi;
            }
    }
}
/* the spectrum in sixth-octave bands from 30 Hz (g_bands): the loss weighed
   every linear bin alike, and at 44.1 kHz everything under 500 Hz is a
   handful of bins among thousands — the trainer gave the fundamental and the
   low partials away for the top (Combust: "they're missing a lot of the
   deeper harmonics we need"). In bands an octave counts as an octave, as it
   does to the ear. A band holds at least one bin; the lowest bands at small
   windows merge into their neighbours */
static bool g_bands = true;
static int  g_sr = 44100;
static std::vector<float> Bands(const std::vector<double>& re, const std::vector<double>& im, int n)
{
    std::vector<float> out;
    double lo = 30.0;
    const double step = std::pow(2.0, 1.0 / 6.0), top = std::min(16000.0, 0.45 * g_sr);
    int k0 = std::max(1, (int)(lo * n / g_sr));
    while(lo < top)
    {
        const double hi = lo * step;
        int k1 = std::max(k0 + 1, (int)(hi * n / g_sr));
        if(k1 > n / 2) k1 = n / 2;
        if(k1 <= k0) break;
        double p = 0; for(int k = k0; k < k1; k++) p += re[k] * re[k] + im[k] * im[k];
        out.push_back((float)std::sqrt(p / (k1 - k0)));
        k0 = k1; lo = hi;
    }
    return out;
}
/* log-magnitude frames of x, window n, hop n/4, Hann, floored at `floor`
   (a magnitude): the take's hiss is not a thing to match */
static std::vector<std::vector<float>> LogSpec(const std::vector<float>& x, int n, double floor)
{
    std::vector<std::vector<float>> out;
    std::vector<double> re(n), im(n);
    for(size_t at = 0; at + n <= x.size(); at += n / 4)
    {
        for(int i = 0; i < n; i++) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n); re[i] = x[at + i] * w; im[i] = 0; }
        Fft(re, im);
        std::vector<float> f;
        if(g_bands) { f = Bands(re, im, n); for(float& v : f) v = (float)std::log(std::max((double)v, floor)); }
        else { f.resize(n / 2); for(int k = 0; k < n / 2; k++) f[k] = (float)std::log(std::max(std::sqrt(re[k] * re[k] + im[k] * im[k]), floor)); }
        out.push_back(std::move(f));
    }
    return out;
}
struct Target { std::vector<float> x; std::vector<std::vector<float>> s[3]; double floor[3]; double rms; std::vector<float> env, onset; float vel; std::string layer, path; };
/* the onset's harshness: the first difference's energy (a crude high band) in
   1 ms frames over the first 30 ms, in dB over the take's own loudness. The
   10 ms envelope and the spectra averaged a click away, and the trained
   hammers came out with a pop at every note (Combust: "too harsh of a pop on
   each note's exciter") — up to 13.6 dB more high band in the first 3 ms than
   the recording's */
static std::vector<float> Onset30(const std::vector<float>& y, double rms, int sr)
{
    const int w = sr / 1000; std::vector<float> o;
    for(int at = 1; at + w <= (int)(0.03 * sr); at += w)
    {
        double d = 0; for(int i = at; i < at + w; i++) { const double x = y[i] - y[i - 1]; d += x * x; }
        o.push_back((float)(10 * std::log10(d / w / (rms * rms + 1e-30) + 1e-9)));
    }
    return o;
}
static double Rms(const std::vector<float>& x) { double e = 0; for(float v : x) e += (double)v * v; return std::sqrt(e / (x.size() ? x.size() : 1)); }
/* the loudest bin of any frame at window n, and the floor 50 dB under it */
static double FloorOf(const std::vector<float>& x, int n)
{
    const auto s = LogSpec(x, n, 1e-30);
    float mx = -1e30f; for(auto& f : s) for(float v : f) mx = std::max(mx, v);
    return std::exp(mx) * std::pow(10.0, -50.0 / 20.0);
}
static std::vector<float> Env(const std::vector<float>& x, int sr)
{
    const int w = sr / 100; std::vector<float> e;
    for(size_t at = 0; at + w <= x.size(); at += w / 2) { double s = 0; for(int i = 0; i < w; i++) s += (double)x[at + i] * x[at + i]; e.push_back((float)(10 * std::log10(s / w + 1e-12))); }
    return e;
}
static const int kSizes[3] = {256, 1024, 4096};
/* where a loss sits: the 256-point spectrum's error in the first 50 ms and
   after it, and the envelope's in dB — a report, not the loss */
static void Where(const std::vector<float>& y0, const Target& t, int sr, double& att, double& ring, double& envdb);
/* the takes are levelled (the Iowa set's peak at -6 dBFS whatever the
   dynamic), so each render is matched to its take's loudness before it is
   compared: the loss is the timbre and the envelope's shape, and the speed
   explains what it can — the attack's length and brightness */
static double Loss(const std::vector<float>& y0, const Target& t, int sr)
{
    std::vector<float> y(y0);
    const double r0 = Rms(y);
    if(r0 > 0) { const float g = (float)(t.rms / r0); for(float& v : y) v *= g; }
    double l = 0;
    for(int r = 0; r < 3; r++)
    {
        const auto s = LogSpec(y, kSizes[r], t.floor[r]);
        const size_t nf = std::min(s.size(), t.s[r].size());
        double d = 0; size_t c = 0;
        for(size_t f = 0; f < nf; f++) for(size_t k = 0; k < s[f].size(); k++) { d += std::fabs(s[f][k] - t.s[r][f][k]); c++; }
        l += c ? d / c : 10.0;
    }
    const auto e = Env(y, sr);
    const size_t ne = std::min(e.size(), t.env.size());
    double de = 0; for(size_t i = 0; i < ne; i++) de += std::fabs(e[i] - t.env[i]);
    const auto o = Onset30(y, t.rms, sr);
    const size_t no = std::min(o.size(), t.onset.size());
    double dn = 0; for(size_t i = 0; i < no; i++) dn += std::fabs(o[i] - t.onset[i]);
    /* and the onset's peak: the loudest sample of the first 30 ms over the
       take's, in dB, only when over it. The harshness term is a high band,
       and the treble's pop after the polarity fix was a smooth bump — a
       1.5 ms pulse at 3-8x the recording's peak at the same loudness
       (exclevel, iowa3), 20x the note's own ring — which a first difference
       barely sees */
    double over = 0;
    {
        const size_t n30 = std::min({y.size(), t.x.size(), (size_t)(0.03 * sr)});
        float py = 0.f, pt = 0.f;
        for(size_t i = 0; i < n30; i++) { py = std::max(py, std::fabs(y[i])); pt = std::max(pt, std::fabs(t.x[i])); }
        if(pt > 0.f && py > pt) over = 20.0 * std::log10(py / pt);
    }
    return l / 3.0 + (ne ? de / ne / 20.0 : 1.0)      /* dB over 20: an envelope 20 dB out costs as much as a spectrum e-fold out */
                   + (no ? 3.0 * dn / no / 20.0 : 0.0)    /* and the onset's harshness three times over: a pop is what the ear objects to first */
                   + 3.0 * over / 20.0;
}

/* the take from its onset (the first 10 ms window within 30 dB of the peak,
   backed off 5 ms), kSec long */
static std::vector<float> Onset(const std::vector<float>& x, int sr)
{
    float pk = 0; for(float v : x) pk = std::max(pk, std::fabs(v));
    size_t at = 0;
    for(size_t i = 0; i < x.size(); i++) if(std::fabs(x[i]) > pk * 0.0316f) { at = i; break; }
    at = at > (size_t)(0.005f * sr) ? at - (size_t)(0.005f * sr) : 0;
    const size_t n = (size_t)(kSec * sr);
    std::vector<float> y(n, 0.f);
    for(size_t i = 0; i < n && at + i < x.size(); i++) y[i] = x[at + i];
    return y;
}

/* the hammer's parameters, searched in logs: felt k, alpha, hammer mass,
   the output's scale (the modal mass it moves), and one speed a layer */
/* ... and the contact's noise (ContactNoise): its level over the force, and
   its band's centre — a hard hit's knock, which the modes cannot carry */
/* ... and the felt's hysteresis (Hammer::mu, Hunt-Crossley) */
/* ... and the hammer's width, as a fraction of the string: a felt strikes a
   patch 1-2 cm wide, and a mode whose wavelength is shorter than the patch is
   averaged away at the contact — which is why a real bass attack thuds where
   the point hammer clicked (C1, C2: 5-8 dB more high band in the first 3 ms
   than the recording, whatever the felt). Baked into the weights */
struct Params { double lk, alpha, lmass, lgain, lspeed[3], lnoise, lnfc, lmu, lwidth; };
static const int kDim = 11;
static void ToVec(const Params& p, double* v) { v[0] = p.lk; v[1] = p.alpha; v[2] = p.lmass; v[3] = p.lgain; for(int i = 0; i < 3; i++) v[4 + i] = p.lspeed[i]; v[7] = p.lnoise; v[8] = p.lnfc; v[9] = p.lmu; v[10] = p.lwidth; }
static Params FromVec(const double* v) { Params p; p.lk = v[0]; p.alpha = v[1]; p.lmass = v[2]; p.lgain = v[3]; for(int i = 0; i < 3; i++) p.lspeed[i] = v[4 + i]; p.lnoise = v[7]; p.lnfc = v[8]; p.lmu = v[9]; p.lwidth = v[10]; return p; }

/* one layer struck by the hammer, kSec long: the voice's modes from rest,
   driven through the coupled loop with EngineCore::StrikeCoupled's weights
   (each mode's unit-strike gain over the loudest; position at the centre) */
static int g_contact = 0;                     /* the last render's contact, in samples */
static std::vector<float> RenderHammer(const ResonatorVoice& v0, const Params& p, int layer, int sr, float* wout = nullptr, bool raw = false)
{
    static ResonatorVoice v; v = v0;
    ResonatorBank& b = v.bank;
    for(int k = 0; k < ResonatorBank::kMax; k++) { b.y1[k] = b.y2[k] = 0.f; for(int q = 0; q < ResonatorBank::kStrikes; q++) b.s1[q][k] = b.s2[q][k] = 0.f; }
    b.tn = 0; b.tail_left = 0;
    float w[ResonatorBank::kMax], gmax = 0.f;
    for(int k = 0; k < b.n; k++) gmax = std::max(gmax, std::fabs(v.gain[k]));
    for(int k = 0; k < ResonatorBank::kMax; k++) w[k] = (k < b.n && gmax > 0.f) ? std::fabs(v.gain[k]) / gmax : 0.f;
    Hammer h; h.Init();
    h.k = (float)std::exp(p.lk); h.alpha = (float)p.alpha; h.mass = (float)std::exp(p.lmass); h.mu = (float)std::exp(p.lmu);
    if(g_refweights)
    {
        /* the fitted gains are the ring after the RECORDED hammer, its felt's
           spectrum already in them: driven through them, a synthetic felt
           filters twice. So strike once at the reference speed (the middle
           layer's) with every mode alike, read each mode's amplitude when
           the felt has let go, and divide it out: at the reference speed the
           ring is the fitted instrument's, and the other speeds move it by
           the felt's physics alone */
        /* the reference strike with the weights themselves, again and again:
           the contact is nonlinear, so a strike through equal weights is not
           the strike through these, and one division left the reference
           speed's own ring 10 dB off the fitted one at the 3rd harmonic.
           Three passes bring each mode's amplitude after the contact to the
           fitted gain's proportion */
        float target[ResonatorBank::kMax];
        for(int k = 0; k < b.n; k++) target[k] = w[k];
        for(int pass = 0; pass < 3; pass++)
        {
            static ResonatorVoice r; r = v;
            for(int k = 0; k < ResonatorBank::kMax; k++) { r.bank.y1[k] = r.bank.y2[k] = 0.f; }
            Hammer hr = h; hr.Strike((float)std::exp(p.lspeed[1]), 0.f);
            float tmp[1]; int ran = 0;
            while(ran < 4000 && ProcessStruck(r.bank, tmp, 1, w, hr, (float)sr, 0.01f)) ran++;
            float a[ResonatorBank::kMax], ratio_max = 0.f;
            for(int k = 0; k < b.n; k++)
            {
                const float r2 = -r.bank.c2[k], rr = r2 > 0.f ? std::sqrt(r2) : 0.f, cw = rr > 0.f ? r.bank.c1[k] / (2.f * rr) : 1.f;
                const float s2 = std::max(1e-6f, 1.f - cw * cw), ry = rr * r.bank.y2[k];
                a[k] = std::sqrt(std::max(0.f, (r.bank.y1[k] * r.bank.y1[k] + ry * ry - 2.f * r.bank.y1[k] * ry * cw) / s2));
            }
            float amax = 0.f; for(int k = 0; k < b.n; k++) amax = std::max(amax, a[k]);
            if(amax <= 0.f) break;
            for(int k = 0; k < b.n; k++)
            {
                const float got = a[k] / amax;                 /* the ring's proportion now */
                /* cut only (Combust: "let's maybe not boost the high modes"):
                   a mode the synthetic felt drives harder than the recorded
                   one did is brought down to the fitted proportion, and one
                   it drives less is left so — the brightness comes from the
                   felt and the speed, not from lifted weights. Lifted (up to
                   512x over three passes), every lifted high mode starting at
                   the strike in phase was the pop at each note */
                if(got > 1e-6f) w[k] *= std::max(0.125f, std::min(1.f, target[k] / got));
            }
            for(int k = 0; k < b.n; k++) ratio_max = std::max(ratio_max, w[k]);
            if(ratio_max > 0.f) for(int k = 0; k < b.n; k++) w[k] /= ratio_max;
        }
    }
    /* the hammer's width: each mode by how much of it survives an average
       over the patch the felt covers, |sin z / z| at z = pi h width / 2, h
       the mode's harmonic number over the lowest */
    {
        float f1 = 1e9f; for(int k = 0; k < b.n; k++) if(v.gain[k] != 0.f && v.hz[k] > 0.f && v.hz[k] < f1) f1 = v.hz[k];
        const double width = std::exp(p.lwidth);
        if(f1 < 1e8f) for(int k = 0; k < b.n; k++)
        {
            const double z = M_PI * (v.hz[k] / f1) * width / 2.0;
            const double sc = z > 1e-6 ? std::fabs(std::sin(z) / z) : 1.0;
            w[k] *= (float)std::max(0.02, sc);
        }
    }
    if(wout) for(int k = 0; k < ResonatorBank::kMax; k++) wout[k] = w[k];
    /* each mode's polarity as the recording has it, sign(g cos phase), as
       EngineCore::StrikeCoupled gives it (the weights baked are magnitudes;
       the world has the signs). The contact cannot tell; the sum can: all
       alike, the modes started in phase and piled into a 2 ms pulse, 5-9x
       the recording's peak in the treble at the same loudness (the pop) */
    for(int k = 0; k < b.n; k++) if(v.gain[k] * std::cos(v.phase[k]) < 0.f) w[k] = -w[k];
    h.Strike((float)std::exp(p.lspeed[layer]), 0.f);
    const int n = (int)(kSec * sr);
    std::vector<float> y(n, 0.f);
    ContactNoise cn; cn.Init((float)sr, (float)std::exp(p.lnfc), 0.7f, (float)std::exp(p.lnoise));
    ProcessStruck(b, y.data(), n, w, h, (float)sr, 0.01f, &cn);
    g_contact = h.contact;
    if(v.pickup.on) v.pickup.Process(y.data(), n);
    if(raw) return y;                          /* the hammer's own units, as the module's contact runs */
    const float g = (float)std::exp(p.lgain);
    for(float& s : y) s *= g;
    return y;
}
/* today's voice at the layer's velocity: the recorded attack and the modes
   ramping in under it */
static std::vector<float> RenderRecorded(const ResonatorVoice& v0, float vel, int sr)
{
    static ResonatorVoice v; v = v0;
    v.Strike(vel);
    const int n = (int)(kSec * sr);
    std::vector<float> y(n, 0.f);
    for(int i = 0; i < n; i += 48) v.Process(y.data() + i, std::min(48, n - i));
    return y;
}

/* the first eight harmonics' levels in dB over the whole window, each re
   the take's loudness (level-matched as the loss is): a number for "the
   deeper harmonics" */
static void Harmonics(const std::vector<float>& y0, double rms_to, int sr, float f0, double* db)
{
    std::vector<float> y(y0);
    const double r0 = Rms(y);
    if(r0 > 0) { const float g = (float)(rms_to / r0); for(float& v : y) v *= g; }
    for(int h = 1; h <= 8; h++)
    {
        double best = 0;
        for(int c = -20; c <= 20; c++)            /* within 40 cents either way: stretched partials */
        {
            const double f = h * f0 * std::pow(2.0, c / 1200.0 * 2);
            double re = 0, im = 0;
            for(size_t i = 0; i < y.size(); i++) { const double ph = 2 * M_PI * f * i / sr; re += y[i] * std::cos(ph); im += y[i] * std::sin(ph); }
            best = std::max(best, std::sqrt(re * re + im * im) / y.size());
        }
        db[h - 1] = 20 * std::log10(best + 1e-12);
    }
}
static void Where(const std::vector<float>& y0, const Target& t, int sr, double& att, double& ring, double& envdb)
{
    std::vector<float> y(y0);
    const double r0 = Rms(y);
    if(r0 > 0) { const float g = (float)(t.rms / r0); for(float& v : y) v *= g; }
    const auto s = LogSpec(y, 256, t.floor[0]);
    const size_t cut = (size_t)(0.05 * sr / 64);                 /* frames in the first 50 ms (hop 64) */
    double a = 0, b = 0; size_t na = 0, nb = 0;
    for(size_t f = 0; f < std::min(s.size(), t.s[0].size()); f++)
        for(size_t k = 0; k < s[f].size(); k++) { const double d = std::fabs(s[f][k] - t.s[0][f][k]); if(f < cut) { a += d; na++; } else { b += d; nb++; } }
    att = na ? a / na : 0; ring = nb ? b / nb : 0;
    const auto e = Env(y, sr); double de = 0; const size_t ne = std::min(e.size(), t.env.size());
    for(size_t i = 0; i < ne; i++) de += std::fabs(e[i] - t.env[i]);
    envdb = ne ? de / ne : 0;
}

int main(int argc, char** argv)
{
    if(argc < 5) { std::fprintf(stderr, "excfit <world.kykm> <midi> <out-prefix> <target.wav>:<layer> [...]\n"); return 2; }
    std::vector<uint8_t> blob;
    { FILE* f = std::fopen(argv[1], "rb"); if(!f) { std::perror(argv[1]); return 1; } std::fseek(f, 0, SEEK_END); blob.resize(std::ftell(f)); std::fseek(f, 0, SEEK_SET); if(std::fread(blob.data(), 1, blob.size(), f) != blob.size()) return 1; std::fclose(f); }
    const float midi = (float)std::atof(argv[2]);
    g_refweights = std::getenv("EXCFIT_WEIGHTS") && std::string(std::getenv("EXCFIT_WEIGHTS")) == "ref";
    g_bands = !(std::getenv("EXCFIT_BINS") && std::atoi(std::getenv("EXCFIT_BINS")));   /* EXCFIT_BINS=1: the old linear bins, to compare */
    const std::string prefix = argv[3];
    std::vector<Target> takes;
    int sr = 0;
    for(int a = 4; a < argc; a++)
    {
        std::string s = argv[a]; const size_t c = s.rfind(':');
        Target t; t.path = s.substr(0, c); t.layer = c == std::string::npos ? "mf" : s.substr(c + 1);
        t.vel = t.layer == "pp" ? 0.15f : t.layer == "mf" ? 0.55f : t.layer == "ff" ? 0.95f : (float)std::atof(t.layer.c_str());
        std::vector<float> x; int tsr = 0;
        if(!kykdesk::ReadWav(t.path, x, tsr)) { std::fprintf(stderr, "cannot read %s\n", t.path.c_str()); return 1; }
        if(sr && tsr != sr) { std::fprintf(stderr, "the takes' rates differ\n"); return 1; }
        sr = tsr; g_sr = sr;
        t.x = Onset(x, sr);
        t.rms = Rms(t.x);
        t.onset = Onset30(t.x, t.rms, sr);
        for(int r = 0; r < 3; r++) { t.floor[r] = FloorOf(t.x, kSizes[r]); t.s[r] = LogSpec(t.x, kSizes[r], t.floor[r]); }
        t.env = Env(t.x, sr);
        takes.push_back(std::move(t));
    }
    if(takes.size() > 3) { std::fprintf(stderr, "three layers at most\n"); return 2; }
    ResonatorWorld R; R.Init();
    if(!R.Attach(blob.data(), (uint32_t)blob.size())) { std::fprintf(stderr, "not a world: %s\n", argv[1]); return 1; }
    static ResonatorVoice v0; v0.Init(); v0.cap = 0; R.At(midi, v0, (float)sr);
    std::printf("%s at %.0f: %d modes, %d takes at %d Hz\n", argv[1], midi, v0.bank.n, (int)takes.size(), sr);

    /* the contact no shorter than a hammer's: 1.5 ms at pp, 1 at mf, 0.5
       at ff, less above C6 as sqrt(1046/f0). The trained grand's were far
       under — C3's ff two samples (0.04 ms), its pp 0.42 ms, C4's ff 0.23
       ms, where a grand's hammer stays some milliseconds on the string in
       the bass and middle — and a contact that short is an impulse: the
       spectrum came right through the weights, the first milliseconds kept
       the click (the pp's 3 ms 8.9 dB over the take's, iowa5). Short of the
       floor costs its shortfall in e-folds, one a fold. Off unless
       EXCFIT_CONTACT=1: tried on six notes it made every contact a
       hammer's (1.7-4 ms at pp, 0.5-1.8 at ff) but the pop no better, and
       four strikes over the level gate — the pp's "pop" had been mostly
       the take's quiet precursor, which the onset at 3% of the peak caught
       (at 10%, the level-safe card is within +/-2 dB of the takes) */
    const double f0n = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
    const double cscale = std::min(1.0, std::sqrt(1046.5 / f0n));
    const bool cfloor = std::getenv("EXCFIT_CONTACT") && std::atof(std::getenv("EXCFIT_CONTACT")) != 0.0;
    auto contact_cost = [&](const Target& t) {
        if(!cfloor) return 0.0;
        const double ms = (t.layer == "pp" ? 1.5 : t.layer == "ff" ? 0.5 : 1.0) * cscale;
        const double got = 1000.0 * g_contact / sr;
        return got >= ms ? 0.0 : std::log(ms / std::max(got, 0.01));          /* an e-fold short costs as a spectrum an e-fold out */
    };
    auto total = [&](const Params& p) { double l = 0; for(size_t i = 0; i < takes.size(); i++) { l += Loss(RenderHammer(v0, p, (int)i, sr), takes[i], sr); l += contact_cost(takes[i]); } return l / takes.size(); };

    /* Nelder-Mead from a spread of starts; the best kept */
    std::mt19937 rng(7);
    auto rnd = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    Params best{}; double bestL = 1e18; int evals = 0;
    /* EXCFIT_PARAMS="k alpha mu mass noise nfc speed_pp speed_mf speed_ff":
       no search — the hammer given (a keyboard's smoothed values, excsmooth),
       only its weights, levels and renders worked out and written */
    int starts = std::getenv("EXCFIT_STARTS") ? std::atoi(std::getenv("EXCFIT_STARTS")) : 8;   /* fewer, for a quick look */
    if(const char* fixed = std::getenv("EXCFIT_PARAMS"))
    {
        double k, a, mu, ms, nz, nf, sp[3], wd = 0.001;
        const int got = std::sscanf(fixed, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", &k, &a, &mu, &ms, &nz, &nf, &sp[0], &sp[1], &sp[2], &wd);
        if(got < 9) { std::fprintf(stderr, "EXCFIT_PARAMS wants nine numbers (and the width, a tenth)\n"); return 2; }
        best.lwidth = std::log(wd);
        best.lk = std::log(k); best.alpha = a; best.lmu = std::log(mu); best.lmass = std::log(ms); best.lnoise = std::log(nz > 0 ? nz : 1e-12); best.lnfc = std::log(nf); best.lgain = 0;
        /* the takes' speeds by their layers (pp, mf, ff in that order among those given) */
        const char* names[3] = {"pp", "mf", "ff"};
        for(size_t i = 0; i < takes.size(); i++) for(int L = 0; L < 3; L++) if(takes[i].layer == names[L]) best.lspeed[i] = std::log(sp[L]);
        bestL = total(best); starts = 0;
    }
    for(int start = 0; start < starts; start++)
    {
        double simplex[kDim + 1][kDim], f[kDim + 1];
        Params p0; p0.lk = rnd(std::log(1e7), std::log(1e10)); p0.alpha = rnd(2.2, 3.5); p0.lmass = rnd(std::log(0.005), std::log(0.015)); p0.lgain = rnd(-2, 4);
        for(int i = 0; i < 3; i++) p0.lspeed[i] = std::log(0.3 * std::pow(16.0, takes.size() > (size_t)i ? takes[i].vel : 0.5)) + rnd(-0.3, 0.3);
        p0.lnoise = rnd(std::log(1e-9), std::log(1e-5)); p0.lnfc = rnd(std::log(500.0), std::log(8000.0));
        p0.lmu = rnd(std::log(0.01), std::log(3.0));
        p0.lwidth = rnd(std::log(0.003), std::log(0.03));
        ToVec(p0, simplex[0]);
        const double step[kDim] = {1.0, 0.3, 0.5, 1.0, 0.4, 0.4, 0.4, 1.5, 0.5, 1.0, 0.7};
        for(int i = 1; i <= kDim; i++) { for(int d = 0; d < kDim; d++) simplex[i][d] = simplex[0][d]; simplex[i][i - 1] += step[i - 1]; }
        /* a grand's hammers weigh 5-15 g (the bass's 10-12): the trainer had
           them at 2-4 g and fast, a short contact and a bright click at every
           bass note */
        /* and a hammer's other numbers where a hammer's are: the felt's
           stiffness 1e6-1e11, its hysteresis mu under 5 s/m, the speeds
           0.1-8 m/s (a grand's pp to its hardest ff). Unbounded, the search
           found corners that fit the spectra and are no hammer — the C2 at
           K 4.4e13, mu 30, speeds 0.017 to 29.6 m/s and levels from 2225 to
           0.0077 — and struck again while ringing it ran to 15-19x the
           recording (iowa4, exclevel) */
        /* the felt's exponent's floor (EXCFIT_ALPHA_MIN, 1.5 by default): half
           the grand sat on 1.5, a near-linear felt that gives a pp the same
           sharp edge as an ff — the pp's first 3 ms 8.9 dB over the take's
           where the ff's matched (iowa5). A real felt is 2.5-3.5 */
        static const double amin = std::getenv("EXCFIT_ALPHA_MIN") ? std::atof(std::getenv("EXCFIT_ALPHA_MIN")) : 1.5;
        auto clampv = [](double* v) { v[0] = std::min(std::log(1e11), std::max(std::log(1e6), v[0]));
                                   v[1] = std::min(4.0, std::max(amin, v[1])); v[2] = std::min(std::log(0.015), std::max(std::log(0.005), v[2])); for(int i = 4; i < 7; i++) v[i] = std::min(std::log(8.0), std::max(std::log(0.1), v[i]));
                                   v[8] = std::min(std::log(16000.0), std::max(std::log(100.0), v[8]));
                                   v[9] = std::min(std::log(5.0), std::max(std::log(1e-4), v[9]));
                                   v[10] = std::min(std::log(0.05), std::max(std::log(0.001), v[10])); };
        for(int i = 0; i <= kDim; i++) { clampv(simplex[i]); f[i] = total(FromVec(simplex[i])); evals++; }
        for(int it = 0; it < 220; it++)
        {
            int o[kDim + 1]; for(int i = 0; i <= kDim; i++) o[i] = i;
            std::sort(o, o + kDim + 1, [&](int a, int b) { return f[a] < f[b]; });
            double c[kDim] = {0};
            for(int i = 0; i < kDim; i++) for(int d = 0; d < kDim; d++) c[d] += simplex[o[i]][d] / kDim;
            const int wst = o[kDim];
            double xr[kDim]; for(int d = 0; d < kDim; d++) xr[d] = c[d] + (c[d] - simplex[wst][d]); clampv(xr);
            const double fr = total(FromVec(xr)); evals++;
            if(fr < f[o[0]])
            {
                double xe[kDim]; for(int d = 0; d < kDim; d++) xe[d] = c[d] + 2 * (c[d] - simplex[wst][d]); clampv(xe);
                const double fe = total(FromVec(xe)); evals++;
                if(fe < fr) { std::memcpy(simplex[wst], xe, sizeof xe); f[wst] = fe; } else { std::memcpy(simplex[wst], xr, sizeof xr); f[wst] = fr; }
            }
            else if(fr < f[o[kDim - 1]]) { std::memcpy(simplex[wst], xr, sizeof xr); f[wst] = fr; }
            else
            {
                double xc[kDim]; for(int d = 0; d < kDim; d++) xc[d] = c[d] + 0.5 * (simplex[wst][d] - c[d]); clampv(xc);
                const double fc = total(FromVec(xc)); evals++;
                if(fc < f[wst]) { std::memcpy(simplex[wst], xc, sizeof xc); f[wst] = fc; }
                else for(int i = 1; i <= kDim; i++) { for(int d = 0; d < kDim; d++) simplex[o[i]][d] = simplex[o[0]][d] + 0.5 * (simplex[o[i]][d] - simplex[o[0]][d]); clampv(simplex[o[i]]); f[o[i]] = total(FromVec(simplex[o[i]])); evals++; }
            }
        }
        int bi = 0; for(int i = 1; i <= kDim; i++) if(f[i] < f[bi]) bi = i;
        if(f[bi] < bestL) { bestL = f[bi]; best = FromVec(simplex[bi]); }
        std::printf("  start %d: loss %.3f\n", start, f[bi]);
    }
    std::printf("trained hammer (%d renders): felt k %.3g alpha %.2f, mass %.1f g, gain %.3g, noise %.3g at %.0f Hz, mu %.3g s/m, width %.3g, speeds", evals, std::exp(best.lk), best.alpha, 1000 * std::exp(best.lmass), std::exp(best.lgain), std::exp(best.lnoise), std::exp(best.lnfc), std::exp(best.lmu), std::exp(best.lwidth));
    for(size_t i = 0; i < takes.size(); i++) std::printf(" %s %.2f m/s", takes[i].layer.c_str(), std::exp(best.lspeed[i]));
    std::printf("\n%-6s %10s %10s\n", "layer", "hammer", "recorded");
    for(size_t i = 0; i < takes.size(); i++)
    {
        const auto yh = RenderHammer(v0, best, (int)i, sr), yr = RenderRecorded(v0, takes[i].vel, sr);
        double ah, rh, eh, ar, rr, er;
        Where(yh, takes[i], sr, ah, rh, eh); Where(yr, takes[i], sr, ar, rr, er);
        std::printf("%-6s %10.3f %10.3f    attack %.2f / %.2f  ring %.2f / %.2f  envelope %.1f / %.1f dB\n", takes[i].layer.c_str(), Loss(yh, takes[i], sr), Loss(yr, takes[i], sr), ah, ar, rh, rr, eh, er);
        const float f0 = 440.f * std::pow(2.f, (midi - 69.f) / 12.f);
        double ht[8], hh[8], hr[8];
        Harmonics(takes[i].x, takes[i].rms, sr, f0, ht); Harmonics(yh, takes[i].rms, sr, f0, hh); Harmonics(yr, takes[i].rms, sr, f0, hr);
        std::printf("       h1-h8, hammer and recorded voice against the take (dB):");
        for(int h = 0; h < 8; h++) std::printf(" %+.0f/%+.0f", hh[h] - ht[h], hr[h] - ht[h]);
        std::printf("\n");
        /* written as the loss hears them: at the take's loudness (the hammer's
           absolute level means nothing — the takes are levelled — and raw it
           sat 45-67 dB down, silent: Combust, "I can't hear anything on those
           hammer wavs"), held under full scale */
        auto level = [&](std::vector<float> y) {
            const double r = Rms(y); if(r > 0) { const float g = (float)(takes[i].rms / r); for(float& v : y) v *= g; }
            float pk = 0; for(float v : y) pk = std::max(pk, std::fabs(v));
            if(pk > 0.99f) for(float& v : y) v *= 0.99f / pk;
            return y;
        };
        const auto wh = level(yh), wr = level(yr);
        kykdesk::WriteWavFloat(prefix + "-" + takes[i].layer + "-hammer.wav", wh.data(), wh.size(), sr);
        kykdesk::WriteWavFloat(prefix + "-" + takes[i].layer + "-recorded.wav", wr.data(), wr.size(), sr);
        kykdesk::WriteWavFloat(prefix + "-" + takes[i].layer + "-target.wav", takes[i].x.data(), takes[i].x.size(), sr);
    }
    /* the result, for excbake (EXCFIT_RESULTS=<file>, appended): the
       parameters, a speed and a level for each layer (pp, mf, ff — the
       velocity 0.15, 0.55, 0.95 the runtime interpolates between), and the
       weights the contact uses, in the point's mode order. The level is the
       ratio that puts the hammer, in its own units, at today's voice's
       loudness at that velocity: switching exciter does not jump */
    if(const char* rp = std::getenv("EXCFIT_RESULTS"))
    {
        float w[ResonatorBank::kMax];
        double speed[3] = {0, 0, 0}, gain[3] = {0, 0, 0};
        const char* names[3] = {"pp", "mf", "ff"};
        const float vels[3] = {0.15f, 0.55f, 0.95f};
        for(int L = 0; L < 3; L++)
        {
            int ti = -1; for(size_t i = 0; i < takes.size(); i++) if(takes[i].layer == names[L]) ti = (int)i;
            /* a layer with no take: its speed the mean, in logs, of the
               layers there are — the runtime interpolates in logs anyway */
            Params q = best; int li = ti;
            if(ti < 0) { double m = 0; for(size_t i = 0; i < takes.size(); i++) m += best.lspeed[i]; q.lspeed[0] = m / (takes.size() ? takes.size() : 1); li = 0; }
            speed[L] = std::exp(q.lspeed[li]);
            const auto yh = RenderHammer(v0, q, li, sr, w, true);
            const auto yr = RenderRecorded(v0, vels[L], sr);
            const double rh = Rms(yh), rr = Rms(yr);
            gain[L] = rh > 0 ? rr / rh : 1.0;
        }
        RenderHammer(v0, best, takes.size() > 1 ? 1 : 0, sr, w, true);      /* the weights: the reference (middle) layer's iteration */
        FILE* f = std::fopen(rp, "a");
        if(f)
        {
            std::fprintf(f, "%.3f\t%.6g\t%.6g\t%.6g\t%.6g\t%.6g\t%.6g", midi, std::exp(best.lk), best.alpha, std::exp(best.lmu), std::exp(best.lmass), std::exp(best.lnoise), std::exp(best.lnfc));
            for(int L = 0; L < 3; L++) std::fprintf(f, "\t%.6g", speed[L]);
            for(int L = 0; L < 3; L++) std::fprintf(f, "\t%.6g", gain[L]);
            std::fprintf(f, "\t%d", v0.bank.n);
            for(int k = 0; k < v0.bank.n; k++) std::fprintf(f, "\t%.6g", w[k]);
            std::fprintf(f, "\t%.6g\n", std::exp(best.lwidth));   /* the width, last: excbake reads the weights and stops; excsmooth takes it */
            std::fclose(f);
        }
    }
    return 0;
}
