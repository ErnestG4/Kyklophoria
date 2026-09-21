/* kyklophoria — the Alchemy Lab v2 main(). M1: the M0/M1 core on the panel.
 *
 *   J3 v/oct · J4–J7 position 0–3 · J8 CV out A · J9/J10 out L/R
 *   (docs/io-map.md; J1 FM and J2 sync land in M3)
 *
 *   Page Play    P1 coarse (octaves)  P2 fine (±1 st)  P3–P6 position 0–3 offsets
 *   Page Rotate  P1–P6 the six plane angles (turns)
 *   Page Orbit   P1–P6 the six plane rates, centre stopped, exponential out
 *   Page Kepler  P1 gravity (bottom = off) P2 eccentricity P3 orbit plane
 *                P4 softening P5 damping P6 radius
 *   Page Stereo  P1 spread  P2 stereo plane  P3 CV out A depth  P4 render div  P5 level
 *   B1 taps through the pages · B2+B3 held: Settings (SDK)
 *
 * Threads: the audio callback reads knobs (ISR-safe) and CVs, runs the
 * engine, and publishes a telemetry snapshot when the control loop asked
 * for one. The control loop (SDK ControlLoop::Tick) drives panel, rings,
 * HostLink (our KykExt at 0x60–0x6F) and the CV out. Nothing in the
 * callback blocks; a slow host only sees older snapshots.
 */
#include <cstring>
#include <cmath>
#include "daisy_seed.h"
#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/hw/alchemy_lab_layout.h"
#include "alchemy/surface/control_loop.h"
#include "alchemy/surface/presets.h"
#include "alchemy/surface/virtual_knob.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/pager.h"
#include "alchemy/surface/settings.h"
#include "alchemy/surface/jack.h"
#include "alchemy/host_link/host.h"

#include "kyk_stereo.h"
#include "kyk_worlds.h"
#include <type_traits>
#include "kyk_telemetry.h"
#include "kyk_ext.h"
#include "kyk_worldrx.h"
#include "kyk_aim.h"
#include "alchemy/storage/sd_card.h"

using namespace alchemy;
using namespace kyk;

/* KYK_FW_VERSION and its history live in shell/common/kyk_ext.h, so the
 * desktop bridge and the module cannot introduce themselves differently. */
#ifndef KYK_GIT_HASH
#define KYK_GIT_HASH "local"
#endif

/* ── memory placement ─────────────────────────────────────────────────────
 * The engine (two voices, ~40 KB) lives in AXI SRAM: fast, DMA-irrelevant.
 * The space blob lives in SDRAM (64 MB; a 6-D lattice is 1.2 MB). */
#define KYK_AXI   __attribute__((section(".axi_bss")))
#define KYK_SDRAM __attribute__((section(".sdram_bss")))

static AlchemyLab  hw;
static ControlLoop loop(hw);
static Pager       pager(hw.buttons[kButtonB1], 7, kNumPots);
static Presets     presets(hw.seed.qspi);
static Settings    settings(hw, &pager);

/* ── knobs ──────────────────────────────────────────────────────────────── */
static constexpr LedPanel::Rgb kPlay   = {0x67, 0xE8, 0xF9};
static constexpr LedPanel::Rgb kRotate = {0xFC, 0xA5, 0xA5};
static constexpr LedPanel::Rgb kStereo = {0xC4, 0xB5, 0xFD};
static constexpr LedPanel::Rgb kOrbit  = {0xFD, 0xE0, 0x68};
static constexpr LedPanel::Rgb kCouple = {0xF0, 0xA0, 0xD8};
static constexpr LedPanel::Rgb kKepler = {0x9A, 0xE6, 0xB4};
static constexpr LedPanel::Rgb kWorld  = {0xF7, 0xC0, 0x8A};

static VirtualKnob k_coarse = VirtualKnob(0, "Coarse").Linear(-3.f, 3.f).Unit("oct").Ident("pitch.coarse").Ring(Level(kPlay));
static VirtualKnob k_fine   = VirtualKnob(1, "Fine").Linear(-1.f, 1.f).Unit("st").Ident("pitch.fine").Ring(Level(kPlay));
static VirtualKnob k_pos0   = VirtualKnob(2, "Position 0").Ident("pos.0").Ring(Level(kPlay));
static VirtualKnob k_pos1   = VirtualKnob(3, "Position 1").Ident("pos.1").Ring(Level(kPlay));
static VirtualKnob k_pos2   = VirtualKnob(4, "Position 2").Ident("pos.2").Ring(Level(kPlay));
static VirtualKnob k_pos3   = VirtualKnob(5, "Position 3").Ident("pos.3").Ring(Level(kPlay));

static VirtualKnob k_ang[6] = {
    VirtualKnob(0, "Angle 0,1").Unit("turn").Ident("rot.01").Ring(Level(kRotate)),
    VirtualKnob(1, "Angle 0,2").Unit("turn").Ident("rot.02").Ring(Level(kRotate)),
    VirtualKnob(2, "Angle 0,3").Unit("turn").Ident("rot.03").Ring(Level(kRotate)),
    VirtualKnob(3, "Angle 1,2").Unit("turn").Ident("rot.12").Ring(Level(kRotate)),
    VirtualKnob(4, "Angle 1,3").Unit("turn").Ident("rot.13").Ring(Level(kRotate)),
    VirtualKnob(5, "Angle 2,3").Unit("turn").Ident("rot.23").Ring(Level(kRotate)),
};

static VirtualKnob k_rate[6] = {
    VirtualKnob(0, "Orbit 0,1").Unit("t/s").Ident("orb.01").Ring(Level(kOrbit)),
    VirtualKnob(1, "Orbit 0,2").Unit("t/s").Ident("orb.02").Ring(Level(kOrbit)),
    VirtualKnob(2, "Orbit 0,3").Unit("t/s").Ident("orb.03").Ring(Level(kOrbit)),
    VirtualKnob(3, "Orbit 1,2").Unit("t/s").Ident("orb.12").Ring(Level(kOrbit)),
    VirtualKnob(4, "Orbit 1,3").Unit("t/s").Ident("orb.13").Ring(Level(kOrbit)),
    VirtualKnob(5, "Orbit 2,3").Unit("t/s").Ident("orb.23").Ring(Level(kOrbit)),
};

/* Centre is stopped, and the useful rates are slow, so a linear knob would
 * bunch everything worth having into the last few degrees. Exponential either
 * side of a small dead zone: 0.01 turns per second at the inside edge (a
 * hundred seconds for one revolution) out to 1.0 at the stops, either
 * direction. */
static float RateFromKnob(float norm)
{
    const float u = (norm - 0.5f) * 2.f;             /* -1 .. +1 */
    const float a = u < 0.f ? -u : u;
    if(a < 0.04f) return 0.f;
    const float t = (a - 0.04f) / 0.96f;             /* 0 .. 1 */
    const float r = 0.01f * exp2f(t * 6.6439f);      /* 0.01 .. 1.0 */
    return u < 0.f ? -r : r;
}

/* ── Kepler page ──────────────────────────────────────────────────────
 * Gravity at the bottom of its travel means off, and winding it up from
 * there launches the body. Radius re-launches too, with a deadband so ADC
 * jitter does not re-launch it forty times a second. */
static const char* kPlaneNames[6] = {"0,1", "0,2", "0,3", "1,2", "1,3", "2,3"};
static VirtualKnob k_grav   = VirtualKnob(0, "Gravity").Ident("kep.g").Ring(Level(kKepler));
static VirtualKnob k_ecc    = VirtualKnob(1, "Eccentricity").Ident("kep.ecc").Ring(Level(kKepler));
static VirtualKnob k_kplane = VirtualKnob(2, "Orbit plane").Selector(6).Labels(kPlaneNames, 6).Ident("kep.plane").Ring(Level(kKepler));
/* Normalised, and mapped exponentially where it is read — the same shape as
 * Gravity, and for the same reason: what this knob really controls is how far
 * the orbit is from closing, and that is not linear in the softening radius. */
static VirtualKnob k_soft   = VirtualKnob(3, "Softening").Ident("kep.soft").Ring(Level(kKepler));
static VirtualKnob k_damp   = VirtualKnob(4, "Damping").Linear(0.f, 1.2f).Unit("/s").Ident("kep.damp").Ring(Level(kKepler));
static VirtualKnob k_radius = VirtualKnob(5, "Radius").Linear(0.08f, 0.55f).Ident("kep.r").Ring(Level(kKepler));
/* Divider 1 (a frame every block, twice over in stereo) overran the block on
 * the bench at 160% of budget and took the module down. The real IFFT bought
 * about 2x, which is not yet enough headroom to offer it, so the knob starts
 * at 2. Restore 1 when CMSIS lands and the bench says it fits. Musically this
 * costs nothing measurable: the morph artifact at divider 4 is -82 dB and at
 * 1 it is -98 dB, both far below anything audible (docs/m2-notes.md). */
static const uint8_t kDivValues[4] = {2, 3, 4, 6};
static const char*   kDivNames[4]  = {"2", "3", "4", "6"};
static VirtualKnob k_spread = VirtualKnob(0, "Spread").Linear(0.f, 0.1f).Unit("turn").Ident("st.spread").Ring(Level(kStereo));
static VirtualKnob k_plane  = VirtualKnob(1, "Stereo plane").Selector(6).Labels(kPlaneNames, 6).Ident("st.plane").Ring(Level(kStereo));
/* P3 was CV-out depth, which only moves a voltage on J8 and reads as a dead
 * knob to a player. Sharpness is the audible control the survey said to steal
 * (Plaits' quantization ramp, Piston Honda's morph resolution): smooth morph
 * at one end, a bank of discrete waves at the other. CV depth moved to P6. */
static VirtualKnob k_sharp  = VirtualKnob(2, "Morph").Unit("sharp").Ident("morph.sharp").Ring(Level(kStereo));
static VirtualKnob k_rdiv   = VirtualKnob(3, "Render div").Selector(4).Labels(kDivNames, 4).Ident("eng.rdiv").Ring(Level(kStereo));
static VirtualKnob k_level  = VirtualKnob(4, "Level").Ident("out.level").Ring(Level(kStereo));
static VirtualKnob k_cvdep  = VirtualKnob(5, "CV out A depth").Ident("lane.cva").Ring(Level(kStereo));

/* ── World page ───────────────────────────────────────────────────────
 * Axes 4 and 5, which the I/O map has always said are pot-only: there are
 * four position CVs and six axes, so these two were reachable from the web
 * page and from nowhere a hand could get at.
 *
 * They have a job now. A world somebody wrote can put a frame effect on an
 * axis (core/kyk_userworld.h), and the editor puts them here rather than on a
 * placement axis, so an effect is a knob of its own instead of something that
 * happens as you cross the space. For the twenty-two built-ins and for any
 * world with n=4 these two do nothing at all, which is why they are on a page
 * of their own rather than taking a Play knob from something that always
 * works: Engine::SetControl only slews the axes the live world actually has. */
