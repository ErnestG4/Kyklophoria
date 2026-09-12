/* kyk_ext.h — the HostLink extension shared by both shells (docs/hostlink.md).
 *
 * Commands 0x60–0x6F. The shell supplies an ExtSource: on the module that
 * copies from the snapshot the audio callback publishes, on the desktop it
 * encodes straight from the engine. Everything here runs in the control
 * loop (module) or the serve loop (desktop), never in the audio callback.
 *
 * Uses only the SDK's header-only wire layer (frame.h / extension.h), so the
 * desktop shell compiles it without any SDK .cpp.
 */
#pragma once
#include <cstdint>
#include <cstring>
#include "alchemy/host_link/extension.h"
#include "alchemy/host_link/frame.h"
#include "kyk_types.h"

namespace kyk {

/* One version string for both shells.
 *
 * It lived in two places, hardcoded, and had already drifted: the module said
 * 0.3.0 while the desktop bridge introduced itself as 0.2.0 to the same page.
 * Bump it when the panel or the sound changes in a way a player would notice.
 *   0.2.0  stereo pair, rotation, the orbit page, field and eigen spaces, the
 *          real-valued transform, level headroom, morph sharpness
 *   0.3.0  Kepler and Couple pages, gravity that wraps when the space does,
 *          Kuramoto coupling on the orbit ratios, the FM and vowel worlds, a
 *          Torus world, six decibels of headroom in the magnitude byte */
#define KYK_FW_VERSION "0.3.0"

constexpr uint8_t kCmdTelemetry  = 0x60;
constexpr uint8_t kCmdSpaceInfo  = 0x61;
constexpr uint8_t kCmdCell       = 0x62;
constexpr uint8_t kCmdStats      = 0x63;
constexpr uint8_t kCmdAction     = 0x64;
constexpr uint8_t kCmdWorlds     = 0x65;   /* the list, and which one is live */
constexpr uint8_t kCmdBasis      = 0x66;   /* an analytic world's formula, chunked */
constexpr uint8_t kCmdPutWorld   = 0x67;   /* a user world, chunked host to module */
constexpr uint8_t kCmdCardWorlds = 0x68;   /* what .kykw files the card holds */
constexpr uint8_t kCmdSetControl = 0x6E;   /* desktop bridge only */

enum ActionOp : uint8_t { kActResetPhase = 0, kActNextSpace = 1, kActLoadSpace = 2, kActRenderDiv = 3,
                          kActSelectWorld = 4,
                          /* which world the Morph knob blends towards; 0xFF
                             clears it. Choosing is setup and lives on the
                             page; how far is a knob and lives on the panel. */
                          kActMorphWorld = 5,
                          kActScanCard = 6,        /* re-read the card's world folder */
                          kActLoadCardWorld = 7,   /* load one by index into the list */
                          /* 0 = the world's own convention, 1 = force sine,
                             2 = force cosine. A cosine twin of any world
                             without doubling the world list. */
                          kActPhase = 8,
                          /* u16 mask: bits 0..14 mute a rotation plane's rate,
                             bit 15 mutes Kepler. A mute rather than a zero, so
                             the knob keeps its value and unmuting restores it —
                             which is the whole difference between a switch and
                             turning something down. */
                          kActMotionMute = 9,
                          /* search the morph world for the position whose
                             spectrum is nearest the one playing, and read
                             there — so a morph goes somewhere, rather than
                             towards whatever the same coordinates collide
                             with in a world that means something else by
                             them. */
                          kActAimMorph = 10 };
constexpr uint16_t kMuteKepler = 0x8000u;

struct ExtStats
{
    uint32_t cycles_last = 0, cycles_max = 0, cycles_avg = 0;
    uint16_t overruns = 0, dropped = 0;
    uint8_t  render_div = 1;
    uint32_t cycles_budget = 0;   /* cycles per block at the audio rate */
};

struct ExtSource
{
    virtual ~ExtSource() = default;
    /* Telemetry body after the status byte; 0 = nothing available yet. */
    virtual int  Telemetry(uint8_t flags, uint8_t* out, int cap) = 0;
    /* The attached space's header, blob CRC and point stride (floats). */
    virtual bool SpaceInfo(SpaceHeader& h, uint32_t& crc, uint16_t& stride) = 0;
    /* One hyperpoint: k dB-scaled magnitudes + p payload floats. */
    virtual bool Cell(uint32_t idx, uint8_t* mags, float* payload, int& k, int& p) = 0;
    virtual void Stats(ExtStats& s) = 0;
    /* Returns a protocol status (0 ok, 2 bad args, 3 bad state, 1 unsupported). */
    virtual uint8_t Action(uint8_t op, const uint8_t* args, int len) = 0;
    /* The world list: how many, which is live, and a name and note each.
     * `current` may be 0xFF, meaning what is playing did not come from this
     * list — a space loaded from a file, say. A host should mark nothing
     * rather than guess. */
    virtual int Worlds(uint8_t& count, uint8_t& current, const char** names, const char** notes,
                       uint8_t* kinds, int max)
    {
        (void)names; (void)notes; (void)kinds; (void)max;
        count = 0; current = 0;
        return 0;
    }
    /* An analytic world's formula, as raw bytes the host can evaluate itself:
     * u8 n, u8 k, f32 extent, f32 floor, f32 mean[k], f32 comp[n][k]. Returns
     * the total length; `out` receives up to `max` bytes from `offset`. */
    virtual int Basis(uint8_t world, uint32_t offset, uint8_t* out, int max, uint32_t& total)
    {
        (void)world; (void)offset; (void)out; (void)max; total = 0;
        return 0;
    }

