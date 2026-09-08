# Lit review — Eurorack wavetable and wave-morphing oscillators

Scope: what hardware Eurorack oscillators actually do with a wavetable, checked
against manufacturer manuals wherever possible. Written for Kyklophoria
(docs/spec.md): an N-D lattice of spectra, position CVs passed through a Givens
rotation before indexing, per-axis topology.

Every non-obvious number below carries a URL. Anything I could not verify from a
primary source is marked **unverified** inline. Nothing here is inferred from
memory.

## The field in one paragraph

Eurorack wavetable oscillators are overwhelmingly **1-D or 2-D**. The dominant
shape is a bank of 64 waves laid out as an 8×8 grid with two CVs indexing row
and column — not a coincidence: Synthesis Technology's E350 fixed it, the E352
and E370 inherited it, ALM and Qu-Bit adopted it via the WaveEdit file format,
and Erica's Graphic VCO independently defaults to the same 8×8.

**Three dimensions is the ceiling in practice, and it is a panel ceiling, not
an idea ceiling.** The genuine 3-D navigators are the Piston Honda lineage
(Industrial Music Electronics, *not* WMD — an 8×8×8 cube with three sliders and
three CV inputs), the 4ms SWN (a 3×3×3 **3-torus**), Ferry Island's Four Seas
(an 8×8×8-per-bank cube on our own MCU, open source), and — as a waveshaper
rather than an oscillator — Malekko's Mega-Wave (ROM/bank/wave, all three under
CV). Nothing navigates a *single homogeneous table* in four or more dimensions.
Dove Audio's Waveplane comes closest with six CV-addressable indices, but those
are a 2-D mixing plane over four *independent* 1-D tables — a product of two
different kinds of coordinate, not a 4-D lattice.

**One module has a CV-controlled vector transform of position, and it matters
for our novelty claim: the 4ms SWN.** Its *Dispersion* takes a CV-controlled
magnitude and applies it along a *CV-selected pattern* of six per-channel
direction vectors through a wrapping 3-torus. That is not a rotation of the
player's control frame — each channel still moves on axis-aligned Depth /
Latitude / Longitude, and Dispersion spreads the *ensemble*, it does not change
what a given CV means — but it is much closer than "nothing", and spec §10
should be amended accordingly. SWN also demolishes the assumption that
non-trivial topology is unoccupied: every one of its axes wraps, and 4ms
explain the 3-torus to the player in the manual.

Four other structural facts worth internalising:

- **Content is almost always a bank of single-cycle frames on removable media,
  in one of two de-facto formats**: the Synthesis Technology "WaveEdit" bank
  (64 waves × 256 samples × 16-bit mono = 32 KB WAV), which Piston Honda mk3,
  E352 and E370 all read; and the Expert Sleepers convention (a folder of
  one-cycle 16-bit mono WAVs, or one concatenated WAV plus a declared frame
  length). Neither stores spectra; both store frames.
- **A "glitch" mode is a feature, not a bug.** Every serious module here ships
  a way to *turn interpolation off* — Piston Honda's per-axis Morph Enable,
  the E350's JP8 jumper, the E352/E370's `Phs Interp = Off` plus three levels
  of Glitch. Users want the PPG-style hard step as an alternative timbre.
- **Anti-aliasing is mostly undocumented or absent.** The E370 manual states
  outright that no anti-alias filtering is applied in Sample Player. Piston
  Honda mk3's answer to fidelity was "go from 8-bit to 16-bit samples", not
  bandlimiting. The honourable exceptions are Waldorf's nw1 (three *named*
  resampling-quality tiers, stored per table, including a deliberately
  "Aliased" one), Mutable's Plaits (integrated wavetables plus a pitch-tracked
  differentiator), Noise Engineering (a sample rate locked to a multiple of f0
  so alias images land on the harmonic grid) and Xaoc's Odessa (additive, so
  band-limited by refusing to instantiate a partial above 21 kHz). Everyone
  else publishes nothing. Kyklophoria's spectral-truncation bandlimiting with a
  measured −80 dBFS bar is genuinely ahead of the field here.
- **The interesting instruments navigate one space with several coordinate
  systems at once.** SWN gives you three axes *and* a space-filling "Browse"
  snake *and* Dispersion, live, simultaneously. E352/E370 give you X/Y *and* a
  Z that scans the grid end-to-end. That is the design pattern our rotation
  belongs to, and framing it that way is more honest and more useful than
  "N-dimensional".

## Summary table

Dimensions = CV-addressable indices into the table. "Interp." = is the morph
smooth, and can it be switched off?

| Module | Table shape | Dims | Frame | Bits | Interp. off? | User content | Anti-alias |
|---|---|---|---|---|---|---|---|
| IME Piston Honda mk3 | 8×8×8 cube, 512 waves | 3 | 256 | 16 | yes, per axis | µSD, 8× WaveEdit files | none documented |
| IME Piston Honda mk2 | 16×16×16, 4096 waves | 3 | unverified | 8 | yes + variable *resolution* CV | EPROM expander + Wave256 | none documented |
| IME Piston Honda mk1 | 16 banks × 256, ×2 ROMs | 2 | unverified | 8 (unverified) | "morph or jump" | ROM swap | none documented |
| SynthTech E350 | 8×8 grid ×3 ROM banks | 2 (+ separate Z reader) | unverified | 8 | yes, jumper JP8 | none | undocumented |
| SynthTech E352 | 8×8, 3 ROM + 26 user banks | 2–3 | 256 | 16 | yes, Phs Interp + 3 glitch levels | µSD, WaveEdit | undocumented |
| SynthTech E370 | 8×8, 26 banks, 1664 waves | 3 (X, Y, bank) | 256 | 16 | yes, + 3 glitch levels | µSD, WaveEdit | **stated absent** (Sample Player) |
| Erica Graphic VCO | resizable matrix, default 8×8 | 2 | 1024 | unverified | **no** | draw on module; USB app never shipped | undocumented |
| Intellijel Shapeshifter | 128 banks × 8 waves | 2 (bank stepped) | 512 | 16 | MULTI/gLcH only | reflash, or community WebUSB tool | ~254× oversample, no mipmaps |
| Mutable Plaits (wavetable) | 8×8×4, 192+15 waves | 3 | 128 | 16 | yes, continuous on z | 15 waves via audio into TIMBRE | **integrated wavetables** |
| Mutable Braids (WTBL/WMAP/WLIN/WTx4) | ragged 1-D / 16×16 / 64-path / 32-path | 1–2 | 128 | 8 | yes, WLIN COLOR | none | 2× naive |
| 4ms SWN | 3×3×3 **3-torus**, 120 spheres | 3 + sphere + Browse + **Dispersion** | 512 | 16 | not documented | record from jack; SphereEdit over audio | undocumented |
| Ferry Island Four Seas | 12 banks × 8 pages × 8×8 (unverified) | 3 + bank | 2048 | unverified | not documented | µSD, WaveEdit; Serum announced | undocumented |
| Waldorf nw1 | 1-D circular, 8–4096 waves | 1 (no CV over table select) | 32–1024 | 16 | no | USB + editor; onboard recorder; TTS | **3 named tiers** |
| ALM MCO mkII | dual 1-D + 5 combine modes | 2 + mode | 256 | 16 | via Mix Modes | **USB mass storage**, WaveEdit | undocumented |
| Malekko Mega-Wave (waveshaper) | 10 ROM × 16 bank × 16 wave | 3 | unverified | unverified | unverified | none | n/a |
| Tiptop ART Vortex | 1-D, 64 waves/table | 1 | unverified | unverified | unverified | µSD | undocumented |
| Ziqal Dimension MK3 | 1-D, ≤256 frames | 1 (+2 macros) | 2048 | unverified | unverified | µSD, **Serum/Vital** | claimed, undetailed |
| Dove Audio Waveplane | 2-D vector plane × 4× 32-wave tables | 6 indices, heterogeneous | unverified | unverified | unverified | none documented | undocumented |
| NE Cursus Iteritas | computed from a spectral description | 4 spectral knobs | n/a | unverified | Edge knob | none | **f0-locked sample rate** |
| XAOC Odessa (additive) | no table; ≤2560 partials | n/a | n/a | unverified | n/a | none | **21 kHz partial cull** |
| Behringer VICTOR (vector) | 4 corners × 128 waveforms | 2 (joystick X/Y) | unverified | unverified | unverified | none documented | undocumented |
| **Kyklophoria** | side^N lattice of **spectra** + payload | **N=4–6, rotated frame** | 1024 | float | planned | `.kyk`, generative + import | **spectral truncation, −80 dBFS measured** |

---

## Industrial Music Electronics (The Harvestman) — Piston Honda

Note on attribution: the Piston Honda is **not** a WMD module. It is The
Harvestman / Industrial Music Electronics, model 1991, three generations. WMD
is an unrelated Denver company. Getting this wrong in the docs would be
embarrassing.

This is the closest existing thing to Kyklophoria and deserves the most care.

### mk3 (model 1991-3) — the current one

