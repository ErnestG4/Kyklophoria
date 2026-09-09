/* corpus.h — loading and analysing a corpus of single cycles.
 *
 * Shared by tools/kykeigen (which bakes a corpus into a space) and
 * tools/kykspace (which measures how well a space covers one). Offline only:
 * it uses libm and allocates, neither of which core/ is allowed to do.
 */
#pragma once
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <dirent.h>
#include "kyk_space.h"
#include "../shell/desktop/wavio.h"

namespace kykcorpus {
using namespace kyk;

/* ── corpus loading ─────────────────────────────────────────────────────── */

/* Pull a named uint8_t array out of a Mutable resources.cc and split it into
 * fixed-length cycles. Braids ships 256 waves of 129 samples (128 plus a wrap
 * sample), 8-bit, which is 64 harmonics — exactly our K. */
static bool LoadBraids(const std::string& path, std::vector<std::vector<float>>& out, int& nwaves)
{
    std::ifstream in(path);
    if(!in) return false;
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t at = all.find("wt_waves[]");
    if(at == std::string::npos) return false;
    const size_t open = all.find('{', at), close = all.find("};", open);
    if(open == std::string::npos || close == std::string::npos) return false;
    std::vector<int> v;
    const std::string body = all.substr(open + 1, close - open - 1);
    const char*       p    = body.c_str();
    while(*p)
    {
        while(*p && !isdigit((unsigned char)*p) && *p != '-') p++;
        if(!*p) break;
        char* end;
        const long x = strtol(p, &end, 10);
        if(end == p) break;
        v.push_back((int)x);
        p = end;
    }
    const int kCycle = 129;                 /* 128 useful + 1 wrap */
    nwaves           = (int)(v.size() / kCycle);
    if(nwaves < 1) return false;
    for(int w = 0; w < nwaves; w++)
    {
        std::vector<float> cyc(kCycle - 1);
        for(int n = 0; n < kCycle - 1; n++) cyc[n] = ((float)v[w * kCycle + n] - 127.5f) / 127.5f;
        out.push_back(std::move(cyc));
    }
    return true;
}

/* Every .wav under a directory. A file that is a whole multiple of a common
 * wavetable frame length is split into frames; anything else is one cycle. */
static void LoadWavDir(const std::string& dir, std::vector<std::vector<float>>& out, int& nfiles)
{
    DIR* d = opendir(dir.c_str());
    if(!d) return;
    std::vector<std::string> names;
    while(dirent* e = readdir(d))
    {
        const std::string n = e->d_name;
        if(n.size() > 4 && (n.substr(n.size() - 4) == ".wav" || n.substr(n.size() - 4) == ".WAV"))
            names.push_back(dir + "/" + n);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for(const auto& f : names)
    {
        std::vector<float> x;
        int                sr = 0;
        if(!kykdesk::ReadWav(f, x, sr) || x.empty()) continue;
        nfiles++;
        int frame = 0;
        for(int cand : {2048, 1024, 600, 512, 256})
            if((int)x.size() % cand == 0 && (int)x.size() >= cand) { frame = cand; break; }
        if(frame == 0) out.push_back(x);
        else
            for(size_t o = 0; o + (size_t)frame <= x.size(); o += (size_t)frame)
                out.push_back(std::vector<float>(x.begin() + (long)o, x.begin() + (long)(o + frame)));
    }
}

/* ── analysis ───────────────────────────────────────────────────────────── */

/* One cycle of arbitrary length → K harmonic magnitudes, unit RMS (Σm² = 2,
 * the convention the generators and the engine share). */
static void Analyse(const std::vector<float>& cyc, int K, std::vector<double>& mags)
{
    const int L = (int)cyc.size();
    mags.assign((size_t)K, 0.0);
    for(int k = 1; k <= K; k++)
    {
        double re = 0, im = 0;
        for(int n = 0; n < L; n++)
        {
            const double a = 2.0 * M_PI * k * n / L;
            re += cyc[n] * std::cos(a);
            im -= cyc[n] * std::sin(a);
        }
        mags[k - 1] = 2.0 * std::sqrt(re * re + im * im) / L;
    }
    double e = 0;
    for(int k = 0; k < K; k++) e += mags[k] * mags[k];
    if(e <= 0) return;
    const double g = std::sqrt(2.0 / e);
    for(int k = 0; k < K; k++) mags[k] *= g;
}


} // namespace kykcorpus