static VirtualKnob k_pos4   = VirtualKnob(0, "Position 4").Ident("pos.4").Ring(Level(kWorld));
static VirtualKnob k_pos5   = VirtualKnob(1, "Position 5").Ident("pos.5").Ring(Level(kWorld));
/* The world tour's three setup knobs. Which worlds are in the loop is a list
 * and comes from the page; how fast, how far and how smoothly it travels are
 * performance and belong under a finger.
 *
 * Division counts edges of J2. Glide is how much of each interval the blend
 * spends travelling: at the top it never stops moving, at the bottom it arrives
 * in the first few milliseconds and waits, which is a sequencer with a
 * crossfade rather than a morph. Free-run is for a rack with no clock in it —
 * at the bottom it is off and J2 is the only thing that moves the loop. */
static const char* kDivNamesTour[8] = {"1", "2", "3", "4", "6", "8", "12", "16"};
static const uint8_t kDivValuesTour[8] = {1, 2, 3, 4, 6, 8, 12, 16};
static VirtualKnob k_tdiv   = VirtualKnob(2, "Tour division").Selector(8).Labels(kDivNamesTour, 8).Ident("tour.div").Ring(Level(kWorld));
static VirtualKnob k_tglide = VirtualKnob(3, "Tour glide").Ident("tour.glide").Ring(Level(kWorld));
static VirtualKnob k_trate  = VirtualKnob(4, "Tour free-run").Unit("s").Ident("tour.rate").Ring(Level(kWorld));
static Page page_world  = Page(6).Name("World").Color("#f7c08a").Knobs(k_pos4, k_pos5, k_tdiv, k_tglide, k_trate);

static Page page_play   = Page(0).Name("Play").Color("#67e8f9").Knobs(k_coarse, k_fine, k_pos0, k_pos1, k_pos2, k_pos3);
static Page page_rotate = Page(1).Name("Rotate").Color("#fca5a5").Knobs(k_ang[0], k_ang[1], k_ang[2], k_ang[3], k_ang[4], k_ang[5]);
static Page page_orbit  = Page(3).Name("Orbit").Color("#fde068").Knobs(k_rate[0], k_rate[1], k_rate[2], k_rate[3], k_rate[4], k_rate[5]);
static Page page_kepler = Page(4).Name("Kepler").Color("#9ae6b4").Knobs(k_grav, k_ecc, k_kplane, k_soft, k_damp, k_radius);
/* ── Couple page ──────────────────────────────────────────────────────
 * Three knobs, and the page is deliberately not padded out to six with
 * things that do not need a knob.
 *
 * Coupling is the one that changes what the instrument is. At zero the six
 * plane orbits are independent and the path never closes, which is the wash.
 * Wind it up and each pair pulls the other toward the nearest simple ratio,
 * the figure closes, and the waveform snaps into shape. Measured on two
 * planes over a rate sweep, the fraction of settings that land on a simple
 * ratio runs 2.7% at zero (chance), 19% at 0.10, 46% at 0.25 and 88% at 0.80,
 * so the whole knob is useful travel rather than an on/off.
 *
 * Reach says how exotic a ratio it will settle on: 1 is unison only, 5 opens
 * the full staircase. Rate scales all six orbit knobs at once, which matters
 * because the six of them are a page away. */
static const char* kReachNames[5] = {"1", "2", "3", "4", "5"};
static VirtualKnob k_couple = VirtualKnob(0, "Coupling").Ident("orb.couple").Ring(Level(kCouple));
static VirtualKnob k_reach  = VirtualKnob(1, "Reach").Selector(5).Labels(kReachNames, 5).Ident("orb.reach").Ring(Level(kCouple));
static VirtualKnob k_ratex  = VirtualKnob(2, "Rate").Unit("x").Ident("orb.ratex").Ring(Level(kCouple));
/* The falling body's company lives on this page rather than on Kepler's,
 * which is full, and it belongs here anyway: both knobs are about motions
 * pulling on each other. One body closes, two beat, three never repeat. */
static VirtualKnob k_kbody  = VirtualKnob(3, "Bodies").Selector(8).Ident("kep.bodies").Ring(Level(kCouple));
static VirtualKnob k_kmass  = VirtualKnob(4, "Company").Ident("kep.mass").Ring(Level(kCouple));
/* How far towards the other world. Which world is a setup choice and lives on
 * the page; how far is a performance one and belongs under a finger. */
static VirtualKnob k_morph  = VirtualKnob(5, "World morph").Ident("world.morph").Ring(Level(kCouple));
static Page page_couple = Page(5).Name("Couple").Color("#f0a0d8").Knobs(k_couple, k_reach, k_ratex, k_kbody, k_kmass, k_morph);
static Page page_stereo = Page(2).Name("Stereo").Color("#c4b5fd").Knobs(k_spread, k_plane, k_sharp, k_rdiv, k_level, k_cvdep);

/* ── jacks (descriptor metadata; the web panel mirror reads these) ───────── */
static const Jack kJacks[10] = {
    Jack("fm", "FM In", JackSig::AudioIn),      Jack("sync", "Sync", JackSig::Trig),
    Jack("voct", "V/Oct", JackSig::Voct),       Jack("pos0", "Position 0", JackSig::CvBi),
    Jack("pos1", "Position 1", JackSig::CvBi),  Jack("pos2", "Position 2", JackSig::CvBi),
    Jack("pos3", "Position 3", JackSig::CvBi),  Jack("cv_a", "CV Out A", JackSig::CvUni),
    Jack("out_l", "Out L", JackSig::AudioOut),  Jack("out_r", "Out R", JackSig::AudioOut),
};

/* ── the space and the engine ────────────────────────────────────────────── */
/* side 8, not 4: the field needs room for a correlation length, and the
 * measured variety per unit of CV travel roughly doubles between them
 * (docs/m2-notes.md). 1.18 MB of the 64 MB SDRAM. */
static constexpr int kBootN = 4, kBootSide = 8, kBootK = 64, kBootP = 8;

/* Two lattice buffers, so a tabulated world can be expanded while the current
 * one keeps playing; the swap is then a single pointer write. Analytic worlds
 * need neither buffer nor wait, which is why the module boots into one. */
/* Four, not three: live, spare, morph target, and one scratch for opening a
   world to edit. SDRAM is at six per cent of sixty-four megabytes, so the
   scratch costs 1.2 MB of the one resource here there is plenty of — and buys
   "any world can be opened" instead of "sixteen of the twenty-one can". */
static uint8_t KYK_SDRAM gBlob[4][Space::BlobSize(kBootN, kBootK, kBootP, kBootSide, false)];
static Space   gSpace[4];
/* Two worlds in the double buffer a switch swaps between, and a third for
 * whatever the Morph knob is blending towards — it has to stay live while the
 * other two are swapped underneath it.
 *
 * The third lives in AXI SRAM rather than with the other two. A World is seven
 * kilobytes, mostly node buffer, and putting all three in DTCM took it from
 * 73.6% to 79.1%. Moving the *playing* pair out would change where the audio
 * path reads its spectra from on every world, and Shapes 2 already measures
 * around 72% of budget on the bench — not a thing to change as a side effect of
 * adding a feature. The morph target is read on the same path, so if this ever
 * shows up in the numbers it can move back and the pair can move out together,
 * deliberately and with a measurement. */
static World   gWorlds[2];
static World   KYK_AXI gMorphWorld;
/* A vertex world's table, one per world buffer so a switch never rewrites
 * the table the other one is still playing from. SDRAM: at K=128 each is
 * 17 KB, which DTCM cannot spare. */
static solids::VertexTable KYK_SDRAM gVertTable[4];
/* Anything in SDRAM must be trivially constructible.
 *
 * .init_array runs before main(), and main() is where hw.Init() brings up the
 * FMC that makes SDRAM addressable at all. A type with any default member
 * initialiser gets a constructor call from there, which stores into an SDRAM
 * controller that is not up, which bus-faults, which hard-faults before a
 * single line of this file executes. That is not a subtle failure — the module
 * is dead on every boot and has to be dropped back to the bootloader — and it
 * is completely invisible on the desktop build, where the same object is
 * ordinary memory. These two lines turn a brick into a compile error. */
static_assert(std::is_trivially_default_constructible<solids::VertexTable>::value,
              "SDRAM objects must not have default member initialisers: "
              ".init_array would write SDRAM before hw.Init() brings up the FMC");
static_assert(std::is_trivially_default_constructible<decltype(gBlob)>::value,
              "SDRAM objects must not have default member initialisers");
static uint8_t gBufIdx    = 0;
static volatile uint8_t gWorldIdx = worlds::kCrop;
static volatile uint8_t gWorldReq = 0xFFu;   /* 0xFF: nothing pending */
/* A user world arriving over HostLink. The chunks land here from the main
 * loop; the parse and the swap happen in ServeWorldRequest with the same
 * double buffer and barrier a built-in world switch uses, because the audio
 * thread is reading the live one throughout. */
/* AXI SRAM, not DTCM. It is nearly seven kilobytes that the audio path never
 * touches — only a transfer does — and DTCM is the scarcest memory here. */
static WorldReceiver KYK_AXI gRx;
/* The worlds you have loaded, as blobs.
 *
 * A World is 7 KB and has to live in DTCM or AXI, where there are about
 * seventeen spare; a .kykw blob is 6.7 KB and lives in SDRAM, which is at 5% of
 * 64 MB. So the library is blobs and only what is *playing* is an expanded
 * World — parsing one is a memcpy into a LockField, no FFT and no lattice
 * expansion, so expanding on demand costs nothing you can hear.
 *
 * 216 KB for all thirty-two. Nothing in here may have a default member
 * initialiser: .init_array runs before hw.Init() brings up the FMC, and a
 * constructor here would store into a controller that is not up. Plain arrays
 * of trivial types, guarded by the static_assert below. */
static uint8_t KYK_SDRAM gSlotBlob[kSlotCount][kUserBlobMax];
static_assert(std::is_trivially_default_constructible<decltype(gSlotBlob)>::value,
              "gSlotBlob is in SDRAM and must not be constructed before hw.Init()");
/* Lengths and names are small, so they stay in ordinary memory where they are
   live at reset and can be read while the FMC is still down. */
static uint32_t gSlotLen[kSlotCount];
static char     gSlotName[kSlotCount][kUserNameLen + 1];
/* Resonate worlds (.kykm, from ModalBake: a fitted body of resonators, struck
 * rather than scanned — core/kyk_resonate.h) are card worlds. One is a sample
 * library's size, 66 KB for a Wurlitzer and a megabyte for an electric piano
 * with its attack bursts, so it never fits the wire's receiver or a slot's
 * 6.7 KB store; it is read off the card into one of these regions and the
 * World reads it *where it lies*, the way the runtime was written (no copy,
 * no heap). Four fixed regions rather than an arena with holes, so a load
 * either fits or is refused, and freeing one can never fragment the rest.
 * 8 MB of the 64 MB SDRAM. A slot holding one keeps its region's number in
 * gSlotRes and its gSlotBlob bytes mean nothing. */
/* Sixteen: four was the first evening's number and the fifth world would
 * not load, silently — a slot past 03 stayed empty. 32 MB of the 64, with
 * the wavetable blobs and the slots taking under 5 MB of the rest. */
constexpr uint32_t kResRegionBytes = 2u << 20;
constexpr int      kResRegions     = 16;
static uint8_t KYK_SDRAM gResArena[kResRegions][kResRegionBytes];
static_assert(std::is_trivially_default_constructible<decltype(gResArena)>::value,
              "gResArena is in SDRAM and must not be constructed before hw.Init()");
