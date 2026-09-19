/* corpus.h — the corpus file, read into doubles.
 *
 * Layout is documented in tools/pack.py, which writes it. This reads exactly
 * that and refuses anything else: a file whose declared sizes disagree with its
 * length is not a corpus we understand, whichever way the disagreement runs.
 */
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mb {

struct Model
{
    std::string id;
    int         family = 0;      /* see FamilyName */
    bool        fitted = false;  /* one recording: flat gain rows, no shape */
    double      param  = 0.0;
    int         nreal  = 0;
    std::vector<double> hz;      /* N */
    std::vector<double> zeta;    /* N */
    std::vector<double> g;       /* N*P, mode-major */
    double G(int i, int p, int P) const { return g[(size_t)i * P + p]; }
};

struct Corpus
{
    int N = 0, P = 0;
    std::vector<Model>       m;
    std::vector<std::string> families;   /* the name table from the file */
    int         Families() const { return (int)families.size(); }
    const char* FamilyName(int f) const { return f >= 0 && f < (int)families.size() ? families[f].c_str() : "?"; }
    int         FamilyIndex(const std::string& name) const
    {
        for(size_t i = 0; i < families.size(); i++) if(families[i] == name) return (int)i;
        return -1;
    }
};

inline uint32_t U32(const uint8_t* b) { uint32_t v; std::memcpy(&v, b, 4); return v; }
inline float    F32(const uint8_t* b) { float v; std::memcpy(&v, b, 4); return v; }

inline bool ReadCorpus(const std::string& path, Corpus& c)
{
    FILE* f = fopen(path.c_str(), "rb");
    if(!f) return false;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    size_t  n;
    while((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    if(b.size() < 20 || std::memcmp(b.data(), "MODB", 4) != 0 || U32(b.data() + 4) != 2u) return false;
    const uint32_t M = U32(b.data() + 8), N = U32(b.data() + 12), P = U32(b.data() + 16);
    /* version 2: a family name table between the header and the models */
    if(b.size() < 24) return false;
    const uint32_t F = U32(b.data() + 20);
    const size_t per = 16 + 4 + 4 + 4 + 4 * (size_t)N * 2 + 4 * (size_t)N * P;
    if(b.size() != 24 + 16 * (size_t)F + per * M) return false;
    c.N = (int)N; c.P = (int)P;
    c.families.clear();
    for(uint32_t f = 0; f < F; f++)
    {
        char nm[17] = {0};
        std::memcpy(nm, b.data() + 24 + 16 * f, 16);
        c.families.push_back(nm);
    }
    c.m.clear();
    const uint8_t* p = b.data() + 24 + 16 * F;
    for(uint32_t k = 0; k < M; k++)
    {
        Model md;
        char id[17] = {0};
        std::memcpy(id, p, 16);
        md.id     = id;
        md.family = p[16];
        md.fitted = (p[17] & 1u) != 0;
        md.param  = F32(p + 20);
        md.nreal  = (int)U32(p + 24);
        p += 28;
        md.hz.resize(N); md.zeta.resize(N); md.g.resize((size_t)N * P);
        for(uint32_t i = 0; i < N; i++) { md.hz[i] = F32(p); p += 4; }
        for(uint32_t i = 0; i < N; i++) { md.zeta[i] = F32(p); p += 4; }
        for(uint32_t i = 0; i < N * P; i++) { md.g[i] = F32(p); p += 4; }
        c.m.push_back(std::move(md));
    }
    return true;
}

} // namespace mb
