/* earcheck — grades a render the way an ear would before a person has to:
 * a rail (samples at full scale), a click (a burst above 12 kHz twenty
 * dB over its surroundings that is gone again within 3 ms on both sides,
 * and broadband — a bright partial dying is not one, and neither is the
 * start of a note the waveform rose into; see Onset), and silence (more
 * than half the file under -80 dB). A port of ModalBake's tools/earcheck.py
 * to the standard library, since the suite runs on python3 with nothing
 * installed; the filters are cascaded RBJ biquads rather than scipy's
 * Butterworth, so the numbers differ in the third figure and the verdicts
 * do not. Interleaved stereo is read as mono by its first channel.
 *
 *   earcheck a.wav [b.wav …]      exit 1 if any fails
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>
#include "../shell/desktop/wavio.h"

namespace {

struct Biquad
{
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double Tick(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

Biquad HighPass(double fc, double q, double sr)
{
    const double w0 = 2 * M_PI * fc / sr, c = cos(w0), a = sin(w0) / (2 * q), a0 = 1 + a;
    return {(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - a) / a0};
}
Biquad LowPass(double fc, double q, double sr)
{
    const double w0 = 2 * M_PI * fc / sr, c = cos(w0), a = sin(w0) / (2 * q), a0 = 1 + a;
    return {(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - a) / a0};
}

/* three biquads in cascade at Butterworth Qs: a sixth-order edge, run
   forward then backward as filtfilt does, so the envelope does not lag */
std::vector<double> Sixth(const std::vector<float>& x, bool high, double fc, double sr, bool band_low = false, double fc2 = 0)
{
    std::vector<double> y(x.begin(), x.end());
    const double qs[3] = {0.5176, 0.7071, 1.9319};
    for(int pass = 0; pass < 2; pass++)
    {
        Biquad f[3], g[3];
        for(int i = 0; i < 3; i++) { f[i] = high ? HighPass(fc, qs[i], sr) : LowPass(fc, qs[i], sr); if(band_low) g[i] = LowPass(fc2, qs[i], sr); }
        if(pass == 0) for(size_t n = 0; n < y.size(); n++) { double v = y[n]; for(auto& b : f) v = b.Tick(v); if(band_low) for(auto& b : g) v = b.Tick(v); y[n] = v; }
        else for(size_t n = y.size(); n-- > 0;) { double v = y[n]; for(auto& b : f) v = b.Tick(v); if(band_low) for(auto& b : g) v = b.Tick(v); y[n] = v; }
    }
    return y;
}

std::vector<double> Env(const std::vector<double>& x, int hop)
{
    std::vector<double> e;
    for(size_t i = 0; i + hop <= x.size(); i += hop)
    {
        double s = 0; for(int k = 0; k < hop; k++) s += x[i + k] * x[i + k];
        e.push_back(sqrt(s / hop));
    }
    return e;
}

/* is this millisecond the start of a note rather than a fault in one? A
   hard mallet's attack is a burst above 12 kHz that is gone again in three
   milliseconds, and so is a coefficient step; what tells them apart is what
   came before. A step interrupts a signal that was already there. An attack
   starts from nothing — the broadband envelope itself rises out of the floor
   at that frame — and it *rises*: the waveform grows into it over a few
   dozen samples rather than leaping, which is what a burst read from the
   wrong sample of its recording would do. Both halves are needed: the first
   alone would excuse a spliced attack, which is a real defect and audible.
   Measured on tests/data/bodies.kykm: an Iowa mallet's strike is a 161x
   onset whose first sample past a hundredth of its peak sits at 1.1% of it;
   a step spliced into the same render leaps straight to 100%. */