static uint8_t gSlotRes[kSlotCount];        /* 0 = a frame world in gSlotBlob, r + 1 = region r */
static uint8_t gResOwner[kResRegions];      /* slot + 1, 0 = free */
static bool IsKykm(const char* name)
{
    const size_t n = std::strlen(name);
    return n > 5 && (name[n - 1] == 'm' || name[n - 1] == 'M') && name[n - 5] == '.';
}
static volatile uint8_t gSlotLive   = 0xFFu;   /* which slot is playing, if any */
static volatile uint8_t gSlotTarget = 0xFFu;   /* which slot is the morph target */
/* Requests, served on the control loop like every other world change. */
static volatile int8_t  gSlotStore  = -1;      /* a completed transfer's destination */
static volatile int8_t  gSlotLiveReq = -1;
static volatile int8_t  gSlotTargetReq = -1;
static volatile int8_t  gCardToSlotReq  = -1;   /* destination slot, -1 = none */
static volatile int8_t  gSnapReq        = -1;   /* snapshot into this slot */
static volatile uint8_t gSnapWorld      = 0xFFu; /* which world, 0xFF = the live one */
/* One World of control-thread scratch, in AXI.
 *
 * AXI and not DTCM: a World is 7 KB and what is left of DTCM *is* the stack —
 * main loop, audio ISR on top of it, FPU exception stacking. Put in DTCM this
 * took the region from 75.7% to 81.0% for something that runs on the control
 * thread and never touches the audio path, which is exactly where gMorphWorld
 * already lives. Shared by the card-file validator and the named snapshot
 * rather than one each, because 7 KB is 7 KB. */
static World KYK_AXI gScratch;
static volatile uint8_t gCardToSlotCard = 0;    /* which card entry */

static volatile uint8_t gUserReq = 0u;
static constexpr uint8_t kMorphSlot = 2u;    /* blob/space/vtable slot */
/* Scratch for building a world nobody is listening to, so opening one to edit
   disturbs neither what is playing nor what it is morphing towards. */
static constexpr uint8_t kScratchSlot = 3u;
static volatile uint8_t gMorphReq = 0xFFu;   /* world index to load there */
static volatile uint8_t gMorphIdx = 0xFFu;   /* what is loaded there now */
/* Which motions are switched off. A mute, not a zero: the knobs keep their
 * values so unmuting restores what was set, which is the difference between a
 * switch and turning something down. Chosen from the page — deciding which
 * planes are live is setup, not performance. */
/* ── the world tour ──────────────────────────────────────────────────────
 * A loop of worlds on a clock (core/kyk_tour.h). Three buffers in a ring, and
 * this module already has exactly three: the two the live double buffer swaps
 * between and the morph target. So a step is a pointer rotation and the world
 * the step after next will need is expanded into the one nobody is listening
 * to, which is what gives a lattice expansion a whole clock interval to finish
 * instead of landing in the middle of something audible.
 *
 * J2 has said Sync on the panel since the I/O map was agreed and has been read
 * by nothing. It is the clock. */
static Tour              gTour;
static World* const      kTourWorld[kTourSlots] = {&gWorlds[0], &gWorlds[1], &gMorphWorld};
static volatile uint8_t  gClockEdges   = 0u;    /* raised by the audio callback */
static volatile uint8_t  gTourSetReq   = 0u;
static volatile uint8_t  gTourOn       = 0u;    /* the audio thread reads only this */
static volatile int8_t   gTourLoadSlot = -1;
static volatile uint8_t  gTourLoadWorld = 0xFFu;
static uint8_t           gTourWant[kTourMax] = {0};
static uint8_t           gTourWantLen = 0u, gTourWantDiv = 1u;
static volatile uint16_t gMute = 0u;
static volatile uint8_t  gAimReq = 0u;   /* aim the morph at the nearest match */

/* ── user worlds on the card ─────────────────────────────────────────────
 *
 * A world is a file, so the library is a folder. Drop .kykw files in
 * /kyklophoria on the card and they appear; copy one to a friend and they have
 * it. No transport, no forking, no upload step.
 *
 * The DMA law, which the SDK's own header states and Audiothurgist found the
 * hard way before that: SDMMC1's IDMA cannot reach DTCM. Every FatFs object
 * and every staging buffer has to sit in a region the controller can see, which
 * is what ALCHEMY_SDMMC_BSS marks. A DIR in DTCM does not fail loudly — it
 * fails as corruption. */
static alchemy::SdCard gSd;
constexpr int kMaxCardWorlds = 32;
ALCHEMY_SDMMC_BSS alignas(32) static DIR      gCardDir;
ALCHEMY_SDMMC_BSS alignas(32) static FILINFO  gCardFno;
ALCHEMY_SDMMC_BSS alignas(32) static FIL      gCardFil;
ALCHEMY_SDMMC_BSS alignas(32) static uint8_t  gCardStage[kUserBlobMax];
static char    gCardNames[kMaxCardWorlds][32];
static uint8_t gCardCount = 0;
static volatile int8_t gCardLoadReq = -1;    /* index to load, -1 = none */

static const char* kWorldDir = "0:/kyklophoria";

/* Names only. Reading every file to validate it would mean a second of card
 * work at boot for a folder nobody may open; a bad file is refused when it is
 * asked for, which is the moment anyone is waiting for an answer. */
static void ScanCard()
{
    gCardCount = 0;
    if(!gSd.EnsureMounted(daisy::System::GetNow())) return;
    alchemy::SdCard::BusyGuard busy(gSd);
    if(f_opendir(&gCardDir, kWorldDir) != FR_OK) return;
    while(gCardCount < kMaxCardWorlds
          && f_readdir(&gCardDir, &gCardFno) == FR_OK && gCardFno.fname[0])
    {
        if(gCardFno.fattrib & AM_DIR) continue;
        const char* dot = std::strrchr(gCardFno.fname, '.');
        if(!dot || std::strlen(gCardFno.fname) > 30) continue;
        /* .kykw, a set of frames; .kykm, a resonator — both are worlds */
        if(std::strlen(dot) != 5 || (dot[1] != 'k' && dot[1] != 'K')) continue;
        if(dot[4] != 'w' && dot[4] != 'W' && dot[4] != 'm' && dot[4] != 'M') continue;
        std::strncpy(gCardNames[gCardCount], gCardFno.fname, 31);
        gCardNames[gCardCount][31] = 0;
        gCardCount++;
    }
    f_closedir(&gCardDir);
}

/* Read one into the staging buffer. Returns bytes read, 0 on any failure. */
static size_t ReadCardWorld(uint8_t i)
{
    if(i >= gCardCount) return 0;
    if(!gSd.EnsureMounted(daisy::System::GetNow())) return 0;
    alchemy::SdCard::BusyGuard busy(gSd);
    char path[80];
    std::snprintf(path, sizeof path, "%s/%s", kWorldDir, gCardNames[i]);
    if(f_open(&gCardFil, path, FA_READ) != FR_OK) return 0;
    const FSIZE_t sz = f_size(&gCardFil);
    size_t got = 0;
    if(sz > 0 && (size_t)sz <= sizeof gCardStage)
    {
        UINT rd = 0;
        if(f_read(&gCardFil, gCardStage, (UINT)sz, &rd) != FR_OK) rd = 0;
        got = rd;
    }
    f_close(&gCardFil);
    return got;
}
/* A card file into a resonate region, through the staging buffer a chunk at a
 * time — IDMA's reach is the staging buffer's section, not SDRAM's, and the
 * DMA law above is not one to test by reading a megabyte straight in. Returns
 * bytes read, 0 on any failure or a file past the region. */
static size_t ReadCardResonate(uint8_t i, uint8_t* dst, size_t cap)
{
    if(i >= gCardCount) return 0;
    if(!gSd.EnsureMounted(daisy::System::GetNow())) return 0;
    alchemy::SdCard::BusyGuard busy(gSd);
    char path[80];
    std::snprintf(path, sizeof path, "%s/%s", kWorldDir, gCardNames[i]);
    if(f_open(&gCardFil, path, FA_READ) != FR_OK) return 0;
    const FSIZE_t sz = f_size(&gCardFil);
    size_t got = 0;
    if(sz > 0 && (size_t)sz <= cap)
    {
        while(got < (size_t)sz)
        {
            const size_t want = (size_t)sz - got < sizeof gCardStage ? (size_t)sz - got : sizeof gCardStage;
            UINT rd = 0;
            if(f_read(&gCardFil, gCardStage, (UINT)want, &rd) != FR_OK || rd != want) { got = 0; break; }
            std::memcpy(dst + got, gCardStage, rd);
            got += rd;
        }
    }
    f_close(&gCardFil);
    return got;
}
/* A slot onto the card, as a file.
 *
 * The first thing in this instrument that *writes* to the card, so three things
 * are deliberate.
 *
 * The bytes are copied into gCardStage before being handed to FatFs. That
 * buffer is ALCHEMY_SDMMC_BSS and alignas(32) because SDMMC's IDMA cannot reach
 * DTCM and does not error when it cannot — it corrupts (docs/sdk-quirks.md).
 * The blob itself lives in SDRAM, and rather than reason about whether the IDMA
 * is happy reading FMC-mapped memory, it goes through the buffer the read path
 * already proved.
 *
 * Written to a temp name and renamed. A write that dies half way must not leave
 * something ScanCard will list and ParseUserWorld will accept; a rename is
 * atomic enough on FAT that the file either is not there or is whole.
 *
 * Synchronous, in the handler, unlike loading a card world — and that asymmetry
 * is the point. Loading swaps the world the audio thread is reading, so it is
 * deferred to the control loop; saving touches no audio state at all. What it
 * does cost is a few milliseconds of control loop, which the panel and HostLink
 * wait out, and it happens because somebody asked for it. kActScanCard already
 * works this way.
 */
