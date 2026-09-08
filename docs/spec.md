# Kyklophoria — N-dimensional morph oscillator for the Alchemy Lab

Working title in the brief: *Hyperwave*; named **Kyklophoria** by Will on
2026-09-08. Code prefix `kyk::` / `kyk_*.h` / `*.kyk`; module id `kyk`.
Target: Hermetic Modular Alchemy Lab v2 (Daisy Seed2 DFM, STM32H750),
sharing the shell built for Audiothurgist. Owner: Will (Combust).
This file supersedes the kickoff brief of 2026-09-08; the brief's text is
kept where it still holds, with the hardware facts filled in (§0) and the
open decisions tracked (§9).

## 0. Hardware facts (measured from `../alchemy-sdk` v0.11 and Audiothurgist, 2026-09-08)

| | |
|---|---|
| MCU | STM32H750 (Daisy Seed2 DFM), 400 MHz stock, **480 MHz** with the `boost` flag Audiothurgist passes to `AlchemyLabV2::Init` (local SDK patch, pending Hermetic's release) |
| Audio | 48 kHz, **24-sample blocks → 500 µs per block**, 2 in / 2 out, 24-bit codec |
| Pots | 6, each with an LED ring; laid out `[P1] B1 [P2] / [P3] B2 [P4] / [P5] B3 [P6]` |
| Buttons | 3 (B1, B2 on-MCU; B3 via I²C expander). **There is no encoder.** Paging is by button (SDK `Pager`) |
| Jacks | J1/J2 codec in (AC-coupled; usable as triggers). **J3–J8 six field-programmable CV jacks**: 16-bit ADC in at audio rate, or 12-bit DAC out (J3–J6 MCP4728 over I²C ≈ 70 µs; J7–J8 STM DAC). J9/J10 codec out, DC-coupled, 24-bit — either audio or the best CV out on the board (block-rate) |
| CV range | ±5 V at the panel, calibrated per board (`hw.cv[i].Volts()`) |
| RAM | 128 K DTCM (tight; Audiothurgist sits at 65 %), 512 K AXI SRAM, 64 MB SDRAM (space files live here), 8 MB QSPI |
| SD | FatFs via the SDK's `SdCard`; browser file access via `FsExtension` (HostLink 0x50–0x5F) |
| HostLink | COBS + CRC32 framed, **stop-and-wait, host polls**; `max_body` 512 default, Audiothurgist builds with 1024. Audiothurgist's Scope tab achieves 20–30 frames/s and 20–30 KB/s over USB FS CDC. Application commands claim 0x60–0x6F via `IHostlinkExtension`. There is no unsolicited/push frame in protocol v1 |
| FFT | CMSIS-DSP (`arm_rfft_fast_f32`) is vendored with libDaisy |
| Web | Audiothurgist's `web/hostlink.js` (framing, priority link, RTT measurement) and `demo.js` (fake port) are reusable as-is; `index.html` is not (it is a config-leaning UI) |

## 1. One-paragraph pitch

A wavetable oscillator whose table is not a line or a grid but an
N-dimensional space (N=4 by default, N≤6 supported). Each point in the space
is a *hyperpoint*: a single-cycle spectrum plus a small payload vector
(filter cutoff, FM index, drive, a free CV out). Position CVs are passed
through an N-D rotation before indexing, so a single CV can sweep diagonal
slices no axis reaches, and slow rotation LFOs produce quasi-periodic orbits
through the space. The space can be given different topologies (clamped
hypercube, flat torus, hypersphere). A WebSerial control surface (§8) is a
cornerstone of the experience: it shows the player, in real time, where their
CV has put them in the space and what the payload is doing — it is an
instrument display, not a config editor. Lit review is in §10; the rotation
part is the novel bit, the rest has precedent.

## 2. Non-negotiable architecture (inherit from BreakFix/Audiothurgist)

- Pure header-only C++ core under `core/`. No libDaisy includes, no heap
  allocation, no exceptions, no RTTI in the audio path. Builds with
  `-fno-exceptions -fno-rtti` and runs on the desktop.
- "One core, many main()s": `shell/alchemy/` (hardware), `shell/desktop/`
  (renders WAV to disk, drives the core from a param script). The desktop
  shell is how we test; nothing may work only on hardware.
- Reuse the Audiothurgist Alchemy Lab shell for CV scaling, jack/LED-ring
  handling, paging, SD, and the HostLink transport. Do not fork it; factor
  shared pieces into a common shell library if needed.
- Deterministic: same params + same seed → bit-identical output on desktop
  and hardware. Rules adopted in M0 (docs/m0-notes.md): all trig comes from
  a generated float table (`core/kyk_tables.h`), phases and noise from
  xorshift32, the phase accumulator is Q32, and every build uses
  `-ffp-contract=off`. If M1 swaps the ARM IFFT for CMSIS, that build is
  documented as "identical modulo FFT rounding".
- Audio callback budget: ≥30 % headroom at 48 kHz / 24 samples, i.e. ≤350 µs
  per block. Measure with the DWT cycle counter, print over HostLink.

## 3. Core concepts and data model

### 3.1 Hyperpoint
K harmonic magnitudes (harmonics 1..K, no DC) plus P payload floats.
Spectra, not sample frames: bandlimiting is a truncation, N-D interpolation
is K+P MACs per corner. Phases are derived from a per-space seed (§9.3);
the file format reserves a flag for stored phases.

### 3.2 Space
- **Lattice** (M0): `side^N` hyperpoints on a regular grid. N=4, side=4 is
  256 cells and 73 KB at K=64, P=8; the module boots **side=8**, 4096 cells
  and 1.18 MB, because side 4 leaves no room for a correlation length. Multilinear interpolation, 2^N
  corners, once per block.
- **Scattered** (M5): arbitrary hyperpoints with a baked Delaunay
  triangulation; barycentric over N+1 vertices. Triangulation is computed
  offline; hardware does point-location + weights.

### 3.3 Topology (per axis)
- `Clamp`: coordinates saturate to [0,1] (M0).
- `Wrap`: flat torus, coordinates mod 1; the seam interval runs from column
  side-1 back to column 0 (implemented and seam-tested in M0, exposed in M2).
- `Sphere`: N coordinates are a direction on S^(N-1); rotation is the only
  motion. Chart to be chosen (§9.4). M2.
- Mixed per-axis topologies are allowed (wrap on 0–1, clamp on 2–3).

### 3.4 Rotation (M2)
Control frame `c ∈ R^N` → `p = R·c` → fold → interpolate. `R` is a product
of Givens rotations in the N(N-1)/2 planes (6 for N=4 — one per pot on a
rotation page). Per plane: a static angle and an orbit rate. Ratio lock over
small rationals (Brocot-style; Audiothurgist's `bb_pattern.h` has a
subdivision walk, not a general Brocot navigator — write ours). Rotation
recomputed at block rate only when an angle changed. Optional single
"rotate" CV driving a chosen plane (io-map).

### 3.5 Payload routing (M3)
Interpolated payload → SVF cutoff/resonance, FM index, soft-clip drive,
1–2 CV outs. Per lane: curve (lin/exp/log, applied after interpolation),
slew, depth.

## 4. Signal path

```
pitch (v/oct + fine + FM in) ─┐
CV/pots → c ─► R·c ─► fold ─► interpolate ─► spectrum ─► bandlimit(K by f0) ─► IFFT ─► frame ─► Q32 phase read (cubic) ─► drive ─► SVF ─► out
                                      └─► payload ─► lanes ─► {cutoff, fm_index, drive, cv_out}
```
- One frame per block (or per `render_div` blocks); the oscillator
  crossfades the previous and the new frame across the block, so per-block
  rendering is a continuous morph.
- **Stereo (M1)**: two positions per render, `R(θ∓δ)·c`, two spectra, two
  frames, two reads sharing one phase accumulator. δ=0 is mono and must be
  bit-identical to the M0 mono path (golden). The M0 `Engine` becomes the
  per-channel voice; a `StereoEngine` owns the pair and the shared payload.
- **Frame 1024, K 64 is the M0 baseline** (docs/m0-notes.md): with cubic
  reads it holds the −80 dBFS aliasing bar over five octaves; 512 does not.
  Confirm against the M1 cycle count (§9.2).
- Bandlimiting: harmonics with k·f0 ≥ Nyquist are zeroed before the IFFT;
  an optional raised-cosine taper over the top `rolloff_bins`.
- IFFT: fixed-size, behind `core/kyk_fft.h`. M0: a portable radix-2 on the
  shared sine table (bit-identical everywhere). M1 may add CMSIS on ARM.

## 5. Authoring / content
Space file `*.kyk` (docs/space-format.md). `tools/kykspace` generates and
validates. Generative fill is the default. Two families ship, and the file
format does not record which one made a blob:
- **Harmonic** — one legible synthesis parameter per axis (shape, tilt,
  formant, hollow, comb2, comb3). Legible, but *separable*, which measures
  as a 9.8x spread of variety across directions: most rotations land a CV on
  a dead direction (docs/m2-notes.md).
- **Field** (default on the module) — a correlated random field over the
  lattice. No privileged directions by construction, 1.8x spread, five times
  the variety per unit of CV travel at side 8, no near-duplicate cells. This
  is the family that makes rotation pay.

M4 adds formant stacks, bells/inharmonic, seeded spectral noise, and
WAV / wavetable import. Payload is generated by simple functions of position.
Later: an offline latent decoder that bakes a space — the format must allow it.

## 6. Milestones
- **M0 — Skeleton** ✅ 2026-09-08: core builds on desktop, lattice N=4
  side=4 from the generator, multilinear interpolation, Clamp (and Wrap
  folding), no rotation, golden WAVs, aliasing sweep. docs/m0-notes.md.
- **M1 — Hardware sound** ◐ 2026-09-08: rotation + stereo pair in the core,
  the Alchemy shell (builds; v/oct, four position CVs, pitch/position/angle/
  spread pots, CV out A, stereo out), telemetry + stats over HostLink, the
  desktop bridge and the page (position dots, trail, spectrum, frame, stats).
  **Pending: cycle counts from the module.** docs/m1-notes.md.
- **M2 — Rotation + topology** ◐: Givens rotations and the stereo pair landed
  early with M1. Done since (docs/m2-notes.md): level-preserving blend, the
  crossfade endpoint fix, the control-frame slew, the Field generator and
  side 8 on the module. **Left**: orbit LFOs, ratio lock, Wrap and Sphere
  exposed on the panel, seam and pole tests, the web orbit trail.
- **M3 — Payload lanes**: SVF, drive, FM index, CV outs; curve/slew/depth.
- **M4 — Content tooling**: generators, import, space browser, SD loading.
- **M5 — Scattered spaces**: offline Delaunay, barycentric, point-location.
- **M6 — Polish**: presets, param lock, remaining option pages.

Each milestone: desktop golden tests + `docs/mN-notes.md`. The web surface
grows alongside and must run against the desktop shell through a local
bridge.

## 7. Testing
- Golden WAVs from the desktop shell for fixed scripts; CI diffs them
  (tolerance 1e-6; bit-identical on one machine). ✅ M0
- Aliasing test: five-octave sweep on the brightest cell, loudest
  non-harmonic bin in dBFS, fail above −80. ✅ M0 (`tests/alias_check`)
- Seam test (Wrap): ✅ M0 for the interpolation; the rendered sweep is M2.
- Rotation identity test (M2): θ=0 bit-identical; θ=90° permutes axes.
- Sphere pole test (M2): bounded spectral change per block through a pole.
- CPU test on hardware (M1): worst cell + max FM, cycles/block.

## 8. WebSerial control surface
As in the brief: read-mostly, latency-honest, zero-install, headless-capable.
Views: Space (hero), Sound, Payload lanes, Panel mirror, Authoring drawer,
Status strip. Attract mode from a recorded telemetry log. Canvas, no
framework, ≤1 MB.

**Protocol reality (§0):** HostLink v1 is host-polled. Telemetry is a
compact binary reply to a `GET_TELEMETRY` command in our 0x60–0x6F block,
polled at 30–60 Hz by the page; the module writes it from a snapshot the
audio callback publishes (double-buffered, never blocking audio). Audiothurgist
proves 30 frames/s is reachable this way. docs/hostlink.md.

## 9. Decisions (ask, don't assume)
Status 2026-09-08 evening — Will confirmed 1–4; 5 is open:
1. **I/O map** ✅ docs/io-map.md, with one change from the proposal: the
   output is **stereo** on J9/J10 (Will: "people love their stereo, and this
   synth lends itself to it"). CV out A moves to J8. Stereo mechanism: the
   two channels read the space at rotation angles θ±δ in a chosen plane
   (io-map *Stereo*); payload taken at the centre.
2. **K and frame size** ✅ 1024/64 baseline until M1 has cycles.
3. **Phases** ✅ derived from a seed; iterate once there is content to judge.
4. **Sphere chart** ✅ cube-map; iterate at M2.
5. **Name** ✅ Kyklophoria.

## 10. Prior art

Full survey in `docs/lit/` (eurorack, software, authoring). Academic line
unchanged from the brief: Bowler et al. 1990; Goudeseune 2002 simplicial
interpolation (MIT reference code); Choi/Bargar/Goudeseune 1995; Bencina
2005 Metasurface; SPINVAE-2; Wessel 1979; Esling et al. 2018; FlowSynth;
Lee et al. 2024 Wavespace; DAFx 2022 topology notes; Nakashima et al. 2022;
Franck & Välimäki 2012 integrated wavetables. Vector synthesis: Prophet VS,
Wavestation.

### 10.1 Corrections to the brief (verified 2026-09-08)

- **The brief's "not found: rotation of the control frame" is too strong.**
  The **4ms Spherical Wavetable Navigator** ships a 3×3×3 wavetable that its
  manual calls a **3-torus** in as many words, explains to the player as "a
  three dimensional structure existing in a four dimensional space", and
  wraps on every axis. Its **Dispersion** control is a CV-controlled scalar
  applied along a **CV-selected pattern of per-channel direction vectors**
  through that wrapping space. Verified by extracting the text of the SWN
  manual 1.0 (`4mscompany.com/SWN/manual/SWN-manual-1.0.pdf`), not from a
  summary. So neither wrap topology nor "a CV transforms position vectors"
  is unoccupied.
- **Our stereo spread has direct precedent**, and it is the same idea as
  SWN's Dispersion and as Ferry Island's output spread: an ensemble read at
  displaced positions. Present it as good practice, not as invention.
- **E352**: three CVs index an 8×8 bank, per Synthesis Technology's own page
  ("Use 3 CVs to select where in the 8x8 bank of wavetables the output is
  generated"). `docs/lit/eurorack.md` reports the **E350** is weaker than the
  brief implies, with X/Y indexing the grid and Morph Z a separate scan
  feeding an independent output; that reading is the survey's, not
  independently checked here.
- **Conductive Labs Terrain Synth** (desktop, manual v0.13 dated 2026-05) is
  the closest thing to our rotation anywhere, and `docs/lit/eurorack.md` does
  not mention it because it is not a Eurorack module; the analysis lives in
  `docs/lit/software.md`. Its own framing is the best sentence in the whole
  survey: "This is a super-set of wavetable synthesis, where the path is
  always a straight line that can only be moved in one way and the terrain is
  frozen." Worth reading in full before we write any marketing copy.
- **Ferry Island Four Seas** (2025) is the closest shipping competitor and
  runs on **our exact chip**: STM32H750 / Daisy Seed, MIT-licensed at
  `github.com/Ferry-Island-Modular/Four-Seas`. Its store is 8 waves × 8
  pages × up to 12 banks of **2048-sample frames** loaded from SD. It is
  marketed as "four-dimensional", where the fourth dimension is four related
  outputs with a spread control rather than a fourth lattice axis. Read it
  before finalising M1 cycle counts.

### 10.2 What is actually ours

Stated so it survives contact with the survey:

1. **Rotation in more than one plane, over a stored lattice.** Two arguments
   we might have reached for are dead, and are recorded here so nobody
   revives them:
   - *"Nobody modulates a rotation."* **False.** Conductive Labs' Terrain
     Synth ships `ROTATE: (Rotation About the X,Y Position) [-180, +180
     degrees]` and its manual instructs you to modulate it: "When a
     continuous spinning effect is desired use the LFO at 100% amount with a
     saw or inv saw waveform." Verified from the manual PDF here, not from a
     summary.
   - *"Rotation preserves where attenuverters shear."* True but weak, since
     a 2-D rotation is orthogonal by construction, so it wins nothing
     against the one competitor that rotates.

   What survives is dimensional and structural. A 2-D space has exactly one
   rotation plane; N=4 has six, each with its own angle and orbit rate, and
   two incommensurate rates give a quasi-periodic orbit that no single-plane
   rotation can trace. Terrain Synth rotates a *reading path over a computed
   terrain* ("the more complex terrains are created with trigonometry
   functions like sine and cosine"); we rotate the *player's control frame*
   before it indexes a stored corpus of spectra. Different object, different
   dimensionality. The field's other transforms are displacements along
   chosen directions (SWN Dispersion, Four Seas spread) or per-axis
   attenuversion.
2. **Four or more dimensions over one homogeneous lattice.** Eurorack tops
   out at three, and the "4-D" on the market is three plus an ensemble. In
   software it is worse: `docs/lit/software.md` finds **no** synth that
   stores a wavetable in three or more dimensions — every "3D" in the field
   is a *renderer* over a 1-D list (Serum, Pigments, Nave, Vital, Falcon).
   Genuine 2-D exists twice: u-he Hive 2 as a fold of a 1-D store, and PPG
   WaveGenerator's 16×16 grid. PPG is also the only oblique traversal
   anywhere: the player *draws a path* across the grid and an envelope walks
   it. Drawn, not transformed, and not modulatable as a mapping.
3. **Spectra at the nodes rather than frames.** This is what makes our morph
   provably click-free (`tests/morph_check`); everyone storing time-domain
   frames inherits the cancellation problem.
4. **Mixed per-axis topology**, and the sphere.
5. **A published aliasing figure.** Nobody in the field states one.
