/* wavio.h — desktop-only WAV read/write (float32 mono). Not part of core/. */
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace kykdesk {

inline void Put32(FILE* f, uint32_t v) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; fwrite(b, 1, 4, f); }
inline void Put16(FILE* f, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; fwrite(b, 1, 2, f); }

inline bool WriteWavFloat(const std::string& path, const float* x, size_t n, int sr, int ch = 1)
{
    FILE* f = fopen(path.c_str(), "wb");
    if(!f) return false;
    const uint32_t data = (uint32_t)(n * 4);
    fwrite("RIFF", 1, 4, f); Put32(f, 36 + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); Put32(f, 16); Put16(f, 3 /* IEEE float */); Put16(f, (uint16_t)ch);
    Put32(f, (uint32_t)sr); Put32(f, (uint32_t)sr * 4 * (uint32_t)ch); Put16(f, (uint16_t)(4 * ch)); Put16(f, 32);
    fwrite("data", 1, 4, f); Put32(f, data);
    for(size_t i = 0; i < n; i++) { uint32_t u; std::memcpy(&u, &x[i], 4); Put32(f, u); }
    fclose(f);
    return true;
}

/* Reads float32 or PCM16/24; all channels interleaved as stored. */
inline bool ReadWav(const std::string& path, std::vector<float>& out, int& sr)
{
    FILE* f = fopen(path.c_str(), "rb");
    if(!f) return false;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    size_t  r;
    while((r = fread(buf, 1, sizeof(buf), f)) > 0) b.insert(b.end(), buf, buf + r);
    fclose(f);
    if(b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) return false;
    auto u32 = [&](size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; };
    auto u16 = [&](size_t o) { return (uint16_t)(b[o] | b[o + 1] << 8); };
    size_t   o = 12;
    int      fmt = 0, ch = 1, bits = 0;
    while(o + 8 <= b.size())
    {
        const uint32_t len = u32(o + 4);
        if(!std::memcmp(&b[o], "fmt ", 4)) { fmt = u16(o + 8); ch = u16(o + 10); sr = (int)u32(o + 12); bits = u16(o + 22); }
        else if(!std::memcmp(&b[o], "data", 4))
        {
            const size_t start = o + 8, end = std::min(b.size(), start + (size_t)len);
            const int    bps   = bits / 8;
            if(bps == 0 || ch == 0) return false;
            out.clear();
            for(size_t p = start; p + (size_t)bps <= end; p += (size_t)bps)
            {
                if(fmt == 3 && bits == 32) { float v; uint32_t u = u32(p); std::memcpy(&v, &u, 4); out.push_back(v); }
                else if(fmt == 1 && bits == 16) out.push_back((float)(int16_t)u16(p) / 32768.f);
                else if(fmt == 1 && bits == 24) { int32_t v = (int32_t)((uint32_t)b[p] << 8 | (uint32_t)b[p + 1] << 16 | (uint32_t)b[p + 2] << 24) >> 8; out.push_back((float)v / 8388608.f); }
                else return false;
            }
            return true;
        }
        o += 8 + len + (len & 1);
    }
    return false;
}

} // namespace kykdesk
