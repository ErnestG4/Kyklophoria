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
#include <cmath>
#include <cstdint>
#include <cstring>
#include "alchemy/host_link/extension.h"
#include "alchemy/host_link/frame.h"
#include "kyk_types.h"
#include "kyk_tour.h"   /* kTourMax, and the op names the wire uses */
#include "kyk_engine.h"  /* the resonate readout reads the engine's voice */

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
 *          Torus world, six decibels of headroom in the magnitude byte
 *   0.4.0  eight more worlds, to twenty-one: Shapes and Shapes R, Lock,
 *          Unison, the three modal worlds and the three single-shape ones.
 *          Sine phase with signed coefficients, so a saw is a saw and a
 *          vertex you arrive at is the waveform it says it is. Worlds you
 *          write yourself: send one from the page, or drop .kykw files in
 *          /kyklophoria on the card. Cross-world morphing with a target you
 *          choose and can aim, starting at zero so arming it does not move
 *          the sound. Motion mutes. The world that was called Braids is
 *          called Crop, and every world now says what its four axes do.
 *          Telemetry carries the live world index and what each knob on the
 *          live page is worth, so the page stops guessing at both. */
#define KYK_FW_VERSION "0.4.0"

constexpr uint8_t kCmdTelemetry  = 0x60;
constexpr uint8_t kCmdSpaceInfo  = 0x61;
constexpr uint8_t kCmdCell       = 0x62;
constexpr uint8_t kCmdStats      = 0x63;
constexpr uint8_t kCmdAction     = 0x64;
constexpr uint8_t kCmdWorlds     = 0x65;   /* the list, and which one is live */
constexpr uint8_t kCmdBasis      = 0x66;   /* an analytic world's formula, chunked */
constexpr uint8_t kCmdPutWorld   = 0x67;   /* a user world, chunked host to module */
constexpr uint8_t kCmdCardWorlds = 0x68;   /* what .kykw files the card holds */
constexpr uint8_t kCmdPutSlot    = 0x69;   /* a user world into a numbered slot */
constexpr uint8_t kCmdSlots      = 0x6A;   /* the slots, and which are live/target */
constexpr uint8_t kCmdSaveCard   = 0x6B;   /* a slot onto the card, as a file */
constexpr uint8_t kCmdGetSlot    = 0x6C;   /* a slot's blob back to the host */
/* "that file is already there". Not BAD_ARGS, because the request was perfectly
   well formed and the answer is a question for the player. */
constexpr uint8_t kStatCardExists = 20u;
/* 0x6D TOUR — a loop of worlds on a clock.
 *
 * Request: `u8 op`, and for `set` a `u8 div, u8 len, u8 entry[len]`. The entries
 * name worlds the way everything else on this wire does: an index below
 * worlds::kCount is a built-in and 0x80 | slot is one of yours.
 *
 *   0 get    just read the state back
 *   1 set    program it; a length below two turns it off
 *   2 tick   one clock edge, exactly as J2's own edge does it. For a host that
 *            wants to drive the loop itself, and for the test suite, which has
 *            no clock cable
 *
 * Reply: `u8 status, u8 len, u8 div, u8 at, u8 blend, u8 entry[len]`, where
 * `at` is which entry is live and `blend` is 0..255 of the way towards the
 * next. The state comes back on every op, so a host never has to ask twice. */
constexpr uint8_t kCmdTour       = 0x6D;
constexpr uint8_t kCmdSetControl = 0x6E;   /* desktop bridge only */
/* What the resonate world is playing (docs/modal-mode.md): the page never
   has the world, so the module says. Reply: status, u8 kind, f32 lo, f32
   hi, f32 param (where on its axis the voice was built), u16 P (points),
   u8 N, then N x (f32 hz, f32 zeta, f32 gain), then u32 burst samples,
   then u8 M and M x f32 point params (the first 64); then, for a family,
   u8 members, u8 member, char[16] its name, then members x char[16] every
   name (zero members for a plain world). The axis and modes reported are
   the member's. BAD_STATE when what is playing is not a resonator. */
