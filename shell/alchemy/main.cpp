/* kyklophoria — the Alchemy Lab v2 main(). M1: the M0/M1 core on the panel.
 *
 *   J3 v/oct · J4–J7 position 0–3 · J8 CV out A · J9/J10 out L/R
 *   (docs/io-map.md; J1 FM and J2 sync land in M3)
 *
 *   Page Play    P1 coarse (octaves)  P2 fine (±1 st)  P3–P6 position 0–3 offsets
 *   Page Rotate  P1–P6 the six plane angles (turns)
 *   Page Orbit   P1–P6 the six plane rates, centre stopped, exponential out
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
#include "kyk_telemetry.h"
#include "kyk_ext.h"

using namespace alchemy;
using namespace kyk;

/* Bumped when the panel or the sound changes in a way you would notice.
 * 0.2.0: stereo pair, rotation, the orbit page, field and eigen spaces, the
 * real-valued transform, level headroom, morph sharpness. */
#define KYK_FW_VERSION "0.2.0"
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
static Pager       pager(hw.buttons[kButtonB1], 4, kNumPots);
static Presets     presets(hw.seed.qspi);
static Settings    settings(hw, &pager);

/* ── knobs ──────────────────────────────────────────────────────────────── */
static constexpr LedPanel::Rgb kPlay   = {0x67, 0xE8, 0xF9};
static constexpr LedPanel::Rgb kRotate = {0xFC, 0xA5, 0xA5};
static constexpr LedPanel::Rgb kStereo = {0xC4, 0xB5, 0xFD};
static constexpr LedPanel::Rgb kOrbit  = {0xFD, 0xE0, 0x68};

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

static const char* kPlaneNames[6] = {"0,1", "0,2", "0,3", "1,2", "1,3", "2,3"};
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

static Page page_play   = Page(0).Name("Play").Color("#67e8f9").Knobs(k_coarse, k_fine, k_pos0, k_pos1, k_pos2, k_pos3);
static Page page_rotate = Page(1).Name("Rotate").Color("#fca5a5").Knobs(k_ang[0], k_ang[1], k_ang[2], k_ang[3], k_ang[4], k_ang[5]);
static Page page_orbit  = Page(3).Name("Orbit").Color("#fde068").Knobs(k_rate[0], k_rate[1], k_rate[2], k_rate[3], k_rate[4], k_rate[5]);
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
static uint8_t KYK_SDRAM gBlob[2][Space::BlobSize(kBootN, kBootK, kBootP, kBootSide, false)];
static Space   gSpace[2];
static World   gWorlds[2];
static uint8_t gBufIdx    = 0;
static volatile uint8_t gWorldIdx = worlds::kBraids;
static volatile uint8_t gWorldReq = 0xFFu;   /* 0xFF: nothing pending */
static volatile uint8_t gWorldBusy = 0;
static StereoEngine KYK_AXI gEng;

/* ── audio ↔ control shared state ────────────────────────────────────────── */
/* Telemetry is encoded on the control thread now, so there is no snapshot
 * buffer and nothing for the audio callback to do (see ModuleSource). */
static volatile uint32_t gCycLast = 0, gCycMax = 0, gCycSum = 0, gCycN = 0;
static volatile uint16_t gOverruns = 0, gDropped = 0;
static volatile float    gPayloadA = 0.f;
static volatile uint8_t  gResetPhase = 0;
static constexpr uint32_t kCpuHz     = 480000000u;
static constexpr uint32_t kCycBudget = kCpuHz / 48000u * kEngineBlockSamples;   /* 240 000 */

static inline uint32_t Cycles() { return DWT->CYCCNT; }

static void AudioCb(daisy::AudioHandle::InputBuffer in, daisy::AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    const uint32_t t0 = Cycles();

    /* pitch: v/oct on J3 (calibrated volts), coarse octaves, fine semitones */
    const float voct = hw.cv[0].Volts();
    const float f0   = 261.6256f * exp2f(voct + k_coarse.Value() + k_fine.Value() * (1.f / 12.f));

    /* position: pot offset (0..1) + CV (±5 V → ±1) per axis */
    float c[4];
    c[0] = k_pos0.Norm() + hw.cv[1].Volts() * 0.2f;
    c[1] = k_pos1.Norm() + hw.cv[2].Volts() * 0.2f;
    c[2] = k_pos2.Norm() + hw.cv[3].Volts() * 0.2f;
    c[3] = k_pos3.Norm() + hw.cv[4].Volts() * 0.2f;

    for(int p = 0; p < 6; p++)
    {
        gEng.rot.SetAngle(p, k_ang[p].Norm());
        gEng.rot.SetRate(p, RateFromKnob(k_rate[p].Norm()));
    }
    gEng.spread       = k_spread.Value();
    gEng.spread_plane = (int)k_plane.Value();
    int sel = (int)k_rdiv.Value();
    if(sel < 0) sel = 0;
    if(sel > 3) sel = 3;
    const int rdiv = (int)kDivValues[sel];
    if(rdiv != gEng.L.render_div) gEng.SetRenderDiv(rdiv);
    gEng.sharp = k_sharp.Norm();
    /* 0.23 is the headroom the measured crest factor needs; the knob scales
     * from silence to that, so a cell can no longer peak past full scale. */
    gEng.SetGain(0.23f * k_level.Norm());
    if(gResetPhase) { gEng.L.ResetPhase(); gEng.R.ResetPhase(); gResetPhase = 0; }

    gEng.SetF0(f0);
    gEng.SetControl(c, 4);
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
        return EncodeTelemetry(gEng, flags, out, cap);
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
    uint8_t Action(uint8_t op, const uint8_t* args, int len) override
    {
        switch(op)
        {
            case kActResetPhase: gResetPhase = 1; return 0u;
            case kActRenderDiv: (void)args; (void)len; return 1u;   /* the pot owns it on the module */
            case kActSelectWorld:
                if(len < 1 || args[0] >= worlds::kCount) return 2u;
                if(gWorldBusy) return 9u;                     /* BUSY */
                gWorldReq = args[0];
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
static void ServeWorldRequest()
{
    const uint8_t req = gWorldReq;
    if(req == 0xFFu || req >= worlds::kCount) return;
    gWorldReq  = 0xFFu;
    gWorldBusy = 1;
    const uint8_t wi = (uint8_t)(gBufIdx ^ 1u);
    if(worlds::IsAnalytic(req))
    {
        worlds::Point(req, gWorlds[wi], kBootP, nullptr);
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
    worlds::Point(worlds::kBraids, gWorlds[0], kBootP, nullptr);
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

    loop.Use(pager).Use(settings).Use(page_play).Use(page_rotate).Use(page_stereo).Use(page_orbit).Use(host).OnFrame(OnFrame);

    presets.Init();
    presets.BootLoad();   /* HostLink starts here: descriptor + panel USB up */

    hw.StartAudio(AudioCb);
    for(;;) loop.Tick();
}