static uint8_t SaveSlotToCard(uint8_t slot, const char* name, bool overwrite)
{
    if(slot >= kSlotCount || !gSlotLen[slot]) return 2u;
    if(!name || !name[0]) return 2u;
    /* The module supplies the directory and the extension, so a host cannot
       write outside the world folder however it spells the name. */
    for(const char* c = name; *c; c++)
        if(*c == '/' || *c == '\\' || *c == ':' || *c < 0x20 || *c > 0x7e) return 2u;
    if(!gSd.EnsureMounted(daisy::System::GetNow())) return 1u;
    alchemy::SdCard::BusyGuard busy(gSd);
    /* Make the folder if it is not there.
     *
     * A blank card has no /kyklophoria, and without this the first save anybody
     * ever does fails with a generic device error: f_open cannot create a file
     * in a directory that does not exist, and nothing in the message says so.
     * Created here rather than behind an "initialize" the player has to know
     * about, because needing to be told to press a button first is the same bug
     * with an extra step. FR_EXIST is success. */
    const FRESULT mk = f_mkdir(kWorldDir);
    if(mk != FR_OK && mk != FR_EXIST) return 1u;
    char path[96], tmp[96];
    std::snprintf(path, sizeof path, "%s/%s.kykw", kWorldDir, name);
    std::snprintf(tmp, sizeof tmp, "%s/%s.part", kWorldDir, name);
    if(!overwrite)
    {
        FILINFO fno;
        if(f_stat(path, &fno) == FR_OK) return kStatCardExists;
    }
    /* a resonate slot came off the card and is still there: BAD_STATE, not
       a second copy of a megabyte */
    if(gSlotRes[slot]) return 3u;
    const uint32_t n = gSlotLen[slot];
    if(n > sizeof gCardStage) return 1u;
    std::memcpy(gCardStage, gSlotBlob[slot], n);
    if(f_open(&gCardFil, tmp, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 1u;
    UINT wr = 0;
    const bool ok = f_write(&gCardFil, gCardStage, (UINT)n, &wr) == FR_OK && wr == n;
    f_close(&gCardFil);
    if(!ok) { f_unlink(tmp); return 1u; }
    /* f_rename will not replace an existing file, so the old one goes first —
       which is why overwrite had to be asked for before we got here. */
    if(overwrite) f_unlink(path);
    if(f_rename(tmp, path) != FR_OK) { f_unlink(tmp); return 1u; }
    ScanCard();
    return 0u;
}

static char             gUserName[kUserNameLen + 1] = {0};
static volatile uint8_t gWorldBusy = 0;
static StereoEngine KYK_AXI gEng;
/* Whether the audio thread is reading a slot's resonate region right now. A
   region is freed only when nothing reads it, because the World reads it in
   place: overwriting a region under a playing world is not a fault, it is a
   glitch nobody could trace. */
static bool ResSlotLive(uint8_t slot)
{
    if(slot >= kSlotCount || !gSlotRes[slot]) return false;
    const World* w = gEng.L.WorldPtr();
    return w && w->IsResonate() && w->Res().blob == gResArena[gSlotRes[slot] - 1];
}
static void ResRelease(uint8_t slot)
{
    if(slot < kSlotCount && gSlotRes[slot]) { gResOwner[gSlotRes[slot] - 1] = 0u; gSlotRes[slot] = 0u; }
}

/* Aiming runs on the main loop, never the audio callback: a few hundred world
 * evaluations is milliseconds, which is nothing between blocks and everything
 * inside one. Done once when a morph is aimed, not tracked. */
static void AimMorph()
{
    if(gMorphIdx == 0xFFu || !gMorphWorld.Ready()) return;
    const World* live = gEng.WorldPtr();
    if(!live || !live->Ready()) return;
    float p0[kMaxN], off[kMaxN];
    live->Fold(gEng.Control(), p0);
    AimSearch(*live, gMorphWorld, p0, gEng.sharp, off);
    gEng.L.SetMorphOffset(off, kMaxN);
    gEng.R.SetMorphOffset(off, kMaxN);
}

/* ── audio ↔ control shared state ────────────────────────────────────────── */
/* Telemetry is encoded on the control thread now, so there is no snapshot
 * buffer and nothing for the audio callback to do (see ModuleSource). */
static volatile uint32_t gCycLast = 0, gCycMax = 0, gCycSum = 0, gCycN = 0;
static volatile uint16_t gOverruns = 0, gDropped = 0;
static volatile float    gPayloadA = 0.f;
static volatile uint8_t  gResetPhase = 0;
static volatile int16_t  gStrike     = -1;     /* velocity 0-255 to strike with, -1 = none */
static bool              gKepOn      = false;
static float             gKepRadius  = 0.f;
static constexpr uint32_t kCpuHz     = 480000000u;
static constexpr uint32_t kCycBudget = kCpuHz / 48000u * kEngineBlockSamples;   /* 240 000 */

static inline uint32_t Cycles() { return DWT->CYCCNT; }

/* J2's rising edges, counted here because this is the only place the samples
 * exist. Schmitt, not a threshold: the jack is an AC-coupled audio input, so a
 * gate arrives as a step that decays and a release dips below zero, and one
 * comparison would count both ends of it. The refractory count is in samples
 * and is there for the same reason a hardware trigger input has one — 5 ms,
 * which is 200 Hz of clock and far past any musical division.
 *
 * Counting rather than acting: a step rotates world pointers and asks for an
 * expansion, and neither belongs under the audio ISR. */
static void ReadClock(const float* in, size_t n)
{
    static bool     armed = false;
    static uint32_t since = 0u;
    for(size_t i = 0; i < n; i++)
    {
        const float x = in[i];
        if(since < 0xFFFFu) since++;
        if(!armed && x > 0.25f && since > 240u) { armed = true; since = 0u; gClockEdges++; }
        else if(armed && x < 0.05f) armed = false;
    }
}

static void AudioCb(daisy::AudioHandle::InputBuffer in, daisy::AudioHandle::OutputBuffer out, size_t size)
{
    const uint32_t t0 = Cycles();
    if(gTour.Running()) ReadClock(in[1], size);

    /* pitch: v/oct on J3 (calibrated volts), coarse octaves, fine semitones */
    const float voct = hw.cv[0].Volts();
    const float f0   = 261.6256f * exp2f(voct + k_coarse.Value() + k_fine.Value() * (1.f / 12.f));

    /* position: pot offset (0..1) + CV (±5 V → ±1) per axis */
    /* Six, not four. The four CV jacks are axes 0-3; 4 and 5 are the World
     * page's two pots and have no jack, which is what the I/O map says they
     * are. A world with fewer axes than this never reads the extra ones —
     * Engine::SetControl writes the whole frame and the slew only follows
     * World::N() of it — so this costs the built-ins nothing. */
    float c[kMaxN];
    c[0] = k_pos0.Norm() + hw.cv[1].Volts() * 0.2f;
    c[1] = k_pos1.Norm() + hw.cv[2].Volts() * 0.2f;
    c[2] = k_pos2.Norm() + hw.cv[3].Volts() * 0.2f;
    c[3] = k_pos3.Norm() + hw.cv[4].Volts() * 0.2f;
    c[4] = k_pos4.Norm();
    c[5] = k_pos5.Norm();

    /* One multiplier over all six rate knobs, three octaves either side of
     * unity, with a detent at the centre so 1.0 is reachable by hand. */
    float ratex = exp2f((k_ratex.Norm() - 0.5f) * 6.f);
    if(ratex > 0.94f && ratex < 1.06f) ratex = 1.f;
    for(int p = 0; p < 6; p++)
    {
        gEng.rot.SetAngle(p, k_ang[p].Norm());
        gEng.rot.SetRate(p, (gMute & (1u << p)) ? 0.f
                                                : RateFromKnob(k_rate[p].Norm()) * ratex);
    }
    /* Squared, because the interesting behaviour is all in the bottom half of
     * the capture curve and the top of the knob is already fully locked. */
    const float cu = k_couple.Norm();
    gEng.rot.SetCouple(0.9f * cu * cu);
    gEng.rot.SetReach((int)k_reach.Value() + 1);
    gEng.spread       = k_spread.Value();
    gEng.spread_plane = (int)k_plane.Value();
    int sel = (int)k_rdiv.Value();
    if(sel < 0) sel = 0;
    if(sel > 3) sel = 3;
    const int rdiv = (int)kDivValues[sel];
    if(rdiv != gEng.L.render_div) gEng.SetRenderDiv(rdiv);
    /* Kepler: gravity's bottom quarter-turn is off. Crossing into it launches
     * the body; so does moving the radius knob meaningfully. */
    {
        const float gk = k_grav.Norm();
        const bool  on = gk > 0.02f && !(gMute & kMuteKepler);
        /* Exponential, because the period goes as 1/sqrt(G) and the long
         * orbits are the interesting end. This spans about half a minute per
         * revolution at the bottom to well under a second at the top, and a
         * high eccentricity stretches the slow end much further still. */
        gEng.kepler.gravity = 0.002f * exp2f(gk * 11.f);
        /* Softening decides whether there is an orbit at all, and the old
         * Linear(0.02, 0.30) put every setting outside the range where there
         * is one. Measured apsis drift per orbit: 0.005 gives 0.4 degrees, a
         * closed ellipse that takes 474 orbits to turn half way round; 0.02
         * gives 5.6; 0.08 — the value that shipped — gives 47.9, so the figure
         * pointed the other way after four orbits and never retraced itself.
         * The knob's own midpoint was 0.16, at 85 degrees an orbit.
         *
         * Exponential over 0.005 to 0.30 puts half the travel below 0.039, i.e.
         * below 19 degrees of drift, which is where the ellipse is still an
         * ellipse. The top is unchanged, so the rosette is still there for
         * anyone who wants it — it just is not the only thing on offer. */
        gEng.kepler.soften  = 0.005f * exp2f(k_soft.Norm() * 5.907f);   /* 0.005 .. 0.30 */
        gEng.kepler.damp    = k_damp.Value();
        gEng.kepler.plane   = (int)k_kplane.Value();
        gEng.kepler.bodies  = (int)k_kbody.Value() + 1;      /* selector is 0-based */
        /* Squared, because the interesting part of the range is the bottom:
         * a companion at full mass rearranges the orbit completely, and the
         * settings worth playing are the ones that perturb it. At zero this is
         * exactly the single-body orbit whatever the count says. */
        const float mk = k_kmass.Norm();
        gEng.kepler.companion = mk * mk;
        const float rad = k_radius.Value();
        const bool  moved = (rad > gKepRadius + 0.02f) || (rad < gKepRadius - 0.02f);
        if(on && (!gKepOn || moved))
        {
            gKepRadius = rad;
            gEng.kepler.Reset(rad, k_ecc.Norm());
        }
        else if(!on && gKepOn) gEng.kepler.Stop();
        gKepOn = on;
    }
    gEng.sharp = k_sharp.Norm();
    /* Nothing to blend towards means no blend, whatever the knob says.
     *
     * While a tour is running the clock owns this, and it is set from the
     * control loop with the rest of the tour's state rather than from here —
     * the Tour object is the control thread's and reading it under the ISR to
     * find the target pointer would be a race for no gain. The Morph knob is
     * inert for as long as the loop is running, which is the honest reading of
     * "the clock is driving": what it controls is exactly what the clock has
     * taken over. */
    if(!gTourOn)
        gEng.SetMorph(gMorphIdx == 0xFFu ? nullptr : &gMorphWorld,
                      gMorphIdx == 0xFFu ? 0.f : k_morph.Norm());
    /* 0.23 is the headroom the measured crest factor needs; the knob scales
     * from silence to that, so a cell can no longer peak past full scale. */
    gEng.SetGain(0.23f * k_level.Norm());
    if(gResetPhase) { gEng.L.ResetPhase(); gEng.R.ResetPhase(); gResetPhase = 0; }

    gEng.SetF0(f0);
    /* after SetF0: a strike retunes the bank to the pitch it is struck at */
    if(gStrike >= 0) { gEng.Strike((float)gStrike / 255.0f); gStrike = -1; }
    gEng.SetControl(c, kMaxN);
    gEng.Process(out[0], out[1], (int)size);
    gPayloadA = gEng.Payload()[4];

    const uint32_t cyc = Cycles() - t0;
    gCycLast = cyc;
    if(cyc > gCycMax) gCycMax = cyc;
    gCycSum += cyc;
    gCycN++;
    if(cyc > kCycBudget) gOverruns++;
}

/* ── HostLink extension source ───────────────────────────────────────────── */
struct ModuleSource : ExtSource
{
    int Telemetry(uint8_t flags, uint8_t* out, int cap) override
    {
        /* hand out the newest snapshot; ask the audio thread for the next.
         * A request that arrives before the previous one was served counts
         * as dropped (the host polls faster than 2 kHz, which it never does). */
        /* Encoded here, on the control thread, not in the audio callback.
         * It used to be published from the ISR for a tear-free snapshot, but
         * that put a few microseconds on top of whichever block happened to be
         * rendering, which is exactly the block that sets the CPU maximum. A
         * field or two may be torn instead; this is a display feed at 60 Hz
         * and nobody can see a one-frame inconsistency. */
        uint8_t pots[6], vals[6];
        const auto q8 = [](float v) {
            return (uint8_t)((v < 0.f ? 0.f : (v > 1.f ? 1.f : v)) * 255.f + 0.5f);
        };
        for(int i = 0; i < 6 && i < kNumPots; i++)
        {
            pots[i] = q8(hw.pots[i].Value());
            /* Where the pot is, and what the knob is worth. They are the same
             * number only once the pot has caught the value — which is exactly
             * the state the mirror could not show, because it only ever had
             * the first one. */
            vals[i] = q8(pager.Value((uint8_t)i));
        }
        const float tb = gTour.Blend(gEng.L.Block(), k_tglide.Norm());
        return EncodeTelemetry(gEng, flags, out, cap, pager.Page(), gMorphIdx, gMute, pots,
                               gWorldIdx, vals, (uint8_t)gTour.Len(), (uint8_t)gTour.At(),
                               (uint8_t)((tb < 0.f ? 0.f : (tb > 1.f ? 1.f : tb)) * 255.f + 0.5f));
    }
    /* The tour. Programming it is deferred like every other world change,
       because it is up to three lattice expansions; ticking it is not, because a
       step is a pointer rotation. */
    uint8_t TourSet(const uint8_t* entry, int n, uint8_t div) override
    {
        if(gTourSetReq || gTourLoadSlot >= 0 || gWorldBusy) return 9u;   /* BUSY */
        if(n > kTourMax) return 2u;
        for(int i = 0; i < n; i++) gTourWant[i] = entry[i];
        gTourWantLen = (uint8_t)n;
        gTourWantDiv = div;
        gTourSetReq  = 1u;
        return 0u;
    }
    uint8_t TourTick() override
    {
        if(!gTour.Running()) return 1u;
        gClockEdges++;
        return 0u;
    }
    bool TourState(uint8_t& len, uint8_t& div, uint8_t& at, uint8_t& blend, uint8_t* entry) override
    {
        len = (uint8_t)gTour.Len();
        div = gTour.Div();
        at  = (uint8_t)gTour.At();
        const float b = gTour.Blend(gEng.L.Block(), k_tglide.Norm());
        blend = (uint8_t)((b < 0.f ? 0.f : (b > 1.f ? 1.f : b)) * 255.f + 0.5f);
        for(int i = 0; i < (int)len && i < kTourMax; i++) entry[i] = gTour.Entry(i);
        return true;
    }
    bool SpaceInfo(SpaceHeader& h, uint32_t& crc, uint16_t& stride) override
    {
        const Space* s = gEng.SpacePtr();
        if(!s || !s->Attached()) return false;   /* an analytic world has no lattice */
        h      = s->Header();
        crc    = gBlobCrc;
        stride = (uint16_t)s->Stride();
        return true;
    }
    bool Cell(uint32_t idx, uint8_t* mags, float* payload, int& k, int& p) override
    {
        const Space* s = gEng.SpacePtr();
        if(!s || !s->Attached() || idx >= s->PointCount()) return false;
        k = s->K(); p = s->P();
        for(int i = 0; i < k; i++) mags[i] = detail::MagToU8(s->Mags(idx)[i]);
        for(int j = 0; j < p; j++) payload[j] = s->Payload(idx)[j];
        return true;
    }
    void Stats(ExtStats& s) override
    {
        s.cycles_last   = gCycLast;
        s.cycles_max    = gCycMaxWin;
        s.cycles_avg    = gCycAvgWin;
        s.overruns      = gOverruns;
        s.dropped       = gDropped;
        s.render_div    = (uint8_t)gEng.L.render_div;
        s.cycles_budget = kCycBudget;
    }
    const World*  ResonateWorld() override { const World* w = gEng.L.WorldPtr(); return w && w->IsResonate() ? w : nullptr; }
    const Engine* ResonateEngine() override { return &gEng.L; }

    int CardWorlds(const char** names, int max) override
    {
        const int n = gCardCount < max ? gCardCount : max;
        for(int i = 0; i < n; i++) names[i] = gCardNames[i];
        return n;
    }

    uint8_t PutWorld(uint32_t total, uint32_t off, const uint8_t* data, int len) override
    {
        if(gUserReq || gWorldBusy) return 1u;        /* a swap is already in flight */
        const uint8_t st = gRx.Take(total, off, data, len);
        if(st != 0u) return st;
        if(gRx.Done()) gUserReq = 1u;
        return 0u;
    }

    uint8_t PutSlot(uint8_t slot, uint32_t total, uint32_t off,
                    const uint8_t* data, int len) override
    {
        if(slot >= kSlotCount) return 2u;
        if(ResSlotLive(slot)) return 3u;            /* its region is being read */
        if(gUserReq || gSlotStore >= 0 || gWorldBusy) return 9u;
        const uint8_t st = gRx.Take(total, off, data, len);
        if(st != 0u) return st;
        /* The same receiver PutWorld uses. One host, one transfer at a time,
           and the guard above says so rather than letting two interleave. */
        if(gRx.Done()) gSlotStore = (int8_t)slot;
        return 0u;
    }

    int SlotBlob(uint8_t slot, uint32_t offset, uint8_t* out, int max, uint32_t& total) override
    {
        if(slot >= kSlotCount) { total = 0; return -1; }
        /* An empty slot is a state, not an error: total 0 and status ok. */
        if(!gSlotLen[slot]) { total = 0; return 0; }
        total = gSlotLen[slot];
        if(offset >= total) return 0;
        uint32_t n = total - offset;
        if(n > (uint32_t)max) n = (uint32_t)max;
        const uint8_t* src = gSlotRes[slot] ? gResArena[gSlotRes[slot] - 1] : gSlotBlob[slot];
        std::memcpy(out, src + offset, n);
        return (int)n;
    }

    uint8_t SaveCardWorld(uint8_t slot, const char* name, bool overwrite) override
    {
        if(gWorldBusy) return 9u;
        return SaveSlotToCard(slot, name, overwrite);
    }

    int Slots(uint8_t& live, uint8_t& target, const char** names, int max) override
    {
        live = gSlotLive; target = gSlotTarget;
        const int n = max < kSlotCount ? max : kSlotCount;
        for(int i = 0; i < n; i++) names[i] = gSlotLen[i] ? gSlotName[i] : nullptr;
        return n;
    }

    uint8_t Action(uint8_t op, const uint8_t* args, int len) override
    {
        switch(op)
        {
            case kActResetPhase: gResetPhase = 1; return 0u;
            case kActStrike:
            {
                const World* w = gEng.L.WorldPtr();
                if(!w || !w->IsResonate()) return 3u;
                gStrike = (int16_t)(len >= 1 ? args[0] : 204u);
                return 0u;
            }
            case kActTune:
            {
                if(len < 2 || args[0] > 2) return 2u;
                const World* w = gEng.L.WorldPtr();
                if(!w || !w->IsResonate()) return 3u;
                /* three floats and a flag the audio thread reads at its
                   next block: the same shape as the morph amount */
                gEng.SetTune((Engine::Tune)args[0], TuneValue(args[0], args[1]));
                return 0u;
            }
            case kActRenderDiv: (void)args; (void)len; return 1u;   /* the pot owns it on the module */
            case kActSelectWorld:
                if(len < 1 || args[0] >= worlds::kCount) return 2u;
                if(gWorldBusy) return 9u;                     /* BUSY */
                gWorldReq = args[0];
                return 0u;
            case kActScanCard:
                ScanCard();
                return 0u;
            case kActLoadCardWorld:
                if(len < 1 || args[0] >= gCardCount) return 2u;
                /* a resonate world needs a region to live in, and a region is
                   a slot's: load it into a slot and play that (the page does
                   exactly this behind its load button) */
                if(IsKykm(gCardNames[args[0]])) return 3u;
                if(gWorldBusy || gCardLoadReq >= 0) return 9u;
                gCardLoadReq = (int8_t)args[0];
                return 0u;
            case kActAimMorph:
                if(len >= 1 && args[0] == 0u)
                {
                    const float zero[kMaxN] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
                    gEng.L.SetMorphOffset(zero, kMaxN);
                    gEng.R.SetMorphOffset(zero, kMaxN);
                    return 0u;
                }
                if(gMorphIdx == 0xFFu) return 1u;
                if(gAimReq) return 9u;
                gAimReq = 1u;
                return 0u;
            case kActMotionMute:
                if(len < 2) return 2u;
                gMute = (uint16_t)(args[0] | (args[1] << 8));
                return 0u;
            case kActPhase:
                if(len < 1 || args[0] > 2) return 2u;
                gEng.L.SetPhaseOverride(args[0] == 2 ? World::Phase::Cosine : World::Phase::Sine,
                                        args[0] != 0);
                gEng.R.SetPhaseOverride(args[0] == 2 ? World::Phase::Cosine : World::Phase::Sine,
                                        args[0] != 0);
                return 0u;
            case kActMorphWorld:
                /* 0xFF clears the target, which is the only way to get the
                   single-world path back regardless of where the knob sits. */
                if(len < 1) return 2u;
                if(args[0] != 0xFFu && args[0] >= worlds::kCount) return 2u;
                if(gWorldBusy) return 9u;
                gMorphReq = args[0];
                return 0u;
            case kActSlotLive:
                if(len < 1 || args[0] >= kSlotCount) return 2u;
                if(!gSlotLen[args[0]]) return 2u;          /* nothing in it */
                if(gWorldBusy || gSlotLiveReq >= 0) return 9u;
                gSlotLiveReq = (int8_t)args[0];
                return 0u;
            case kActSlotTarget:
                if(len < 1) return 2u;
                if(args[0] != 0xFFu && (args[0] >= kSlotCount || !gSlotLen[args[0]])) return 2u;
                /* a resonate world is not a frame and cannot be blended
                   towards: BAD_STATE, the same word as a morph that cannot happen */
                if(args[0] != 0xFFu && gSlotRes[args[0]]) return 3u;
                if(gWorldBusy || gSlotTargetReq >= 0) return 9u;
                gSlotTargetReq = args[0] == 0xFFu ? (int8_t)kSlotCount : (int8_t)args[0];
                return 0u;
            case kActSnapshot:
            {
                if(len < 1 || args[0] >= kSlotCount) return 2u;
                const uint8_t which = len >= 2 ? args[1] : 0xFFu;
                if(which != 0xFFu && which >= worlds::kCount) return 2u;
                /* a resonate world has no frame to sample; and a slot whose
                   region is playing cannot be written over */
                if(which == 0xFFu && gEng.L.WorldPtr() && gEng.L.WorldPtr()->IsResonate()) return 3u;
                if(ResSlotLive(args[0])) return 3u;
                if(gWorldBusy || gSnapReq >= 0) return 9u;
                gSnapWorld = which;
                gSnapReq   = (int8_t)args[0];
                return 0u;
            }
            case kActCardToSlot:
                /* Deferred, because a card read is slow and the read path is
                   already on the control loop. Unlike a save, which touches no
                   audio state and is done where it is asked. */
                if(len < 2 || args[1] >= kSlotCount) return 2u;
                if(ResSlotLive(args[1])) return 3u;   /* its region is being read */
                if(gWorldBusy || gCardToSlotReq >= 0) return 9u;
                gCardToSlotCard = args[0];
                gCardToSlotReq  = (int8_t)args[1];
                return 0u;
            case kActSlotSwap:
            {
                if(len < 2 || args[0] >= kSlotCount || args[1] >= kSlotCount) return 2u;
                const uint8_t a = args[0], b = args[1];
                if(a == b) return 0u;
                /* Safe to do here, in the handler, and worth saying why: what
                 * is *playing* is an expanded World in gWorlds and the morph
                 * target is another in gMorphWorld, so neither reads a blob
                 * except at the moment it is loaded. Exchanging the blobs
                 * cannot reach the audio thread.
                 *
                 * Chunked through a small stack buffer rather than a second
                 * staging area: 6.7 KB of SDRAM reserved for a swap nobody
                 * does in a hurry is 6.7 KB spent on nothing. 13.5 KB of SDRAM
                 * memcpy is on the order of a hundred microseconds. */
                uint8_t        tmp[256];
                uint32_t       n = gSlotLen[a] > gSlotLen[b] ? gSlotLen[a] : gSlotLen[b];
                /* a resonate slot's length is its region's, not its blob's;
                   the region numbers swap below and the blob bytes are moot */
                if(n > kUserBlobMax) n = kUserBlobMax;
                for(uint32_t off = 0; off < n; off += (uint32_t)sizeof tmp)
                {
                    const uint32_t c = (n - off) < (uint32_t)sizeof tmp
                                           ? (n - off) : (uint32_t)sizeof tmp;
                    std::memcpy(tmp, gSlotBlob[a] + off, c);
                    std::memcpy(gSlotBlob[a] + off, gSlotBlob[b] + off, c);
                    std::memcpy(gSlotBlob[b] + off, tmp, c);
                }
                const uint32_t ln = gSlotLen[a]; gSlotLen[a] = gSlotLen[b]; gSlotLen[b] = ln;
                const uint8_t  rs = gSlotRes[a]; gSlotRes[a] = gSlotRes[b]; gSlotRes[b] = rs;
                if(gSlotRes[a]) gResOwner[gSlotRes[a] - 1] = (uint8_t)(a + 1);
                if(gSlotRes[b]) gResOwner[gSlotRes[b] - 1] = (uint8_t)(b + 1);
                char nm[kUserNameLen + 1];
                std::memcpy(nm, gSlotName[a], sizeof nm);
                std::memcpy(gSlotName[a], gSlotName[b], sizeof nm);
                std::memcpy(gSlotName[b], nm, sizeof nm);
                /* The indices follow the contents. Without this, rearranging
                   the list would leave "playing" pointing at whatever moved
                   into that number. */
                if(gSlotLive == a) gSlotLive = b; else if(gSlotLive == b) gSlotLive = a;
                if(gSlotTarget == a) gSlotTarget = b; else if(gSlotTarget == b) gSlotTarget = a;
                return 0u;
            }
            case kActSlotFree:
                if(len < 1 || args[0] >= kSlotCount) return 2u;
                if(ResSlotLive(args[0])) return 3u;   /* the world reads it where it lies */
                ResRelease(args[0]);
                gSlotLen[args[0]] = 0u;
                if(gSlotLive == args[0]) gSlotLive = 0xFFu;
                if(gSlotTarget == args[0]) { gSlotTarget = 0xFFu; gMorphReq = 0xFFu; }
                return 0u;
            default: return 1u;
        }
    }
    int Worlds(uint8_t& count, uint8_t& current, const char** names, const char** notes,
               uint8_t* kinds, int max) override
    {
        count   = (uint8_t)(worlds::kCount < max ? worlds::kCount : max);
        current = gWorldIdx;
        for(int i = 0; i < (int)count; i++)
        {
            names[i] = worlds::Get((uint8_t)i).name;
            notes[i] = worlds::Get((uint8_t)i).note;
            kinds[i] = (uint8_t)worlds::Get((uint8_t)i).kind;
        }
        return count;
    }

    /* The formula, so the page can evaluate the space itself instead of
     * asking for it a point at a time. About 1.3 KB for the whole world. */
    int Basis(uint8_t world, uint32_t offset, uint8_t* out, int max, uint32_t& total) override
    {
        total = 0;
        if(!worlds::IsAnalytic(world)) return 0;
        World w;
        if(!worlds::Point(world, w, kBootP, nullptr)) return 0;
        if(w.Which() == World::Kind::Bend)
        {
            uint8_t blob[kBendBlobBytes];
            total = (uint32_t)BendBlob(w.Bend(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(w.Which() == World::Kind::Modal)
        {
            uint8_t blob[kModalBlobBytes];
            total = (uint32_t)ModalBlob(w.Modal(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(w.Which() == World::Kind::Lock)
        {
            uint8_t blob[kLockBlobBytes];
            total = (uint32_t)LockBlob(w.Lock(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(w.Which() == World::Kind::Unison)
        {
            uint8_t blob[kUnisonBlobBytes];
            total = (uint32_t)UnisonBlob(w.Unison(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(w.Which() == World::Kind::Table)
        {
            uint8_t blob[kShapeBlobBytes];
            total = (uint32_t)ShapeBlob(w.Shapes(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(w.Which() == World::Kind::Formant)
        {
            uint8_t blob[kFormantBlobBytes];
            total = (uint32_t)FormantBlob(w.Formant(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(w.Which() == World::Kind::Fm)
        {
            uint8_t blob[kFmBlobBytes];
            total = (uint32_t)FmBlob(w.Fm(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        const EigenBasis& b = w.Basis();
        const uint32_t    hdr = 2u + 8u;
        total = hdr + (uint32_t)(sizeof(float) * (size_t)b.k * (size_t)(b.n + 1));
        if(offset >= total) return 0;
        uint32_t n = total - offset;
        if(n > (uint32_t)max) n = (uint32_t)max;
        for(uint32_t i = 0; i < n; i++)
        {
            const uint32_t at = offset + i;
            uint8_t        v  = 0;
            if(at == 0) v = (uint8_t)b.n;
            else if(at == 1) v = (uint8_t)b.k;
            else if(at < 6) { float e = b.extent; v = ((const uint8_t*)&e)[at - 2]; }
            else if(at < 10) { float f = b.floor_; v = ((const uint8_t*)&f)[at - 6]; }
            else
            {
                const uint32_t o  = at - hdr;
                const uint32_t fi = o / 4u, bo = o % 4u;
                const float    x  = fi < (uint32_t)b.k ? b.mean[fi] : b.comp[fi - (uint32_t)b.k];
                v                 = ((const uint8_t*)&x)[bo];
            }
            out[i] = v;
        }
        return (int)n;
    }

    uint32_t gBlobCrc   = 0;
    uint32_t gCycMaxWin = 0, gCycAvgWin = 0;
};
static ModuleSource  gSource;
static KykExt         gExt(gSource);
static hostlink::Host host(presets, "kyk", "Kyklophoria", KYK_FW_VERSION, KYK_GIT_HASH);

/* Switch worlds on the control thread. Analytic is a pointer write; a
 * tabulated world is expanded into the spare buffer first, which takes long
 * enough that it must not happen anywhere near the audio callback. */
/* Build the world a tour entry names into one of the ring's three buffers. Slow
 * (a lattice world is an expansion), which is why it only ever runs from the
 * control loop and only ever into the buffer the ring says nobody is hearing. */
static bool TourLoad(int slot, uint8_t w)
{
    if(slot < 0 || slot >= kTourSlots) return false;
    World& dst = *kTourWorld[slot];
    if(w & 0x80u)
    {
        const uint8_t sl = (uint8_t)(w & 0x7Fu);
        if(sl >= kSlotCount || !gSlotLen[sl]) return false;
        /* a resonate slot is not a stop: a step blends between its two
           worlds and a resonator has no frame to blend */
        if(gSlotRes[sl]) return false;
        return dst.UseUserWorld(gSlotBlob[sl], gSlotLen[sl], kBootP, nullptr) == UserError::Ok;
    }
    if(w >= worlds::kCount) return false;
    if(worlds::IsAnalytic(w)) return worlds::Point(w, dst, kBootP, nullptr, &gVertTable[slot]);
    const size_t n = worlds::Expand(w, kBootN, kBootSide, kBootK, kBootP, gBlob[slot], sizeof(gBlob[slot]));
    if(!n || gSpace[slot].Attach(gBlob[slot], n) != SpaceError::Ok) return false;
    dst.UseLattice(&gSpace[slot]);
    return true;
}

/* Point the engine at the ring. Cheap — both worlds are already built — so it
 * can happen the moment the clock says so. */
static void TourPoint()
{
    if(!gTour.Running()) return;
    const int lv = gTour.LiveSlot(), tg = gTour.TargetSlot();
    if(!kTourWorld[lv]->Ready() || !kTourWorld[tg]->Ready()) return;
    gWorldBusy = 1;
    __asm__ volatile("dmb" ::: "memory");
    gEng.SetWorld(kTourWorld[lv]);
    gBufIdx = (uint8_t)(lv < 2 ? lv : gBufIdx);   /* keep the double buffer honest */
    const uint8_t lw = gTour.WorldForSlot(lv), tw = gTour.WorldForSlot(tg);
    gWorldIdx   = (lw & 0x80u) ? 0xFFu : lw;
    gSlotLive   = (lw & 0x80u) ? (uint8_t)(lw & 0x7Fu) : 0xFFu;
    gMorphIdx   = (tw & 0x80u) ? kMorphUser : tw;
    gSlotTarget = (tw & 0x80u) ? (uint8_t)(tw & 0x7Fu) : 0xFFu;
    gWorldBusy = 0;
}

static void ServeWorldRequest()
{
    /* A tour, programmed from the page: build every buffer it needs, then play
     * the first world. Deferred to here because that is up to three lattice
     * expansions and the request that asked for it should return. */
    if(gTourSetReq)
    {
        gTourSetReq = 0u;
        if(gTourWantLen < 2u) { gTour.Clear(); gEng.SetMorph(nullptr, 0.f); gMorphIdx = 0xFFu; return; }
        if(gTour.Set(gTourWant, gTourWantLen, gTourWantDiv))
        {
            bool ok = true;
            for(int s = 0; s < gTour.Slots(); s++)
                if(!TourLoad(s, gTour.WorldForSlot(s))) ok = false;
            if(ok) TourPoint();
            else gTour.Clear();
        }
        return;
    }
    /* A world chosen by hand stops the loop.
     *
     * Everything below builds into the same three buffers the ring is using, so
     * the two cannot both be right — and of the two possible rules, "the hand
     * wins and the clock stops" is the one somebody can predict from the outside.
     * The alternative, letting the next step quietly undo what you just chose,
     * is the kind of state nobody can see and nobody can explain. */
    if(gTour.Running()
       && (gWorldReq != 0xFFu || gMorphReq != 0xFFu || gSlotLiveReq >= 0 || gSlotTargetReq >= 0
           || gCardLoadReq >= 0 || gUserReq))
    {
        gTour.Clear();
        gTourOn       = 0u;
        gTourLoadSlot = -1;
    }
    /* The load a step asked for. One per pass, like every other world change,
     * and it has until the next step to arrive. */
    if(gTourLoadSlot >= 0)
    {
        const int sl = gTourLoadSlot;
        const uint8_t w = gTourLoadWorld;
        gTourLoadSlot = -1;
        TourLoad(sl, w);
        return;
    }
    if(gAimReq) { gAimReq = 0u; AimMorph(); return; }
    if(gCardLoadReq >= 0)
    {
        /* Card work is slow and must not happen under the audio callback, so
         * it lands here with every other deferred world change. */
        const uint8_t idx = (uint8_t)gCardLoadReq;
        gCardLoadReq = -1;
        const size_t n = ReadCardWorld(idx);
        if(n)
        {
            gWorldBusy = 1;
            const uint8_t wi = (uint8_t)(gBufIdx ^ 1u);
            if(gWorlds[wi].UseUserWorld(gCardStage, n, kBootP, nullptr, gUserName)
               == UserError::Ok)
            {
                __asm__ volatile("dmb" ::: "memory");
                gEng.SetWorld(&gWorlds[wi]);
                gBufIdx   = wi;
                gWorldIdx = 0xFFu;
            }
            gWorldBusy = 0;
        }
        return;
    }
    if(gUserReq)
    {
        /* Into the buffer the audio thread is not reading, exactly as a
         * built-in switch does. A parse that refuses leaves that spare buffer
         * holding rubbish, which costs nothing — it is never swapped in. */
        gUserReq = 0u;
        gWorldBusy = 1;
        const uint8_t wi = (uint8_t)(gBufIdx ^ 1u);
        const UserError e = gWorlds[wi].UseUserWorld(gRx.Blob(), gRx.Size(), kBootP,
                                                     nullptr, gUserName);
        gRx.Reset();
        if(e == UserError::Ok)
        {
            __asm__ volatile("dmb" ::: "memory");
            gEng.SetWorld(&gWorlds[wi]);
            gBufIdx   = wi;
            gWorldIdx = 0xFFu;          /* not one of the built-ins any more */
        }
        gWorldBusy = 0;
        return;
    }
    /* A completed transfer into a slot. Just bytes: nothing is parsed until
     * somebody asks to play it, so a blob that turns out to be rubbish costs a
     * slot and not the sound. The name is read out of the header here so the
     * list can show it without parsing the whole thing every time. */
    if(gSlotStore >= 0)
    {
        const uint8_t sl = (uint8_t)gSlotStore;
        gSlotStore = -1;
        const size_t n = gRx.Size();
        if(sl < kSlotCount && n && n <= kUserBlobMax && !ResSlotLive(sl))
        {
            ResRelease(sl);
            std::memcpy(gSlotBlob[sl], gRx.Blob(), n);
            gSlotLen[sl] = (uint32_t)n;
            for(int c = 0; c < kUserNameLen; c++)
            {
                const char ch = n > (size_t)(16 + c) ? (char)gSlotBlob[sl][16 + c] : '\0';
                gSlotName[sl][c] = (ch >= 0x20 && ch < 0x7f) ? ch : '\0';
            }
            gSlotName[sl][kUserNameLen] = '\0';
            if(!gSlotName[sl][0]) { gSlotName[sl][0] = '?'; gSlotName[sl][1] = '\0'; }
        }
        gRx.Reset();
        return;
    }
    /* Sample the live world at the 24-cell vertices into a slot, as a world
     * somebody can open and edit.
     *
     * Only a Lock-shaped world can be handed over as nodes, and exactly one of
     * the twenty-one built-ins is — so for a formula world this is the only
     * thing there is, and it is honestly a snapshot: twenty-four spectra read
     * off the formula at twenty-four points. It will be rendered at sine phase,
     * so a phase-blind world will not sound like its original, and for the
     * vertex worlds — which currently put a "saw" and a "square" on their
     * vertices that are neither — it will sound better.
     *
     * Built straight into the slot rather than through a staging buffer: the
     * blob is the only copy needed and a second 6.7 KB of scratch for it would
     * be 6.7 KB spent on nothing. */
    if(gSnapReq >= 0)
    {
        const uint8_t sl = (uint8_t)gSnapReq;
        gSnapReq = -1;
        /* A named world is built into scratch, so sampling one leaves whatever
           is playing alone — which is what "start a new world from Lock" wants.
           A null vertex table is deliberate: the vertex worlds hold pointers
           into one, so Point refuses them here rather than being handed a table
           the morph is using. Reach those through the live path instead. */
        const uint8_t which = gSnapWorld;
        gSnapWorld = 0xFFu;
        const World* srcp = &gWorlds[gBufIdx];
        if(which != 0xFFu)
        {
            /* Built into scratch — its own blob, space and vertex table — so
               opening a world to edit leaves the live world and the morph
               target alone. A lattice is expanded here too, which costs the
               same 8 to 11 ms that selecting it would. */
            bool ok = false;
            if(worlds::IsAnalytic(which))
                ok = worlds::Point(which, gScratch, kBootP, nullptr, &gVertTable[kScratchSlot]);
            else
            {
                const size_t sn = worlds::Expand(which, kBootN, kBootSide, kBootK, kBootP,
                                                 gBlob[kScratchSlot], sizeof(gBlob[kScratchSlot]));
                ok = sn != 0 && gSpace[kScratchSlot].Attach(gBlob[kScratchSlot], sn) == SpaceError::Ok;
                if(ok) gScratch.UseLattice(&gSpace[kScratchSlot]);
            }
            srcp = ok ? &gScratch : nullptr;
        }
        const World& live = *(srcp ? srcp : &gWorlds[gBufIdx]);
        if(sl < kSlotCount && srcp && live.Ready())
        {
            const int n = 4;
            const int k = live.K() < kShapeK ? live.K() : kShapeK;
            const size_t need = UserBlobSize(n, k, kWorldNodes);
            if(need <= kUserBlobMax)
            {
                uint8_t* p = gSlotBlob[sl];
                std::memset(p, 0, need);
                const uint32_t magic = kUserMagic;
                std::memcpy(p, &magic, 4);
                /* A snapshot of a world with effects keeps the effects. The
                   nodes hold the *unshaped* spectra, which is exactly how the
                   worlds that have them work — blend the grid, then shape the
                   rendered cycle — so carrying them over is the faithful thing
                   and dropping them would make "open Grit to edit" hand back
                   something that cannot grit. The axes they name go on doing
                   their placement job as well, which is the honest cost of
                   snapshotting a grid onto a polytope. */
                bool any = false;
                for(int q = 0; q < 2; q++)
                    if(live.FxShaper(q) != Shaper::None) any = true;
                const uint16_t ver = any ? kUserVersionFx : kUserVersion;
                std::memcpy(p + 4, &ver, 2);
                p[6] = (uint8_t)n; p[7] = (uint8_t)k; p[8] = (uint8_t)kWorldNodes; p[9] = 1u;
                const float sigma = 0.26f;
                std::memcpy(p + 10, &sigma, 4);
                for(int q = 0; q < 2; q++)
                    p[14 + q] = live.FxShaper(q) == Shaper::None
                                    ? 0u
                                    : (uint8_t)((uint8_t)(live.FxAxis(q) << 4) | (uint8_t)live.FxShaper(q));
                const uint8_t named = which != 0xFFu ? which : gWorldIdx;
                const char*   nm    = named == 0xFFu ? "snapshot" : worlds::Get(named).name;
                std::snprintf((char*)(p + 16), kUserNameLen + 1, "%s", nm);
                size_t at = kUserHeader;
                for(int v = 0; v < kWorldNodes; v++)
                {
                    /* the 24-cell's own vertices, plane by plane, which is the
                       arrangement import and the Lock world both use */
                    float pos[kMaxN] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
                    int   c = 0;
                    for(int i = 0; i < 4; i++)
                        for(int j = i + 1; j < 4; j++)
                            for(int si = -1; si <= 1; si += 2)
                                for(int sj = -1; sj <= 1; sj += 2, c++)
                                    if(c == v)
                                    {
                                        pos[i] = 0.5f + (float)si * 0.42f * 0.7071f;
                                        pos[j] = 0.5f + (float)sj * 0.42f * 0.7071f;
                                    }
                    float   mags[kMaxK] = {0.f}, pay[kMaxP] = {0.f}, folded[kMaxN];
                    Weights wt;
                    live.Fold(pos, folded);
                    live.Evaluate(folded, gEng.sharp, mags, pay, wt);
                    for(int a = 0; a < n; a++) { std::memcpy(p + at, &pos[a], 4); at += 4; }
                    for(int i = 0; i < k; i++) { std::memcpy(p + at, &mags[i], 4); at += 4; }
                }
                ResRelease(sl);
                gSlotLen[sl] = (uint32_t)need;
                std::snprintf(gSlotName[sl], sizeof gSlotName[sl], "%s", nm);
            }
        }
        return;
    }
    /* A card file into a slot. The existing read path, landing somewhere it can
     * be kept instead of going straight live — which is what makes the card a
     * library rather than a one-shot load. Parsed before it is kept, so a slot
     * never holds something that would be refused later. */
    if(gCardToSlotReq >= 0)
    {
        const uint8_t sl = (uint8_t)gCardToSlotReq;
        gCardToSlotReq = -1;
        if(sl < kSlotCount && gCardToSlotCard < gCardCount && IsKykm(gCardNames[gCardToSlotCard]))
        {
            /* a resonate world: into the region this slot already has, or
               a free one; the file's base name is its name, since the
               header carries none. Probed as the world it will be before
               the slot claims it, like a frame world is. */
            int r = gSlotRes[sl] ? gSlotRes[sl] - 1 : -1;
            if(r < 0) for(int k = 0; k < kResRegions; k++) if(!gResOwner[k]) { r = k; break; }
            if(r >= 0)
            {
                const size_t n = ReadCardResonate(gCardToSlotCard, gResArena[r], kResRegionBytes);
                if(n)
                {
                    gScratch.UseResonate(gResArena[r], (uint32_t)n);
                    if(gScratch.IsResonate())
                    {
                        gSlotRes[sl]  = (uint8_t)(r + 1);
                        gResOwner[r]  = (uint8_t)(sl + 1);
                        gSlotLen[sl]  = (uint32_t)n;
                        const char* nm = gCardNames[gCardToSlotCard];
                        const size_t bl = std::strlen(nm) - 5;
                        std::snprintf(gSlotName[sl], sizeof gSlotName[sl], "%.*s",
                                      (int)(bl < (size_t)kUserNameLen ? bl : (size_t)kUserNameLen), nm);
                    }
                    else if(!gSlotRes[sl]) gResOwner[r] = 0u;
                }
                else if(!gSlotRes[sl]) gResOwner[r] = 0u;
            }
            return;
        }
        const size_t n = ReadCardWorld(gCardToSlotCard);
        if(n && sl < kSlotCount && n <= kUserBlobMax)
        {
            if(gScratch.UseUserWorld(gCardStage, n, kBootP, nullptr) == UserError::Ok)
            {
                ResRelease(sl);
                std::memcpy(gSlotBlob[sl], gCardStage, n);
                gSlotLen[sl] = (uint32_t)n;
                for(int c = 0; c < kUserNameLen; c++)
                {
                    const char ch = n > (size_t)(16 + c) ? (char)gSlotBlob[sl][16 + c] : '\0';
                    gSlotName[sl][c] = (ch >= 0x20 && ch < 0x7f) ? ch : '\0';
                }
                gSlotName[sl][kUserNameLen] = '\0';
                if(!gSlotName[sl][0]) { gSlotName[sl][0] = '?'; gSlotName[sl][1] = '\0'; }
            }
        }
        return;
    }
    /* Play a slot. The same double-buffered swap a card world uses: parse into
     * the buffer the audio thread is not reading, then point the engine at it. */
    if(gSlotLiveReq >= 0)
    {
        const uint8_t sl = (uint8_t)gSlotLiveReq;
        gSlotLiveReq = -1;
        if(sl < kSlotCount && gSlotLen[sl])
        {
            gWorldBusy = 1;
            const uint8_t wi = (uint8_t)(gBufIdx ^ 1u);
            bool ok;
            if(gSlotRes[sl])
            {
                /* the World reads the region in place; the engine's SetWorld
                   builds the voice from it */
                gWorlds[wi].UseResonate(gResArena[gSlotRes[sl] - 1], gSlotLen[sl]);
                ok = gWorlds[wi].IsResonate();
                if(ok) std::snprintf(gUserName, sizeof gUserName, "%s", gSlotName[sl]);
            }
            else ok = gWorlds[wi].UseUserWorld(gSlotBlob[sl], gSlotLen[sl], kBootP, nullptr, gUserName)
                      == UserError::Ok;
            if(ok)
            {
                __asm__ volatile("dmb" ::: "memory");
                gEng.SetWorld(&gWorlds[wi]);
                gBufIdx   = wi;
                gWorldIdx = 0xFFu;
                gSlotLive = sl;
            }
            gWorldBusy = 0;
        }
        return;
    }
    /* Morph towards a slot. This is the thing that was unrepresentable: the
     * morph target index space was the twenty-one built-ins and nothing else,
     * so a world you made could never be one end of a blend. It parses into the
     * morph slot, which is its own World for exactly this reason. */
    if(gSlotTargetReq >= 0)
    {
        const uint8_t sl = (uint8_t)gSlotTargetReq;
        gSlotTargetReq = -1;
        if(sl >= kSlotCount)                       /* the clear sentinel */
        {
            gEng.SetMorph(nullptr, 0.f);
            gMorphIdx = 0xFFu; gSlotTarget = 0xFFu;
        }
        else if(gSlotLen[sl]
                && gMorphWorld.UseUserWorld(gSlotBlob[sl], gSlotLen[sl], kBootP, nullptr) == UserError::Ok)
        {
            __asm__ volatile("dmb" ::: "memory");
            gMorphIdx   = kMorphUser;
            gSlotTarget = sl;
        }
        return;
    }
    /* The morph target, built into its own slot so a world switch can swap the
     * other two underneath it without disturbing what we are blending towards. */
    const uint8_t mreq = gMorphReq;
    if(mreq != 0xFFu)
    {
        gMorphReq = 0xFFu;
        if(mreq >= worlds::kCount) { gEng.SetMorph(nullptr, 0.f); gMorphIdx = 0xFFu; }
        else
        {
            bool ok = true;
            if(worlds::IsAnalytic(mreq))
                ok = worlds::Point(mreq, gMorphWorld, kBootP, nullptr, &gVertTable[kMorphSlot]);
            else
            {
                const size_t n = worlds::Expand(mreq, kBootN, kBootSide, kBootK, kBootP,
                                                gBlob[kMorphSlot], sizeof(gBlob[kMorphSlot]));
                ok = n != 0 && gSpace[kMorphSlot].Attach(gBlob[kMorphSlot], n) == SpaceError::Ok;
                if(ok) gMorphWorld.UseLattice(&gSpace[kMorphSlot]);
            }
            __asm__ volatile("dmb" ::: "memory");
            gMorphIdx = ok ? mreq : 0xFFu;
        }
        return;
    }
    const uint8_t req = gWorldReq;
    if(req == 0xFFu || req >= worlds::kCount) return;
    gWorldReq  = 0xFFu;
    gWorldBusy = 1;
    const uint8_t wi = (uint8_t)(gBufIdx ^ 1u);
    if(worlds::IsAnalytic(req))
    {
        worlds::Point(req, gWorlds[wi], kBootP, nullptr, &gVertTable[wi]);
    }
    else
    {
        const size_t n = worlds::Expand(req, kBootN, kBootSide, kBootK, kBootP,
                                        gBlob[wi], sizeof(gBlob[wi]));
        if(n == 0 || gSpace[wi].Attach(gBlob[wi], n) != SpaceError::Ok) { gWorldBusy = 0; return; }
        gWorlds[wi].UseLattice(&gSpace[wi]);
    }
    __asm__ volatile("dmb" ::: "memory");
    gEng.SetWorld(&gWorlds[wi]);
    gBufIdx    = wi;
    gWorldIdx  = req;
    gWorldBusy = 0;
}

/* one-second stats window and the CV out, from the control loop (~40 Hz) */
/* The clock, the free-run, and the blend — all on the control thread, because
 * a step rotates world pointers and asks for an expansion. */
static void TourService()
{
    static uint32_t free_at = 0u;
    gTourOn = gTour.Running() ? 1u : 0u;
    if(!gTour.Running()) return;
    const uint32_t blk = gEng.L.Block();
    uint8_t        n   = gClockEdges;
    if(n)
    {
        gClockEdges = (uint8_t)(gClockEdges - n);
        for(uint8_t i = 0; i < n; i++)
        {
            const TourStep s = gTour.Edge(blk);
            if(!s.step) continue;
            TourPoint();
            if(s.world != 0xFFu && s.slot >= 0) { gTourLoadWorld = s.world; gTourLoadSlot = (int8_t)s.slot; }
        }
        free_at = blk;
    }
    else if(k_trate.Norm() > 0.02f)
    {
        /* No cable, or nothing on it. Exponential, 4 s a step down to about 30
         * ms, because the slow end is where a loop of worlds is a piece of music
         * and the fast end is where it is a texture. Skips the division, since
         * dividing a rate you set by hand is one knob fighting another. */
        const float    secs   = 4.f * exp2f(-k_trate.Norm() * 7.f);
        const uint32_t period = (uint32_t)(secs * 48000.f / (float)kEngineBlockSamples);
        if(blk - free_at >= (period < 2u ? 2u : period))
        {
            free_at = blk;
            const TourStep s = gTour.Fire(blk);
            if(s.step)
            {
                TourPoint();
                if(s.world != 0xFFu && s.slot >= 0) { gTourLoadWorld = s.world; gTourLoadSlot = (int8_t)s.slot; }
            }
        }
    }
    const int tg = gTour.TargetSlot();
    if(kTourWorld[tg]->Ready()) gEng.SetMorph(kTourWorld[tg], gTour.Blend(blk, k_tglide.Norm()));
}

static void OnFrame()
{
    static uint32_t win_t  = 0;
    const uint32_t  now_ms = daisy::System::GetNow();
    if(now_ms - win_t >= 1000u)
    {
        const uint32_t n = gCycN;
        gSource.gCycAvgWin = n ? gCycSum / n : 0;
        gSource.gCycMaxWin = gCycMax;
        gCycSum = 0; gCycN = 0; gCycMax = 0;
        win_t = now_ms;
    }
    hw.j8.SetVolts(gPayloadA * 5.f * k_cvdep.Norm());
    TourService();
    ServeWorldRequest();
}

int main()
{
    hw.Init(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ, kEngineBlockSamples, true /* 480 MHz */);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Boot into an analytic world: it is a formula, so there is nothing to
     * expand and the module makes sound immediately. */
    worlds::Point(worlds::kCrop, gWorlds[0], kBootP, nullptr, &gVertTable[0]);
    gEng.Init(&gWorlds[0], hw.SampleRate());

    hw.j8.EnableCvOutput();

    settings.UseBrightness();
    settings.UsePresets(presets);
    settings.Page(0).Name("Setup");
    presets.Manage(pager);
    presets.Manage(settings);
    presets.UseNames();

    host.Jacks(kJacks);
    host.Extend(gExt);
    /* Without this the descriptor carries the jacks and nothing about the
     * panel, so the web page cannot say what any knob does — the names, idents
     * and units are all declared above and were simply never published. Six
     * pages against the SDK's limit of eight. */
    host.Pages(page_play, page_rotate, page_stereo, page_orbit, page_kepler, page_couple, page_world);

    loop.Use(pager).Use(settings).Use(page_play).Use(page_rotate).Use(page_stereo).Use(page_orbit).Use(page_kepler).Use(page_couple).Use(page_world).Use(host).OnFrame(OnFrame);

    /* The Rate multiplier is centred on 1x, so a stored zero would silently
     * run every orbit at an eighth speed on a fresh boot — which reads as
     * "the orbit does nothing", a fault this instrument has already shipped
     * once. Seed it at the detent; pot-catch means the physical knob still
     * has to move through 1x before it takes over, and a preset load
     * overwrites it as it should.
     *
     * The phys array is not optional. SetStored ends in InitCatch(s,
     * phys[pot]) with no null check, and on this part a null read lands at
     * 0x8, inside ITCM — which this firmware never writes a byte of, so it is
     * uninitialised ECC RAM. Reading it raises a double-bit ECC error and
     * hard-faults, deterministically, on every boot. That is what 0.3.0
     * shipped with. */
    float phys[kNumPots];
    for(uint8_t i = 0; i < kNumPots; i++) phys[i] = hw.pots[i].Value();
    pager.SetStored(5, 2, 0.5f, phys);
    /* Softening at a quarter turn: about 0.014, roughly 2.5 degrees of drift
     * per orbit, which reads as an orbit rather than a wash. The default that
     * shipped sat at 47.9 degrees. */
    pager.SetStored(4, 3, 0.25f, phys);
    /* World morph at nothing.
     *
     * The SDK seeds every stored value at 0.5, which is right for a knob whose
     * centre is its rest — the orbit rates are stopped at the centre and the
     * Rate multiplier is 1x there. It is wrong for this one: choosing a morph
     * target is setup, done on the page, and with a stored half-turn the
     * instant a target is chosen the sound is already halfway into another
     * world. Engaging something should not move it. At zero the knob is where
     * it claims to be, and pot-catch means the physical knob has to travel up
     * from the bottom before it takes hold, which is the behaviour you want
     * from a blend you have just armed. */
    pager.SetStored(5, 5, 0.f, phys);
    /* Axes 4 and 5 at nothing, for the reason above and one more: in the
     * worlds that use them they are effect depths, and a stored half turn
     * would boot a world you wrote into half a wavefolder. */
    pager.SetStored(6, 0, 0.f, phys);
    pager.SetStored(6, 1, 0.f, phys);

    gSd.Init();
    ScanCard();          /* so the folder is already listed when a page connects */

    presets.Init();
    presets.BootLoad();   /* HostLink starts here: descriptor + panel USB up */

    hw.StartAudio(AudioCb);
    for(;;) loop.Tick();
}