Primary source throughout: the mk3 operations manual, firmware v1.1
(https://ime-assets.s3.amazonaws.com/uploads/manual/manual/24/phmk3manual11.pdf).

**1. Table shape.** "The Piston Honda contains 512 waveforms, organized into a
'cube' of 8×8×8 with the axis labeled X, Y, and Z." Each waveform is 256
samples, 16-bit. The 512 waves are shared by both oscillators; each oscillator
holds its own X/Y/Z position. So: a genuine 3-D lattice of frames, 8 per side,
256 points per node.

**2. Navigation.** Three physical sliders (X, Y, Z) plus three dedicated CV
inputs, each with a nonlinear attenuverter ("subtly modulate a small range of
the parameter, or full-swing travel without the use of external amplification").
CV range ±8 V at the jacks, ±10 V tolerated. Morphing is trilinear-ish and
smooth by default, and **can be disabled per axis** via the Oscillator Options
menu's MORPH ENABLE: "An axis with disabled morphing makes hard transitions
between the eight waveforms instead of a smooth interpolation, for a more
'glitchy' effect… It also allows you to hear a selection with no content from
the neighbor waveforms mixed in." The display drops the colon after an axis
label when that axis's morph is off. There is also a per-oscillator "Waveform
CV: Off" so one oscillator can ignore the shared XYZ CVs entirely.

Two oscillators share the sliders. The SELECT buttons choose which oscillator
receives slider/attenuverter motion, with a **lock-until-moved** behaviour: on
preset load or SELECT press the sliders are locked and only take effect once
physically moved past their stored value. Same for the CV attenuverters.

A separate **Preset Morph** mode gives a ninth navigation axis of sorts: 8
presets, a CV input that smoothly morphs the whole panel state between them
(sliders, attenuverters), with the non-morphable menu settings taken from a
chosen "base" preset.

**3. Authoring.** microSD, FAT. "1-channel .WAV file, 16 bits, 32Kbytes (256
2-byte samples per waveform. 64 waveforms per file total). One file occupies
one of the eight slots available on the Z axis slider." Files must be named
`1.wav`…`8.wav` in the card root, and **all eight must be present** — you cannot
load a single Z slice. Loading writes to internal memory and reboots the module.
Recommended authoring tool is Synthesis Technology's WaveEdit
(https://synthtech.com/waveedit/, open source at
github.com/AndrewBelt/WaveEdit). Note the consequence of the format: a file is
a **Z slice** (an 8×8 XY plane), so the X and Y axes are ordering *within* a
file and Z is file index. Authoring across Z means keeping eight files
coherent by hand.

**4. Anti-aliasing.** Not mentioned anywhere in the manual. The opposite
appears: the TONE menu offers deliberate degradation — "Orthodox" is undistorted
full 16-bit, the other settings "introduce different flavors of quantization
noise" and the manual explicitly says you may hear "quiet foldover of high
frequency aliases of the distortion products". Reviews describe the mk3 as
"virtually alias-free at most settings thanks to the high resolution 16 bit
outputs" (https://waveformmagazine.com/waveform-reviews/industrial-music-electronics-piston-honda-mk-iii/)
— that is a bit-depth claim, not a bandlimiting claim, and should be read as
"quieter than the 8-bit predecessors", not "bandlimited". **No documented
bandlimiting; treat as absent.**

**5. What it gets right.**
- Three *physical* sliders, one per axis, with position visible at a glance.
  Kyklophoria has 6 pots and no encoder; the lesson is that a player needs to
  see all N coordinates simultaneously without paging. Our WebSerial Space view
  is the equivalent, but the panel needs a cheap always-on version (LED rings).
- Per-axis morph disable. Cheap to implement, changes the instrument's
  character completely, and it doubles as "let me audition exactly this node".
- The lock-until-moved parameter behaviour on preset recall. We already need
  this (spec §6 M6 "param lock"); IME's version is the reference.
- Preset Morph: a CV that interpolates the entire control state, not just the
  table position. Different axis of motion from table navigation.
- Honest about its own degradation modes and gives them names.

**6. The gap.**
- Three dimensions, hard-coded. No fourth. No topology options — the cube is
  clamped at the edges, no wrap, no sphere.
- **The three CVs hit the three axes and nothing else.** There is no way to
  make one CV sweep a diagonal. If you want the diagonal you patch three
  attenuverted copies of the same CV, which costs three cables and a
  multiple, and even then you only get an axis-aligned linear combination
  fixed by knob positions — not a rotation you can modulate, and no orbit.
  This is precisely the hole Kyklophoria's Givens product fills.
- Frames, not spectra: nothing at a node except 256 samples. No payload, no
  per-node filter/FM/drive/CV-out data.
- The SD format forces content authoring to be Z-slice-major and all-or-nothing
  (8 files, always). Painful for iterative sound design.
- Loading requires a reboot.

### mk2 (2011)

4096 **8-bit** waveforms in a 16×16×16 cube, three illuminated sliders with a
numeric display of cube position, CV over all three axes with attenuverters,
thru-zero FM, external-input waveshaping mode, 17 HP
(https://www.analoguehaven.com/the-harvestman/model-1991-mark-ii/). Two extra
features worth noting that the mk3 dropped:

- **Voltage-controllable morph resolution**: a knob and CV that take the morph
  "from completely smooth to stepped" — i.e. a continuously variable
  quantisation of the interpolation, rather than mk3's binary per-axis on/off.
  Sourced from search summaries of the mk2 manual, not the manual itself
  (analoguehaven confirms "adjustable morphing resolution"); the exact law is
  **unverified**.
- Per-axis morph disable via an "axis select" button with slider LEDs
  (**unverified in a primary manual**; consistent with mk3's behaviour).

Custom waves loaded via the "classic Piston Honda expander board" plus Wave256
software; last six memory locations (256 waves each) are user-definable
(https://www.analoguehaven.com/the-harvestman/model-1991-mark-ii/). No SD card.

**Steal:** continuous morph resolution as a *modulatable* parameter is a better
idea than mk3's on/off switch, and it is nearly free for us — quantise the
interpolation weights per axis before the multilinear blend, with the
quantisation step under CV. It gives a continuum from smooth morph to hard
stepping to (with a hard step and a fast LFO) something like wave sequencing.

### mk1 (model 1991, 2009-ish)

Two on-board ROMs, "each containing 256 waveforms in 16 banks", i.e. **two
dimensions** (wave within bank × bank), and "waves may be smoothly morphed or
violently jumped in two dimensions"
(https://greatsynthesizers.com/en/general/harvestman-piston-honda-2/).
8-bit **unverified** but near-certain given mk2's 8-bit and the ROM sizes. An
optional expander adds six ROM sockets that accept Blacet/Wiard
Miniwave-format waveform data; Wave256 writes custom ROMs. So the lineage went
2-D (mk1) → 3-D (mk2) → 3-D at higher bit depth and lower node count (mk3).
Nobody went to 4.

---

## Synthesis Technology — E350 / E352 / E370

One family, one table shape, three generations. The E350's 8×8 bank is the
single most influential structure in Eurorack wavetables.

### E350 Morphing Terrarium

Primary source: the E350 manual (https://synthtech.com/docs/E350_manual.pdf)
and the product page (https://synthtech.com/eurorack/E350/).

**1. Table shape.** "There are 3 banks of 64 wavetables. Each bank is arranged
like a chessboard: there are 8 rows (the 'X' direction) and 8 columns (the 'Y'
direction)." Bank A is mostly sine/harmonic series plus Hammond drawbar sets,
Bank B a wide assortment including male/female vocal formants, Bank C is aimed
at LFO/modulation shapes. Bank is a **3-position switch**, not a CV
destination. 192 waves total. 8-bit samples — stated on the E352 page, which
describes the E352's 16-bit waves as "5x lower noise & THD" versus the E350
(https://synthtech.com/eurorack/E352/). Samples per wave: **unverified for the
E350 specifically**; the E352 ships "all 192 waves of the E350" in its 256-sample
16-bit format, which implies 256 but is not stated for the original.

**2. Navigation — and an important correction.** The E350 is *not* a 3-D
morpher, despite the "Morph X / Morph Y / Morph Z" panel labels. X and Y index
the 8×8 grid to produce the **XY OUT** signal. Morph Z is an entirely separate
1-D scan of the same 64 waves — "It scans through all 64 waves in the selected
bank… It starts at Row 1, Column 1 (wavetable 1) and goes to Row 8 Column [8]"
— producing an **independent second output, Z OUT**. Two readers of one 2-D
table, not a 3-D table. Anyone describing the E350 as 3-D (including Synthesis
Technology's own marketing copy on the product page) is describing the panel,
not the data.

Each of the three has a panel knob summed with a DC-coupled CV input; there are
no on-board attenuators, so you attenuate externally and centre the knob for
full-scale ±5 V CV. Interpolation is smooth by default and can be switched off
in hardware: jumper **JP8 — "Interpolation Enabled (OFF) / Interpolation
Disabled (ON)"**. A second jumper JP7 switches "Morphing Wavetables operation"
for "Phase Shift Mode".

**3. Authoring.** None. Factory ROM only, three banks, selected by switch. No
SD, no USB.

**4. Anti-aliasing.** The product page claims a "proprietary smoothing/anti-
aliasing algorithm" that computes 128 interpolated waveforms between adjacent
basic waves, yielding "over 24,000 unique waves"
(https://synthtech.com/eurorack/E350/). Read that carefully: 128 interpolation
steps between table entries is **interpolation density**, not bandlimiting.
Calling it anti-aliasing is marketing. The real anti-aliasing behaviour of the
E350 is **unverified/undocumented**.

**5. What it gets right.** The 8×8 chessboard with a legible ordering rule
("waves in a row tend to be alike, or will progress from 'dark' to 'bright' in
timbre") — a *documented semantic* for each axis. That is exactly the design
discipline Kyklophoria's generative Harmonic family already adopts (spec §5:
"one legible parameter per axis"), and it is the thing that makes a
multidimensional table playable rather than a lucky dip. Also: two independent
readers of one table, from one module, giving a free second voice.

**6. The gap.** 2-D, 8-bit, no user content, no CV over bank. Frozen in ROM.

### E352 Cloud Terrarium

Primary source: the E352 manual
(https://imagescdn.juno.co.uk/manual/660479-01U.pdf) and product page
(https://synthtech.com/eurorack/E352/).

**1. Table shape.** Same 8×8 grid of 64 waves per bank. Ships with the 3 E350
ROM banks (192 waves) plus **26 user banks** in internal FLASH — "a bank is a
8x8 matrix of 64 individual waves". Waves are 256 samples, 16-bit unsigned.
Total accessible ≈ 1,650+.

**2. Navigation.** Three parameter CVs, PARM X / Y / Z, each with a knob;
"Each setting has a unique way the 3 Parameter CVs (PARM X, Y and Z) are used
within that mode." In the Morphing modes: "The 'X direction' is within a row.
The 'Y direction' is within a column, and 'Z direction' scans the entire bank
end-to-end (from the first to the last wavetable)." Whether X/Y and Z act
simultaneously on one output or feed the two outputs separately (as on the
E350) is **not stated in the manual — unverified**; a "Mode Help" screen on the
module shows the live mapping.

Note what Z *is*, structurally: a single CV that traverses the 8×8 grid along a
serpentine path — one control reaching cells that neither X nor Y alone
reaches. It is a hard-coded, degenerate ancestor of what Kyklophoria does with
a rotation.

Glitch: "Menu item **Phs Interp** (Phase Interpolation) can be set to Off and
**Glitch** can be set to 1 of 3 intensities to simulate older wavetable synths
(like the PPG) that do not smoothly morph. This also allows a specific
wavetable to be indexed, without any morphing from the adjacent wavetable."
Other modes: Morph+WFld (wavefolder), Morph+Phs (0–360° phase shift between the
two outputs), Cloud (2/4/8 detuned copies; X = spread, Y = chaos, Z = noise
bandwidth), Cloud+Morph (X = spread, Y = chaos, **Z = morph the bank
start-to-finish**), Sample Player. Frequency ranges High 10 Hz–10 kHz, plus a
"Zero" range that lets an external CV up to 10 kHz phase-modulate a wavetable.

**3. Authoring.** microSD (FAT32, ≤32 GB, read-only — "Writing/saving to the
uSD card is not supported"), banks and single wavetables loaded from the File
page into one of 26 USR slots; card can be removed afterwards. Format is the
WaveEdit bank: "The file length is fixed at 16K 16bit words + the WAV header",
reported as 32 KB or 33 KB. WaveEdit resamples arbitrary WAVs into that
length. Patches (16 of them) live in internal FLASH, not on the card.

**4. Anti-aliasing.** Not stated in the manual. 96 kHz / 32-bit DSP engine /
16-bit output per the product page. **Bandlimiting unverified; assume none.**

**5–6.** Right: the removable-media user bank with a free cross-platform
editor, and the named glitch levels. Gap: still 2-D, still frames, no payload,
and the "third" control is a fixed path rather than a transform.

### E370 Quad Morphing VCO

Primary source: the E370 manual v1.4
(https://vcvrack.com/SynthesisTechnology/E370_manual.pdf) and
https://synthtech.com/eurorack/E370/.

**1. Table shape.** Identical structure, more of it. Spec section: "Number of
wavetables: 1,664 in 26 shared banks: a Bank is an array of 8x8 wavetables.
Wavetable Size: 256 samples, 16bit unsigned integer (7FFFh is mid-scale)."
Ships with 3 factory banks of 64 (192 waves). Four independent VCOs, 54 HP.

**2. Navigation.** 16 CV inputs — "(1V/O, FM, PARM X and PARM Y for each VCO)"
— plus 4 SYNC, and critically **all of them are freely assignable**: "Any of
the CVs can be assigned to any of a VCO's variable parameters… you can for
example assign all 4 VCOs to use PARAM X3 in different algorithms, so turning 1
panel knob affect all 4 VCOs in many different ways." Default is no sharing.
Modes with more than two parameters can pin the extras to static 0–99 values.

MorphXY: "the 2 parameters are used to create an output from the 8x8 wavetable
array. X will scan back & forth across a row and Y scans up & down a column."
Glitch: "To turn off the 'smooth morphing', turn Phase Interpolation to OFF.
You can then select 3 levels of 'Glitching'… you can 'dial in' a specific
wavetable in a bank without any influence from adjacent wavetables… the waves
will 'jump and lock' as you scan through them." MorphZ+Phase and
MorphZ+Wavefolder scan all 64 waves end-to-end. Seven modes total: MorphXY,
MorphZ+Phase, MorphZ+Wavefolder, Cloud, 2-OP FM (index 0–314%, quantised
mod:carrier ratio 1/8 to 8), Noise, Sample Player.

**3. Authoring.** microSD FAT32 ≤32 GB, 8.3 filenames, ≤255 files in root, read
only, not required to boot. `.WAV` for a single wavetable or a whole 64-wave
bank (the file *size* decides which), `.TAB` for chord tables, `.BIN` for
firmware. 26 user bank slots in internal FLASH. 16 whole-module patches
internally.

**4. Anti-aliasing.** Explicitly disclaimed for one mode: "Please note that no
anti-aliasing filtering is applied during playback. Very short sample lengths
will increase aliasing/noise." (Sample Player.) Nothing claimed for the morph
modes. Inputs are ±5 V, 12 kHz max.

**5. What it gets right.**
- **The fully assignable CV matrix.** One CV source can drive four different
  parameters in four different algorithms. This is the closest anything in the
  field comes to Kyklophoria's idea of decoupling "the CV you patched" from
  "the coordinate it moves" — but it is a *routing* matrix (permutation +
  scaling), not a rotation. It cannot produce a diagonal that no single
  parameter reaches, because each destination still receives a scaled copy of
  one source. That distinction is worth stating precisely in our own docs: we
  generalise E370's assignment matrix from "sparse, per-destination scalar" to
  "dense orthogonal N×N, and animated".
- Falling back to a static 0–99 value when a mode has more parameters than you
  have CVs. Kyklophoria has 6 CV jacks and up to 6 axes plus payload; the same
  "unpatched axis takes a stored constant" rule should be explicit in the
  io-map.
- One editor, one format, three products. Content portability across a product
  line is a real user benefit.

**6. The gap.** Four voices of the same 2-D thing. No cross-VCO structure
beyond shared CVs and output mixing.

---

## Erica Synths Graphic VCO

The acknowledged inspiration. Its "wavetable matrix" is **2-D and dynamically
resizable**, which is more interesting than it sounds.

Primary source: the user manual PDF
(https://starkerack.com/manuals/graphic_vco_manual_web.pdf, mirror of the
Erica download) and the product page (https://www.ericasynths.lv/graphic-vco-2/).

**1. Table shape.** Waves are **1024 points**; module runs at **96 kHz**;
output 10 Vpp; 16 HP; C0–C8. Bit depth: **unverified** (not in the manual's
spec table).

Three levels of organisation:
- a flat **wave list** (user + factory waves);
- a **wavetable** = up to **16 waves**; "individual wavetables don't require
  all 16 slots filled, as in-between waveforms will be interpolated
  automatically" (Sound On Sound review,
  https://www.soundonsound.com/reviews/erica-synths-graphic-vco);
- the **matrix**, whose "rows represent wavetables (wavetables appear in the
  order as set in MANAGE menu) and columns – individual waves".

Capacity: "64 wavetables and 1024 waveforms" per the Sound On Sound review;
"32 classic wavetables included" per the product page. The manual does not
state the capacity — treat 64×1024 as **review-sourced, single source**.

The **default matrix is 8 wavetables × 8 waves** — the same 8×8 as Synthesis
Technology, arrived at independently.

**2. Navigation.** Three modes: (1) A/B morph between two selected waves, (2)
Wavetable — pick a 16-wave table from a bank list and morph through it with the
MORPH knob/CV, (3) Wavetable Bank Matrix.

In Matrix mode: the **FX PARAMETER CV input** "selects waves in horizontal
direction" and the **MORPH CV input** "selects waves in vertical direction",
with the corresponding knobs acting as attenuators when a cable is patched.
So only two of the four CV inputs navigate; the other two are 1V/oct and FX
amount.

The genuinely good idea: **the matrix is a user-defined rectangular window over
a larger list**, set by clicking the left encoder to place a top-left corner,
then a bottom-right corner, then freezing the size and panning the window with
the encoders. And: "CV inputs automatically adjust to the size of the matrix,
meaning, CV span of -5V to +5V will play back all waves in the matrix and will
not play back ones outside." There is also an OFS (offset) readout — "if you
have 32 wavetables in total then OFS: 2/16 will mean that the screen indicates
wavetables from 3 to 18".

Interpolation is smooth ("wavetable interpolation is smooth as butter", SOS);
waves added to a wavetable are "automatically morphed for the best sound". No
documented glitch/step mode — unlike every Synthesis Technology and IME module.
This is a real omission on Erica's part.

**3. Authoring.** On-module drawing: PEN SKETCH (freehand, cursor cannot move
left — the wave is drawn forward in time), LINE SKETCH (line segments between
cursor points), and **SPECTRAL** editing of the **first 32 harmonics**
(navigate harmonic with the left encoder, set its amplitude with the right).
Waves are named and saved into wavetables; factory wavetables are read-only but
can be edited and saved under a new name. Snapshots store the whole module
state (waves, wavetable, FX, sub-osc) and are recalled with a pot **catch**
behaviour: "potentiometers (except TUNE) will start to have effect only when
they are rotated to the position saved with the snapshot".

Off-module: mini-USB. Firmware update runs through a **Google Chrome web app**
(WebUSB/Web Serial style) — "Open Google Chrome and open Erica Synths Firmware
Updater App". The manual says wavetable management via a "dedicated Graphic VCO
web interface (will be announced later)"; the SOS review (2018) also describes
it as unreleased. **Whether a wavetable-upload web app ever shipped is
unverified — I found no evidence that it did.** Practically: no SD card, and
user WAV import is not documented as working.

**4. Anti-aliasing.** Nothing in the manual. 96 kHz sample rate and 1024-point
waves are the whole story; reviewers praise the sound but do not test aliasing.
**Unverified/undocumented.** The manual does warn that hand-drawn waves not
starting and ending at zero "may hear undesired clicks" — a discontinuity
problem, not aliasing, but the same root cause.

**5. What it gets right — steal these.**
- **The resizable matrix window with CV auto-rescaling.** A player crops the
  space to the region they care about and the ±5 V CV span maps exactly onto
  that crop. This is the single best idea in this document for Kyklophoria: our
  space is `side^N` cells, and a player who wants to work a 2×2×3×2 sub-box
  should be able to select it and have the CVs rescale. It also composes
  cleanly with our topology: a window in Clamp is a crop; a window in Wrap is a
  smaller torus.
- **On-module spectral editing.** Erica lets you set the first 32 harmonic
  amplitudes with two encoders. Kyklophoria's hyperpoint *is* K=64 harmonic
  magnitudes — the same representation. Their UI proves it is playable with
  minimal controls, and our WebSerial surface can do it far better.
- **Snapshot pot-catch.** Same lesson as IME's slider lock; both vendors landed
  on it independently, so implement it.
- Legible mode separation: A/B morph, 1-D wavetable, 2-D matrix. Not everything
  has to be N-D at once. Kyklophoria should have a "flatten to 1-D" and a
  "flatten to 2-D" presentation for players who want that.
- A browser as the module's editor, over USB, with no install. We are already
  doing this (spec §8) — Erica validates it commercially.

**6. The gap.**
- 2-D, and the second dimension is "which wavetable", i.e. bank index. Rows are
  not a continuous timbral parameter, they are a list ordering set by hand in a
  MANAGE menu. Kyklophoria's axes have *meaning* (spec §5) and are
  interpolated in both directions equally.
- Only two navigation CVs, and one of them is stolen from the FX section.
- No glitch/step mode.
- No payload — the FX (FM, phase distortion, ring mod, fold, wrap, bitcrush,
  drive) are global master settings applied after the oscillator, explicitly
  "master settings, and they alter the signal before the output", identical at
  every point in the matrix. Kyklophoria's per-node payload vector (cutoff, FM
  index, drive, CV out) is the direct answer to that: the effect settings
  become part of the terrain.
- User WAV import appears never to have shipped.

---

## Expert Sleepers disting EX (worth knowing for the content format)

Not a dedicated wavetable oscillator, but its file convention is the other
de-facto standard and is friendlier than WaveEdit's.

Primary source: disting EX user manual 1.8
(https://www.expert-sleepers.co.uk/downloads/manuals/disting_EX_user_manual_1.8.pdf).

**1/2.** The "Poly Wavetable" algorithm is an 8-voice poly synth over a 1-D
wavetable (position + per-voice "Wave offset", −100…100, added to each voice's
position so a chord spreads across the table). 1-D only.

**3. Authoring — the part that matters.** "All wavetable files need to be in a
top-level folder on the SD card called 'wavetables'." Within it, "wavetables
can take one of two forms: a single WAV file containing all the waveforms
concatenated, or a folder of WAV files, one per waveform." Ordering is
alphabetical unless a `playlist-wavetable.txt` overrides it. "Waveform WAVs
must be in 16 bit mono format. The sample rate is unimportant, since the file
is assumed to contain exactly one cycle and so can be pitched arbitrarily."
For the concatenated form, frame length is declared per-playlist via
`-wavelength` (default 600). Ships with Adventure Kid AKWF-derived content.

**5. Steal.** Two things. (a) A **folder of one-cycle WAVs** is a far better
authoring unit than a fixed 32 KB blob — it diffs, it version-controls, it
lets you replace one node. (b) **Sample rate is explicitly ignored**; frame
length is declared out-of-band. Both should apply to `tools/kykspace`'s WAV
import (spec §5, M4): accept a directory tree whose depth equals N, each leaf a
one-cycle WAV of any length, resample to our frame size, FFT to K magnitudes.

**6. Gap.** 1-D, and the "wave offset" trick is a per-voice constant, not a
transform.

---
## Behringer VICTOR (vector synthesis, not wavetable navigation)

Included because it is the Prophet-VS lineage in Eurorack and because its
geometry is the one Kyklophoria generalises.

**1. Shape.** Four oscillators sitting at the corners of a square. Each
oscillator selects from "up to 128 waveforms"
(https://www.behringer.com/en/products/0720-ADA). So the *table* is 1-D per
oscillator (a 128-entry list); the *space* the player moves in is the 2-D
simplex/square of mixing weights between four chosen corner waves.

**2. Navigation.** A joystick plus external X and Y CV inputs with XMOD/YMOD
depth buttons; a mix output sums all four. Whether it has a recordable vector
envelope like the VS/Wavestation is **unverified** — not stated on the product
page. Bit depth, sample resolution and HP are **unverified**.

**3–4.** Waveform set appears fixed (no user loading documented) —
**unverified**. Anti-aliasing: **unverified/undocumented**.

**5/6.** The lesson is the contrast. Vector synthesis mixes *four selected
waves* by barycentric weights in 2-D — the corners are chosen by hand and the
space between them is a simplex, so the geometry is the same as one cell of a
Kyklophoria lattice, and the whole instrument is that one cell. Kyklophoria's
multilinear blend over 2^N corners is literally vector synthesis generalised to
a tiled N-D grid; worth saying so plainly in the spec's prior-art section (it
already cites Prophet VS / Wavestation). What VS-style vector gives that we do
not yet: a *recorded, playable trajectory* through the space with its own
envelope. Our orbit LFOs are the parametric version; an explicit recordable
path is a natural M6 feature.

## Ziqal Dimension MK3 (the modern DAW-format end of the field)

**1. Shape.** 1-D. "2048 samples * 256 waveforms" per wavetable, 25-wavetable
internal pool, 128 patches, 32-bit / 44.1 kHz engine
(https://ziqal.com/dimension-mk3). Up to 24 oscillators / 4 voices with
internal VCA+VCF.

**2. Navigation.** A dedicated "NAV" CV input (±8 V) plus two macro inputs
A and B, and a real-time "warp processor". Interpolation mode not documented —
**unverified**.

**3. Authoring.** microSD card file browser, and — the important part —
**native Serum and Vital wavetable format compatibility**. Drop a Serum table
on the card and play it. Firmware also updates from the card with no computer.

**4. Anti-aliasing.** Claims a "high resolution anti-aliasing filter". No
detail; method **unverified**.

**5. Steal.** Read Serum/Vital-format WAVs. That is where the world's free
wavetable content already lives (2048-sample frames, concatenated, frame count
from file length or a `clm ` chunk), and `tools/kykspace`'s import path should
accept it: resample 2048→1024, FFT→K=64 magnitudes, and lay a 256-frame table
along one axis of the lattice with the other axes filled generatively. Also:
firmware update from SD with no host — we should not require the browser for
maintenance.

**6. Gap.** 1-D. The two macros warp the *sound*, not the *position*.

## Intellijel / Cylonix Shapeshifter

Primary source: manual v2.03
(https://intellijel.com/downloads/manuals/cylonix-shapeshifter_manual_2.03_2020.03.10.pdf).
There is no v1/v2 hardware split — "v2" is firmware 2.x on the same Altera
Cyclone IV FPGA.

**1. Table shape.** 2-D and asymmetric: "128 banks, each with 8 individual
512-sample waveforms, for a total of 1024 waves", 16-bit. The bank axis is 128
wide but **not crossfaded** — it is a hard switch. The morph axis is only **8
frames deep**. So it is really 128 independent 8-frame morph groups, not one
table. A pseudo-third axis, MULTI (1/2/4/8), *concatenates* successive waves
into a 512/1024/2048/4096-sample cycle — stitching, not interpolation.

**2. Navigation.** SHAPE1/SHAPE2 pots sum with their CV jacks, smooth
interpolation within a bank (crossfade law **unverified**). Bank is
encoder-only, except that from firmware 2.01 selecting the 128th entry hands
bank select to MOD B CV — stepped over 128 banks. Deliberately non-smooth
paths: MULTI, the `gLcH` combo mode, `Bump` sync.

The firmware detail worth remembering: **a CV resolution asymmetry**.
SHAPE1/SHAPE2/MOD B/PITCH are 12-bit at 25 kHz; MOD A and FM1 are 24-bit at
98 kHz. The wavetable-position axis is the *lowest*-bandwidth input on the
module, and the reported shape-modulation artifacts follow from that.

**3. Authoring.** Hostile. No SD, no USB storage, no official editor. Appendix D
documents the format — wave data at hex `100000`, 512 two-byte signed
little-endian samples per wave, 1024 waves — but writing it means hex-editing
the firmware `.bin` and doing a full 2 MB chip erase plus ~16-minute write over
a Terasic DE0-Nano. A community WebUSB tool (WavePort,
https://github.com/mardlib/shapeshifter-wavetable-editor) now writes a single
bank without reflashing.

**4. Anti-aliasing.** Brute force. 25 MHz core clock, 55-bit phase
accumulators, tables shadowed into 100 MHz SDRAM at boot, table read rate
3 MHz per oscillator (dropping to 700 kHz in CHORD mode with 9 oscillators),
linear inter-sample interpolation, then filtered and decimated to 98 kHz. **No
mipmaps, no band-limited table variants** — the flat 1024×512×16 layout rules
them out. Decimation filter type: **unverified**.

**5. Steal.** Oversample the *nonlinearities*, not just the oscillator (ring
mod, XOR, hard sync, glitch latch are the things that actually alias). A
4 ms settle delay before latching a CV-selected preset "because many sequencers
send the gate signal before the voltage has settled" — we take preset/space
select over CV too. Soft pickup on preset recall (third vendor to land on it).
CV-ising a discrete axis by a sentinel last entry rather than a mode switch.
And: **publishing the binary format is why a community editor exists a decade
later** — argues for documenting `*.kyk` properly (docs/space-format.md).

**6. Gap.** 8 frames deep, no cross-bank morph, fixed ROM in practice. Reviewers
note the banks are "random arrangements of waveforms, rather than a smooth
progression" (https://blog.starthief.net/synth-studies-intellijel-shapeshifter/)
— curation, not count, is the limit. Our generative axes with one legible
parameter each are the direct answer.

## Mutable Instruments Plaits — wavetable model (model 6)

Verified against source
(https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/engine/wavetable_engine.cc)
and the manual
(https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/).
This is the most technically instructive module in the review.

**1. Table shape — 3-D, 8 × 8 × 4.** `kNumBanks = 4`, `kNumWavesPerBank = 64`,
`kNumWaves = 192`, `kTableSize = 128`. Waves are **128 samples, int16, plus 4
guard samples** (stride 132). 192 factory waves = 3 banks of 64, each an 8×8
grid; the 4th bank is a 64-entry *map* into the other three (default
permutation `w * 101 % 192`). Bank A additive, B formant/waveshaped, C
Shruthi/Ambika ROM-synth captures, D the permutation.

**2. Navigation — the best single idea in this document after Erica's window.**
TIMBRE = row (x), MORPH = column (y), HARMONICS = bank (z), trilinear (8 wave
reads per sample), each read Hermite-interpolated in phase. The z axis runs
0→7 over 4 banks as `if (z0 >= 4) z0 = 7 - z0;` — forward then backward — and
the backward half is **progressively un-interpolated**:

```c
const float quantization = min(max(z - 3.0f, 0.0f), 1.0f);
x_fractional += quantization * (Clamp(x_fractional, 16.0f) - x_fractional);
```

One knob traverses smooth morph, then hard wave-switching, on a continuum. No
mode switch, no menu, and the smoothing filter widens along with it.

**3. Authoring.** 4096 bytes of user data at flash `0x08007000`: a 64-byte
bank-D map plus **15** custom waves × 132 int16. Transfer is a WAV **played
into the TIMBRE input** while the wavetable model is selected — no connector at
all. Editor at https://pichenettes.github.io/plaits-editor/wavetable.html
(resamples a single-cycle WAV up to 8192 samples down to 128, with an optional
"align all phases" step).

**4. Anti-aliasing — integrated wavetable synthesis, and we should consider it.**
Waves are stored **pre-integrated**; playback differentiates on the fly through
a one-pole whose cutoff tracks pitch:

```c
const float gain   = (1.0f / (f0 * 131072.0f)) * (0.95f - f0);
const float cutoff = min(kTableSizeF * f0, 1.0f);
mix = diff_out_.Process(cutoff, mix) * gain;
```

DPW-style: integration in storage + differentiation at playback gives a
pitch-dependent alias roll-off with **no mipmap pyramid and no crossfade
logic** — one table, one differentiator. Its documented failure mode is
amplitude error at high f0 (fixed in firmware 1.2). Sample rate 47872.34 Hz,
and `plaits/dsp/dsp.h` documents and corrects the I2S divider error explicitly.

**5. Steal.** (a) The palindromic z axis that doubles as an interpolation
on/off ramp — for us, a *rotation-plane* or per-axis "quantization" parameter
that hard-clamps interpolation weights, continuously. (b) Waves sorted by
brightness *within* a row so the axis is perceptually monotonic. (c) Guard
samples baked into the stride so interpolation needs no wrap branch — directly
applicable to our lattice corner fetch. (d) Documenting and compensating a
4.6-cent clock error. (e) A user-content path over an existing CV jack.

**6. Gap.** 128-sample tables. 15 user waves and a 64-entry map — banks A–C are
fixed. Single user slot shared across models. The z axis mirrors rather than
extends, so it is 4 banks, not 8. Frames, not spectra; no payload.

Note for our own code review: the published source allocates
`wave_map_ = allocator->Allocate<const int16_t*>(kNumWavesPerBank)` (64
pointers) but `LoadUserData` writes `kNumBanks * kNumWavesPerBank` = 256 — an
over-write of the shared allocation block. Confirmed against `master` HEAD on
2026-09-08 (both the `Allocate` call and the `wave_map_[i]` write loop read as
quoted). Benign in practice given the shared buffer layout, but it is a good
argument for our static-allocation discipline.

## Mutable Instruments Plaits — wave terrain model (firmware 1.2)

**1. Shape.** Mostly *not* a table. Nine terrains: 5 closed-form analytic
functions, 3 that reinterpret the wavetable banks as a terrain (x = wave index
over 64, y = sample position over 128), 1 user terrain at 64×64 int8. The
source header states the reason: pre-computed terrains at 64×64×8-bit cost 4 kB
each, and "directly evaluating the terrain function on the fly uses less flash,
but is also faster than bicubic interpolation of the terrain data".

**2. Navigation.** HARMONICS = terrain (crossfaded between adjacent terrains),
TIMBRE = path radius, MORPH = path offset. The path is an ellipse from a
quadrature oscillator, rendered at 2× oversampling; **radius is attenuated as
pitch rises** (`max(1 - 8*f0, 0)`) — shrinking the trajectory *is* the
anti-alias measure.

**5. Steal.** Two things. The finding that evaluating a function beat
interpolating stored data, on both flash and cycles — relevant to our M5
scattered spaces and to whether generated content ever needs to be baked.
And: a **parametric closed path through the space, with radius and offset as
the two knobs**, is exactly our orbit-LFO idea in 2-D. Plaits proves it is
playable.

## Mutable Instruments Braids — WTBL / WMAP / WLIN / WTx4

Verified against `braids/settings.cc` and `braids/digital_oscillator.cc`
(https://github.com/pichenettes/eurorack/tree/master/braids). Note `WTFM` is
chaotic feedback FM, not wavetable.

**1. Shape.** One ROM: `WT_WAVES_SIZE = 33024 = 256 × 129` — **256 waves × 128
samples, 8-bit unsigned, +1 guard sample**. `WT_MAP_SIZE = 256` (a 16×16 grid).
96 kHz. Four different maps over the same 256 waves:

- **WTBL** — 2-D and *ragged*: 20 hand-authored wavetables of **4, 8 or 16**
  steps each (`WavetableDefinition { num_steps; wave_index[17] }`). TIMBRE =
  position (interpolated), COLOR = which table (**hard switch, with ±64 counts
  of hysteresis** so a single DAC bit error can't cause a glitchy transition).
- **WMAP** — 2-D continuous 16×16, TIMBRE = X, COLOR = Y, bilinear.
- **WLIN** — 1-D curated 64-entry path; **COLOR selects the interpolation
  mode** in four zones: none / inter-sample only / full / reduced playback
  resolution (phase masked to `& 0xfe000000`, i.e. 128 steps).
- **WTx4** — 1-D 32-entry path driving 4 detuned phase accumulators sharing one
  wave crossfade.

**2/3.** Fixed ROM only — no SD, no USB, no editor, no import. `waves.bin` is
an opaque baked blob even in the open source repo.

**4. Anti-aliasing.** Essentially none: "2x naive oversampling" (the source's
own words), 8-bit source data, linear inter-sample interpolation, no mipmaps.
This is precisely the gap Plaits' integrated wavetables closed two products
later.

**5. Steal.** Hysteresis on discrete axes (one line, fixes a real hardware
problem — our space/preset select over CV needs it). **Ragged step counts** so
a 4-frame axis need not be padded to 16 — relevant if we ever allow non-uniform
lattice sides. Interpolation quality as a front-panel timbre control. One ROM
reused four ways: sequence, grid, curated line, paraphonic.

**6. Gap.** 8-bit, no anti-aliasing, no user content.

## Instruo Cs-L — not a wavetable oscillator

Fully analogue dual complex VCO
(https://www.instruomodular.com/wp-content/uploads/2019/09/Cs-L-Manual-A5.pdf).
The words "wavetable", "morph" and "through-zero" do not appear in the manual.
What reads as morphing is **wavefolding**. Waveforms are simultaneous physical
outputs, not an indexable sequence. No table, no loading, no sampling in the
audio path. Included here only to stop it being miscatalogued.

**Steal one idea:** the **Index bus** — two internal VCAs carry each
oscillator's sine, one global Index knob+CV scales both, and the Index button
is a **shift key**: held, six other buttons toggle six modulation destinations,
with the routing state shown on the very LEDs it shifts. One macro, six
destinations, no menus. Kyklophoria has 3 buttons and 6 pots and no encoder;
this is a workable pattern for the rotation page. Second idea: **disabling a
destination reverts it to a direct un-indexed normal rather than to silence** —
graceful degradation on every internal route.

## Qu-Bit — Surface, Chance, and (the actual wavetable one) Chord v2

**Surface** (https://www.qubitelectronix.com/s/QB_Surface-j8mz.pdf) is
physical modelling / FM / noise, not wavetable: 7 heterogeneous algorithms
(waveguide pluck, modal bar, two FM e-pianos, sine kick, noise snare, prepared
piano). The Model knob "selects the model to be used for the *next triggered
voice*" — **model changes commit at note-on**, which sidesteps discontinuity
entirely at the cost of losing timbre sweep. Runs on an Electro-smith Daisy
(firmware updates via the Electro-smith Web Programmer), i.e. our platform. No
user content, no presets, and **no documented sample rate, bit depth or
anti-aliasing — unverified**. Its transferable idea is a **constant control
surface across heterogeneous engines**: Strike and Tone mean something
different per model but occupy the same two knobs, documented per model.

**Chance** (https://www.qubitelectronix.com/s/Chance_Manual.pdf) is a
stochastic modulation source, not an oscillator. It has a jack literally
labelled "Wavetable Output", but that is a fixed ROM of 8 LFO shapes played at
one of 5 randomly chosen rate factors, with **no shape-select control** and an
internal clock topping out at 50 Hz. Not wavetable synthesis. Its one good idea
is worth noting: **a single coin toss reseeds seven outputs simultaneously**,
so they are different renderings of the same random draw — coherent related
chaos rather than seven independent noise sources. Directly applicable to our
rotation orbit rates and to any "randomise position" gesture.

**Qu-Bit's actual wavetable module is Chord v2**: a 2-D bank+position table
loaded from the **microSD root**, with a `chordconfig.txt` flag
`LONGFORM_USERBANK` — off gives "8 banks of 8 waveforms", on gives "a single
bank of 64 waveforms". Qu-Bit recommends Synthesis Technology's WaveEdit for
authoring (https://www.qubitelectronix.com/wavetables). Exact file format, bit
depth and samples-per-frame **unverified**. Two observations: WaveEdit's 64×256
bank has become a cross-vendor standard, and a **plain text config file on the
card** is a cheap way to expose structural options — worth copying for `*.kyk`
(e.g. declaring N, side and per-axis topology in a sidecar).

## Noise Engineering — which ones are actually wavetable

Primary sources: https://manuals.noiseengineering.us/. The honest answer:

- **Cursus Iteritas** (CI / CIM / CIP / Alia) — the only one NE calls
  wavetable, and **the table is computed on the fly**, not stored. "Center,
  Width, Tilt, Structure determine amplitudes for each harmonic. This
  description is fed into the inverse transform for the current function set to
  produce the time-domain wavetable. The wavetable is normalized to reduce
  amplitude variations across spectral changes." Basis sets: Fourier, Walsh,
  Daubechies-4. **This is architecturally the closest module in Eurorack to
  Kyklophoria's hyperpoint** — a spectral description, inverse-transformed per
  update, normalised. The difference is that NE synthesises the description
  from four knobs, where we interpolate it out of an N-D lattice.
- **Ataraxic Iteritas** — fixed bit tables (LFSR / SQR / SQR2). NE state the
  trade explicitly: encoding the waveforms into a table "ended up being
  significantly faster than computing it on the fly", whereas BIA computes on
  the fly *because* Morph needs it.
- **Basimilus Iteritas** — additive: six tonal oscillators plus an LCG noise
  oscillator, each enveloped and summed. NE: "essentially wavetable though they
  are evaluated on the fly as this is needed for the Morph knob."
- **Loquelic Iteritas — not wavetable.** VOSIM / Moorer summation synthesis /
  naive phase modulation. (The brief's guess was wrong.)
- **Debel Iteritas Alia — not wavetable.** Hybrid additive phase modulation.
- **"Ruina" is not an oscillator** — it is NE's distortion family
  (https://manuals.noiseengineering.us/ruina/).

**4. Anti-aliasing — the one genuinely novel trick in the field.** NE run a
**sample rate that is a multiple of the fundamental**: "This moves alias power
that is a multiple of the fundamental to be mapped to a multiple of this tone,
therefore making the aliasing align with the harmonics of the tone… As the
period of the full length of the wavetable always evenly divides the sample
rate, the additional aliasing is largely harmonic in nature." Rather than
pushing aliases above Nyquist, steer them onto the harmonic grid. It costs
nothing and only degrades when the spectrum is strongly inharmonic — which NE
document. Not applicable to us as-is (we are locked to 48 kHz by the codec) but
the *idea* — choose a frame length so that alias images land harmonically — is
worth a thought for the IFFT read.

**3. Authoring — none, anywhere in the line.** No SD, no USB storage, no WAV
import, no table format, for any NE module. Their answer to "more sounds" is
swapping the whole firmware (Alia has 7 oscillator firmwares, swapped over
micro-USB via their customer portal; the Alia binary format is closed —
**unverified**). Versio is the open exception and is Daisy Seed based, with
libDaisy support, but NE state it cannot pitch-track as an oscillator.

**5. Steal.** Normalise the spectrum after every regeneration so timbre knobs
are not stealth volume knobs — we regenerate a spectrum every block and have
exactly this problem. Expose interpolation quality as a timbre knob (their
"Edge": point-sampling → cubic spline). A **Hold** button that freezes incoming
CV — one GPIO, big performance payoff, and trivially useful for auditioning a
point in our space. Parameterise the spectrum with a legible metaphor (a
bandpass over harmonic index) and then swap the *basis function* for free
range.

## XAOC Devices Odessa — spectral additive, not wavetable

Primary source: manual rev. 1975/1.2
(http://xaocdevices.com/manuals/xaoc_odessa_manual.pdf). "Wavetable", "table",
"preset" and "memory" appear zero times in it.

**1. Structure.** Live sum of individually computed sinusoids, "up to 2560
harmonic partials" = 512 per voice × 5 unison voices, on an FPGA (exact part
**unverified**). Three closed-form laws: amplitude `Aₙ = A₁ / n^γ` (SPECTRAL
TILT, γ ≈ 3 → 1 → 0), a **warped Sinc comb in the frequency domain** (DENSITY
= 0–256 notches, WARP = notch-spacing uniformity, PEAKING = shape), and
frequency `Fₙ = n × F₁` stretched or compressed by TENSION. Explicitly not a
filter: "there is no time-domain filtering applied to the signal… instead, a
frequency-domain shaping function is applied to the amplitude of each harmonic
partial."

**2. Navigation.** One knob per function, 12 CV inputs, no menu. Continuous
where the parameter is a continuous exponent; **inherently stepped where it is
an integer count** — the manual says PARTIALS' "response appears to be stepped
… because it causes consecutive partials to be turned on and off", and does not
paper over it.

**3.** No storage medium of any kind, and no firmware updates (Odessa is absent
from XAOC's firmware list). The spectrum is a parameter set, not data.

**4. Anti-aliasing.** "So as to avoid aliasing, Odessa does not produce
partials whose frequencies would exceed the maximum frequency of 21 kHz." That
is the whole strategy — additive is band-limited by construction if you refuse
to instantiate a partial above the ceiling, and it survives MHz-range FM
excursions because the culler is the same code path either way. Note the
ceiling is a fixed 21 kHz, not Nyquist. Sample rate and DAC depth
**unverified**.

**5. The distinction that matters for us.** Kyklophoria is on Odessa's side of
this line, not the wavetable side: our hyperpoint is K harmonic magnitudes, our
bandlimiting is "zero every harmonic with k·f0 ≥ Nyquist before the IFFT"
(spec §4), which is exactly Odessa's policy applied once per block instead of
once per partial. The write-up should say so: **we are an additive engine with
a wavetable-shaped control surface**, and we get additive's free band-limiting
without additive's per-partial silicon cost, because the IFFT amortises it.
That is the honest architectural claim.

Two more portable lessons. **Normalise loudness perceptually and document the
cost** — XAOC state that with a flat spectrum the difference "can exceed 50 dB"
and that their compensation makes low partials duck as the spectrum brightens.
And **never insert a discontinuity behind the user's back**: leaving TENSION's
centre decorrelates partial phases and returning does not restore the waveform;
XAOC refuse to auto-resync "because this produces an audible click" and bind
resync to a deliberate button. Our phase-from-seed derivation (spec §3.1, §9.3)
has exactly this hazard when the seed or K changes mid-note.

**6. Gap.** Nothing outside `A₁/n^γ` × warped-sinc comb over a stretched
harmonic series is reachable: no absolute-frequency formants, no per-partial
amplitudes, no analysis/resynthesis, no presets, no user content. Kyklophoria's
lattice *is* per-partial amplitudes, arbitrary and authored.

## Modbap Osiris (brief note)

Marketed as a "bi-fidelity wavetable oscillator" with "4 banks of 32×32
wavetables each" and "user wavetables via Micro SD Card"
(https://www.modbap.com/products/osiris). That figure is internally
inconsistent with the "256 total" claim on the same page and I could not reach
the manual to resolve it — **treat the table shape as unverified**. Worth
revisiting if a 2-D 32×32 bank turns out to be real; it would be the largest
per-bank grid in the field.

## 4ms Spherical Wavetable Navigator (SWN) — read this one twice

**This is the most important module in the review after Piston Honda, and the
one closest to Kyklophoria's *ideas* (as opposed to its shape).** It is the only
Eurorack wavetable module with a wrapping topology, and the only one with any
kind of CV-controlled vector transform of position. Primary source throughout:
SWN User Manual 1.0, 3 May 2019
(https://4mscompany.com/SWN/manual/SWN-manual-1.0.pdf), verified here by direct
text extraction of the PDF.

**1. Table shape — 3-D, and explicitly a torus.** "A Sphere is a collection of
27 waveforms arranged in a 3x3x3 shape. A Sphere can be navigated in three
directions: Depth, Latitude, and Longitude. Navigating in a direction 'wraps'
around to the beginning when it reaches the end." And in the geometry section:
"While we use the term 'Sphere' when talking about the wavetables in the SWN,
the structure of each wavetable is actually a 3-torus… if you navigate far
enough in any direction, you end up back to where you started." Specs: "512
samples per waveform, 16 bits per sample, 44.1kHz sampling rate, 27 waveforms
per Spherical Wavetable, arranged in a 3x3x3 3-torus, 12 Factory Spherical
Wavetables + 108 Custom Spherical Wavetables = 120 maximum", 108 preset slots.
Six channels, each a full voice with its own position in the space.

**2. Navigation — four coordinate systems on one manifold, plus a transform.**
- **Depth** (knob + CV), **Latitude** (knob + CV), **Longitude** (**knob only —
  no CV jack**). The axes are not equal citizens under voltage control. CV range
  0 to +5 V, offsetting the knob. Holding a Channel button restricts the change
  to that channel.
- **Browse** (knob + CV): "navigates in a zig-zag (or 'snake') pattern that
  covers the entire Sphere" — a 1-D reparameterisation of the whole 3-torus,
  available *simultaneously* with the three axis controls.
- **Sphere** (push-turn + CV): which sphere each channel navigates.
- **Dispersion** (push-turn Depth; knob + CV): "It effects all channels by
  navigating each channel in a different combination of directions, as if all
  the channels were moving away from a central point on the Sphere. Dispersion
  wraps around the Sphere, so if you keep turning the knob, all the channels
  will start moving towards each other."
- **Dispersion Pattern** (push-turn Latitude; knob + CV): "It selects one of
  several patterns of directions that Dispersion moves the channels."
- **WT Spread** (knob + CV): the same idea one level up — a pattern-selected
  per-channel offset applied to the *sphere* index.

So: a CV-controlled scalar applied along a CV-selected set of six per-channel
direction vectors in a wrapping 3-D space. **This is the nearest prior art to
Kyklophoria's rotation and it means spec §10's claim needs qualifying** — see
Gaps below.

Legibility without a screen: red hue = Depth, green = Latitude, blue =
Longitude on the outer light ring; inner ring colour = sphere selection. Six
voices' positions in a 3-torus, readable at a glance.

**3. Authoring — no SD card and no USB.** Two paths, both interesting:
- **Sphere Recording Mode**: record from the Waveform In jack and auto-split,
  documented to the sample — "8 Spheres (27 waveforms each) 512 samples per
  waveform = 110,592 samples recorded at 44,100Hz = 2.508 seconds. An additional
  130ms of samples is recorded for padding afterwards, which is required for
  Waveform Stretch to be applied to the last waveform."
- **SphereEdit**, free and open source, and per the manual "heavily based on
  Synthesis Technology's WaveEdit… written by Andrew Belt". Transfer is by
  **playing the sphere as audio into the Waveform In jack.**

Post-recording editing is deep and, crucially, **dimension-aware**: Waveform
Spread (spacing of the 27 slices through the 2.5 s buffer), Waveform Shift
(start offset), Waveform Stretch (changes samples per waveform then resamples
back to 512, so it also changes pitch), plus Wavefolding, Bit Decimation,
Metalizer, brick-wall Low Pass, Gain/Normalization and **Seam Smoothing**.
Effects apply to one waveform, to a whole *dimension* of three waveforms (hold
the effect and turn Depth/Lat/Long), or to all 27.

**4. Anti-aliasing.** Not documented for the oscillators; the only aliasing
statement in the manual concerns the LFOs at audio rate. **Unverified — assume
none.**

**5. What it gets right — four things to take.**
- **Wrapping topology on every axis, stated as a topological fact and explained
  to the player in the manual.** Our Wrap mode (spec §3.3) has a commercial
  precedent — which is reassuring, not deflating: players cope with it. It also
  means our seam handling must be as good as theirs. Note that **Seam
  Smoothing** exists precisely because a recorded frame does not loop cleanly;
  in the spectral domain we get that for free, which is a point in our favour
  worth making.
- **Multiple coordinate systems over one space, live at once.** Depth/Lat/Long
  *and* Browse. Kyklophoria should offer the same trio: axis coordinates, a
  space-filling scan, and the rotated frame.
- **Dispersion as a per-voice displacement field with a CV-selectable pattern.**
  Our stereo mechanism (spec §9.1, positions at θ±δ) is a two-voice special case
  of exactly this. Generalising δ to "a spread amount along a selectable pattern
  of direction vectors" is a small change to code we are already writing and is
  what would let the stereo pair grow into an ensemble.
- **Editing operations aware of the space's dimensions** — apply an effect along
  a whole axis, not to one node. `tools/kykspace` should work this way.

**6. Gaps.** Only 3×3×3 = 27 nodes per sphere; all the richness comes from
interpolation and the content density is very low. **Longitude has no CV
input.** Dispersion patterns are a fixed built-in set, not user-definable, and
Dispersion moves *channels relative to each other* — a single channel still
moves on axis-aligned coordinates, so it is not a transform of the player's
frame. No file system: transfer is a 2.5 s audio playback, level-dependent and
slow. No documented anti-aliasing.

## Ferry Island Modular Four Seas — our closest sibling, and open source

**Same MCU as us.** https://github.com/Ferry-Island-Modular/Four-Seas, **MIT
licensed**, "Wavetable multi-output eurorack module" (confirmed via the GitHub
API, 2026-09-08), built on the "STM32H750IBK6 - ARM Cortex-M7 microcontroller
(Daisy Seed)". Product page
https://www.ferryislandmodular.com/products/four-seas. 26 HP, 599 €, released
2025.

**1. Table shape.** README: "Up to 12 banks × 8 pages of wavetables", directory
layout `/[1-12]/[1-8].wav`, each WAV holding "8 waves × 8 columns of samples".
Read literally that is 12 banks each holding an 8×8×8 cube — the Piston Honda
geometry, twelve times over, with much longer frames. **The exact reading of
"8 waves × 8 columns" is ambiguous in the README; treat the geometry as
unverified until the source is read.** `WAVE_SAMPLES` defaults to **2048
samples per wave and is a compile-time constant** (`make WAVE_SAMPLES=1024`).
Bit depth not stated — **unverified**.

**2. Navigation.** Marketed as "four-dimensional": X, Y, Z through the cube, CV
over the bank, plus a **spread across the four outputs** with relationship modes
(harmonic ratios, primes, golden-ratio proportions, per the vendor). Be precise
about this in our own materials: **the navigation is 3-D; the "fourth
dimension" is a per-output displacement**, architecturally the same family as
SWN's Dispersion, not a fourth index. Also thru-zero phase modulation,
phase-distortion waveshaping, bitwise XOR, two sync inputs, I²C
(Teletype/Crow/ER-301) and Xaoc Leibniz compatibility.

**3. Authoring.** microSD on the front panel, WaveEdit-compatible, Serum table
support announced. The `/bank/page.wav` tree is close to the N-D directory
convention we want.

**4. Anti-aliasing.** Nothing in the README — **unverified**. With 2048-sample
frames they may be relying on frame length, which is not bandlimiting. Community
impressions of "less digital crunch" than Piston Honda are anecdotal.

**5/6. Consequences for us,** in order of importance: (a) it proves the CPU
budget — four wavetable oscillators at 2048-sample frames on an H750; (b)
MIT-licensed source on our exact platform is worth reading before M1's cycle
counts are finalised; (c) the market accepts a 26 HP, €600 wavetable navigator;
(d) its "fourth dimension" is marketing for an output spread — exactly the
claim Kyklophoria must not make loosely. Our N is an actual index count.

## Waldorf nw1 (and the rest of the Waldorf Eurorack line)

Primary source: nw1 Reference Manual rev1.0
(https://www.strumentimusicali.net/manuali/2018/01/15/bb/waldorf-nw1-en.pdf),
plus https://waldorfmusic.com/legacy-nw1/.

Line inventory first, because two premises need correcting. Waldorf's Eurorack
modules are **nw1** (the only oscillator), **kb37** (a 37-key Fatar keyboard and
100 HP case with a 16-bit CV interface — *not* an oscillator,
https://waldorfmusic.com/en/kb37), **mod1**, **dvca1**, **cmp1** (analogue
compressor) and **vcf1** (analogue 12 dB/oct multimode filter). **There is no
Waldorf Eurorack "Dual Oscillator" — not verified to exist.**

**1. Table shape — strictly 1-D, but circular.** Position is in **degrees
(0°–360°)**, scan speed in **RPM**. A table holds **8 to 4096 waves**; wave
length is selectable per table from 32/64/128/256/512/1024 samples; 16-bit.
(The widely repeated "64 waves per table" figure is wrong — 64 is one of the
legal *wave lengths in samples*.) Content: **80 ROM wavetables**
(Microwave/Wave/Nave classics), 10 user slots, 50 text-to-speech slots, in an
8 MB flash budget split 5 MB ROM / 2.5 MB user / 0.5 MB text. Recorded user
tables are capped at 256 waves ≈ 7 s at a fixed 512 samples/wave, in a
compressed format that **discards phase** and keeps a more detailed envelope
representation.

**2. Navigation.** Three assignable Mod CV inputs (−12…+12 V), each with a gain
pot and a 3-position destination switch, plus 1V/oct and a gate. Scanning is
interpolated, not stepped: "the wave cursor is not discrete… played waves are
interpolated between adjacent waves" (§6.3). Three travel modes — **Gated**
(runs from Position while the gate is high), **Step** (each gate advances by one
predefined *section*; section counts are a per-table property listed in Appendix
A, 4–13 per table), **Free** (continuous loop). Travel = 0 RPM turns Position
into a static selector.

**The big navigation gap: wavetable *selection* is not CV-controllable at all** —
only position, travel, spectrum and noise are. Sound On Sound also reports the
destination switches collide
(https://www.soundonsound.com/reviews/waldorf-nw1-wavetable-module).

**The second, genuinely orthogonal axis** is inherited from Nave and is the
reason to care: **Spectrum** transposes the spectral envelope ±64 semitones
*independent of pitch*; **Brilliance** (−100…+100%) sharpens or flattens
spectral peaks; **Keytrack** at 100% is classic Microwave behaviour and at 0%
becomes a fixed formant filterbank; **Noisy** moves the rendering from periodic
to aperiodic. A formant axis decoupled from pitch, on a wavetable oscillator, is
rare in this format.

**3. Authoring.** **USB only, no SD.** Galvanically isolated USB carries
wavetable transfer, TTS text and firmware; SysEx over MIDI also works in
bootloader mode. Free Mac/Windows nw1 Wavetable Editor
(https://waldorfmusic.com/faq-items/how-can-i-load-in-my-own-wavetables-to-the-nw1/).
Plus an **onboard recorder** turning audio at Mod CV input 1 into a table in
real time, threshold- or gate-triggered, with a compression/normalisation
control; Waldorf's product page calls the analysis "time domain multiple
foldover analysis". Separately an integrated **text-to-speech** synthesiser
turns typed text into a wavetable (50 slots). Caveat worth recording: manual
rev1.0 §8 ("Transferring wavetables") and §9 ("Speech wavetables") both read
**"Coming soon."** — the shipped documentation never described the two headline
features.

**4. Anti-aliasing — the best-documented in the whole review, and it is a user
choice.** Set per recorded table with the Travel Speed pot at record time
(§7.4.2): **Nave-grade** ("best resampling using multiple filters"), **Microwave
quality** ("resampling with additional harmonics at low pitches, not
mathematically perfect"), and **Aliased** ("crude resampling with a lot of
aliasing"). Nobody else in this document names their aliasing tiers.

**5. Steal.** (a) **Named, selectable resampling-quality tiers stored with the
content.** We already have a measured aliasing bar; exposing a
clean/vintage/dirty choice as a *space-level property* costs almost nothing in
the spectral domain (change the K truncation and the rolloff taper) and is
honest about what it does. (b) The **spectral-envelope axis decoupled from
pitch** — a formant-shift payload lane that scales harmonic index rather than
frequency: cheap for us, expensive in any frame-based engine. (c) **Position in
degrees with a signed RPM travel rate** is a better mental model for a wrapping
axis than "0..1 with wrap", and it is exactly what our orbit LFOs need at the UI
level.

**6. Gap.** 1-D. No CV over table selection. Three mod inputs with colliding
destination sets. User tables are lossy (phase discarded) and capped. No SD. Now
on Waldorf's Legacy page.

## Rossum Electro-Music — Panharmonium and Trident

### Panharmonium — spectral resynthesis, not wavetable

https://www.rossum-electro.com/products/panharmonium. Analyse → modify →
resynthesise; there is no table of stored waveforms. The live state is a
spectrum of **up to 33 partials**, and **Voices (1–33)** sets how many
oscillators rebuild it — at 1, monophonic tracking of the loudest component; at
33, dense resynthesis. Oscillator waveforms: sine, triangle, sawtooth, pulse
plus two crossfading sine/saw variants. Storage: 12 factory + 12 user Spectra
(frozen slices) and 12 factory + 12 user presets.

Controls that matter to us: **Slice** (analysis rate — knob, musical multiplier,
tap, or external clock, so spectral stepping can be tempo-locked);
**Center Freq / Bandwidth** (which band gets *analysed*, not a filter over the
audio — bipolar Bandwidth gives passbands one way, notches the other);
**Blur** (a CV-controlled rate limiter on how fast the spectrum may change);
**Freeze** (sustains the current spectrum, which then plays chromatically over
1V/oct — the moment it becomes a 33-partial additive oscillator); **Spectral
Warping** (shifts harmonics *individually* rather than preserving harmonic
ratios). Firmware ships as a **WAV played into the 1V/oct input**.

FFT size, bin count, window and the slice range in ms are **not published —
unverified. Do not cite a bin count for this module.** Rossum do document that
latency is inherent: spectral analysis takes time, "fundamental to the
algorithm, not a bug".

**Steal:** (a) **Blur** — an explicit, CV-controlled rate limiter on *spectral*
change, distinct from position smoothing. We have slew on payload lanes (spec
§3.5); a slew on the interpolated spectrum itself is a different and very
musical control. (b) **Freeze** — hold the current spectrum and play it
chromatically; trivial for us (stop updating the interpolation) and the same
primitive as NE's Hold. (c) Make the scan rate **clockable** so navigation
becomes rhythmic.

**Distinction to state plainly:** Panharmonium *derives* its spectrum from
incoming audio; Kyklophoria *interpolates* its spectrum from stored data. Same
representation, opposite direction. If we ever want an analysis input this is
the reference design — and its acknowledged latency is the reason to think
twice.

### Trident — analogue, not wavetable

Three precision analogue oscillators: one Carrier (all three waveforms out at
once) and two Modulation Oscillators. The "morphing" is analogue shape control —
**Symmetry** morphs triangle → sawtooth or varies duty cycle, and **Zing** (a
ring-mod-like carrier/modulator interaction) combined with **Sync** produces the
wavetable-like impression. Sound On Sound: "Using the combination of Zing and
Sync, it sounds very much like wavetable synthesis in the way the waveforms can
morph and evolve"
(https://www.soundonsound.com/reviews/rossum-electro-music-trident) — a simile
in a review, not an architecture claim. **Do not list Trident as a wavetable
oscillator.** Its value is as the analogue counter-example: continuous,
artifact-free, alias-free timbral motion with no interpolation machinery — and
no recallable, indexable position, which is the whole point of a navigator.

## ALM Busy Circuits — one real wavetable module, and it is not the famous one

Confirmed **not** wavetable: **Akemie's Castle** (ALM011) and **Akemie's Taiko**
(ALM015) are 4-operator digital FM on NOS Yamaha ICs
(https://busycircuits.com/pages/alm011, https://busycircuits.com/pages/alm015);
**Squid Salmple** (ALM022) is an 8-channel sampler; **Pip Slope** (ALM028) is a
function generator.

**ALM's wavetable module is the MCO mkII (ALM046)**, manual v0.4
(https://assets.busycircuits.com/docs/alm046-manual.pdf).

**1. Table shape.** A **dual 1-D frame sequence with a nonlinear combine axis**.
User banks are **64 waveforms × 256 samples, 16-bit, 44.1 kHz** — the WaveEdit
bank again. Internally, "User tables are split into 4 groups of 16 waves and
further divided in half, with the first half to Wave A and second half to Wave
B." ALM's own copy calls it a "multi dimensional wavetable oscillator", which is
generous.

**2. Navigation — the interesting part.** Wave A, Wave B (independent
positions), Invert B, AB Mix, and **Mix Mode**: five *nonlinear* combine
algorithms — *Morph* (crossfade), *Splice* (overlays then splices A and B at a
point in the two cycles, "more complex PWM style sounds"), *Multiply* (mixes A
with A×B), *Min* (mixes A with whichever of A/B is closer to zero), *Max*
(whichever is further from zero). Plus PWM ("replaces the last X% of the output
waveform cycle with silence"). Four freely assignable CV inputs, and **preset
recall is itself CV-assignable**.

**3. Authoring.** **USB-C mass storage, drag and drop.** Power down, plug in,
the module mounts as removable storage, copy a WaveEdit-format `.wav`. ALM's
manual points at synthtech.com/waveedit and at the public-domain library at
https://smpldsnds.github.io/wavedit-online/. Same channel for firmware and a
`PRESETS.BAK` backup.

**4. Anti-aliasing.** Not documented; "aliased" appears only as a deliberate
preset character. **Unverified.**

**5. Steal.** (a) **Nonlinear combine modes between two positions in the same
space.** Our stereo pair already reads at θ±δ; letting the *mono* sum choose
Morph/Multiply/Min/Max instead of a plain mix is a handful of lines in the
spectral domain, and it is the glitch-morph axis Erica lacks. (b)
**CV-assignable preset recall** — one CV jumps whole configurations. (c) **USB
mass storage as the content transport**: no card, no editor, no driver. We
expose the SD over HostLink `FsExtension`; the lesson is that drag-and-drop
beats a protocol for bulk content.

**6. Gap.** One bank at a time, 64 waves fixed, and the rigid A/B split (16-wave
groups, half to A, half to B) is a content constraint authors must design
around.

## Malekko / Richter Mega-Wave — a 3-D index that is not an oscillator

The other 3-D navigator in the format, and it predates most of them. A
**waveshaper / transfer-function module** descended from the Wiard Miniwave:
**10 ROMs × 16 banks × 16 waves = 2,560 waveforms, with wave, bank and ROM
selection all voltage-controllable** plus manual fine-tune per parameter
(https://www.analoguehaven.com/malekko-heavy-industry/richter-mega-wave/). No
user loading. The lesson: three CV-addressable indices over a ROM/bank/wave
hierarchy has existed since the Miniwave, and — like Piston Honda — nobody
added a fourth or a transform. ("Richter Oscillator II" is analogue and
unrelated.)

## Quick verdicts on the rest

- **Tiptop Audio ART Vortex** — Tiptop's actual wavetable module (not ONE, not
  Z3000): dual wavetable oscillator voice in 8 HP, **64 waveforms per table**,
  factory plus user tables from a front-panel microSD, position knob + CV.
  **1-D position only**
  (https://tiptopaudio.com/manuals/Tiptop_Audio_ART_vortex.pdf). **Tiptop ONE**
  is a sample player that can be looped to *act* as a wavetable oscillator;
  **Z3000** is all-analogue.
- **Dove Audio Waveplane** — the strongest "more than three CV-addressable
  indices" case in the format. Four corner oscillators sounding at once; X and Y
  (both with CV) vector-morph between them; and "Each 'corner' wave has 32
  waveforms which are arranged in wavetables", with "the ability to change all
  four waveforms independently using control voltages"
  (https://postmodular.co.uk/modules/waveplane-oscillator/). **Six
  CV-addressable indices** — but not six dimensions of one space: a 2-D mixing
  plane over four *independent* 1-D tables, a product of two different kinds of
  coordinate. Plus inversion switches for the right pair and the bottom pair.
  16 HP, 50/50/100 mA and it **requires a regulated +5 V rail**. No user content
  and no anti-aliasing documented — both **unverified**.
- **RYK Vector Wave** — vector synthesis with a joystick over four banks, and
  the joystick vectors can be **stored, animated and triggered**. But each bank
  is 16 sine oscillators in series/parallel FM or additive, not wavetable frames
  (https://www.analoguehaven.com/ryk-modular/vector-wave/manual.pdf). Vector ≠
  wavetable; the recordable animated vector is the feature to note.
- **Codex Modulex** — the brand exists (https://codex-modulex.com/) and builds
  8 HP µ-format reimplementations of Mutable designs: **µOsc-I = Braids,
  µOsc-II = Plaits**. No original wavetable engineering and no vendor
  documentation of format or anti-aliasing. Cite Mutable, not Codex.
- **"Zetamix"** — **no Eurorack module, brand or product by this name could be
  found.** Zetamix is a French ceramic/metal 3D-printing filament line by Nanoe
  (https://zetamix.fr/en/). Drop the name unless a source turns up.
- **Befaco** — **has no wavetable oscillator.** Pony VCO, Even VCO and Octaves
  VCO are all analogue (https://www.befaco.org/pony-vco/). **"POKO" and "Bard"
  could not be verified as Befaco modules — unverified**, possibly confusions
  with Percall/Kickall. Motion MTR is a mixer with metering. The closest is
  **Lich**, a Rebel Technology OWL programmable DSP module — relevant only as a
  reminder that an open DSP platform is a different product category from a
  designed instrument.
- **Xaoc Batumi** — quadruple digital LFO, not wavetable
  (https://xaocdevices.com/main/batumi/). Xaoc's wavetable-adjacent device is
  **Jena**, not Odessa.

---

## Gaps Kyklophoria can fill

Ordered by how defensible each one is.

**1. Dimensionality above three, in one homogeneous space.** The maximum for a
single table is 3: Piston Honda mk3 (8×8×8), 4ms SWN (3×3×3), Four Seas
(8×8×8 per bank), Mega-Wave (ROM/bank/wave); Plaits is 8×8×4 with a mirrored z,
so effectively 8×8×4. Dove Audio's Waveplane has six CV-addressable indices but
they are a 2-D mixing plane over four *independent* 1-D tables — heterogeneous
coordinates, not a 4-D lattice. Our N=4 default with N≤6 over one homogeneous
lattice is unoccupied. Two honest caveats: **3 is where the panel runs out, not
where the idea runs out** (IME can give you three sliders and then stops), and
**bank-select CVs are a de facto extra index** on the E370, Four Seas and
Mega-Wave — the difference is that a bank index is discrete and unblended, where
our fourth axis interpolates like every other.

**2. A linear transform on the position CVs — the novelty claim, restated
carefully.** No module applies a matrix to the position CVs before indexing.
Ranked by closeness:
- **4ms SWN's Dispersion + Dispersion Pattern** (nearest prior art): a
  CV-controlled magnitude applied along a CV-selected pattern of per-channel
  direction vectors, wrapping. Genuinely a voltage-controlled displacement field
  over the navigation space with a voltage-selectable basis. What it is *not*:
  it moves the six channels relative to each other, so a single voice still
  travels on axis-aligned coordinates; the patterns are a fixed built-in set;
  and it does not change what any given CV *means*.
- **E370's assignable CV matrix**: any of 16 CVs to any parameter of any of 4
  VCOs. But each destination receives a scaled copy of *one* source — a sparse
  permutation-with-gain, not a dense rotation; it cannot make a coordinate that
  is a mix of two CVs.
- **Attenuverters on each axis** (Piston Honda mk1–mk3): patch one CV to all
  three axes through three attenuverters and you *do* get an axis-aligned linear
  combination — a fixed direction in the cube. This is the real prior art for
  the diagonal and we should say so. What it cannot do: preserve the metric (the
  gains are not orthogonal, so the "diagonal" shears the space), be modulated
  coherently (three knobs, no shared angle), or orbit.
- **E352's Z and SWN's Browse**: fixed space-filling paths through a grid — one
  control reaching cells no axis reaches, hard-coded.

**So the defensible claim, which spec §10 should be amended to, is:** *a
modulated orthogonal rotation of the player's control frame — a product of
Givens rotations with per-plane static angles and orbit rates, applied to the
position CVs before indexing — has no Eurorack precedent.* Not "nobody
transforms position CVs" (SWN does) and not "nobody reaches diagonals" (three
attenuverters do). The three things that are ours: **orthogonality** (rotation
preserves the space rather than shearing it), **modulation of the transform
itself** (angles with their own LFOs and ratio lock, producing quasi-periodic
orbits), and **application to the player's frame rather than to a voice
ensemble**.

**3. Topology — partly occupied, and by a good module.** The claim "everything
is clamped" is false: **4ms SWN wraps on all three axes and 4ms describe the
structure as a 3-torus in the manual**, exactly our Wrap mode. So Wrap has
precedent and a proven ergonomic. What remains unoccupied: **per-axis choice of
topology** (SWN wraps everything, everyone else clamps everything; mixed
clamp/wrap per axis is ours), and **Sphere** — no Eurorack module treats the
coordinates as a direction on S^(N-1) where rotation is the only motion. A minor
free extra: the E370 manual says MorphXY "will scan back & forth across a row",
which reads as a *reflecting* boundary — a fourth topology, cheap to add
alongside Wrap.

**4. Spectra at the nodes instead of frames.** Every module here stores time-
domain frames: 128 samples (Plaits, Braids), 256 (Synthesis Technology, IME),
512 (Shapeshifter), 1024 (Erica), 2048 (Ziqal). Only Cursus Iteritas works from
a spectral description, and it synthesises that description from four knobs
rather than storing it. Storing K harmonic magnitudes per node gives us three
things nobody else has together: bandlimiting is free (truncation), N-D
interpolation is K+P MACs per corner instead of a frame blend, and the storage
is small enough that 4^4 nodes fits in 73 KB.

**5. Per-node payload.** Nothing in this review attaches non-audio data to a
table position. Erica's FX are global master settings, identical everywhere in
the matrix; Synthesis Technology's wavefolder and phase shift are per-mode, not
per-cell. Making cutoff, FM index, drive and a CV out part of the terrain — so
that moving through the space changes the *patch*, not just the waveform — is
genuinely new here, and it is what turns the space from a table into a place.

**6. Documented, *measured* anti-aliasing.** The field's state: Odessa culls
partials above 21 kHz (rigorous, but additive-only); Plaits uses integrated
wavetables (clever, one product); Noise Engineering locks the sample rate to a
multiple of f0 so aliases land harmonically (clever, one line); Waldorf's nw1
offers three *named* quality tiers stored per table (the best UX in the field);
Shapeshifter oversamples ~254× on an FPGA; Braids does 2× and hopes; E370 states
outright that Sample Player applies none; the E350's "anti-aliasing algorithm"
is interpolation density mislabelled; SWN, Four Seas, Piston Honda mk3, MCO
mkII, Vortex, Waveplane and Erica publish nothing at all. **Nobody publishes a
number.** A module that ships a measured bar — −80 dBFS worst non-harmonic bin
over a five-octave sweep, checked in CI (spec §7) — is ahead of every product
in this document, and it costs us nothing to say so because the test already
exists.

**7. A real-time instrument display of position in the space.** SWN is the best
attempt: red/green/blue hue on a light ring encoding Depth/Latitude/Longitude
for six voices, no screen. Piston Honda gives you three sliders and a numeric
readout. Erica's firmware updater is a Chrome web app but the promised
wavetable-management web interface never shipped, and Intellijel's editor is
community-built. **Nobody ships a browser page that shows you where you are in
the space while you play.** That is our §8, and above three dimensions it is not
a luxury — it is the only thing that makes the instrument legible. Steal SWN's
colour encoding for the LED rings as the panel-side fallback, so the module is
still playable with the laptop closed.

**8. Content that is authored, not sampled.** Every user-content path in this
review takes recorded audio and slices it: SWN records 2.5 s and cuts 27 frames,
nw1 records and analyses, WaveEdit resamples a WAV to 256 points. The results
are arbitrary orderings — which is exactly the criticism levelled at
Shapeshifter's banks. Kyklophoria generates its space from parameterised
families with one legible meaning per axis (spec §5). Nothing else in the format
does that, and it is what makes an N-D space navigable rather than a lucky dip.

## What to steal

Concrete, ranked by value-to-effort. Each maps to a milestone.

**Steal now (M2–M3):**

1. **A resizable window over the space, with CV auto-rescaling** — Erica's
   matrix crop, generalised to N-D. Select a sub-box of the lattice; ±5 V then
   spans exactly that box. Composes with topology (crop under Clamp, smaller
   torus under Wrap) and makes a 4^4 space playable with 6 pots.
2. **Per-axis interpolation quantisation, continuously variable, modulatable** —
   the union of Piston Honda mk2's morph-resolution CV, mk3's per-axis morph
   disable, and Plaits' `quantization` ramp. Implementation: clamp the
   fractional part of each axis coordinate toward 0/1 by a per-axis amount
   before the multilinear blend. One float per axis, a handful of instructions,
   and it delivers PPG-style hard stepping, wave-sequencing under an LFO, and
   "let me hear exactly this node" — three features for one parameter.
3. **Hysteresis on every discrete axis and soft pickup on every recall.** Braids
   uses ±64 ADC counts of hysteresis on its table-select; IME locks sliders
   until moved; Erica requires pots to be rotated to the stored position; and
   Intellijel waits 4 ms after a gate before latching a CV-selected preset.
   Four vendors, four independent arrivals at the same conclusion. Spec §6 M6
   already lists "param lock" — promote it.
4. **Normalise after regenerating the spectrum.** Cursus Iteritas normalises
   every regenerated table; Odessa applies perceptual loudness compensation and
   documents its side-effect. We rebuild a spectrum every block; without this,
   every position knob is also a volume knob. Do it, and document the trade
   (bright cells will duck) rather than hiding it.
5. **A Hold / freeze button.** Noise Engineering's Hold freezes incoming CV;
   Rossum's Freeze holds the current spectrum and lets you play it chromatically
   over 1V/oct. Both cost one GPIO. We have three buttons; a long-press Hold on
   the position CVs is nearly free, pairs perfectly with the web display, and
   turns any point in the space into a playable fixed timbre.
6. **A spectral rate limiter, separate from position smoothing** — Rossum's
   *Blur*. We already smooth position and slew payload lanes; a CV-controlled
   cap on how fast the *interpolated spectrum* may change is a different control
   and a very musical one, especially with a fast orbit.
7. **Named resampling-quality tiers stored with the space** — Waldorf's
   Nave / Microwave / Aliased. In the spectral domain this is just a choice of K
   truncation and rolloff taper, and it converts our anti-aliasing work into a
   playable timbre control instead of an invisible engineering virtue.
8. **Nonlinear combine between the two stereo positions** — ALM's Mix Modes
   (Morph / Splice / Multiply / Min / Max). Our stereo pair already reads the
   space at θ±δ; giving the mono sum a choice of combine operators is a small
   change and supplies the glitch-morph axis in a form nobody else has.
9. **SWN's colour encoding on the LED rings** — hue per axis, so panel-side
   position is readable at a glance with the laptop closed. Six pots with LED
   rings is the hardware we have (spec §0).

**Steal at M4 (content):**

10. **Read the two existing bank formats.** (a) The **WaveEdit bank**: 64 waves
    × 256 samples, mono 16-bit PCM little-endian WAV written at 44100 Hz
    (constants confirmed in `src/bank.cpp`,
    https://github.com/AndrewBelt/WaveEdit) — read by Piston Honda mk3, E352,
    E370, ALM MCO mkII, Qu-Bit Chord and Ferry Island Four Seas, and forked as
    SphereEdit for the 4ms SWN. It is *the* de-facto Eurorack interchange
    format and there is a large free corpus (e.g.
    https://smpldsnds.github.io/wavedit-online/). (b) The **Serum/Vital**
    convention (2048-sample frames concatenated) that Ziqal reads and that all
    modern DAW content uses. Both import to one axis of our lattice: resample
    to 1024, FFT, keep K=64 magnitudes.
11. **Prefer a folder of one-cycle WAVs as the authoring unit** (Expert
    Sleepers' convention; Four Seas' `/[1-12]/[1-8].wav` tree is the same idea
    one level up) over a monolithic blob: it diffs, it version-controls, and you
    can replace one node. For N-D, a directory tree of depth N is the obvious
    generalisation, with alphabetical ordering and an optional manifest to
    override it. Explicitly ignore the WAV sample rate; declare frame length out
    of band.
12. **A plain-text sidecar on the SD card** for structural options (Qu-Bit
    Chord's `chordconfig.txt`). Declaring N, side and per-axis topology in text
    next to the `.kyk` costs nothing and saves a menu page.
13. **Dimension-aware content operations** — SWN applies an effect to one
    waveform, to a whole *dimension* of three waveforms, or to all 27.
    `tools/kykspace` should take "apply this spectral tilt along axis 2" as a
    first-class command, not just "edit node 47".
14. **Update firmware from the SD card with no host** (Ziqal, E370). The browser
    is the instrument display, not a maintenance dependency. And consider ALM's
    trick for bulk content: **USB mass storage, drag and drop** — no card, no
    editor, no driver.
15. **Document the binary format publicly.** Intellijel's Appendix D is the only
    reason a Shapeshifter wavetable editor exists ten years on; Mutable's open
    source is why every Plaits and Braids number in this file is exact; NE's
    closed Alia format is why theirs are not. docs/space-format.md should be
    written to that standard.

**Steal later / consider:**

16. **A recorded, playable trajectory** through the space with its own envelope —
    vector synthesis' contribution (Prophet VS → Behringer VICTOR; RYK Vector
    Wave stores, animates and triggers joystick vectors), and the thing our
    parametric orbit LFOs approximate. Plaits' wave-terrain mode shows the
    parametric version is playable with two knobs (radius, offset); a recordable
    path is the other half.
17. **Generalise the stereo spread into a dispersion field.** SWN's Dispersion +
    Dispersion Pattern is a CV-controlled magnitude along a CV-selected pattern
    of per-voice direction vectors. Our θ±δ stereo is the two-voice special
    case; making δ "a spread along a selectable direction pattern" is a small
    change to code we are writing at M1–M2 and is what lets the pair become an
    ensemble later.
18. **A morph over whole control states,** not just position — IME's Preset
    Morph CV interpolates the entire panel between eight presets, and ALM's
    preset recall is itself CV-assignable. In our terms: interpolate rotation
    angles, window and payload routing between saved presets under one CV.
19. **A shift-key macro bus** (Instruo's Index): one global depth control, a
    held button revealing per-destination enables on the LEDs that already
    exist, and destinations that revert to a sane direct normal when disabled
    rather than to silence. With 3 buttons, 6 pots and no encoder, this is the
    pattern that fits the rotation page.
20. **Make the scan rate clockable** (Rossum's Slice; Waldorf's RPM travel) so
    orbits and scans can be tempo-locked, not just free-running.
21. **Plaits' integrated-wavetable trick** is *not* for us — we bandlimit in the
    spectral domain, which is strictly better and already implemented. But its
    *guard-samples-in-the-stride* idea is: pad each stored row so the corner
    fetch never needs a wrap branch.
22. **Nothing here does spectral editing well.** Erica exposes 32 harmonics on
    two encoders and reviewers call it a highlight; Waldorf's Spectrum/
    Brilliance/Keytrack is a formant axis decoupled from pitch. Our hyperpoint
    is 64 harmonic magnitudes and we have a browser. Per-node spectral editing
    in the Authoring drawer, plus a formant-shift payload lane that scales
    harmonic index rather than frequency, is a differentiator hiding in plain
    sight — and it is nearly free for us and expensive for every frame-based
    engine in this document.

## Things to get right that this field gets wrong

- **Do not call interpolation density "anti-aliasing"** (E350's product page
  does). Keep the two words separate in our docs and in the UI.
- **Do not let the position axis be the lowest-resolution input.** Shapeshifter
  digitises SHAPE at 12 bits / 25 kHz and MOD A at 24 bits / 98 kHz, and the
  artifact reports track that choice exactly. Our CV jacks are 16-bit at audio
  rate (spec §0) — spend the resolution on position, and smooth it.
- **Do not resync phase behind the player's back.** XAOC refuse to auto-resync
  partials because it clicks. Our phases come from a seed; changing the seed,
  K, or the frame size mid-note has the same hazard.
- **Curate the content, don't just count it.** Shapeshifter has 1024 waves and
  is criticised because the banks are arbitrary orderings. E350 documents that
  a row progresses dark → bright. Our generative families give each axis one
  legible parameter — that is the right instinct; hold to it when WAV import
  lands and it becomes tempting to fill a lattice with whatever is on the card.
- **Give every axis a CV input.** SWN's Longitude has a knob and no jack;
  Waldorf's nw1 has no CV over table selection at all; Erica navigates its
  matrix with two CVs, one of them borrowed from the FX section. Asymmetric
  axes are the most common ergonomic failure in this field. We have six
  field-programmable CV jacks (spec §0) and up to six axes — the io-map must
  make every axis reachable, and every axis unpatched must fall back to a
  stored constant (E370's "static 0-99" rule).
- **Do not claim a dimension you do not have.** Ferry Island market Four Seas as
  "4D" where the fourth is an output spread; the E350 panel says X/Y/Z where Z
  is a second independent reader of a 2-D bank; ALM call a dual 1-D oscillator
  "multi dimensional". Kyklophoria's N is an actual index count into one
  lattice, and the docs should be able to survive somebody checking.
- **Ship the module usable without the browser.** SWN puts six voices' 3-D
  positions on an LED ring with no screen at all. Our WebSerial surface is the
  differentiator, but a player with a closed laptop must still be able to see
  where they are.

## Corrections to the brief's premises

Recorded so they do not propagate into other docs:

- **Piston Honda is Industrial Music Electronics (formerly The Harvestman), not
  WMD.** WMD is an unrelated company.
- **The E350 is not a 3-D morpher.** X and Y index the 8×8 grid for the XY
  output; Morph Z is a separate 1-D scan of the same 64 waves feeding an
  independent Z output.
- **Waldorf nw1 tables are not "64 waves".** Tables hold 8–4096 waves; 64 is one
  of the legal *wave lengths in samples*.
- **"Zetamix" does not exist in Eurorack** — it is a French 3D-printing filament
  brand (https://zetamix.fr/en/).
- **Befaco has no wavetable oscillator,** and "POKO" and "Bard" could not be
  verified as Befaco module names — **unverified**.
- **Loquelic Iteritas is not wavetable** (VOSIM / Moorer summation / phase
  modulation); **"Ruina" is not an oscillator** (Noise Engineering's distortion
  family).
- **Waldorf kb37 is a keyboard and case,** not an oscillator, and there is no
  Waldorf Eurorack "Dual Oscillator" — **not verified to exist**.
- **Rossum Trident is analogue,** with no table of any kind.
- Modules the brief did not mention that turned out to matter most: **4ms SWN**
  (torus topology, dispersion field), **Ferry Island Four Seas** (open source on
  our exact MCU), **ALM MCO mkII** (nonlinear combine modes), **Dove Audio
  Waveplane** (six CV-addressable indices), **Waldorf nw1** (named aliasing
  tiers, formant axis).
