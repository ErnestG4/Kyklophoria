/* kyk_userworld.h — a world as a file, so people can curate and share their own.
 *
 * Every world in kyk_worlds.h is a function: to add one you edit C++ and
 * rebuild. That is fine for the twenty-one that ship and hopeless for anyone
 * who wants to place their own waveforms and hand the result to a friend.
 *
 * The observation this comes from is Will's, about the Erica GraphicVCO: its
 * character is that specific slots hold specific recognisable waves — a literal
 * organ here, a bell there, stacked squares and saws you hit hard at points in
 * a traversal. The instrument already does that. `Lock` is twenty-four spectra
 * at twenty-four positions, blended by distance, with the Morph knob narrowing
 * the basins until arriving at a vertex arrives at that exact waveform. The
 * only reason it cannot be curated is that its spectra come from a switch
 * statement rather than from data.
 *
 * So a user world is exactly that switch statement's output, written down:
 * positions and spectra, nothing else. It loads into the same LockField and
 * the same node buffer the built-in worlds use, and is evaluated by the same
 * code — no second rendering path, no second set of bugs, and `sharp`,
 * click-freedom and the continuity guarantee all come along unchanged.
 *
 * ── the format ──────────────────────────────────────────────────────────
 *
 *   0   u32  magic 'KYKW'
 *   4   u16  version (1, or 2 when it declares an effect)
 *   6   u8   n       dimensions, 2..kMaxN
 *   7   u8   k       harmonics, 1..kShapeK
 *   8   u8   count   nodes, 1..kWorldNodes
 *   9   u8   flags   bit0: sine phase (clear = random phase)
 *  10   f32  sigma   basin width before Morph narrows it
 *  14   u8   effect A: (axis << 4) | Shaper, zero for none   [v2]
 *  15   u8   effect B: the same                              [v2]
 *  16   char name[16]  nul-padded, not required to be nul-terminated
 *  32   per node: f32 pos[n], then f32 mags[k]
 *
 * Coefficients are f32 and signed, not the u8 log encoding telemetry uses.
 * That costs about 6.5 KB for a full twenty-four-node world instead of about
 * 2 KB, and buys three things worth more than 4 KB: it round-trips exactly, so
 * an export of an import is the same file; it carries sign directly, which the
 * sine-phase worlds need and a magnitude encoding would have needed a separate
 * bitmap for; and there is no quantisation to get subtly wrong at the quiet
 * end. QSPI flash on this board is 7936 KB and currently holds nothing, which
 * is room for over a thousand of these.
 *
 * ── effects, and why the version moves only when they are used ──────────
 *
 * Two of the frame shapers (core/kyk_shapes.h) can be put on axes of a world
 * you wrote: wavefolding, ring modulation, phase distortion, bit reduction,
 * rate reduction. They are the same shapers the Shapes and Grit worlds use and
 * they run in the same stage, so they cost a world nothing it was not already
 * paying and inherit the band-limit headroom rule for free.
 *
 * The two bytes they live in were the v1 "reserved (zero)", so the layout does
 * not move and no existing file changes meaning. The version still has to,
 * because a reader that ignored these bytes would play the world *differently*
 * rather than refuse it, and silently-different is the worse failure. So the
 * writer emits version 1 when there is no effect and 2 when there is: a file
 * an older module can render truthfully still loads there, and precisely the
 * files it would get wrong are the ones it turns away.
 *
 * An effect axis is not a special axis. The depth is that axis's folded
 * position and the nodes keep their coordinates on it; a page that wants an
 * axis to be nothing but an effect puts every node at 0.5 there, which drops
 * out of the softmax exactly. Nothing here enforces that, because "this axis
 * both moves the blend and opens the folder" is a legitimate world.
 *
 * ── parsing ─────────────────────────────────────────────────────────────
 *
 * This is the first thing in the instrument that reads data a stranger wrote.
 * Every field is range-checked before it is used to size or index anything,
 * the declared length must match the geometry exactly rather than merely be
 * large enough, and a blob that fails any check leaves the destination
 * untouched rather than half-written. Silence is the correct output for a
 * corrupt file; a hard fault is not.
 */
#pragma once
#include "kyk_lock.h"