    /* Desktop: set f0, control frame, angles and spread. Module: unsupported. */
    /* Receive one chunk of a user world.
     *
     * Offsets must arrive in order and start at zero: random access would mean
     * trusting a stranger's offsets to index a buffer, and in-order costs the
     * host nothing since it is sending a file it already has. The source
     * accumulates, and on the last chunk parses, validates and loads — so a
     * transfer that stops half way leaves the running world untouched. */
    /* Names of the worlds on the card. Returns how many were written into
     * `names`; a shell with no card returns zero, which is not an error. */
    virtual int CardWorlds(const char** names, int max) { (void)names; (void)max; return 0; }

    virtual uint8_t PutWorld(uint32_t total, uint32_t off, const uint8_t* data, int len)
    { (void)total; (void)off; (void)data; (void)len; return 1u; }

    virtual uint8_t SetControl(float f0, const float* c, int n, const float* angles, int planes, float spread)
    {
        (void)f0; (void)c; (void)n; (void)angles; (void)planes; (void)spread;
        return 1u;
    }
};

class KykExt : public alchemy::hostlink::IHostlinkExtension
{
public:
    explicit KykExt(ExtSource& src) : src_(src) {}

    uint8_t     FirstCmd() const override { return 0x60u; }
    uint8_t     LastCmd() const override { return 0x6Fu; }
    const char* DescriptorRootJson() const override
    {
        return "\"kyk\":{\"ext\":5,\"telemetry\":96,\"space\":97,\"cell\":98,\"stats\":99,\"action\":100,"
               "\"worlds\":101,\"basis\":102,\"control\":110}";
    }