constexpr uint8_t kCmdResonate   = 0x6F;
enum TourOp : uint8_t { kTourGet = 0, kTourSet = 1, kTourTick = 2 };

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
                          /* args[0]: 0 clears the aim and reads the target at
                             the same coordinates, anything else searches. A
                             toggle rather than a fire-and-forget button,
                             because an aim you cannot see is an aim you will be
                             surprised by. */
                          kActAimMorph = 10,
                          /* The worlds you made, kept as blobs and expanded on
                             demand. A blob is 6.7 KB in SDRAM, which is at 5%
                             of 64 MB, while an expanded World is 7 KB of DTCM
                             or AXI with seventeen spare — so the library is
                             blobs and only what is playing is a World. */
                          kActSlotLive   = 11,   /* u8 slot: play it */
                          kActSlotTarget = 12,   /* u8 slot, 0xFF clears: morph towards it */
                          kActSlotFree   = 13,   /* u8 slot: forget it */
                          /* u8 a, u8 b: exchange two slots. Reordering has to
                             happen here rather than on the host, because a host
                             does not have the blob for a slot it did not put
                             there — it knows the names and nothing else. */
                          kActSlotSwap   = 14,
                          /* u8 card, u8 slot: a card file into a slot, rather
                             than straight to the live world as op 7 does. */
                          kActCardToSlot = 15,
                          /* u8 slot [, u8 world]: sample a world at the
                             24-cell vertices and write it into that slot as a
                             world you can edit. With no second argument, or
                             0xFF, it samples whatever is *live*; with a world
                             index it samples that built-in instead, without
                             disturbing what is playing — which is what "start
                             a new world from Lock" needs. Formula worlds only
                             for the named form: a lattice has to be expanded
                             into a megabyte first, and the live path already
                             covers one that is.
                             A formula cannot be handed over as nodes, so this
                             is the only way to get one as a set of spectra; a
                             world that came from a slot should be read back
                             with 0x6C instead and arrives exactly. */
                          kActSnapshot   = 16,
                          /* u8 velocity (0-255), 204 if absent: strike the
                             resonate world that is playing — a fitted body
                             (kyk_resonate.h) is struck, not scanned, and until
                             the module has a trigger of its own this is the
                             only hand on it. BAD_STATE when what is playing
                             has nothing to strike. */
                          kActStrike     = 17,
                          /* u8 which, u8 value: the spin on a resonate world.
                             which 0 = voicing, the pickup's pole off its
                             fitted centre, (value - 128) / 64 widths; 1 =
                             decay, every mode's T60 x 2^((value - 128) / 64),
                             a quarter to four times; 2 = coil, its resonance
                             x 2^((value - 128) / 128), half to double. 128 is
                             the world as fitted. Takes effect on the next
                             block with the state ringing on. BAD_STATE when
                             what is playing is not a resonator. */
                          kActTune       = 18,
                          /* u8 voices, 1, 2 or 4: a resonate world's
                             polyphony, the way Rings does it — a strike takes
                             the next voice round-robin, the ones before ring
                             on at the notes they were struck at, the newest
                             follows the pitch. On the module the World page's
                             third pot (the tour division, which a resonate
                             world has no use for) sets it as well. BAD_STATE
                             when what is playing is not a resonator. */
                          kActPolyphony  = 19,
                          /* u8 on: the pitch lock of a resonate world, on
                             (1, the default) or off (0). Locked, a strike
                             takes the pitch to the nearest semitone and the
                             ring keeps it — the tail does not bend up behind
                             the next note; a bank driven with no strikes
                             follows by the semitone. Unlocked, the ring
                             follows the pitch by the cent: a bend. BAD_STATE
                             when what is playing is not a resonator. */
                          kActPitchLock  = 20,
                          /* u8 track, 0..255: the strike a little harder the
                             faster the playing — at eight strikes a second
                             and above, track / 255 x 0.2 of velocity added
                             (the density is a leaky count with a one-second
                             time constant); 0 (the default) for none.
                             BAD_STATE when what is playing is not a
                             resonator. */
                          kActVelTrack   = 21,
                          /* u8 on: a family's body axis morphs between its
                             members (1) or switches at the midpoint (0, the
                             default) — Engine::SetMemberMorph. BAD_ARG past
                             1, BAD_STATE when what is playing is not a
                             resonator */
                          kActMemberMorph = 22,
                          /* u8 release in 5 ms steps, 1..200 (5..1000 ms):
                             how long a stolen voice's last note takes to fall
                             60 dB — Engine::SetReleaseMs. BAD_ARG at 0,
                             BAD_STATE when not a resonator */
                          kActRelease     = 23 };
/* the tune byte to its value, shared by the host and the module so a page
   sees one mapping */
