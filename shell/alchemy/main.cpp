/* kyklophoria — the Alchemy Lab v2 main(). M1: the M0/M1 core on the panel.
 *
 *   J3 v/oct · J4–J7 position 0–3 · J8 CV out A · J9/J10 out L/R
 *   (docs/io-map.md; J1 FM and J2 sync land in M3)
 *
 *   Page Play    P1 coarse (octaves)  P2 fine (±1 st)  P3–P6 position 0–3 offsets
 *   Page Rotate  P1–P6 the six plane angles (turns)
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
#include "kyk_gen.h"
#include "kyk_telemetry.h"
#include "kyk_ext.h"

using namespace alchemy;
using namespace kyk;

#define KYK_FW_VERSION "0.1.0-m1"

/* ── memory placement ─────────────────────────────────────────────────────
 * The engine (two voices, ~40 KB) lives in AXI SRAM: fast, DMA-irrelevant.
 * The space blob lives in SDRAM (64 MB; a 6-D lattice is 1.2 MB). */
#define KYK_AXI   __attribute__((section(".axi_bss")))
#define KYK_SDRAM __attribute__((section(".sdram_bss")))

static AlchemyLab  hw;
static ControlLoop loop(hw);
static Pager       pager(hw.buttons[kButtonB1], 3, kNumPots);
static Presets     presets(hw.seed.qspi);
static Settings    settings(hw, &pager);

/* ── knobs ──────────────────────────────────────────────────────────────── */
static constexpr LedPanel::Rgb kPlay   = {0x67, 0xE8, 0xF9};
static constexpr LedPanel::Rgb kRotate = {0xFC, 0xA5, 0xA5};
static constexpr LedPanel::Rgb kStereo = {0xC4, 0xB5, 0xFD};

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

static const char* kPlaneNames[6] = {"0,1", "0,2", "0,3", "1,2", "1,3", "2,3"};
static const char* kDivNames[4]   = {"1", "2", "3", "4"};
static VirtualKnob k_spread = VirtualKnob(0, "Spread").Linear(0.f, 0.1f).Unit("turn").Ident("st.spread").Ring(Level(kStereo));
static VirtualKnob k_plane  = VirtualKnob(1, "Stereo plane").Selector(6).Labels(kPlaneNames, 6).Ident("st.plane").Ring(Level(kStereo));
static VirtualKnob k_cvdep  = VirtualKnob(2, "CV out A depth").Ident("lane.cva").Ring(Level(kStereo));
static VirtualKnob k_rdiv   = VirtualKnob(3, "Render div").Selector(4).Labels(kDivNames, 4).Ident("eng.rdiv").Ring(Level(kStereo));
static VirtualKnob k_level  = VirtualKnob(4, "Level").Ident("out.level").Ring(Level(kStereo));
static VirtualKnob k_spare  = VirtualKnob(5, "—").Ring(Level(kStereo));

static Page page_play   = Page(0).Name("Play").Color("#67e8f9").Knobs(k_coarse, k_fine, k_pos0, k_pos1, k_pos2, k_pos3);
static Page page_rotate = Page(1).Name("Rotate").Color("#fca5a5").Knobs(k_ang[0], k_ang[1], k_ang[2], k_ang[3], k_ang[4], k_ang[5]);
static Page page_stereo = Page(2).Name("Stereo").Color("#c4b5fd").Knobs(k_spread, k_plane, k_cvdep, k_rdiv, k_level, k_spare);

/* ── jacks (descriptor metadata; the web panel mirror reads these) ───────── */
static const Jack kJacks[10] = {
    Jack("fm", "FM In", JackSig::AudioIn),      Jack("sync", "Sync", JackSig::Trig),
    Jack("voct", "V/Oct", JackSig::Voct),       Jack("pos0", "Position 0", JackSig::CvBi),
    Jack("pos1", "Position 1", JackSig::CvBi),  Jack("pos2", "Position 2", JackSig::CvBi),
    Jack("pos3", "Position 3", JackSig::CvBi),  Jack("cv_a", "CV Out A", JackSig::CvUni),
    Jack("out_l", "Out L", JackSig::AudioOut),  Jack("out_r", "Out R", JackSig::AudioOut),
};

/* ── the space and the engine ────────────────────────────────────────────── */
static uint8_t      KYK_SDRAM gBlob[Space::BlobSize(4, 64, 8, 4, false)];
static Space        gSpace;
static StereoEngine KYK_AXI gEng;

