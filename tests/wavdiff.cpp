/* wavdiff a.wav b.wav [tolerance] — compare two mono WAVs sample by sample.
 * Exit 0 when lengths match and max |a-b| ≤ tolerance (default 1e-6). */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "../shell/desktop/wavio.h"

int main(int argc, char** argv)
{
    if(argc < 3) { fprintf(stderr, "usage: wavdiff a.wav b.wav [tol]\n"); return 2; }
    const double tol = argc > 3 ? atof(argv[3]) : 1e-6;
    std::vector<float> a, b;
    int sa = 0, sb = 0;
    if(!kykdesk::ReadWav(argv[1], a, sa)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    if(!kykdesk::ReadWav(argv[2], b, sb)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    if(a.size() != b.size() || sa != sb)
    {
        printf("%s vs %s: length %zu@%d vs %zu@%d — DIFFERENT\n", argv[1], argv[2], a.size(), sa, b.size(), sb);
        return 1;
    }
    double maxd = 0, sum = 0;
    size_t at = 0, exact = 0;
    for(size_t i = 0; i < a.size(); i++)
    {
        const double d = std::fabs((double)a[i] - b[i]);
        if(d == 0) exact++;
        if(d > maxd) { maxd = d; at = i; }
        sum += d * d;
    }
    const double rms = std::sqrt(sum / (double)(a.size() ? a.size() : 1));
    printf("%s vs %s: %zu samples, max diff %.3g at %zu, rms diff %.3g, %zu identical (%.1f%%) — %s\n", argv[1], argv[2],
           a.size(), maxd, at, rms, exact, 100.0 * exact / (double)(a.size() ? a.size() : 1), maxd <= tol ? "ok" : "OVER TOLERANCE");
    return maxd <= tol ? 0 : 1;
}