inline float TuneValue(uint8_t which, uint8_t v)
{
    const float c = ((float)v - 128.f);
    return which == 0 ? c / 64.f : which == 1 ? std::exp2(c / 64.f) : std::exp2(c / 128.f);
}
/* Slots, matching the card's own list size so the two stay one to one. A u8
   index then still has room for the two sentinels below. */
constexpr int     kSlotCount   = 32;
/* A morph target that is one of yours has no world index, so it needs a name
   of its own on the wire — 0xFF already means "no target at all". */
constexpr uint8_t kMorphUser   = 0xFEu;
constexpr uint16_t kMuteKepler = 0x8000u;

struct ExtStats
{
    uint32_t cycles_last = 0, cycles_max = 0, cycles_avg = 0;
    uint32_t engine_max = 0, at_per_s = 0;   /* the engine alone, and how many voices were built a second: the two numbers a bench needs to place an overrun */
    uint32_t strike_max = 0;   /* the costliest strike in the window (Engine::Strike, the voice's rebuild): the third, since the overruns come on strikes */
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

    /* Write a slot to the card as `name`.kykw.
     *
     * `overwrite` has to be asked for. A card is somebody's collection and a
     * save that quietly replaces a file on it is the one destructive thing in
     * this protocol — so the default refuses, with kStatCardExists, and a host
     * that forgets to ask cannot do damage by forgetting. The page asks the
     * player and offers a suffixed name as the alternative; the refusal here is
     * the belt to that braces. */
    virtual uint8_t SaveCardWorld(uint8_t slot, const char* name, bool overwrite)
    { (void)slot; (void)name; (void)overwrite; return 1u; }

    /* Read card entry `card` into slot `slot`, which is the existing read path
       landing somewhere it can be kept rather than going straight live. */
    virtual uint8_t CardToSlot(uint8_t card, uint8_t slot)
    { (void)card; (void)slot; return 1u; }

    /* A slot's bytes back to the host, chunked. The host can then draw its
       nodes, or open it for editing — neither of which it could do for a slot
       it had not put there itself. */
    virtual int SlotBlob(uint8_t slot, uint32_t offset, uint8_t* out, int max, uint32_t& total)
    { (void)slot; (void)offset; (void)out; (void)max; total = 0; return -1; }

    virtual uint8_t PutWorld(uint32_t total, uint32_t off, const uint8_t* data, int len)
    { (void)total; (void)off; (void)data; (void)len; return 1u; }

    /* A world into a numbered slot, chunked exactly like PutWorld. Separate
       from PutWorld rather than a destination byte on it, because PutWorld's
       request layout is eight fixed bytes followed by payload and there is no
       room in it to say anything new without a rule for telling the two shapes
       apart. */
    virtual uint8_t PutSlot(uint8_t slot, uint32_t total, uint32_t off, const uint8_t* data, int len)
    { (void)slot; (void)total; (void)off; (void)data; (void)len; return 1u; }

    /* Which slots hold something, and which of them is playing or being
       morphed towards. `names[i]` is null for an empty slot. */
    virtual int Slots(uint8_t& live, uint8_t& target, const char** names, int max)
    { (void)live; (void)target; (void)names; (void)max; return -1; }

    /* the live world if it is a resonator, and the engine playing it */
    virtual const World*  ResonateWorld() { return nullptr; }
    virtual const Engine* ResonateEngine() { return nullptr; }

    virtual uint8_t SetControl(float f0, const float* c, int n, const float* angles, int planes, float spread)
    {
        (void)f0; (void)c; (void)n; (void)angles; (void)planes; (void)spread;
        return 1u;
    }

    /* The world tour (core/kyk_tour.h). Programming it is one call because the
       sequence has to arrive whole — half a tour is a different tour — and the
       shell decides what that costs it: on the module the loads are deferred to
       the control loop like every other world change. */
    virtual uint8_t TourSet(const uint8_t* entry, int n, uint8_t div)
    { (void)entry; (void)n; (void)div; return 1u; }
    /* One clock edge from the host rather than from J2. */
    virtual uint8_t TourTick() { return 1u; }
    /* len, div, which entry is live, how far towards the next (0..255), and the
       sequence itself. False when the shell has no tour at all. */
    virtual bool TourState(uint8_t& len, uint8_t& div, uint8_t& at, uint8_t& blend, uint8_t* entry)
    { (void)len; (void)div; (void)at; (void)blend; (void)entry; return false; }
};