bool Onset(const std::vector<double>& x, const std::vector<double>& env, size_t n, int hop)
{
    double pre = 0;
    for(size_t j = n >= 5 ? n - 5 : 0; j < n; j++) pre = std::max(pre, env[j]);
    if(n < 5 || !(env[n] > 20 * pre)) return false;
    const size_t a = n * hop > 3 * (size_t)hop ? n * hop - 3 * hop : 0, b = std::min(x.size(), (n + 3) * (size_t)hop);
    double pk = 0; for(size_t i = a; i < b; i++) pk = std::max(pk, std::fabs(x[i]));
    if(pk <= 0) return false;
    for(size_t i = a; i < b; i++) if(std::fabs(x[i]) > 0.01 * pk) return std::fabs(x[i]) < 0.1 * pk;
    return false;
}

double Median(std::vector<double> v)
{
    if(v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

} // namespace

int main(int argc, char** argv)
{
    if(argc < 2) { fprintf(stderr, "usage: earcheck a.wav [b.wav ...]\n"); return 2; }
    int bad = 0;
    for(int i = 1; i < argc; i++)
    {
        std::vector<float> x; int sr = 0;
        if(!kykdesk::ReadWav(argv[i], x, sr)) { printf("  %-40s cannot read\n", argv[i]); bad++; continue; }
        if(x.empty() || sr <= 0) { printf("  %-40s empty\n", argv[i]); bad++; continue; }
        const int hop = std::max(1, sr / 1000);
        std::vector<double> xd(x.begin(), x.end());
        const auto env = Env(xd, hop);
        double peak = 0; size_t rails = 0;
        for(float v : x) { peak = std::max(peak, (double)fabs(v)); if(fabs(v) >= 0.999f) rails++; }
        const auto hf = Sixth(x, true, 12000.0, sr);
        const auto mf = Sixth(x, true, 4000.0, sr, true, 12000.0);
        const auto henv = Env(hf, hop), menv = Env(mf, hop);
        int clicks = 0; std::string when;
        const int k = 100;
        for(size_t n = 0; n < henv.size(); n++)
        {
            if(henv[n] < pow(10.0, -50.0 / 20)) continue;
            std::vector<double> around;
            for(size_t j = n >= (size_t)k ? n - k : 0; j < n; j++) around.push_back(henv[j]);
            for(size_t j = n + 1; j < std::min(henv.size(), n + 1 + k); j++) around.push_back(henv[j]);
            if(around.empty() || henv[n] <= 10 * (Median(around) + 1e-9)) continue;
            double after = 1e9, before = 1e9;
            for(size_t j = n + 1; j < std::min(henv.size(), n + 4); j++) after = std::min(after, henv[j]);
            for(size_t j = n >= 3 ? n - 3 : 0; j < n; j++) before = std::min(before, henv[j]);
            const bool ends = after < 0.1 * henv[n] && (n == 0 || before < 0.1 * henv[n]);
            if(ends && henv[n] > 0.1 * menv[n] && !Onset(xd, env, n, hop)) { clicks++; if(clicks <= 6) { char b[32]; snprintf(b, sizeof b, "%s%.3fs", clicks > 1 ? "," : "", (double)n * hop / sr); when += b; } }
        }
        size_t quiet = 0; for(double e : env) if(e < 1e-4) quiet++;
        const double silence = env.empty() ? 1.0 : (double)quiet / env.size();
        double rms = 0; for(double v : xd) rms += v * v; rms = sqrt(rms / xd.size());
        std::string verdict;
        if(rails) verdict += " rail x" + std::to_string(rails);
        if(clicks) verdict += " " + std::to_string(clicks) + " click" + (clicks > 1 ? "s" : "") + " at " + when;
        if(silence > 0.5) verdict += " silence " + std::to_string((int)(100 * silence)) + "%";
        printf("  %-40s %5.1fs %6.1f dBFS  %s\n", argv[i], (double)x.size() / sr, 20 * log10(rms + 1e-9), verdict.empty() ? "ok" : ("FAIL:" + verdict).c_str());
        if(!verdict.empty()) bad++;
    }
    return bad ? 1 : 0;
}