namespace kyk {

constexpr uint32_t kUserMagic   = 0x574B594Bu;   /* 'KYKW' */
constexpr uint16_t kUserVersion    = 1u;   /* without effects */
constexpr uint16_t kUserVersionFx  = 2u;   /* with them */
constexpr int      kUserHeader  = 32;
constexpr int      kUserNameLen = 16;
/* The largest a world can be: every dimension, every harmonic, every node.
 * Staging buffers are sized from this so a transfer can never outrun them. */
constexpr size_t   kUserBlobMax = (size_t)kUserHeader
                                 + (size_t)kWorldNodes * 4u * (size_t)(kMaxN + kShapeK);

enum class UserError : uint8_t
{
    Ok = 0, TooShort, BadMagic, BadVersion, BadN, BadK, BadCount, BadSigma, BadLength, NotFinite,
    BadFx
};

inline const char* UserErrorName(UserError e)
{
    static const char* names[] = {"ok", "too short", "bad magic", "bad version", "bad N",
                                  "bad K", "bad count", "bad sigma", "length mismatch",
                                  "non-finite coefficient", "bad effect"};
    const int i = (int)e;
    return names[i >= 0 && i < 11 ? i : 0];
}

/* Exact size of a blob with this geometry. */
inline size_t UserBlobSize(int n, int k, int count)
{
    return (size_t)kUserHeader + (size_t)count * 4u * (size_t)(n + k);
}

namespace detail {

inline uint32_t UserU32(const uint8_t* b) { uint32_t v; std::memcpy(&v, b, 4); return v; }
inline uint16_t UserU16(const uint8_t* b) { uint16_t v; std::memcpy(&v, b, 2); return v; }
inline float    UserF32(const uint8_t* b) { float v;    std::memcpy(&v, b, 4); return v; }
/* No <cmath> in core's audio path, and this only needs to reject the two
 * things that would poison a blend: NaN fails every comparison with itself,
 * and an infinity exceeds any finite bound. */
inline bool UserFinite(float v) { return v == v && v < 1e30f && v > -1e30f; }

} // namespace detail

/* Read a blob into `f` and `node`. On any error neither is touched. */
inline UserError ParseUserWorld(const uint8_t* blob, size_t len,
                                LockField& f, float (*node)[kShapeK],
                                char* name_out = nullptr)
{
    if(!blob || len < (size_t)kUserHeader) return UserError::TooShort;
    if(detail::UserU32(blob) != kUserMagic) return UserError::BadMagic;
    const uint16_t ver = detail::UserU16(blob + 4);
    if(ver != kUserVersion && ver != kUserVersionFx) return UserError::BadVersion;

    const int n = blob[6], k = blob[7], count = blob[8];
    const uint8_t flags = blob[9];
    if(n < 2 || n > kMaxN) return UserError::BadN;
    if(k < 1 || k > kShapeK) return UserError::BadK;
    if(count < 1 || count > kWorldNodes) return UserError::BadCount;

    const float sigma = detail::UserF32(blob + 10);
    if(!detail::UserFinite(sigma) || sigma < 0.01f || sigma > 4.f) return UserError::BadSigma;

    /* Effects. A v1 file has nothing here and must say so: those two bytes were
     * documented as reserved and zero, so junk in them is either a v2 file with
     * the wrong version or a writer nobody should trust with the rest of the
     * header. Each byte is (axis << 4) | shaper; the axis must be one this
     * world has, and the two effects must not name the same axis — one knob
     * driving two shapers is representable and ambiguous, and refusing it here
     * is cheaper than every reader guessing which order they chain in. */
    Shaper  fx[2]      = {Shaper::None, Shaper::None};
    uint8_t fx_axis[2] = {0, 0};
    if(ver == kUserVersion)
    {
        if(blob[14] || blob[15]) return UserError::BadFx;
    }
    else
    {
        for(int q = 0; q < 2; q++)
        {
            const uint8_t byte = blob[14 + q];
            const uint8_t kind = byte & 0x0Fu, axis = byte >> 4;
            if(kind == 0u) { if(axis) return UserError::BadFx; continue; }
            if(kind > (uint8_t)Shaper::Drop) return UserError::BadFx;
            if(axis >= (uint8_t)n) return UserError::BadFx;
            fx[q]      = (Shaper)kind;
            fx_axis[q] = axis;
        }
        if(fx[0] != Shaper::None && fx[1] != Shaper::None && fx_axis[0] == fx_axis[1])
            return UserError::BadFx;
        /* A version that claims effects and declares none is a writer bug, and
         * accepting it would make "version 2" mean nothing. */
        if(fx[0] == Shaper::None && fx[1] == Shaper::None) return UserError::BadFx;
    }

    /* Exactly, not at least: a blob whose length disagrees with its own header
     * is not a blob we understand, whichever way the disagreement runs. */
    if(len != UserBlobSize(n, k, count)) return UserError::BadLength;

    /* Validate every coefficient before writing anything, so a bad file cannot
     * leave a half-loaded world behind. */
    const uint8_t* p = blob + kUserHeader;
    for(int v = 0; v < count; v++)
        for(int i = 0; i < n + k; i++)
            if(!detail::UserFinite(detail::UserF32(p + 4 * (v * (n + k) + i))))
                return UserError::NotFinite;

    f = LockField();
    f.n = n; f.k = k; f.count = count; f.sigma = sigma;
    for(int q = 0; q < 2; q++) { f.fx[q] = fx[q]; f.fx_axis[q] = fx_axis[q]; }
    for(int v = 0; v < count; v++)
    {
        const uint8_t* q = p + 4 * (size_t)v * (size_t)(n + k);
        for(int a = 0; a < kMaxN; a++)
            f.pos[v][a] = a < n ? detail::UserF32(q + 4 * a) : 0.5f;
        for(int i = 0; i < kShapeK; i++)
            node[v][i] = i < k ? detail::UserF32(q + 4 * (n + i)) : 0.f;
        /* Coefficients are used exactly as written, and deliberately so.
         *
         * An earlier draft normalised each node to unit energy on the way in,
         * the way the built-in Lock waveforms are built. Two things are wrong
         * with that. It is not idempotent — scaling by a computed
         * sqrt(2)/sqrt(e) that lands a hair off 1.0 moves every coefficient by
         * an ulp, so a world exported and re-imported is not the world you
         * exported, and an author who edits one waveform finds the other
         * twenty-three have drifted. And it overrides the author: the engine
         * normalises the finished spectrum regardless, so per-node level is
         * not loudness, it is how much each node weighs in the blend. Someone
         * placing a quiet bell against a loud saw means that.
         *
         * The cost is that a badly scaled file sounds badly balanced, which is
         * an author's problem and audible as such, rather than a fault. */
    }
    if(name_out)
    {
        for(int i = 0; i < kUserNameLen; i++) name_out[i] = (char)blob[16 + i];
        name_out[kUserNameLen] = '\0';
    }
    (void)flags;   /* phase convention is applied by the caller via World */
    return UserError::Ok;
}

/* Write `f`/`node` out. Returns the length used, or 0 if `cap` is too small. */
inline size_t WriteUserWorld(const LockField& f, const float (*node)[kShapeK],
                             const char* name, uint8_t* out, size_t cap, bool sine_phase = true)
{
    const size_t need = UserBlobSize(f.n, f.k, f.count);
    if(!out || cap < need) return 0;
    std::memset(out, 0, need);
    const uint32_t magic = kUserMagic; std::memcpy(out, &magic, 4);
    /* Version 2 only when there is an effect to declare, so a world without one
     * still loads on a module that predates them. */
    const bool     fx  = f.fx[0] != Shaper::None || f.fx[1] != Shaper::None;
    const uint16_t ver = fx ? kUserVersionFx : kUserVersion;
    std::memcpy(out + 4, &ver, 2);
    out[6] = (uint8_t)f.n; out[7] = (uint8_t)f.k; out[8] = (uint8_t)f.count;
    out[9] = sine_phase ? 1u : 0u;
    std::memcpy(out + 10, &f.sigma, 4);
    for(int q = 0; q < 2; q++)
        out[14 + q] = f.fx[q] == Shaper::None
                          ? 0u
                          : (uint8_t)((uint8_t)(f.fx_axis[q] << 4) | (uint8_t)f.fx[q]);
    for(int i = 0; i < kUserNameLen && name && name[i]; i++) out[16 + i] = (uint8_t)name[i];
    uint8_t* p = out + kUserHeader;
    for(int v = 0; v < f.count; v++)
    {
        uint8_t* q = p + 4 * (size_t)v * (size_t)(f.n + f.k);
        for(int a = 0; a < f.n; a++) std::memcpy(q + 4 * a, &f.pos[v][a], 4);
        for(int i = 0; i < f.k; i++) std::memcpy(q + 4 * (f.n + i), &node[v][i], 4);
    }
    return need;
}

} // namespace kyk