class KykExt : public alchemy::hostlink::IHostlinkExtension
{
public:
    explicit KykExt(ExtSource& src) : src_(src) {}

    uint8_t     FirstCmd() const override { return 0x60u; }
    uint8_t     LastCmd() const override { return 0x6Fu; }
    const char* DescriptorRootJson() const override
    {
        return "\"kyk\":{\"ext\":6,\"telemetry\":96,\"space\":97,\"cell\":98,\"stats\":99,\"action\":100,"
               "\"worlds\":101,\"basis\":102,\"putslot\":105,\"slots\":106,\"savecard\":107,\"getslot\":108,\"tour\":109,\"control\":110}";
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
            case kCmdPutSlot:
            {
                /* slot, then the same u32 total / u32 offset PutWorld uses */
                if(f.len < 9) { w.U8(2u); return; }
                uint32_t total, off;
                std::memcpy(&total, f.body + 1, 4);
                std::memcpy(&off, f.body + 5, 4);
                w.U8(src_.PutSlot(f.body[0], total, off, f.body + 9, (int)f.len - 9));
                w.U32(off + (uint32_t)(f.len - 9));
                return;
            }
            case kCmdGetSlot:
            {
                /* slot, u32 offset, u16 max — the same shape as GET_BASIS */
                if(f.len < 7) { w.U8(2u); return; }
                uint32_t off;
                uint16_t want;
                std::memcpy(&off, f.body + 1, 4);
                std::memcpy(&want, f.body + 5, 2);
                static uint8_t buf[alchemy::hostlink::kMaxBody];
                const int cap = (int)sizeof buf - 11 < (int)want ? (int)sizeof buf - 11 : (int)want;
                uint32_t  total = 0;
                const int n = src_.SlotBlob(f.body[0], off, buf, cap < 0 ? 0 : cap, total);
                if(n < 0) { w.U8(1u); return; }
                w.U8(0u);
                w.U32(total);
                w.U32(off);
                w.U16((uint16_t)n);
                for(int i = 0; i < n; i++) w.U8(buf[i]);
                return;
            }
            case kCmdSaveCard:
            {
                /* slot, flags (bit0: overwrite), then the bare name — no
                   extension, no directory, both of which the module supplies so
                   a host cannot write outside the world folder. */
                if(f.len < 3) { w.U8(2u); return; }
                const int nlen = f.body[2];
                if(nlen < 1 || nlen > 32 || f.len < 3 + nlen) { w.U8(2u); return; }
                char name[33];
                for(int i = 0; i < nlen; i++) name[i] = (char)f.body[3 + i];
                name[nlen] = '\0';
                /* Validated here, in the shared handler, rather than in each
                 * shell. The module checked and the desktop concatenated, so
                 * `../escape` wrote outside the world folder on one of the two
                 * — found by the test, which is the argument for having the
                 * rule in the one place both shells go through.
                 *
                 * Printable, and no separator or drive letter: the module
                 * supplies the folder and the extension, so a name has no
                 * business containing either. */
                for(int i = 0; i < nlen; i++)
                {
                    const char c = name[i];
                    if(c == '/' || c == '\\' || c == ':' || c < 0x20 || c > 0x7e) { w.U8(2u); return; }
                }
                /* And no leading dot, which is how you spell `..` */
                if(name[0] == '.') { w.U8(2u); return; }
                w.U8(src_.SaveCardWorld(f.body[0], name, (f.body[1] & 1u) != 0u));
                return;
            }
            case kCmdResonate:
            {
                const World* wd = src_.ResonateWorld(); const Engine* en = src_.ResonateEngine();
                if(!wd || !en || !wd->IsResonate()) { w.U8(3u); return; }
                /* a family reports the instrument the voice is built from,
                   then says which of how many, and its name */
                const ResonatorWorld& fam = wd->Res(); const ResonatorVoice& v = en->Voice();
                ResonatorWorld r; fam.Member(en->ResMember(), r);
                auto f32 = [&](float x) { uint32_t u; std::memcpy(&u, &x, 4); w.U32(u); };
                /* the voice's own modes: between two points it holds the
                   blend of both, which can be more than the world's N */
                int vn = 0; while(vn < ResonatorBank::kMax && !(v.gain[vn] == 0.f && v.hz[vn] == 0.f)) vn++;
                if(r.kind == 1 && vn < r.N) vn = r.N;
                w.U8(0u); w.U8(r.kind); f32(r.lo); f32(r.hi); f32(en->ResParamNow()); w.U16(r.P); w.U8((uint8_t)vn);
                for(int k = 0; k < vn; k++) { f32(v.hz[k]); f32(v.zeta[k]); f32(v.gain[k]); }
                w.U32(v.burst_len);
                const int m = r.P < 64 ? r.P : 64;
                w.U8((uint8_t)m);
                for(int i = 0; i < m; i++) f32(r.Param(i));
                w.U8(fam.kind == 2 ? fam.M : 0u); w.U8((uint8_t)en->ResMember());
                const char* nm = fam.MemberName(en->ResMember());
                for(int i = 0; i < 16; i++) w.U8((uint8_t)nm[i]);
                for(int i = 0; i < (fam.kind == 2 ? fam.M : 0); i++) { const char* q = fam.MemberName(i); for(int c = 0; c < 16; c++) w.U8((uint8_t)q[c]); }
                /* the voice count and the pitch lock, which the page has
                   no other way to read: on the module a pot sets the one
                   and a button the other */
                w.U8((uint8_t)en->Polyphony()); w.U8(en->PitchLock() ? 1u : 0u);
                w.U8(r.form);   /* 0 no pickup (the voicing and coil axes are position and brightness), 1 a bell field, 2 a gap */
                /* the morph and the release, which the page sets and must
                   read back rather than remember */
                w.U8(en->MemberMorph() ? 1u : 0u);
                const float rel = en->ReleaseMs(); w.U16((uint16_t)(rel + 0.5f));
                return;
            }
            case kCmdSlots:
            {
                const char* names[kSlotCount];
                uint8_t     live = 0xFFu, target = 0xFFu;
                const int   n = src_.Slots(live, target, names, kSlotCount);
                if(n < 0) { w.U8(1u); return; }
                w.U8(0u);
                w.U8((uint8_t)n);
                w.U8(live);
                w.U8(target);
                /* Same budget idiom as the other two lists: stop before the
                   body would overflow, because a reply that does not fit is
                   dropped silently and the host waits out its timeout. */
                const int budget = (int)alchemy::hostlink::kMaxBody - 24;
                int       used = 0;
                /* Only the slots that hold something. `count` above says how
                   many there are in total, so a host fills the rest in as
                   empty — and an empty slot then arrives as *absent* rather
                   than as a blank name, which are different things. */
                for(int i = 0; i < n; i++)
                {
                    const char* nm = names[i];
                    if(!nm) continue;
                    const int len = (int)std::strlen(nm);
                    if(len > 255 || used + len + 2 > budget) break;
                    w.U8((uint8_t)i);
                    w.U8((uint8_t)len);
                    for(int c = 0; c < len; c++) w.U8((uint8_t)nm[c]);
                    used += len + 2;
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
                /* appended: the engine's own peak in the window and the
                   voices built a second, so a bench can say whether an
                   overrun is the engine's block work or a retune */
                w.U32(s.engine_max); w.U32(s.at_per_s);
                w.U32(s.strike_max);
                return;
            }
            case kCmdAction:
            {
                if(f.len < 1) { w.U8(2u); return; }
                w.U8(src_.Action(f.body[0], f.body + 1, (int)f.len - 1));
                return;
            }
            case kCmdTour:
            {
                /* u8 op [, u8 div, u8 len, u8 entry[len]] */
                const uint8_t op = f.len >= 1 ? f.body[0] : (uint8_t)kTourGet;
                uint8_t st = 0u;
                if(op == kTourSet)
                {
                    if(f.len < 3) { w.U8(2u); return; }
                    const uint8_t div = f.body[1], n = f.body[2];
                    if(n > kTourMax || f.len < 3 + n) { w.U8(2u); return; }
                    st = src_.TourSet(f.body + 3, (int)n, div);
                }
                else if(op == kTourTick) st = src_.TourTick();
                else if(op != kTourGet) { w.U8(2u); return; }
                uint8_t len = 0u, div = 0u, at = 0u, blend = 0u, entry[kTourMax] = {0};
                if(!src_.TourState(len, div, at, blend, entry)) { w.U8(st ? st : 1u); return; }
                w.U8(st);
                w.U8(len); w.U8(div); w.U8(at); w.U8(blend);
                for(int i = 0; i < (int)len && i < kTourMax; i++) w.U8(entry[i]);
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