/* ── audio ↔ control shared state ────────────────────────────────────────── */
static volatile uint8_t  gTelReq   = 0;      /* control: please snapshot with these flags (+1) */
static volatile uint8_t  gTelIdx   = 0;      /* audio: the buffer that holds the newest snapshot */
static volatile uint16_t gTelLen[2] = {0, 0};
static uint8_t           gTelBuf[2][hostlink::kMaxBody];
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

    for(int p = 0; p < 6; p++) gEng.rot.SetAngle(p, k_ang[p].Norm());
    gEng.spread       = k_spread.Value();
    gEng.spread_plane = (int)k_plane.Value();
    const int rdiv    = 1 + (int)k_rdiv.Value();
    if(rdiv != gEng.L.render_div) gEng.SetRenderDiv(rdiv);
    gEng.SetGain(0.5f * k_level.Norm());
    if(gResetPhase) { gEng.L.ResetPhase(); gEng.R.ResetPhase(); gResetPhase = 0; }

    gEng.SetF0(f0);
    gEng.SetControl(c, 4);
    gEng.Process(out[0], out[1], (int)size);
    gPayloadA = gEng.Payload()[4];

    /* telemetry snapshot, only when asked: ~5 µs */
    const uint8_t req = gTelReq;
    if(req)
    {
        const uint8_t wi = (uint8_t)(gTelIdx ^ 1u);
        const int     n  = EncodeTelemetry(gEng, (uint8_t)(req - 1u), gTelBuf[wi], (int)sizeof(gTelBuf[wi]));
        gTelLen[wi]      = (uint16_t)(n > 0 ? n : 0);
        __asm__ volatile("dmb" ::: "memory");
        gTelIdx = wi;
        gTelReq = 0;
    }

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
        const uint8_t  ri = gTelIdx;
        const uint16_t n  = gTelLen[ri];
        if(gTelReq) gDropped++;
        gTelReq = (uint8_t)(flags + 1u);
        if(n == 0 || (int)n > cap) return 0;
        std::memcpy(out, gTelBuf[ri], n);
        return (int)n;
    }
    bool SpaceInfo(SpaceHeader& h, uint32_t& crc, uint16_t& stride) override
    {
        if(!gSpace.Attached()) return false;
        h      = gSpace.Header();
        crc    = gBlobCrc;
        stride = (uint16_t)gSpace.Stride();
        return true;
    }
    bool Cell(uint32_t idx, uint8_t* mags, float* payload, int& k, int& p) override
    {
        if(!gSpace.Attached() || idx >= gSpace.PointCount()) return false;
        k = gSpace.K(); p = gSpace.P();
        for(int i = 0; i < k; i++) mags[i] = detail::MagToU8(gSpace.Mags(idx)[i]);
        for(int j = 0; j < p; j++) payload[j] = gSpace.Payload(idx)[j];
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
            default: return 1u;
        }
    }
    uint32_t gBlobCrc   = 0;
    uint32_t gCycMaxWin = 0, gCycAvgWin = 0;
};
static ModuleSource  gSource;
static KykExt         gExt(gSource);
static hostlink::Host host(presets, "kyk", "Kyklophoria", KYK_FW_VERSION, "m1");

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
}

int main()
{
    hw.Init(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ, kEngineBlockSamples, true /* 480 MHz */);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* the boot space: the Harmonic family, seed 1 (SD loading is M4) */
    GenParams gp;
    gp.seed = 1;
    const size_t n = BuildLattice(gp, gBlob, sizeof(gBlob));
    gSpace.Attach(gBlob, n);
    gSource.gBlobCrc = Crc32(gBlob, n);
    gEng.Init(&gSpace, hw.SampleRate());

    hw.j8.EnableCvOutput();

    settings.UseBrightness();
    settings.UsePresets(presets);
    settings.Page(0).Name("Setup");
    presets.Manage(pager);
    presets.Manage(settings);
    presets.UseNames();

    host.Jacks(kJacks);
    host.Extend(gExt);

    loop.Use(pager).Use(settings).Use(page_play).Use(page_rotate).Use(page_stereo).Use(host).OnFrame(OnFrame);

    presets.Init();
    presets.BootLoad();   /* HostLink starts here: descriptor + panel USB up */

    hw.StartAudio(AudioCb);
    for(;;) loop.Tick();
}
