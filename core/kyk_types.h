/* kyk_types.h — constants, the space header, and the small shared helpers.
 *
 * Pure header-only core (no libDaisy, no heap, no exceptions, no RTTI).
 * Every module in core/ builds on the desktop and on the Daisy with the same
 * bytes; see docs/spec.md §2 for the determinism rules.
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace kyk {

/* ── Compile-time limits ─────────────────────────────────────────────────
 * kMaxN   dimensions of the space (positions, rotation planes = N(N-1)/2)
 * kMaxK   harmonics per hyperpoint (1..K; there is no DC bin)
 * kMaxP   payload lanes per hyperpoint
 * kFrame  samples per rendered single-cycle frame. K ≤ kFrame/4 keeps at
 *         least four table samples per cycle of the top harmonic (the read
 *         interpolator's images are the aliasing story — tests/alias_check).
 */
constexpr int kMaxN = 6;
#ifndef KYK_MAX_K
#define KYK_MAX_K 128
#endif
constexpr int kMaxK = KYK_MAX_K;
constexpr int kMaxP = 8;
#ifndef KYK_FRAME
#define KYK_FRAME 1024
#endif
constexpr int kFrame     = KYK_FRAME;
constexpr int kFrameLog2 = (kFrame == 256) ? 8 : (kFrame == 512) ? 9 : (kFrame == 1024) ? 10 : (kFrame == 2048) ? 11 : 0;
static_assert(kFrameLog2 != 0, "KYK_FRAME must be 256, 512, 1024 or 2048");
static_assert(kMaxK * 4 <= kFrame, "K must leave >= 4 table samples per cycle of the top harmonic");
constexpr int kMaxCorners = 1 << kMaxN;          /* multilinear: 2^N corner reads */
constexpr int kMaxPlanes  = kMaxN * (kMaxN - 1) / 2;

/* ── Topology, per axis ─────────────────────────────────────────────────── */
enum class Topo : uint8_t { Clamp = 0, Wrap = 1, Sphere = 2 };

/* ── Space file header (docs/space-format.md) ───────────────────────────
 * 64 bytes, little-endian, followed by point_count hyperpoints, each
 * K mags [+ K phases when kFlagPhases] + P payload floats (float32).
 * Lattice mode: point_count == side^N, axis 0 fastest. */
constexpr uint32_t kSpaceMagic   = 0x314B594Bu;   /* "KYK1" */
constexpr uint16_t kSpaceVersion = 1;
constexpr uint8_t  kModeLattice  = 0;
constexpr uint8_t  kModeScattered = 1;             /* reserved: M5 */
constexpr uint8_t  kFlagPhases   = 0x01;           /* per-point phases stored */

struct SpaceHeader
{
    uint32_t magic;          /* kSpaceMagic */
    uint16_t version;        /* kSpaceVersion */
    uint8_t  n;              /* dimensions, 1..kMaxN */
    uint8_t  mode;           /* kModeLattice | kModeScattered */
    uint8_t  k;              /* harmonics per point, 1..kMaxK */
    uint8_t  p;              /* payload lanes, 0..kMaxP */
    uint8_t  side;           /* lattice points per axis (lattice mode), ≥ 2 */
    uint8_t  flags;          /* kFlag* */
    uint8_t  topo[kMaxN];    /* Topo per axis (axes ≥ n ignored) */
    uint8_t  pad0[2];
    uint32_t phase_seed;     /* derived phases when !kFlagPhases */
    uint32_t point_count;
    char     name[32];       /* NUL-padded, not necessarily terminated */
    uint32_t reserved;       /* 0; M5 will point at the triangulation */
};
static_assert(sizeof(SpaceHeader) == 64, "SpaceHeader must stay 64 bytes");

/* ── xorshift32 (same family as bb_rng.h; state never 0) ───────────────── */
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void     Seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t Next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    /* [0, 1) with 24 significant bits — exact in float, identical everywhere */
    float Uniform() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); }
};

/* ── Small deterministic helpers ─────────────────────────────────────────── */
inline float Clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
inline float Fract(float x)
{
    /* x - floor(x) without libm; exact for |x| < 2^23 */
    const int32_t i = (int32_t)x - (x < 0.f && x != (float)(int32_t)x ? 1 : 0);
    return x - (float)i;
}
inline uint32_t Crc32(const uint8_t* p, size_t n, uint32_t seed = 0)
{
    /* Reflected 0xEDB88320, the HostLink/preset CRC. Bytewise, no table. */
    uint32_t c = ~seed;
    for(size_t i = 0; i < n; i++)
    {
        c ^= p[i];
        for(int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

} // namespace kyk