    void Handle(const alchemy::hostlink::ParsedFrame& f, alchemy::hostlink::FrameWriter& w, uint32_t) override
    {
        switch(f.type)
        {
            case kCmdTelemetry:
            {
                const uint8_t flags = f.len >= 1 ? f.body[0] : 0u;
                static uint8_t buf[alchemy::hostlink::kMaxBody];
                const int n = src_.Telemetry(flags, buf, (int)sizeof(buf) - 1);
                if(n <= 0) { w.U8(3u); return; }   /* BAD_STATE: no snapshot yet */
                w.U8(0u);
                w.Bytes(buf, (size_t)n);
                return;
            }
            case kCmdSpaceInfo:
            {
                SpaceHeader h;
                uint32_t    crc;
                uint16_t    stride;
                if(!src_.SpaceInfo(h, crc, stride)) { w.U8(3u); return; }
                w.U8(0u);
                w.Bytes(reinterpret_cast<const uint8_t*>(&h), sizeof(h));
                w.U32(crc);
                w.U16(stride);
                return;
            }
            case kCmdCell:
            {
                if(f.len < 4) { w.U8(2u); return; }
                uint32_t idx;
                std::memcpy(&idx, f.body, 4);
                uint8_t mags[kMaxK];
                float   pl[kMaxP];
                int     k, p;
                if(!src_.Cell(idx, mags, pl, k, p)) { w.U8(2u); return; }
                w.U8(0u);
                w.U8((uint8_t)k);
                w.U8((uint8_t)p);
                w.Bytes(mags, (size_t)k);
                for(int j = 0; j < p; j++) { uint32_t u; std::memcpy(&u, &pl[j], 4); w.U32(u); }
                return;
            }
            case kCmdWorlds:
            {
                /* Paged, because the list outgrew a frame.
                 *
                 * At eighteen worlds the names and notes came to about 1160
                 * bytes against a 1024-byte body. FrameWriter refuses to
                 * overflow, Encode then returns zero, and the reply is simply
                 * never sent — so the host sat waiting for a frame that would
                 * never come. Nothing reported an error anywhere; it just
                 * hung. The list only grows, so it is paged rather than
                 * merely made to fit.
                 *
                 * The request's start index is optional: an empty body means
                 * zero, which is what every host sent before this existed. */
                constexpr int kCap = 32;
                const char* names[kCap];
                const char* notes[kCap];
                uint8_t     kinds[kCap];
                uint8_t     count = 0, current = 0;
                src_.Worlds(count, current, names, notes, kinds, kCap);
                if(count == 0) { w.U8(1u); return; }
                const uint8_t start = f.len >= 1 ? f.body[0] : 0u;
                if(start >= count) { w.U8(2u); return; }

                w.U8(0u);
                w.U8(count);
                w.U8(current);
                w.U8(start);
                const int sent_at = 0;   /* patched below via a second pass */
                (void)sent_at;
                /* Count how many fit before writing any of them, so the
                 * `sent` field is right the first time. */
                const int budget = (int)alchemy::hostlink::kMaxBody - 24;
                int       sent = 0, used = 0;
                for(int i = (int)start; i < (int)count; i++)
                {
                    int n = 1;
                    for(const char* c = names[i]; c && *c; c++) n++;
                    for(const char* c = notes[i]; c && *c; c++) n++;
                    n += 2;                      /* the two length bytes */
                    if(used + n > budget) break;
                    used += n;
                    sent++;
                }
                if(sent == 0) sent = 1;          /* always make progress */
                w.U8((uint8_t)sent);
                for(int i = (int)start; i < (int)start + sent; i++)
                {
                    w.U8(kinds[i]);
                    w.Str(names[i] ? names[i] : "");
                    w.Str(notes[i] ? notes[i] : "");
                }
                return;
            }
            case kCmdCardWorlds:
            {
                const char* names[32];
                const int   n = src_.CardWorlds(names, 32);
                w.U8(0u);
                /* Written before the entries so a host can size its list even
                   if the body runs out before the names do. */
                w.U8((uint8_t)n);
                /* Same budget idiom as the world list: stop before the body
                   would overflow rather than after, since a reply that does
                   not fit is dropped silently and the host waits out its
                   timeout (see the note on GET_WORLDS below). */
                const int budget = (int)alchemy::hostlink::kMaxBody - 24;
                int used = 0;
                for(int i = 0; i < n; i++)
                {
                    const int len = (int)std::strlen(names[i]);
                    if(len > 255 || used + len + 1 > budget) break;
                    w.U8((uint8_t)len);
                    for(int c = 0; c < len; c++) w.U8((uint8_t)names[i][c]);
                    used += len + 1;
                }
                return;
            }
            case kCmdPutWorld:
            {
                if(f.len < 8) { w.U8(2u); return; }
                uint32_t total, off;
                std::memcpy(&total, f.body, 4);
                std::memcpy(&off, f.body + 4, 4);
                w.U8(src_.PutWorld(total, off, f.body + 8, (int)f.len - 8));
                w.U32(off + (uint32_t)(f.len - 8));   /* what the module now holds */
                return;
            }
            case kCmdBasis:
            {
                if(f.len < 7) { w.U8(2u); return; }
                const uint8_t world = f.body[0];
                uint32_t      off;
                uint16_t      want;
                std::memcpy(&off, f.body + 1, 4);
                std::memcpy(&want, f.body + 5, 2);
                static uint8_t buf[alchemy::hostlink::kMaxBody];
                int cap = (int)sizeof(buf) - 12;
                if((int)want < cap) cap = (int)want;
                uint32_t  total = 0;
                const int n     = src_.Basis(world, off, buf, cap, total);
                if(total == 0) { w.U8(1u); return; }   /* not an analytic world */
                w.U8(0u);
                w.U32(total);
                w.U32(off);
                w.U16((uint16_t)n);
                w.Bytes(buf, (size_t)n);
                return;
            }
            case kCmdStats:
            {
                ExtStats s;
                src_.Stats(s);
                w.U8(0u);
                w.U32(s.cycles_last); w.U32(s.cycles_max); w.U32(s.cycles_avg);
                w.U16(s.overruns); w.U16(s.dropped);
                w.U8(s.render_div);
                w.U32(s.cycles_budget);
                return;
            }
            case kCmdAction:
            {
                if(f.len < 1) { w.U8(2u); return; }
                w.U8(src_.Action(f.body[0], f.body + 1, (int)f.len - 1));
                return;
            }
            case kCmdSetControl:
            {
                /* f32 f0, u8 n, f32 c[n], u8 planes, f32 angle[planes], f32 spread */
                if(f.len < 6) { w.U8(2u); return; }
                float f0;
                std::memcpy(&f0, f.body, 4);
                const int n = f.body[4];
                if(n < 1 || n > kMaxN || f.len < 5 + 4 * n + 1) { w.U8(2u); return; }
                float c[kMaxN];
                std::memcpy(c, f.body + 5, 4 * (size_t)n);
                int       at     = 5 + 4 * n;
                const int planes = f.body[at++];
                if(planes > kMaxPlanes || f.len < at + 4 * planes + 4) { w.U8(2u); return; }
                float ang[kMaxPlanes] = {0};
                std::memcpy(ang, f.body + at, 4 * (size_t)planes);
                at += 4 * planes;
                float spread;
                std::memcpy(&spread, f.body + at, 4);
                w.U8(src_.SetControl(f0, c, n, ang, planes, spread));
                return;
            }
            default: w.U8(1u); return;   /* UNSUPPORTED */
        }
    }

private:
    ExtSource& src_;
};

} // namespace kyk
