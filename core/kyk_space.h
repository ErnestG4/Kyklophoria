/* kyk_space.h — a read-only view over a space blob (the *.kyk bytes in RAM).
 *
 * The blob is owned by the shell (SDRAM on the module, a std::vector on the
 * desktop); the core never allocates. Attach() validates the header against
 * the compile-time limits and the byte count, then every accessor is a
 * pointer into the blob. docs/space-format.md is the wire description.
 */
#pragma once
#include "kyk_types.h"

namespace kyk {

enum class SpaceError : uint8_t
{
    Ok = 0, TooShort, BadMagic, BadVersion, BadN, BadK, BadP, BadSide, BadMode,
    BadTopo, BadCount, BadSize
};

class Space
{
public:
    Space() { std::memset(&hdr_, 0, sizeof(hdr_)); }

    /* Validate and adopt. Header bytes are copied (alignment-safe); point
     * data is referenced in place and must outlive this object. */
    SpaceError Attach(const uint8_t* blob, size_t len)
    {
        data_ = nullptr;
        if(len < sizeof(SpaceHeader)) return SpaceError::TooShort;
        std::memcpy(&hdr_, blob, sizeof(SpaceHeader));
        const SpaceHeader& h = hdr_;
        if(h.magic != kSpaceMagic) return SpaceError::BadMagic;
        if(h.version != kSpaceVersion) return SpaceError::BadVersion;
        if(h.n < 1 || h.n > kMaxN) return SpaceError::BadN;
        if(h.k < 1 || h.k > kMaxK) return SpaceError::BadK;
        if(h.p > kMaxP) return SpaceError::BadP;
        if(h.mode != kModeLattice) return SpaceError::BadMode;   /* scattered: M5 */
        if(h.side < 2) return SpaceError::BadSide;
        for(int a = 0; a < h.n; a++)
            if(h.topo[a] > (uint8_t)Topo::Sphere) return SpaceError::BadTopo;
        uint32_t want = 1;
        for(int a = 0; a < h.n; a++)
        {
            if(want > 0xFFFFFFFFu / h.side) return SpaceError::BadCount;
            want *= h.side;
        }
        if(h.point_count != want) return SpaceError::BadCount;
        const size_t need = sizeof(SpaceHeader) + (size_t)h.point_count * Stride() * sizeof(float);
        if(len < need) return SpaceError::BadSize;
        data_ = reinterpret_cast<const float*>(blob + sizeof(SpaceHeader));
        return SpaceError::Ok;
    }
    bool Attached() const { return data_ != nullptr; }

    const SpaceHeader& Header() const { return hdr_; }
    int      N() const { return hdr_.n; }
    int      K() const { return hdr_.k; }
    int      P() const { return hdr_.p; }
    int      Side() const { return hdr_.side; }
    bool     HasPhases() const { return (hdr_.flags & kFlagPhases) != 0; }
    uint32_t PointCount() const { return hdr_.point_count; }
    Topo     TopoOf(int axis) const { return (Topo)hdr_.topo[axis]; }

    /* floats per hyperpoint */
    size_t Stride() const { return (size_t)hdr_.k * (HasPhases() ? 2u : 1u) + hdr_.p; }

    const float* Mags(uint32_t idx) const { return data_ + (size_t)idx * Stride(); }
    const float* Phases(uint32_t idx) const { return HasPhases() ? Mags(idx) + hdr_.k : nullptr; }
    const float* Payload(uint32_t idx) const
    {
        return Mags(idx) + (size_t)hdr_.k * (HasPhases() ? 2u : 1u);
    }

    /* Lattice index from per-axis integer coordinates, axis 0 fastest. */
    uint32_t LatticeIndex(const int* ix) const
    {
        uint32_t idx = 0, mul = 1;
        for(int a = 0; a < hdr_.n; a++)
        {
            idx += (uint32_t)ix[a] * mul;
            mul *= hdr_.side;
        }
        return idx;
    }

    /* Byte size a lattice blob of these dimensions needs (for builders). */
    static constexpr size_t BlobSize(int n, int k, int p, int side, bool phases)
    {
        size_t count = 1;
        for(int a = 0; a < n; a++) count *= (size_t)side;
        return sizeof(SpaceHeader) + count * ((size_t)k * (phases ? 2u : 1u) + (size_t)p) * sizeof(float);
    }

    static const char* ErrorName(SpaceError e)
    {
        static const char* names[] = {"ok", "too short", "bad magic", "bad version", "bad N", "bad K",
                                      "bad P", "bad side", "bad mode", "bad topology", "bad point count",
                                      "blob smaller than header claims"};
        return names[(int)e];
    }

private:
    SpaceHeader  hdr_;
    const float* data_ = nullptr;
};

} // namespace kyk
