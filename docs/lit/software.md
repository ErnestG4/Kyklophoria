# Lit review — software wavetable synthesizers

*Scope: what a software wavetable synth's table actually **is**, and how the player
moves through it. Compiled 2026-09-08 for Kyklophoria (docs/spec.md). Primary
sources cited inline; anything not confirmed against a vendor manual, vendor site
or source code is marked **unverified**.*

---

## Lead — the state of the field

Almost every mainstream software wavetable synth ships the same data structure: an
**ordered 1-D list of single-cycle frames**, indexed by one scalar "wavetable
position" parameter. The list is usually 256 frames of 2048 samples (Serum,
Serum 2, Phase Plant, Pigments, Hive), 257 × 2048 (Vital), or 256 × 1024
(Ableton). Where a product appears to have two axes, it is almost always *two
independent 1-D tables summed or crossfaded* — Ableton's two oscillators,
Pigments' two engines, Serum 2's three — not a 2-D field with bilinear
interpolation.

There are exactly two exceptions, and it is worth being precise about them:

- **u-he Hive 2** folds one 256-frame list into a grid: "Hive's wavetables become
  two-dimensional (some would even say three-dimensional) if the `Tables`
  parameter, which specifies the number of frames in the y-axis, is set to 2 or
  more." Two position knobs, both axis-aligned; the second axis is crossfade-only.
- **PPG WaveGenerator / WaveMapper** (Wolfgang Palm) lay waves out on a real 2-D
  grid — "256 waves assembled within a wave grid", "16 rows with 16 places each" —
  and the player traverses it by **drawing a path**, which may be diagonal. That
  is the only shipped oblique traversal of a wavetable space found in this survey.

Four things have moved in the last decade and are worth taking seriously:

1. **The frame list is increasingly a spectrum list.** Vital stores per-frame
   harmonic magnitudes, normalised complex frequencies and phases, and
   re-synthesises every playback frame by IFFT with the harmonic series truncated
   at Nyquist — there are no band-limited mip-map copies at all
   ([`spectral_morph.h`](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/spectral_morph.h),
   [`wavetable.h`](https://github.com/mtytel/vital/blob/main/src/synthesis/lookups/wavetable.h)).
   u-he's Zebralette 3 goes further and stores a *spline over log-frequency* that
   is sampled to 1024 harmonic magnitudes. Both are the architecture Kyklophoria
   uses; Vital's is the closest match.
2. **A second, orthogonal timbre axis has appeared — but it is a *process* axis,
   not a *position* axis.** Vital's Spectral Morph amount, Serum 2's dual Warp and
   Pigments' three parallel modifier stages all give the player continuous knobs
   genuinely independent of wavetable position. They move through a space of
   transformations, not a space of stored timbres, and the transformation is
   applied uniformly to whatever frame you landed on.
3. **Phase alignment became a named feature.** Ableton minimises inter-frame phase
   at import, Bitwig exposes `Aligned`/`Diffuse`/`Original`, Hive offers a
   `zero phase` interpolator, Serum offers spectral-zero-phase morphing, and Phase
   Plant names three separate alignment strengths. Everyone independently
   discovered that morphing is a phase problem before it is a magnitude problem.
4. **Authoring got a corpus and a language.** Resynthesis import, text-to-wavetable
   (Vital's server-side TTWT, Nave's local `Talk`), formula parsers (Serum), full
   scripting (Hive's UHM), spectral drawing and image import mean the table is now
   generated, not curated.

What nobody ships is a table with **more than two** stored position axes. Oblique
and even rotated traversal *does* ship — PPG's drawn path, the Wavestation Vector
Envelope's arbitrary X-Y breakpoints, and wave-terrain plugins with an explicit
trajectory rotation — but always in two dimensions, which admits exactly one
rotation plane, and in the latter two cases over a mixer or a computed surface
rather than a stored corpus. No product applies a rotation to **N > 2** control
coordinates before indexing a stored lattice. See the cross-cutting answer at the
end, which names each near-neighbour precisely.

---

## Xfer Serum 1

Primary source: *Serum Synthesizer Manual* (Xfer Records)
[PDF](https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf).

**1. Table shape and limits.** Up to **256 sub-tables (frames)**, **2048
samples per frame**, 32-bit float — "the maximum file size would be
2048(samples) x 256(sub-tables) x 32(bits) ... which is exactly 2 megabytes"
(p. 11–12). **Two** wavetable oscillators (A and B) plus dedicated Noise and Sub
oscillators. WT Pos selects one frame; frames are discrete. Continuity comes from
filling the unused slots with pre-computed interpolated frames at load time, not
from continuous interpolation at playback ("These sub-tables are computed at
load-time: Serum embeds the information for which interpolation type is in use,
rather than the interpolated waveforms", p. 12).

**2. Interpolation and morphing.** Done offline in the WT Editor via the Morph
menu, which fills the table out to 256 frames. Exact menu items (p. 49):

| Morph item | What it does |
|---|---|
| Morph - Crossfade | "interpolated tables are created by crossfading the neighbor tables together. This is the recommended default, and what traditional wavetable synths do." |
| Morph - Spectral | "The spectral and phase content of the neighboring tables is used to re-synthesize the interpolated tables. This is what additive synthesizers do." |
| Morph – Spectral (zero fundamental phase) | as above, fundamental's phase zeroed so "the lowest frequency will not shift/rotate between tables" |
| Morph – Spectral (zero all phases) | all phase discarded; "will also make for the smoothest transitions between tables since no frequencies need to shift phase" (destructive) |
| Remove Morph Tables | undo |

The zero-all-phases note is the single most useful sentence in the manual for a
design like ours: **discarding phase is what makes morphing smooth**, which is
why Kyklophoria stores magnitudes only and derives phases from a seed (spec §3.1).

**3. Warp modes.** Verbatim from p. 14–16: `Off`, `Self Sync`, `Windowed Sync`,
`Bend +`, `Bend -`, `Bend +/-`, `PWM`, `Asym +`, `Asym -`, `Asym +/-`, `Flip`,
`Mirror`, `Remap 1`, `Remap 2`, `Remap 3`, `Remap 4`, `Quantize`,
`FM (from other OSC)`, `AM (from other OSC)`, `RM (from other OSC)`,
`FM NoiseOsc`, `FM Sub Osc`. One warp slot per oscillator, with one depth knob.

**4. Authoring.** Drag-and-drop audio import with a named analysis mode chosen by
*where you drop the file* (p. 54–57): `import: normal (dynamic pitch zero-snap)`,
`import: normal (dynamic pitch follow)`, `Import: constant frame size (pitch avg)`,
manual constant frame size (type a sample count or MIDI note name into the formula
box), and spectral import at `FFT 256 / FFT 512 / FFT 1024 / FFT 2048`. Plus:
waveform drawing with grid snapping, **FFT bin drawing with a separate phase row**
(right-click menu: Clear All, Clear HF, Clear LF, Generate Saw, Randomize Low X
bins, Randomize All, Create random series gaps, Progressive Fade, Shift Octave
Up/Down, Draw Odd/Even Harmonics Only), and a **formula parser** that can generate
one frame or the entire set. Table files are plain WAV, mono 32-bit 44100 Hz,
2048 samples per sub-table, with header info carrying the interpolation mode.

**5. Anti-aliasing.** Not documented in detail. The manual claims "solid
high-frequency representation all the way to the limits of the human ear, without
the audible aliasing artifacts / nyquist reflection commonly found on most
wavetable synthesizers ... this takes more CPU on both load-time and run-time" and
notes "advanced SSE optimizations" (p. 11). Load-time cost implies per-frame
band-limited copies, but the mechanism is **unverified**. The Oversampling setting
is explicitly *only* for warp modes: "This setting applies to situations using a
Warp mode (OSC Sync, FM, etc) for an oscillator. Otherwise the standard oscillator
quality is always maximum!" (p. 40).

**6. What to steal.** (a) The Morph menu's honesty about phase — offering
crossfade, spectral-with-phase, and spectral-zero-phase as *separate user
choices*. (b) **Unison WT Pos spread**: "a single note with unison enabled can be
playing multiple sub-tables in your wavetable simultaneously" (p. 41) — the direct
1-D ancestor of Kyklophoria's stereo `R(θ±δ)·c` pair. (c) Unison Warp spread, same
idea for the modifier axis. (d) Sorting frames by a spectral property so that
"when you move the WT Pos, it feels like you are traveling in a straight line,
instead of some zig-zag fashion, spectrally speaking" (p. 68) — a lattice needs
this per axis or the axes are not legible.

**7. Where the 1-D table limits it.** One WT Pos per oscillator. To get a second
timbre dimension you must either use OSC B as a separate voice and crossfade
manually, or spend the warp slot. The warp axis is a *transformation* applied to
whatever frame you landed on; it cannot reach a timbre that is not on the line.
Unison WT Pos spread is the only "off-axis" motion available and it is a
symmetric ± offset along the same single line.

---

## Xfer Serum 2

Primary sources: *Serum 2 User Guide*, 354 pp.
([PDF](https://www.xferrecords.com/manual/serum-2/docs)); *What's New in Serum 2*,
manual v1.0.0, 2025-03-17
([PDF](https://images.equipboard.com/uploads/item/manual/127411/xfer-records-serum-2-advanced-wavetable-synthesizer-manual.pdf));
[product page](https://xferrecords.com/products/serum-2) (Serum 2.1.5 as of
2026-09).

**What Serum 2 actually added** (verified against the *What's New* PDF, not
recalled): **three** primary oscillators instead of two; each oscillator can be
**Wavetable, Multisample, Sample, Granular or Spectral**; **two warp slots per
oscillator** ("Dual Warp") with a `Swap Warps` command; a large expansion of the
warp mode list (true linear/thru-zero FM, PD including PD from the filters and
`PD (Self)`, LPF/HPF, and a bank of distortion shapers); a **Smooth Interpolation**
option on WT POS; up to 10 LFOs, 4 envelopes, 8 macros; clip sequencer; arpeggiator;
new mixer, matrix and FX. It ships "over 626 presets, 288 wavetables".

**1. Table shape and limits.** Unchanged from Serum 1: "when Serum loads a
wavetable, it uses **2048 samples** for a frame (subtable) ... maximum file size is
2048 (samples) x **256** (frames) x 32 (bits), which is exactly 2 megabytes"
(User Guide p. 12). Three oscillators can each hold a wavetable. Position is
frame-stepped by default; **`Smooth Interpretation` [sic]** — "Right-click the WT
POS knob and choose Smooth Interpretation in the menu to allow smoother waveform
transitions without being destructive. This helps keep the wavetable intact while
morphing smoothly between positions" (p. 45) — makes position effectively
continuous without needing to bake 256 morph frames. Xfer's product page calls it
"near-infinite frame positions in the wavetable".

**2. Interpolation and morphing.** Same MORPH menu as Serum 1 (Crossfade,
Spectral, Spectral zero-fundamental-phase, Spectral zero-all-phases), plus the
non-destructive Smooth Interpolation at playback. New **SORT** menu reorders
frames by a spectral property: highest peak frequency bin, spectral centroid
("where the average spectral content exists"), highest overall peak, spectral
spread ("how many frequency bins contain energy"), highest frequency bin
containing spectra, and fundamental energy; plus reverse (p. 283).

**3. Warp modes** (User Guide p. 50–56, verbatim, grouped by the manual's own
categories — two of these can be active at once):

- **Off**
- **Sync** — with a `WARP Var` fader from hard sync to soft sync
- **Alt Warp**: `Bend +`, `Bend -`, `Bend +/-`, `PWM`, `Asym +`, `Asym -`,
  `Asym +/-`, `Flip`, `Mirror`, `Remap 1`, `Remap 2`, `Remap 3`, `Remap 4`,
  `Quantize`, `Odd/Even`
- **Filter**: `LPF`, `HPF`
- **Distortion**: `Tube`, `Soft Clip`, `Hard Clip`, `Diode 1`, `Diode 2`,
  `Linear Fold`, `Sine Fold`, `Zero-Square`, `Asym`, `Rectify`, `Sine Shaper`,
  `Stomp Box`, `Tape Sat.`, `Soft Sat.`
- **FM**: from another oscillator (`FM (B)`, `FM (C)`), `FM (Noise)`, `FM (Sub)`,
  `FM (Filter 1)`, `FM (Filter 2)`; plus the mode qualifiers `Thru-Zero`, `Exp`,
  `Linear`
- **PD**: `PD (B)`, `PD (C)`, `PD (Noise)`, `PD (Sub)`, `PD (Filter 1)`,
  `PD (Filter 2)`, **`PD (Self)`**
- **AM**: `AM (B)`, `AM (C)`, `AM (Noise)`, `AM (Sub)`, `AM (Filter 1)`, `AM (Filter 2)`
- **RM**: `RM (B)`, `RM (C)`, `RM (Noise)`, `RM (Sub)`, `RM (Filter 1)`, `RM (Filter 2)`
- **`Swap Warps`** — swaps the WARP 1 and WARP 2 assignments

New in 2 relative to 1: `Odd/Even`, the whole Filter and Distortion categories, the
FM qualifiers, all PD modes, filters as modulation sources, and the second slot.

**4. Authoring.** All of Serum 1's, plus a new import mode. Named import options
(p. 292–294): `DYNAMIC PITCH - ZERO SNAP`, `DYNAMIC PITCH - FOLLOW`,
**`FREQUENCY ESTIMATION`** (new: "Serum analyzes the incoming waveform to determine
its dominant frequencies and harmonic content ... By matching the detected
fundamental frequency to a musical pitch ... Serum then uses the estimated
frequencies to guide the conversion of the audio signal into a series of wavetable
frames"), `Constant framesize (PITCH AVERAGE)`, manual constant frame size, and
`FFT 256/512/1024/2048`. A Sample-mode oscillator can be converted in place via
`Switch to Wavetable → Frequency Estimation`
([support article](https://support.xferrecords.com/article/59-converting-samples-to-wavetables)).
Drawing, FFT-bin drawing and the formula parser are retained. Export is WAV,
mono, 16-bit or 32-bit, 44100 Hz, 2048 samples per subtable — note that
"additional header information for interpolation mode and interpolation tables are
**not** saved as part of the WAV data" (p. 297).

**5. Anti-aliasing.** Same as Serum 1; the User Guide's appendix on optimisation
only discusses unison counts. Mechanism **unverified**.

**6. What to steal.** (a) **Non-destructive smooth interpolation as a right-click
option on the position knob** — this is exactly the "should position be stepped or
continuous?" question, answered as a per-preset user choice rather than a global
policy. (b) The SORT-by-spectral-property menu: a lattice with N axes needs each
axis monotone in *something*, and Serum 2 gives six concrete orderings (centroid,
peak bin, spread, fundamental energy) that a `kykspace` generator could reuse
directly. (c) Dual Warp: the player expects to stack two modifiers, not one.
(d) Categorising warp modes (Alt Warp / Filter / Distortion / FM / PD / AM / RM)
so a long list stays navigable on a small display.

**7. Where the 1-D table limits it.** Serum 2's answer to "one axis isn't enough"
is *more oscillators and more warps*, not more table dimensions. Three
oscillators give three independent 1-D positions that you sum — no interpolation
between them, no single point in a shared space. The Spectral oscillator mode is
Xfer's escape hatch, and it is a sample-analysis engine (time/pitch/timbre shift of
an analysed sound), not a navigable timbre lattice.

---

## Vital / Vitalium

Primary source: the GPL source at [github.com/mtytel/vital](https://github.com/mtytel/vital)
(authoritative per the brief), plus [vital.audio](https://vital.audio/).
"Vitalium" is the community name for a build from that source; the
[README](https://github.com/mtytel/vital) forbids using the Vital name for such
distributions, forbids shipping the official presets, and forbids connecting to
`vital.audio` — so a GPL build has **no text-to-wavetable**, since TTWT is a web
service.

**1. Table shape and limits.** `kNumOscillatorWaveFrames = 257`
([`synth_constants.h`](https://github.com/mtytel/vital/blob/main/src/common/synth_constants.h)),
`kWaveformSize = 1 << 11 = 2048` samples per frame, `kNumHarmonics = 1025`
([`wavetable.h`](https://github.com/mtytel/vital/blob/main/src/synthesis/lookups/wavetable.h)).
`kNumOscillators = 3`, plus a sampler. Each frame is stored **three ways at once**:
time-domain `wave_data`, plus `frequency_amplitudes`, `normalized_frequencies` and
`phases` — i.e. the table is a spectrum list, not just a sample list.

Position: the `wave_frame` parameter runs 0 … 256 as a float, but at playback
`wave_index = utils::toInt(utils::clamp(wave_frame, 0, kNumOscillatorWaveFrames-1))`
([`synth_oscillator.cpp`](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp)).
**Vital's wavetable position is therefore quantised to 257 steps.** Smoothness
comes from `kWavetableFadeTime = 0.007f` — a 7 ms equal-length crossfade between
the previously rendered buffer and the newly rendered one, re-triggered whenever
the render changes. (Compare Kyklophoria's per-block crossfade of the previous and
new frame, spec §4 — same trick, longer window.)

**2. Interpolation and morphing.** There is no time-domain frame crossfade at
playback. **Every** playback path goes through
`setFourierWaveBuffers<...>`, which selects one of twelve morph kernels — the
"no morph" case is `passthroughMorph`, which reads that frame's stored
`frequency_amplitudes * normalized_frequencies`, zeroes everything above
`last_harmonic`, and IFFTs. The morph functions themselves are the interesting
part: the second axis. In the editor, interpolation *between keyframes* is per
component and can be `kNone`, `kLinear` or `kCubic`
([`wavetable_component.h`](https://github.com/mtytel/vital/blob/main/src/common/wavetable/wavetable_component.h)).

**3. Warp modes.** Vital splits them into two independent menus, both with their
own depth knob. Verbatim from
[`synth_strings.h`](https://github.com/mtytel/vital/blob/main/src/interface/look_and_feel/synth_strings.h):

*Spectral Morph* (`kSpectralMorphNames`, acts on harmonics before the IFFT):
`None`, `Vocode`, `Formant Scale`, `Harmonic Stretch`, `Inharmonic Stretch`,
`Smear`, `Random Amplitudes`, `Low Pass`, `High Pass`, `Phase Disperse`,
`Shepard Tone`, `Spectral Time Skew`.
(Internal enum names differ — `kFormScale`, `kHarmonicScale`, `kInharmonicScale`,
`kSkew` — the strings above are what the player sees.)

*Phase Distortion / warp* (`kPhaseDistortionNames`, acts on the phase read):
`None`, `Sync`, `Formant`, `Quantize`, `Bend`, `Squeeze`, `Pulse`,
`FM <- Osc`, `FM <- Osc`, `FM <- Sample`, `RM <- Osc`, `RM <- Osc`, `RM <- Sample`.

There is also `Spectral Unison`, which spreads the *spectral morph amount* (and
the frame, via `kUnisonFrameSpread` / `kUnisonSpectralMorphSpread`) across unison
voices — i.e. unison voices sit at different points in a 2-D (frame × morph)
control space.

**4. Authoring.** The wavetable editor is a **stack of groups, each a chain of one
source plus modifiers, all rendered per frame index 0…256 and summed**
([`wavetable_group.cpp`](https://github.com/mtytel/vital/blob/main/src/common/wavetable/wavetable_group.cpp)).
Component names, verbatim from
[`wavetable_component_factory.cpp`](https://github.com/mtytel/vital/blob/main/src/common/wavetable/wavetable_component_factory.cpp):
sources `Wave Source`, `Line Source`, `Audio File Source` (+ deprecated
`Shepard Tone Source`); modifiers `Phase Shift`, `Wave Window`, `Frequency Filter`,
`Slew Limiter`, `Wave Folder`, `Wave Warp`. Each component holds **keyframes at
integer positions** and interpolates between them (none/linear/cubic) — so the
table is authored as a small number of keyframes and *rendered* to 257 frames,
much like a Kyklophoria lattice is a small number of stored corners rendered by
interpolation.

Audio import styles (`WavetableCreator::AudioFileLoadStyle`): `kWavetableSplice`,
`kVocoded`, **`kTtwt`**, `kPitched`. `kTtwt` is text-to-wavetable, implemented as a
vocoded import with pitch detection capped at a 20 ms period
(`kMaxTTWTPeriod = .02f`,
[`wavetable_creator.cpp`](https://github.com/mtytel/vital/blob/main/src/common/wavetable/wavetable_creator.cpp)).
Note: TTWT is a **server-side service** on vital.audio, rate-limited to 5
requests/day on free accounts — the client vocode-imports the returned audio.
The Audio File Source also exposes fade styles `kWaveBlend`, `kNoInterpolate`,
`kTimeInterpolate`, `kFreqInterpolate` and phase styles `kNone`, `kClear`,
`kVocode` ([`file_source.h`](https://github.com/mtytel/vital/blob/main/src/common/wavetable/file_source.h)).

**5. Anti-aliasing.** **No mip-maps, no per-frame band-limited copies.** In
`computeSpectralWaveBufferPair` the code computes a float frequency bin from the
per-voice phase increment and derives
`last_harmonic = kWaveformSize * 2^-(kFrequencyBins + 1 - bin)`, clamped to
`kWaveformSize/2`; harmonics above that index are zeroed before the inverse
transform. Substituting `phase_inc = f0/fs` this reduces to
`last_harmonic ≈ 0.5·fs/f0` — exactly the harmonics below Nyquist. Anti-aliasing
is **harmonic truncation per voice per re-render**, and the re-render is
crossfaded over 7 ms. `Wavetable::getFrequencyBin` (the integer, log2-binned
version, with `kFrequencyBins = 11`) exists but is not called from the oscillator
— vestigial. The site's marketing claim, "sharp cutoff at Nyquist for almost no
aliasing", is a fair description of that code.

**6. What to steal.** Vital is the closest existing thing to Kyklophoria's audio
architecture and validates most of the M0 decisions:
- **Store spectra, IFFT per render, band-limit by truncation.** Identical to
  spec §3.1/§4. Vital does it at 2048 points and 1025 harmonics per voice on a
  desktop CPU; our 1024/64 on an H750 is the same shape, cheaper.
- **Crossfade successive renders** rather than interpolating frames (7 ms there,
  one 24-sample block here).
- **Keyframes + rendered frames.** Author a handful of control points, render the
  dense table. This is the `.kyk` lattice model, and it means the *editor's* data
  model and the *engine's* data model can differ deliberately.
- **A second orthogonal timbre knob with named, legible modes.** `Formant Scale`,
  `Harmonic Stretch`, `Inharmonic Stretch`, `Smear`, `Phase Disperse` are all
  cheap operations on a magnitude vector and map straight onto Kyklophoria's
  payload lanes or onto generative axes in `tools/kykspace`.
- **`Spectral Unison`**: spreading two parameters across voices, not one. Our
  stereo pair currently spreads only rotation angle (spec §9.1); spreading the
  interpolated position along a second control would be near-free.

**7. Where the 1-D table limits it.** `wave_frame` is one integer index into one
list, and the spectral-morph axis is a *function applied to the frame*, not a
second stored coordinate — you cannot put a different timbre at (frame 40,
morph 0.7) than the morph function computes. Frame position is quantised to 257
steps; a slow sweep is 257 discrete re-renders, each smoothed by a 7 ms
crossfade. Groups sum, they do not interpolate, so a two-group table is a mix of
two 1-D positions rather than a point in a 2-D field.

---

## NI Massive (1)

Primary source: *MASSIVE Manual*, English, v1.5.5
([PDF](https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/MASSIVE_Manual_English_1_5_5.pdf)).

**1. Table shape.** Three wavetable oscillators. The manual describes the table as
literally 2-D-*looking* and then collapses it: "The horizontal axis represents
time, and the 'recorded' waveforms run from left to right ... Along the vertical
axis, on the other hand, there are different waveforms one above another"
(p. 40). Frame count is variable per table: "The number of individual waveforms
represented in each table can range from only 2 to as many as 128 or more"
(p. 41). Samples per frame not documented — **unverified**. Position is the
"Wt-position" knob; whether it is stepped or continuous is not stated, but the
manual's language ("a series of intermediate waveforms that gradually morph from
the bottom waveform shape to the top", p. 40) implies interpolated frames are
baked into the table.

**2. Interpolation.** Not documented beyond "between them are a series of
intermediate waveforms that gradually morph". Mechanism **unverified**.

**3. Modes.** The Intensity knob's meaning is set by an oscillator mode menu. The
manual names: **Spectrum** (default; "the Intensity control reduces the
higher-frequency harmonics ... the square wave gradually becomes a sine wave"),
**Formant** ("Intensity controls the additional transposition of waveform
formants ... simulating the sound of morphing vowels"), and **three Bend modes**
— **BendA** (upper half of the readout-curve range only), **BendB** (full
bipolar range), and a third that "only interpolates between the middle image ...
and the lower image" (p. 42–43). The manual does not print the third bend mode's
label, and does not claim the list is exhaustive; any longer mode list is
**unverified**.

**4. Authoring.** Massive 1 ships a fixed factory wavetable library and has **no
wavetable editor and no import** — the player picks a table from a menu.

**5. Anti-aliasing.** Not documented. **Unverified.**

**6. What to steal.** The **Wt-position voice-spread** behaviour and, crucially,
its documented failure mode: "If you adjust the Wavetable Position ... those
voices already using a very low wavetable position due to the spreading will not
be able to follow this move" (p. 29–30, on "Wavetable Position Spreading") — i.e.
spread saturates against the end of
the table. Kyklophoria's `Clamp` topology has exactly this artefact and `Wrap`
is the fix; this is the canonical prior-art description of why per-axis topology
matters. Also: three modulation slots *under* each Wt-position knob with a visible
range ring — a good model for the WebSerial surface's position display.

**7. Where the 1-D table limits it.** One knob per oscillator; three oscillators
give three unrelated positions that are mixed, not interpolated. The Intensity
axis is a per-mode transformation, not a coordinate.

---

## NI Massive X

Primary source: [Massive X manual — Wavetable
oscillators](https://docs.native-instruments.com/ni-tech-manuals/massive-x-manual/en/wavetable-oscillators).

**1. Table shape.** **Two** wavetable oscillators. Frame count, samples per frame
and whether Wavetable Position is stepped or continuous are **not documented** —
unverified. Wavetables are organised in 11 categories: Basics, Operators,
Harmonics, Additive + FM, Monster, Drift, Filter, Formant, FX, Mixed, Remastered.

**2. Interpolation.** Not documented. **Unverified.**

**3. Modes.** Each oscillator runs one of **ten** modes, each with the shared
Wavetable Position control **plus two mode-specific parameters**: `Standard`
(Filter, Phase), `Bend` (Bend, Filter), `Mirror` (Bend, Ratio), `Hardsync`,
`Wrap`, `Formant Capture`, `ART`, `Gorilla`, `Random`, `Jitter`. (The per-mode
parameter pairs for modes beyond the three the manual illustrates are
**unverified**.)

**4. Authoring.** No user wavetable editor or import; factory library only.

**5. Anti-aliasing.** Not documented. **Unverified.**

**6. What to steal.** The **"one shared position axis + two mode-specific
parameters" control template** is the single best UI idea in this survey for a
6-pot panel: whatever mode you are in, one knob always means the same thing
(position), and exactly two knobs change meaning. Kyklophoria has the inverse
problem — N position axes and a shared payload — but the principle holds: keep
one control's meaning invariant across pages.

**7. Where the 1-D table limits it.** Same as Massive 1. `Wrap` is a
readout-phase mode, not a table topology; no relation to a toroidal position
space.

---

## Ableton Live — Wavetable

Primary sources: [Live 12 manual, Live Instrument
Reference](https://www.ableton.com/en/live-manual/12/live-instrument-reference/);
[Ableton Help Centre, "User
Wavetables"](https://help.ableton.com/hc/en-us/articles/360002719179-User-Wavetables).
The Live 11 and Live 12 manual text for Wavetable is identical apart from
capitalisation — the engine has not changed since 10.1.

**1. Table shape and limits.** The reference manual gives no numbers ("a wavetable
is simply an arbitrary collection of short, looping samples that are arranged
together"); the Help Centre defines them: a **wavetable** is "a 1024 sample long
single cycle waveform", a **sprite** is "a collection of wavetables in one file",
and on import "Wavetable reads the first few seconds (**up to 256 wavetables**)
from the file", downmixed to mono, with "at least 2 wavetables so that the
position slider has something to cycle through". So **≤256 frames × 1024 samples,
mono**. **Two** wavetable oscillators, each with its own sprite and `Wave
Position`; the third oscillator is a sub (sine + `Tone`), not wavetable.
**Position is continuous** — no stepped mode. Two display projections, linear
(waterfall) and polar (cycle as a loop).

**2. Interpolation and morphing.** Playback interpolation is **not documented —
unverified**. Phase is dealt with at *load* time instead: with `Raw` off the
importer trims silence, fades frame edges to zero, **"the phase differences between
neighboring wavetables are minimized to avoid weird phasing noises"**, and
normalises. `Raw` bypasses all four. Serum files are special-cased: "it's
downsampled to 1024 samples per wavetable ... because Serum generates larger files
(2048 samples per wavetable)".

**3. Effect modes.** One chooser, three modes, each exposing exactly two sliders:
`FM` (`Amt`, `Tune`), `Classic` (`PW`, `Sync`), `Modern` (`Warp`, `Fold` —
"Warp is similar to pulse width, while Fold applies wavefolding distortion").
Critically: **"the values of the two effects parameters don't change when the
effect type changes. This makes it possible to move between the effects to
experiment with how the different processes affect the timbre with the same
values."** Unison modes that touch position: `Position spread` ("The wavetable
positions for each oscillator are evenly spread out by an amount"), `Random note`,
`Shimmer` and `Noise` (both apply "a small amount of wavetable offset ... for extra
fullness").

**4. Authoring.** **Import only** — no drawing, no resynthesis, no formula entry.
Drag any WAV or AIFF onto the sprite visualisation; the containing folder then
becomes an ad-hoc bank in the chooser.

**5. Anti-aliasing.** One hedged sentence: "**As long as no modulation is applied**,
the raw output of the oscillators is perfectly band-limited and will not produce
aliasing artifacts at any pitch." Mechanism **unverified**. Separately, `Hi-Quality`
off (the default since 11.1) means "Wavetable modulation is calculated every 32
samples", saving up to 25% CPU.

**6. What to steal.** (a) **Positional, type-agnostic modifier slots** whose values
persist as the mode changes underneath — on a 6-pot panel this means two pots that
never need relabelling and instant A/B across warp algorithms. (b) **Import
conditioning as a defeatable stage**: silence trim, edge fade, inter-frame phase
minimisation, normalise, with one `Raw` switch. The phase-minimisation step is
exactly what makes arbitrary corpus audio morph without nulling, and it is a
load-time cost only — directly applicable to `tools/kykspace`'s WAV import (spec
§5, M4). (c) The **polar projection**: on a small display a single cycle drawn as a
loop reads better than a waterfall. (d) An honest, documented control-rate/quality
switch — "modulation is calculated every 32 samples" is our `render_div` (spec §4).

**7. Where the 1-D table limits it.** One position axis per oscillator, one Effect
mode at a time (you cannot have `Fold` and `Sync` together). Two oscillators is the
whole second dimension, and combining them is a crossfade, not a bilinear read.
Frame size is hard-coded at 1024, so any source whose natural period differs gets
resampled.

---

## Arturia Pigments — Wavetable engine

Primary source: [Pigments 7.0.1 User Manual, ch. 8 "The Wavetable
Engine"](https://dl.arturia.net/products/pigments/manual/pigments_Manual_7_0_1_EN.pdf),
pp. 92–107 (current version as of 2026 is Pigments 7, per
[arturia.com resources](https://www.arturia.com/products/software-instruments/pigments/resources)).
Pigments 6 and 7 added wavetable *content* (53 and 50 new tables respectively) and
new engines/filters/effects, **not** new wavetable-engine capability.

**1. Table shape and limits.** Stated explicitly (p. 92): "Up to **256
waveforms/positions** in each wavetable"; "Each position holds a waveform
containing **2,048 samples**"; "Only the first **524,288 samples** will be used
(256 x 2,048)". **Two engines** maximum, each selectable from Analog / Wavetable /
Sample / Harmonic / Modal / Utility, so at most two wavetables at once.
**Position is switchable between continuous and stepped** via the `Morph` button:
"Transitions between wavetable positions will occur smoothly when the Morph feature
is enabled. When it is disabled the transitions will be immediate. This is how the
wavetable will behave whether you are adjusting the Position knob with the cursor
**or modulating it**."

**2. Interpolation and morphing.** Algorithm **not specified — unverified**. The
manual does confirm the intermediate positions are real and addressable: in 3D view
"the blue lines represent the original wavetable positions. The green line shows the
current position, including the intermediate ('morphed') positions." Phase is a
separate named subsystem — `Phase Retrigger source` with four options (§8.5.2):
`Key` ("Each incoming MIDI note resets the wavetable phase"), `Random`, `Self`
("The wavetable phase resets at a rate defined by the main Coarse and Fine Tune
parameters"), `Mod Osc` ("resets each time the phase of the Wavetable Modulator
resets to 0"), plus a numeric initial phase and a `Phase Mod` depth knob.

**3. Warp / modifier modes.** There is no single warp menu. Three independent,
**simultaneously active** sections, each with its own amount knob *and* its own
dedicated mod-oscillator depth knob:

- **Modulation Type** (§8.4.1): `Linear`, `Exponential`, `Ring Mod`.
- **Phase Transform "Types"** (§8.6.1, seven): `Pulse Width`, `Skew`, `Round`,
  `Tri/Pulse`, `Octave Plus`, `Pseudo PW`, `Fractalize`. Manual caveat worth
  heeding: "The remap curves for each Target wave are based on the way they
  affected a sine wave, so the results will vary when the input (original) waveform
  is more complex."
- **Wavefolding** (§8.7): "Rather than folding the original wave back on top of
  itself, Pigments uses a selectable waveform and 'folds' it **downward onto the
  peaks** of the current wavetable." Three `Fold Shape` options; **their names are
  not printed in the manual — unverified.**

Also: Modulator Wave (10): `Sine`, `Triangle`, `Sawtooth`, `Ramp`, `Square`,
`Blue Noise`, `White Noise`, `Pink Noise`, `Red Noise`, `Rumble`; modulator tuning
modes `Ratio`, `Absolute`, `Relative`, `Fixed (Hz)`; unison up to 8 voices with a
continuous `Phase` coherence knob ("When Phase is set to 1.00 ... all voices have
random phases. Set to 0.00, all voices begin with the exact same phase. The latter
sounds more punchy, but also more digital and less natural").

**4. Authoring.** Import only — no drawing, resynthesis or formula entry
documented. `Add Folder` imports a user bank; `Load Wavetable` imports individual
WAVs. Arbitrary WAV auto-slicing has an explicit arithmetic rule: "The first 2,048
samples will be placed in position 1, the second 2,048 samples will be placed in
position 2, and so on, until the maximum of 256 positions has been filled" — and a
short file yielding few positions is framed as a feature.

**5. Anti-aliasing.** Not documented anywhere in the 282-page manual (the only
occurrence of "aliasing" is in the Bitcrusher description). **Unverified.**

**6. What to steal.** (a) **`Morph` as a one-button toggle of the position axis's
entire character**, governing both knob motion *and* modulation. Kyklophoria's
interpolation is always on; a "step to nearest lattice cell" switch is nearly free
and doubles the character of every space. (b) **Phase retrigger as a named four-way
source**, especially `Self` (phase resets at an independently tuned rate — a
decoupled hard sync) and `Mod Osc`. Cheap, and it turns one space into four
instruments. (c) **Parallel always-on modifier stages with a per-stage modulation
depth knob**, rather than one-of-N. Maps well to a column-per-stage panel page.
(d) The **explicit, arithmetic import rule** — users can plan their tables, unlike
Ableton's "the first few seconds".

**7. Where the 1-D table limits it.** One `Position` per engine; two engines total,
and using both for wavetables consumes the whole synth for a crossfade. Cross-mod
(§13.2) exposes `Position` of the *other* engine, described as "Mirrors that
Engine's Position knob" — remote control of a duplicate 1-D knob, not a second
axis. And **the modifier stages are 0-D**: Phase Transform, Wavefold and FM each
take a scalar amount that cannot vary across the table.

---

## Bitwig — The Grid `Wavetable`, `Wavetable LFO`, Polymer, Sampler

Primary sources: [Bitwig Studio User
Guide](https://www.bitwig.com/userguide/latest/grid_modules/) (§19.29.5.6
Wavetable, §19.28.4.7 / §19.29.7.3 Wavetable LFO, §4.2.5.2 Wavetable Browser,
§19.16 Polymer/Sampler); [Bitwig 6.0 release
notes](https://downloads.bitwig.com/6.0/Release-Notes-6.0.html). Documentation
currency: the guide states "Updated for Bitwig Studio version 5.3, February 2025"
while the shipping product is 6.1; the 6.0 notes contain no changes to the
Wavetable oscillator's table handling.

**1. Table shape and limits.** Bitwig documents **formats, not dimensions**: "Any
WT file (**Surge format**) can be read, as well as **Serum- and WaveEdit-compatible
WAV files**". The manual links [Surge's
`doc/wavetables.md`](https://github.com/surge-synthesizer/surge/blob/bffb770fe8a93db2587ac97ffda99121782a2e5b/doc/wavetables.md),
which specifies mono 16-bit frames of **128–1024 or so** samples, length a power of
2, arbitrary table count — i.e. **variable frame length**. Bitwig's own internal
ceilings are **unverified**. Factory content: over 200 WT files in six categories
(`Acoustic`, `Analog`, `Digital`, `Fractal`, `Harmonics`, `LFO and Sequences`).
**Tables at once: unbounded** — the Grid places as many `Wavetable` modules as CPU
allows. Position is `Table Index`, and both behaviours exist: it "defaults to
**interpolating** the loaded wavetable for smooth transitions, but has an **option
to disable this (so that only tables in the loaded file are available)**". The
option's on-screen name is **not printed — unverified**.

**2. Interpolation and morphing.** Interpolation type **unverified**. Two things
here are documented nowhere else:
- **`Table Index` is a stereo signal**: "All in ports are fully stereo including
  Table Index, so the left and right channels can read different parts of the same
  wavetable."
- **`Harmonic Phases`**, three modes: `Aligned` ("All harmonics use the same phase
  (for a 'focused' sound)"), `Diffuse` ("All phases are randomized (for a 'blurred'
  sound)"), `Original` ("Original wavetable file's values are preserved"). This
  implies a per-frame harmonic decomposition at load.
Plus two whole-table transforms: `Remove Fundamental` and `Remove DC Offset` — the
latter "can transform wavetables made for modulation (such as the `LFO and
Sequences` category) into interesting audio sources".

**3. Warp / modifier modes.** **None on the oscillator.** Warping is patched from
other Grid modules. Its named-mode surface is unison and phase: unison modes `Fat`,
`Focused`, `Complex` ("producing polyrhythms between voices (and providing smoother
retriggering)"), plus `Spread Unison Phases`. `Wavetable LFO` gets exactly one line
in the manual — "A morphable LFO, with Bitwig WT file support" — everything else
about it is **unverified**. Polymer's oscillator slot list is "Sine, Triangle,
Pulse, Saw, Phase-1, Swarm, and Wavetable" (6.0 added `Union`), with the module's
options reached through a `Pop-out Editor`.

**4. Authoring.** Import only for the oscillator: WT (Surge), Serum WAV, WaveEdit
WAV, browsed via "a tilted 3D layout of each file's tables". No drawing, formula or
resynthesis on the module. Adjacent: `Sampler`'s **`Cycles`** play mode is "a
wavetable playback mode that captures periods of the waveform for playback. `Speed`
doesn't affect pitch, and broad `Formant` shifting is available", and **frame size
arrives as metadata**: "When a WAV file with a 'clm' chunk is imported into Sampler
... the play mode will be set to `Cycles`, and the `Root key` will use the
appropriate value (which determines the size of the wavetables in use)." The Grid
also has `Array` ("Recordable lookup table") and the BWCURVE free-form curve family.

**5. Anti-aliasing.** One sentence, policy not mechanism: "**Context-specific
anti-aliasing is applied on playback.**" Mechanism **unverified**. Note the absence
of Ableton's "as long as no modulation is applied" caveat, and that neighbouring
modules (shapers, `Curves`) do expose explicit AA toggles while `Wavetable` does
not.

**6. What to steal.** (a) **`Harmonic Phases`: `Aligned` / `Diffuse` / `Original`.**
The best single idea in the software survey for Kyklophoria specifically. We already
derive phases from a per-space seed (spec §3.1, §9.3); Bitwig's three-way choice is
the same decision exposed as an instrument control — coherent, randomised, or as
stored — and the `.kyk` format already reserves a stored-phases flag. Costs nothing
at runtime. (b) **Vector-valued position inputs**: L and R reading different frames
of one table gets width with no detune and no extra voices. Generalise it — our
stereo pair spreads rotation angle; letting *any* axis take a ± spread is the same
trick with more room. (c) **Per-table utility switches** (`Remove Fundamental`,
`Remove DC Offset`) — trivial spectral edits on a magnitude vector that repurpose
whole corpora; `Remove DC Offset` turning control-rate shapes into audio is
directly relevant since our payload and spectrum share a file. (d) **The same table
engine addressed at control rate**: run the interpolator slowly and you have a free
arbitrary-shape N-D LFO. (e) **Variable frame length and metadata-carried period**
(the `clm` chunk) instead of one hard-coded number.

**7. Where the 1-D table limits it.** One `Table Index` per module; stereo splits it
into two read heads on the same axis, it does not add an axis. Building a second
axis means two modules plus a crossfader — a bilinear approximation you implement
yourself, because **the Grid has no primitive for interpolating between tables**.
The `XY` modulator (§19.28.3.9, "Four control sources derived from one continuous XY
control") is a 2-D *controller*, not 2-D table navigation. And there is no
warp/modifier stage on the oscillator at all.

---

## u-he Zebra2 and Zebralette (Legacy)

Primary source: [*Zebra2 User Guide* v2.9.4](https://uhe-dl.b-cdn.net/manuals/plugins/zebra2/Zebra2-user-guide.pdf), Generators chapter. Zebra2 now ships only inside **Zebra Legacy**, which also bundles ZebraHZ / The Dark Zebra; Zebralette (legacy) is literally one Zebra2 oscillator — "A single Zebra2 oscillator with a 16-slot waveset" ([product page](https://u-he.com/products/zebralette/)).

**1. Table shape and limits.** Zebra2's oscillator is not a long frame list: it is a
**waveset of 16 waves**. The `Wave` knob sets "the position (index 1–16) within the
waveset. Unlike the row of selectors below the wave editor, this knob lets you set
**intermediate values**"; the modulation knob beneath it is ±16. Four oscillators
in the full synth; Zebralette (legacy) is one Zebra oscillator in a free wrapper.
Position is **continuous**, but how continuous is a CPU setting — see `Resolution`
below.

**2. Interpolation and morphing.** Governed by the oscillator's **Waveform mode**,
one of four, and this is the interesting design: two modes *morph* (move control
points) and two *blend* (crossfade sample columns).
- `GeoMorph` — "lets you draw waveforms by positioning **up to 32 points**, and
  adjusting the curvature of the lines between them ... all waves in the waveset
  adopt the same total number" of points. Morphing therefore moves point *i* of
  wave *n* to point *i* of wave *n+1* — a **correspondence-based** morph, not a
  crossfade.
- `SpectroMorph` — same editor, but "it does not depict a waveform directly, but
  rather its spectrum. **1023 harmonics** in the horizontal axis are **scaled
  logarithmically** for a total range of about 10 octaves."
- `GeoBlend` — "A single cycle is defined by **128 columns** ... when the morph
  function is used or the wave index is modulated, waveforms are **not morphed,
  they are blended**." Freehand drawable.
- `SpectroBlend` — "The spectrum is represented by **128 bipolar columns** ...
  fewer harmonics, scaled **linearly** for a total range of six octaves. The lower
  half is 'anti-phase', so the same harmonic in adjacent waves ... with opposite
  phases, can cancel each other out."

`Resolution` is the explicit render-rate control: "Controls the time between
successive waveform calculations, ensuring that Zebra2 is still very CPU-efficient
compared with other synthesizers that calculate their waveforms in realtime. The
range is from 4 seconds (at 1.00) to below one millisecond (at 9.00) ... Low
resolution can actually make transitions smoother, as intermediates are smoothly
interpolated, but can introduce other unwanted effects e.g. during rapid
pitch-modulation."

**3. Spectral effects (OSC FX).** Two in series, all 26 verbatim from pp. 33–34:
`Fundamental`, `Odd for Even`, `Brilliance`, `Filter`, `Bandworks`,
`Registerizer`, `Scrambler`, `Turbulence`, `Expander`, `Symmetry`, `Phase Xfer`,
`Phase Root`, `Trajector`, `Ripples`, `Formanzilla`, `Sync Mojo`, `Fractalz`,
`Exophase`, `Scale`, `Scatter`, `ChopLift`, `HyperComb`, `PhaseDist`, `Wrap`,
`DX`, `Smear`. (u-he's marketing says "24 spectral effects" — the manual lists 26.)
Several are cheap magnitude-vector operations worth lifting directly: `Filter`
("Because in reality the 'filter' code only manipulates amplitudes, its slope is
more than 100dB/octave"), `Scale` ("The relative amplitudes of harmonics are
scaled, either to the power of 2 (negative, softer) or 3 (positive, brighter).
Results in finer resolution of quiet harmonics"), `ChopLift` ("Negative values
raise an amplitude threshold below which harmonics are faded out (Chop). Positive
values raise levels of fainter harmonics (Lift)"), `Smear` ("Blurs the spectrum in
one direction"), `Registerizer` ("Boosts any octaves of the fundamental while
attenuating all other harmonics"). Note the manual's warning: "the
speed and smoothness of most spectral effects **depend on the Resolution
setting**". Also a `Renderer` switch, `soft` or `crisp`: "We recommend only
switching to crisp if you really need those extra 'spikes' and are not too
concerned about aliasing."

**4. Authoring.** Draw in the editor (points + curvature in the Morph modes,
freehand columns in the Blend modes), with a right-click menu of `insert point`,
`smooth`, `linear`, `peaks`, `distribute all`, `line up selected`, `clear`,
`copy / paste`. No native audio import ("GeoMorph wavesets can be extracted from
audio sources. A few third-party utilities are available for this purpose ... but
they do require some dedication"). Zebra2 **exports** wavetables: "Whenever Zebra2
detects a significant change in the waveform, including any caused by the spectral
effects but not by anything else (e.g. Sync, Stack or PWM) the waveform will be
recorded into a 'frame' ... Saving creates a plain mono WAV file with up to 256
frames containing 2048 samples each, which can be loaded into Hive 2 or
wavetable-based synths such as Xfer Serum."

**5. Anti-aliasing.** Only the `crisp`/`soft` renderer hint. Mechanism
**unverified**.

**6. What to steal.** (a) **`Resolution` as an exposed, honest parameter.** u-he
tells the player that the waveform is recomputed at a chosen rate and that a *lower*
rate can sound smoother. Kyklophoria's `render_div` (spec §4) is the same control;
this is precedent for exposing it rather than hiding it. (b) **Correspondence-based
morphing.** GeoMorph morphs point-to-point rather than crossfading — the reason two
waves with the same number of control points morph *through* plausible shapes
rather than through a mush. A lattice whose corners are magnitude vectors of the
same length gets this for free, which is an argument for fixed K. (c) **A single
editor that reinterprets the same data as geometry or as spectrum** (Geo/Spectro
pairs) — one authoring UI, two meanings, chosen by a switch. (d) Zebra2's
record-on-change wavetable exporter is a neat way to bake any modulated path into a
table; the same trick would let `tools/kykspace` bake an orbit through a `.kyk`
space into a flat 1-D wavetable for other synths.

**7. Where the table limits it.** 16 waves is a *small* 1-D set — deliberately, so
that morphing between them is meaningful — and there is exactly one `Wave` index.
The spectral effects are a second axis but again 0-D: a scalar applied after the
wave is chosen.

---

## u-he Zebralette 3 (and Zebra 3)

Primary sources: [*Zebralette 3 User Guide* v3.0](https://uhe-dl.b-cdn.net/manuals/plugins/zebralette3/Zebralette3%20user%20guide.pdf) (2025-12-04) and [*Zebra 3 User Guide* v3.0.1](https://u-he.com/downloads/manuals/plugins/zebra3/Zebra3%20user%20guide.pdf) (2026-07-07); dates from [u-he news](https://u-he.com/news/). Zebralette 3 went to public beta 2024-02-21 and released 2025-12-10; Zebra 3 released 2026-04-20 (249 EUR), 3.0.1 on 2026-07-13. The two share one oscillator: "Zebralette 3 presets can be loaded directly into Zebra 3, but please note that any direct modulation of Pan, Volume and Width as well as the effects will be removed in the process."

**1. Table shape and limits.** Zebralette 3 is **not a frame store at all**. It
holds a **Curve Set** — "the multiple waveforms distributed along the timeline" —
where each Curve is a spline of control points (Bézier points plus per-segment
curvature). The Zebralette 3 guide says "up to 15 moveable Curve thumbnails" with
"a minimum of two curves"; the Zebra 3 guide (v3.0.1) says "**2–16** moveable
thumbnails" — take Zebra 3's as current. Curves sit at arbitrary positions on a
continuous **0–100** timeline (with a `Distribute Evenly` menu item), so node
spacing is non-uniform by design. The position parameter is `Curve Morph`:
"Direct modulation of this parameter generally operates at a higher resolution
than indirect modulation via the modulation matrix." There are also **3 Guides** —
global reusable curves that most oscillator effects can take as their source.
A single `Source` switch decides what the spline *means*:
- `Curve Geometry` — "The curve represents the waveform as-is (like GeoMorph in
  Zebra2)."
- `Curve Spectrum` — "The curve represents the harmonic spectrum: **1024
  harmonics are scaled logarithmically for a range of about 10 octaves**."

**2. Rendering and interpolation.** A separate `Renderer` switch: `Wavetable`
("Renders the waveform like a classic wavetable synthesizer, updating it at the
rate set by the Resolution parameter"; Unison 2–16, "processed in blocks of 4")
or `Additive` ("Reproduces the spectrum of the waveform with the number of partials
(sine waves) specified by the Harmonics parameter ... As these are **free running
and independently tunable**, they can be processed to create inharmonic sounds").
`Harmonics` ranges **16 to 1024**, default 256. `Resolution` is **200 Hz, 800 Hz or
2000 Hz** — "the density of waveform calculations ... how often they are updated per
second"; it has no effect in Additive mode. A `Maths` switch trades precision for
CPU: `Precise` / `Fast` / `Rough`.

**3. Morph correspondence — the genuinely novel bit.** "Morph types are set
**individually for each pair of Curves along the Timeline**", verbatim:
`Crossfade` ("No actual morphing — points from both curves are used to interpolate
vertically only (points are not moved along the x-axis)"), `Point By Point`
("Connects points by index, left to right. Surplus points in the more complex Curve
are connected to the final point in the simpler one"), `Closest X` ("Connects points
by proximity along the X axis"), `Closest X & Y`, `Peaks & Valleys` ("Connects high
points to high points and low points to low points" — the default). Plus
**Morph Vectors** — "This only works with the two Closest morph types. Clicking and
dragging any point (except the first or last) makes that point '**pretend**' to be
elsewhere as far as the morph calculation is concerned" — and `Ease In / Out`
(0–100 each) controlling "the linearity of morphing between adjacent Curves".
Nothing else in this survey lets you author *what happens between* neighbouring
points in the table.

Additive-mode `Modifier` / `Spectral Modifier`, all applied "relative to the
fundamental frequency i.e. the first harmonic": `Expansion` ("Stretches harmonics
up one octave. At maximum Spectral Distortion the result is odd-numbered harmonics
only"), `Compression` ("Compresses all harmonics down towards the fundamental"),
`Curve` ("Shifts overtone pitches according to the Guides or Curve Set ... the
fundamental is not affected"), `Harmonic Clusters` (with an 11-stop
`Cluster Select`, from `0 Even Harmonics` / `10 Odd Harmonics` through
`100 Every 7th harmonic, starting from the 2nd`; "Intermediate values are
crossfades: the pitches of clusters are shifted while their relative tuning remains
intact"), `Log Clusters` ("clusters ... distributed to ensure **equal energy across
the spectrum**. Starting with 3 clusters, at maximum level 10 clusters are spaced
precisely octaves apart – great for bells or organs"), `Chaos Patterns`
("[Random Seed] selects one of 100 preset patterns", with `Distortion Range` =
`Full Spectrum` / `One Octave` / `Four Octaves` / `Ordered` / `One Harmonic`), and
`Wild Randomness` ("Reorganises the harmonics in a randomised pattern sampled at
Note On. Similar to Chaos Patterns but without repeats").

`Phase`: `Random` ("If the Renderer is Wavetable ... a random phase each time a
note is played. If the Renderer is Additive, the phase of each individual harmonic
is randomized") or `Reset`. `DC Block` in Wavetable mode only. Zebra 3 is candid
about the additive path's phase behaviour: "The phases of harmonics will gradually
drift apart over time. This is intentional — numerical precision is traded for
faster computation here."

**Oscillator FX**: two serial, swappable slots, 21 named effects in four documented
groups (identical list in both guides). *Spectral Effects* ("process harmonics
differentially"): `Curve Filter`, `Filter`, `Formant`, `Sparse`, `Spectral Focus`,
`Tone Works`. *Warping Effects* ("directly process the complete waveform"):
`Delta X`, `Map-o-Matic`, `Phase Distortion`, `Scrambler`, `Symmetry`, `Sync`,
`Wrap & Zap`. *Windowing Effects* ("change the 'view' of the Curve"): `Dual Wave`,
`Window`, `Zoom`. *Animation Effects*: `Dissociate`, `Posterize`, `Spectral Decay`,
`Spectral Noise`, `Twinkles`. Three of these are near-free on a magnitude vector
and have no equivalent in a frame store: `Spectral Decay` ("Uses a Guide or Curve
Set to make harmonics decay differently: high values along the curve mean longer
decay"), `Sparse` ("Randomly generates gaps in the spectrum"), `Twinkles` ("Random
overtones ... 'pings' an overtone each time it leaves zero in the positive
direction"). Most FX take a `Source` of `Guides` or `Curve Set`, and the manual
notes you can partition the timeline — "0-50 for the audio and 60-100 for the
effect" — at the cost of "considerably" more CPU, "as these curves are calculated
at audio rates".

**4. Authoring.** A full spline editor: shape tools with a **Harmonic Grid**
overlay ("stronger lines are at harmonics 2, 4, 8, 16, 32, 64, 128, 256, 512"),
freehand `Paint` ("The curve is optimized as soon as you release the mouse
button"), `Warp` (`WarpLinear` / `WarpExpo` / `WarpCubic`), `Rotate` ("Shifts the
'phase' of the entire Curve or selection"), and a clean-up menu including
`Beautify`, `Simplify` ("Removes all points that have little or no impact on the
shape ... adding curvature wherever necessary") and `Sine-O-Matic` ("attempts to
create perfect sine arcs"). Timeline menu includes `Add Morphed Curve` ("inserts an
interpolated Curve ('**frozen morph**')"). Guide↔Curve transfer ops
(`Move Points Down To Guide`, `Scale Curve Below Guide`, `Replace Curve With
Guide`, `Skew Curve With Guide`, …) — "Points are automatically added or removed
wherever necessary."

Interchange: "**Copy Curve** is high resolution and uses our '**UHM**' scripting
format. **Copy Curve as SVG** ... lets you paste the selection into any graphics
application that supports Scalable Vector Graphics", and "Curves can be
copied/pasted as plain text (XY values in hexadecimal), or single floating point
values between 0.00000 and 100.00000." Export: "Saves the entire Curve Set,
including all morphed intermediates as a **101-frame wavetable file in .WAV
format** ... 101 frames, the number of possible positions on the timeline.
Intermediates are interpolated according to the selected Morph Types." Import is
resynthesis-adjacent but flagged experimental: "Single cycle files ... will only
replace the currently selected Curve, while multi-cycle samples will replace the
entire Curve Set. ... The import routine **detects pitch then slices the sample
into individual cycles, noting which ones represent the most significant
differences**", with a CAUTION that large WAVs "can cause Zebra 3 to stall or crash
the host app".

**5. Anti-aliasing.** Not mentioned in the guide at all. **Unverified.**

**6. What to steal.** (a) **Per-edge morph rules.** The single most transferable
idea in this survey for an N-D lattice: Kyklophoria interpolates every edge the same
way (multilinear over 2^N corners). Zebralette 3 shows there is a real instrument in
letting the *edge* carry a rule. Even one bit per axis (linear vs eased) is a
cheap, legible version. (b) **`Resolution` = 200 / 800 / 2000 Hz.** Our block rate
at 48 kHz / 24 samples is exactly 2000 Hz — u-he's top setting. That is independent
confirmation that per-block spectrum rendering is the right resolution and that
`render_div` = 2 or 4 (1000/500 Hz) remains musically usable if we need the cycles.
(c) **One store, two interpretations** (`Curve Geometry` / `Curve Spectrum`) chosen
by a switch. (d) **Two renderers over the same data** (wavetable IFFT vs free-running
additive) — the additive path is what makes inharmonic modifiers possible, and a
hyperpoint's magnitude vector could drive either.

**7. Where it is limited.** The Curve Set is still a **timeline** — one axis, up to
15 curves. And it stores a *curve* (an envelope function over harmonic index),
not a bin array, so per-harmonic control is indirect; both manuals concede it —
"In the current version, editing individual harmonics is a bit fiddly — the plan is
to improve usability in future versions" (Zebra 3), "Work in progress! Zebra 3
might include a more comfortable method for the first 32 harmonics" (Zebralette 3).
Additive mode loses `Resolution`, `Unison`, `Detune`, `Width`, `DC Block` and the
phase-based effects. `Dual Wave` is the only way to hear two curve positions at
once, and "the extra curve is always interpreted as Curve Geometry".

**Adjacent 2-D, worth naming precisely.** Zebra 3's **4in1** mixer does expose real
vector navigation — but over **four signals**, not over a table. `Scan Mode` has a
`Blend Law` of `Power` ("signal levels are proportional to the square root of the
distance from 1/2/3/4"), `Linear` or `Switched`, plus `Scan` and `Width` ("The
width of the 'window', the number of adjacent lanes included in the mix.
Maximum 4"). `Vector Mode` places handles A–E on an XY square — "The closer the
handle is to a corner, the more prominent that input will be" — auto-traverses
A→B→C→D→E with per-leg time multipliers and `Loop Mode` options
(`ABCDE`, `BCDE`, `CDE`, `DE`, `ABCDEDCBA`, `BCDEDCB`, `CDEDC`, `DED`), and offers
`X Mod` / `Y Mod` for live modulation of the output dot. The `Blend Law` trio and
the `Width` windowing idea generalise straight into an N-D lattice interpolator,
and the auto-traversed vector path is the closest u-he gets to an orbit — but it
mixes four sources, it does not index a space.

---

## u-he Hive 2 — the only mainstream synth with a two-axis wavetable

Primary sources: [*Hive User Guide*](https://u-he.com/downloads/manuals/plugins/hive/Hive-user-guide.pdf) (WAVETABLES chapter) and [*Hive Wavetables*](https://u-he.com/downloads/manuals/plugins/hive/Hive%20Wavetables.pdf) (the UHM scripting reference). u-he's [product page](https://u-he.com/products/synths/hive/) states it outright: "2 oscillators with standard waveforms or **2D wavetables**, up to 16x unison."

**1. Table shape and limits.** "Hive wavetables can contain **up to 256
single-cycle waves**." WAV files are "interpreted by default as having a cycle
length of **2048 samples per frame** – the number of frames is calculated from the
total size of the file and is limited to 256. To specify other samples-per-frame
values, the file name must end with '-WT' followed by the number e.g.
MyWavetable-WT512.wav (or 64, 128, 256, 1024)" — i.e. **variable frame length by
filename convention**. Two oscillators.

**2. Two-dimensional navigation — verbatim.** "**Hive's wavetables become
two-dimensional (some would even say three-dimensional) if the `Tables` parameter,
which specifies the number of frames in the y-axis, is set to 2 or more. The lower
`Position` knob then crossfades between frames in the y-axis.**" The manual's own
diagrams show a 16-frame table folded as `Tables = 1` (16×1), `Tables = 2` (8×2),
`Tables = 4` (4×4). "Things can get rather interesting if you set Tables to a value
that doesn't divide the number of frames in the wavetable so evenly."

This is a **fold of a 1-D store into a grid**, not a natively 2-D authored table,
and the second axis is second-class: "This option only applies to the main
`Position` parameter — **the interpolation through `Multi Position` is always
crossfade**."

**3. Interpolator modes** (X axis only), verbatim:
`switch` ("no interpolation at all, sudden jumps between frames"),
`crossfade` ("smoothly interpolates waveform magnitudes"),
`spectral` ("like crossfade, but also interpolates the phases of each partial.
CPU-hungry!"),
`zero phase` ("like spectral, but also forces the phase of each partial to zero
first"). The manual adds: "As blending different phases requires extra computation,
spectral is actually the highest quality mode, and therefore the most
CPU-intensive. Tip: The CPU-friendly crossfade is usually best."

Also on the position axis: `WT Auto Mode` (`One Shot`, `Loop >`, `Loop ><`) with a
`Tempo` control — **a built-in scanner, no LFO required**; `Reverse` ("Reverses the
order of frames"); and `Cyclic`, which "adds a copy of the first frame to the end of
the table ... Cyclic mode also lets loops cross wavetable boundaries." **`Cyclic` is
a wrap topology on the position axis** — the direct 1-D precedent for Kyklophoria's
`Wrap` (spec §3.3), including the seam-frame duplication trick.

**4. Authoring — UHM.** Hive loads `.wav` or **`.uhm`**, "readable text files ...
scripts create wavetables step-by-step by interpreting a list of commands /
formulas", stored in presets **by reference only**. Commands, verbatim: `Info`,
`NumFrames` (defaults to 256), `Seed`, `Wave` ("Parses formula on time domain
Wavetable"), `Spectrum` ("Parses formula on **magnitudes** of frequency domain
Wavetable"), `Phase` ("Parses formula on **phase info** of frequency domain
Wavetable"), `Import`, `Export`, `Move`, `Interpolate`, `Normalize`, `Envelope`.
Every command takes `Start=`/`End=` frame ranges, a `Blend=` mode, `Direction=`, and
`Target=main|aux1|aux2` (three buffers). `Spectrum` takes `Lowest=`/`Highest=`
partial bounds (default 1…1024); `Phase` defaults 1…1023. Blend modes: `replace`,
`add`, `sub`, `multiply`, `multiplyAbs`, `divide`, `divideAbs`, `min`, `max`
("good for emulating formants when used with Spectrum"). Random functions are
seeded and split three ways: `rand` (per operation), `randf` (per frame), `rands`
(per sample). `Interpolate` takes `Type=switch|crossfade|spectrum|zerophase|morph1|morph2`
with `Snippets=` (1–500), `Threshold=` (−120…0 dB) and `Weighting=none|distance|level|both`
for the morph types.

**5. Anti-aliasing.** Oversampling is documented for the filters only. Wavetable
oscillator mechanism **unverified**.

**6. What to steal.** Hive is the closest software precedent to Kyklophoria and the
most directly plunderable:
- **`Tables` as a reshape parameter.** One integer that reinterprets a linear store
  as a grid, with the deliberate, documented weirdness of non-divisible values. A
  `.kyk` space is already `side^N`; exposing "how do I fold this?" as a performance
  parameter is nearly free and gives a rotation-adjacent gesture (changing the fold
  changes which cells are neighbours).
- **`Cyclic`** — the wrap-seam done by duplicating frame 0 at the end. Our M0 seam
  runs column side−1 back to column 0; same idea, and Hive's is a per-table switch.
- **`WT Auto Mode` + `Tempo`.** A scanner built into the oscillator, so the simplest
  wavetable gesture costs no modulation slot. Kyklophoria's orbit LFOs (spec §3.4)
  are the N-D generalisation; keeping a plain per-axis `One Shot / Loop > / Loop ><`
  is worth it for the same reason.
- **Named interpolators with an explicit CPU ranking.** `switch` / `crossfade` /
  `spectral` / `zero phase`, and the manual says outright which is expensive and
  which to prefer. This is the exact menu we should offer, and our
  magnitude-only + seeded-phase design *is* Hive's `zero phase`.
- **UHM.** A text format that parses formulas over the **time domain, the magnitude
  spectrum and the phase spectrum separately**, with frame ranges, blend modes,
  three buffers and file import — and is stored by reference so a preset is small.
  `tools/kykspace` should steal this wholesale, extended to N indices: `Spectrum
  start=(0,0,1,2) end=(3,3,1,2) "..."`.

**7. Where it is still limited.** The 2-D is a **fold**, not a store: there are
still only 256 hyperpoints total and the grid is always `n × 256/n`. The y-axis
cannot use any interpolator but crossfade. There is no third axis, no per-axis
topology on y, and no way to rotate or traverse the grid obliquely — `Position` and
`Multi Position` are strictly axis-aligned.

---

## Kilohearts Phase Plant

Primary sources: [kilohearts.com/docs/phase_plant](https://kilohearts.com/docs/phase_plant),
[/docs/wavetables](https://kilohearts.com/docs/wavetables),
[/docs/modulation](https://kilohearts.com/docs/modulation).

**1. Table shape and limits.** Fixed and explicit: "It is backed by a wavetable,
which contains **256 frames**, each one holding a sampled waveform **2048 samples**
long." Import requires exactly that: "Phase Plant can load wavetables from wav and
flac files, provided they have the right length (**256 × 2048 = 524 288 samples**)."
The generator area holds up to **32 modules**, so on the order of 30 independent
wavetable oscillators can coexist. The *same* 256-frame format also backs the
**LFO Table** modulator — "cycle through 256 LFO shapes in one modulator" — so one
data structure and one editor serve both audio and control rate. `Frame` "selects
what frame in the wavetable to play back"; **whether `Frame` is continuous or
stepped is never stated — unverified**, though the changelog entry "Reduced some
artifacts in wavetable interpolation" (2.4.5, 2025-12-11) and the stated purpose of
the Align fixes ("to reduce disturbing phase effects **when modulating the frame**")
both imply intermediate positions are blended.

**What "Phase Plant 2" added to the wavetable engine: nothing.** v2.0.0
(2022-05-18) added the modular modulation system, MPE, curvature on modulations,
Snapin/modulator groups and ten new modulators including `LFO Table`. Wavetable
changes came later in the 2.x train: `Symmetrize` (a new editor Effect) and
`Fix Seam` (a new Fix tool) in **2.1.1** (2023-10-04); sample→wavetable conversion
from the browser in 2.2.3; the interpolation-artefact fix in 2.4.5. Note Kilohearts
version-numbers the whole product line together, so "Phase Plant 2" is a release
train, not a separate SKU.

**2. Interpolation and morphing.** Handled at authoring time, in the editor, by the
`Morph Tool`, which "crossfades between frames using **linear or spectral**
morphing". Runtime behaviour unverified.

**3. Modifiers.** The oscillator itself is deliberately thin: `Frame`, `Phase`
(with a `+/−` randomiser that "will randomize the phase of each unison voice
individually"), `Bandlimit` ("Bandlimits the wavetable using a very sharp internal
low pass filter. This feature can be used to tame a wavetable which is under heavy
phase modulation, **since the filter is applied before the phase modulation**"),
level, unison. Warping is done by Snapins downstream. Unison modes: `Unison – Hard /
Smooth / Synthetic`, `Creative – Frequency Stack / Pitch Stack / Shepard`, and
`Chords`.

Modulation architecture, verbatim and directly relevant: "Values in the generator
section are updated for every sample ... at **audio rate**. While modulators output
values much less often. Typically every **64 samples** ... at **control rate**."
Audio-rate targets are documented as Phase → Classic FM, Harmonic/Shift → Linear FM,
Level → Ring Mod, Pitch → Exponential FM. **Whether `Frame` accepts audio-rate
modulation is not stated — unverified**, and it is the most consequential gap here.

**4. Authoring — the strongest editor in the survey.** Modal tools: `Selection Tool`,
`Morph Tool`, `Pen Tool`, `Brush Tool`, `Wave Tool`, `Harmonic Edit Tool` ("directly
edits partials in the spectrum with phase adjustment widgets"), `Filter Tool` —
with **keyframe animation across the wavetable**, so a tool's parameters are
automated along the frame axis. Modal effects: `Automatic EQ`, `Frame Blend`,
`Comb Filter`, `Disperse`, `Distortion`, `Phase Offset`, `Power Sync`, `Rectify`,
`Reset Phases`, `Self FM`, `Sine FM`, `Squarify`, `Sync`, `Tilt EQ`, plus
`Symmetrize` (added 2.1.1, absent from the docs page). Non-modal "fixes":
`Normalize`, `Normalize Global Peak`, `Normalize Frame Peak`, `Remove DC`,
`Invert Time` / `Invert Amplitude`, `Fix Seam` (2.1.1), and the three alignments,
whose definitions are worth having exactly:
- `Align Fundamentals` — "sets the phase of the first harmonic in all frames to be
  identical to the selected frame."
- `Align All Phases` — "sets the phase of all harmonics to be identical to the
  selected frame."
- `Align Frames` — "phase-shifts each frame **without changing the phase ratio of
  its harmonics**, so that they get maximum correlation to the selected frame."

`Frame Blend` is the other operator worth naming: "Frame 5 will after processing
become a mixture of frames 2, 3, 4, 5, 6, 7 and 8. The amount of frames to be mixed
can be set with the **Distance** parameter" — a 1-D box blur along the table axis.

Sample conversion is itself a keyframe-animated modal tool, not a one-shot import:
automatic root-pitch detection (manually overridable), a `Pitch bend` parameter
"for samples where the pitch is not static" (which "can also create interesting
effects similar to a formant shift"), a per-keyframe `Source` parameter to "adjust
which location of the sample they map to" (so the frame→time mapping is drawn, not
forced linear), a `mix` parameter that blends against the *existing* table rather
than replacing it, and a choice of phase-alignment strategies to mitigate drift.

**5. Anti-aliasing.** One documented mechanism: the `Bandlimit` low-pass, applied
before phase modulation. Whether the oscillator itself is band-limited is **not
documented — unverified**.

**6. What to steal.** (a) **The bake/play split, done properly.** All the
intelligence is in an offline editor; the runtime oscillator has four knobs. That is
exactly Kyklophoria's `tools/kykspace` + thin `Engine` division, and it is a
validation of the architecture. (b) **`Align Fundamentals` / `Align All Phases` /
`Align Frames` as named one-click fixes.** Phase alignment is the recurring theme
of this entire survey (Serum's zero-phase morph, Ableton's load-time phase
minimisation, Bitwig's `Harmonic Phases`, Hive's `zero phase`); Phase Plant is the
only one that names three distinct strengths of it. Our importer should offer the
same three. (b′) **`Frame Blend` with a `Distance` parameter** — a box blur along
the table axis, which generalises directly to an N-D smoothing kernel and is
exactly the operator that makes a heterogeneous corpus playable. (c) **Keyframed
authoring tools**: a filter or an EQ whose parameters
animate *along the frame axis* is how you get a table that is coherent rather than
a pile of unrelated frames — generalises directly to "payload is a simple function
of position" (spec §5). (d) **`Bandlimit` before the nonlinearity**, not after —
the right order, and cheap on a magnitude vector.

**7. Where the 1-D table limits it.** One `Frame` per generator, 256 frames, and a
hard 524288-sample import contract. Two generators is a mix, not an interpolation.
Unison spreads pitch and phase but has **no documented way to spread voices across
the table position** — the affordance Serum, Ableton and Falcon all have and the one
an N-D oscillator wants on every axis.

---

## UVI Falcon

Primary sources: [*Falcon User Manual*](https://cdn.uvi.net/UVIFC_Falcon/manuals/Falcon_manual_en.pdf) (UVI, 2026 edition), Appendix A: Synthesis Oscillators → WaveTable, p. 140; [custom-wavetables KB article](https://support.uvi.net/hc/en-us/articles/360001260738-Falcon-Loading-Custom-Wavetables); [scripting reference](https://lua.uvi.net/class_oscillator.html).

**1. Table shape and limits.** "The WaveTable oscillator uses a table containing
multiple waveform shapes. While a **single waveform is played at any given time**,
modulation between the different waveforms produces distinct and unique sounds."
Falcon calls frames **slices**. Frame and slice-count limits are **not documented —
unverified**. Falcon is a multi-oscillator sampler-synth: any number of WaveTable
oscillators can be layered in the program tree. `WAVE INDEX` "determines which slice
in the wavetable will be played."

**2. Interpolation and morphing.** Two named smoothing options rather than an
interpolation mode: "Depending on the wavetable, transitioning from slice to slice
has the potential to be abrupt. There are therefore two smoothing options:
**`SMOOTH WAVE INDEX`** and **`SMOOTH OCTAVES`**." The underlying algorithm is
**unverified**. Note that Falcon's global `ALWAYS USE BEST INTERPOLATION` preference
is scoped to "best interpolation mode **in Sample oscillators** at loading time" —
the Lo-fi/Standard/Best ladder does not reach the WaveTable oscillator.

**3. Named modes.** `PHASE DISTORTION MODE` ("chooses the style of the phase
distortion") with `PHASE DISTORTION AMOUNT` — **the mode list is not printed in the manual**. Only
two mode names appear anywhere in UVI's own documentation: **`Bend+`**, visible
selected in the Mode menu in the manual's own p. 140 screenshot, and **`SymForm`**,
named in the Falcon 1.3.1 changelog ("fix Wavetable SymForm mode when phase
distortion is at 0"). Longer lists circulate on forums; none is UVI-published, so
the rest is **unverified**. `START PHASE`. FM: `ENABLE`, `DEPTH`, `RATIO` with a
`SNAPPING` menu (`Harmonic`, `Chromatic`, `Octaves`, `Oct + 5th`, `Fourths`,
`Fifths`), `FINE`, `HZ`. Unison as per the Analog oscillator plus one extra:
**`WAVE SPREAD`, "which sets the range of Wave Index values for each voice"**.

**4. Authoring.** Import by drag-and-drop, with two unusual affordances:
"Audio files are imported with one slice per channel. Slices will also be imported
if slices are arranged one after the other within an audio file, and **the number of
samples per slice is specified at the end of the file name following an underscore**
(e.g. 'MySweep_128.wav')" — variable frame length by filename, as in Hive. And:
"**Image files are imported with each row of pixels as the wave cycle, with one
slice per row.** (Very large image files may be resampled or cropped.)" No drawing
editor, no formula parser on this oscillator; Falcon's scripting (Lua) is a separate
facility.

**5. Anti-aliasing.** Not described. The changelog entry "Better aliasing handling
in wavetable oscillator" (Falcon 2.0.5) confirms a scheme exists and was revised
but describes nothing. The existence of a **`SMOOTH OCTAVES`** control distinct
from `SMOOTH WAVE INDEX` implies octave-banded (mip-mapped) table copies with a
crossfade between bands — **inference, unverified**; UVI never says so. Note that
Falcon documents oversampling explicitly on effects and on its Phase Shaper
oscillator (2x–16x) but gives the Wavetable oscillator no oversampling control at
all, which suggests whatever it does is fixed and internal.

**6. What to steal.** (a) **`WAVE SPREAD`** — unison voices spread along the table
axis, named as its own parameter rather than buried in a menu. Generalise to one
spread per axis and you have Kyklophoria's stereo δ (spec §9.1) turned into a
per-axis vector. (b) **Frame size declared in the filename** (`_128`) — a
zero-metadata convention that works on an SD card, worth copying for `.kyk` sidecar
WAVs in M4. (c) **Image import, one row per cycle.** A genuinely good idea for an
N-D device: a 2-D image is a natural authoring surface for a 2-D slice of a lattice,
and it costs nothing but a decoder.

**7. Where the 1-D table limits it.** One `WAVE INDEX` per oscillator; "a single
waveform is played at any given time"; layering is the only route to a second axis
and it mixes rather than interpolates. Smoothing is a pair of on/off-ish helpers
rather than a documented interpolation model.

---

## PPG WaveGenerator and PPG WaveMapper / WaveMapper 2 (Wolfgang Palm)

Primary sources: *PPG WaveGenerator plugin manual* and *PPG WaveMapper 2 plugin
manual* (Wolfgang Palm / PPG). Palm's `wolfgangpalm.com` is dead and parked; both
were recovered via the Wayback Machine
([WaveGenerator](https://web.archive.org/web/2020id_/http://wolfgangpalm.com/WaveGenVst/WaveGeneratorPlugin.pdf),
[WaveMapper 2](https://web.archive.org/web/2020id_/http://wolfgangpalm.com/WaveMapVst/WaveMapper2Plugin.pdf),
[WaveMapper 1](https://web.archive.org/web/2015id_/http://wolfgangpalm.com/mediafiles/2013/01/WaveMapper_Manual.pdf)).

**Correction to a common secondary-source claim: WaveMapper *1* has no Wave Map.**
The phrase appears nowhere in the v1 manual, and the v2 manual says so itself —
"WaveMapper 2 is a greatly expanded version, with many new features, **including the
Wave Map**." What v1 has is a path *within a single 1-D wavetable*: "A path within
a wavetable is a set of two or more path-points, pointing to wave positions", with
two named types, `Linear paths` ("these paths have always the wave path points in
successive order") and `Complex paths` ("only the set path points are part of the
path… The in-between wave points are not a part of the path!"), and the caveat "You
can not create complex wave paths in [WaveMapper]" — they arrive by importing
WaveGenerator tables. So v1's path is a **non-monotonic ordering of a 1-D index**,
not a 2-D traversal. The 2-D case is WaveGenerator (2012) and WaveMapper 2. **These lay their waves out on a real 2-D grid — as
opposed to Hive 2, which folds a 1-D list — and they are the only place in this
survey where a product exposes a non-axis-aligned traversal.** Everything below is
quoted from those manuals.

### PPG WaveGenerator — a 16 × 16 wave grid with a path

**1. Table shape.** "**256 waves assembled within a wave grid**"; "The wave grid
contains **16 rows with 16 places each**, which can be filled up with waveforms."
"The waves are collected in a grid of 256 fields, to which the **3 oscillators** of
the synthesizer have **arbitrary and independent access**." So: one shared 2-D
store, three independent readers.

**2. How the player moves through it.** Not by a position knob. "The way you access
these waves depends on the envelope settings and **a path which connects single
waves out of the grid**. This path is shown as a grey line, which connects the path
points." The grid has **seven modes**: `Browse` ("a temporary mode, which disables
the path"), `Path` ("Here you can define the path on which the envelopes will
control the wave-access"), `Linear` ("an easy way to create linear paths, which run
through all points of one column ... If you move from left to right, more columns
will be added to the path"), `Edit` ("Click and drag a column of waves to another
position"), `Record`, `Overdub`, `Modify` ("Select a single wave from the path, and
change its waveform or spectrum. **You still play the complete path**").

`Linear` mode is column-wise — i.e. axis-aligned, the classic PPG wavetable read.
`Path` mode is a free polyline across the grid, i.e. **oblique**.

**3. Loading and layout.** `Exchange` / `Append` / `Over` control how a loaded
wavetable is written into the grid; "When you change the program, a new wavetable
is loaded from the bottom of the grid upward." So the vertical axis is a stack of
loaded 16-wave tables and the horizontal axis is position within them — a real
two-axis layout the user can rearrange (`Edit` mode drags whole columns).

**4. Authoring.** Draw waveforms; draw spectra in the **Spectrum Editor**, which
has three modes — `Pick` ("You pick a single bar from the spectrum and change its
amplitude ... you always stay on the selected bar"), `Draw` ("when you move
horizontally, the controller switches to another bar"), `Over` ("You overwrite bars
on the selected wave. This can be recorded into a new wavetable, using Record mode
in the wave-grid") — plus `GAIN` and `TILT` on the spectrum window and a
`Normalize` tool on the grid. And **bitmap import**: "Another way is to transform a
picture into a wavetable. You can load bitmaps via drag & drop and directly hear
the result as wavetable sweep."

Grid tools: `SAVE`, **`S-PATH`** ("saves a new wavetable, which only contains the
points which are in the current path" — i.e. bake the traversal down to a flat
table), `CLEAR`; path buttons `RESET`, `DELETE`, `NORMAL`.

**Interpolation is a named continuous control**: "The **`Blending`** parameter works
as a smoothing function as the oscillator progresses through the waves. When set to
1, it gives you hard switching, 0 gives the smoothest blend."

**And the path can be translated under modulation.** "The **`W Offset`** para shifts
the path you have setup inside the wave-grid … If you set −2 the offset increases.
**At −16 the offset is a full grid column, so the path is now parallel shifted to
the left.** Now if you activate the `Horizontal` switch, the path is only allowed to
offset in horizontal direction."

**Image Transformer** — "transforms a 128 × 128 pixel image into a wavetable …
bmp, png or jpg", with three named modes: `Wave` ("the image is taken row by row,
and directly copied into the wavetable. The top row becomes wave #0, the bottom row
#127"), `Spect D` ("the rows of the image are interpreted as **spectra**, and then
transformed into waves"), `Spect H` ("the spectra get an additional tilt, to make
them more soft"). It "always occupies the left half of the wavegrid."

**5. Anti-aliasing.** Documented, but obliquely and without the word. Oscillator
modes are `Audio 1` ("the standard audio mode which has the most possible
overtones"), `Audio 2` ("has a softer sound. **Especially for high pitches it may be
better to use mode 2**") and a sub-audio mode; the Image Transformer chapter advises
"If you use waves with strong high end spectra, it may be useful to use `Audio 2`
mode." So `Audio 2` is effectively a band-limiting option, exposed as a timbre
choice rather than a quality setting. Not characterised numerically —
**partially verified**.

**6/7.** See the consolidated notes below.

### PPG WaveMapper 2 — a zoned 2-D map with a typed, translatable path

**1. Map shape.** "The **WaveMap** is the central part of the synthesizer engine ...
You load your own or PPGs audio resources into the Map and arrange them in a way
which is appropriate for the synthesizer." "The WaveMap is divided into **columns**
holding portions of the loaded audio resources" — each column is a **zone** holding
a wavetable (WT), a Time-Corrected Sample (TCS), or a plain sample. "The **WT is
scaled so that 16 periods fit in one column**"; "A TCS file is scaled so that it
always will fill the zone." Sparse tables "are expanded to fill the column."
**"The vertical resolution is always 100 steps regardless of the material loaded in
the zones."** Up to 16 resources per WaveMap. "**Each of the 3 oscillators may have
its own Map and Path.**" Zones carry per-zone octave and gain adjustment "so that
the resources nicely work together". The whole map is saved as a **Multi setup**
(`.m`) file, separate from the program.

**2. How the player moves through it — the Path.** "Then you design a **Path**,
which is the way the synthesizer plays the material over time. When you play a note
you see a **green light-point that shows the exact position the oscillator is
playing**." Editing: "CMD + click inserts a new point behind the active one. To
make a path segment a linear path, hold the SHIFT key and move the point ... RESET
removes all points and puts a single point in the bottom/left corner."

Segments are **typed**: "There are two types of path segments: The stroked line
stands for a **Linear Path**, and the normal line for a **Point to Point** path
segment. In the linear mode all intermediate points are connected, which means all
waves are played in between. In the point mode, only the selected points are
played. Please note that in a TCS resource always all material between 2 points is
played."

And it is **separable from the content**: "the Path is part of the sound program
and is saved with it. **Any Path can be used with any Multi setup!** It doesn't
matter if path points are located in empty parts of the WaveMap."

**3. Live transforms of the path.** "The **Wave Offset** parameter shifts the path
you have setup inside the Wave Map in a **vertical direction**. When the offset is
higher than 16, the vertical shift is reset but you get an additional **horizontal
shift**." Plus `KEY` ("adds an offset depending on the played key. This can be seen
as equivalent to the Keys parameter in an analog synth") and `HORIZ` ("If you don't
want the vertical shift, but just the horizontal"). `BLEND` runs the interpolation:
"When set to 1, it gives you hard switching, 0 gives the smoothest blend."

The drive is a scalar: an envelope (one of 13) walks the position along the path,
and the manual's tutorial shows the envelope running the path *backwards* during
attack.

**4. Authoring.** Load WT sets, TCS resources or samples into zones; "PPG
WaveMapper allows you to **convert any sample you load into the app into
synthesized waveforms**" (the TCS analyser, on the Analyzer/TCS page). Resource
categories: `PPG WTS inventory`, `PPG TCS inventory`, `PPG multi inventory`,
`Sound Designers`, `user`.

So the map is **16 columns × 100 vertical steps**, three of them (one per
oscillator), each with its own path.

**5. Anti-aliasing.** Same as WaveGenerator and slightly more explicit: the key
features list "Audio engine with **4 synthesis modes, and variable wave blending
quality**", with `Audio 1` / `Audio 2` ("especially with high pitches … softer
sound") / sub-audio, plus a `TCS-Mode` of `TCS` / `SMP` / `SMPL` where the sample
modes trade time control for freedom from "any artifacts the TCS may have". Not
characterised numerically — **partially verified**.

**A note on the separate `Sound Map`**, which is easy to confuse with the WaveMap
and is not the same thing: a 32-field grid (8 × 4, top-left is the `Base` field)
over which eight icons float — `Oscillator 1/2/3 Sound Source`, `Filter Parameters
and Envelope`, `Amplifiers and Envelopes`, `Noise Generators`, `LFO Parameters`,
`Oscillator Parameters` — with a `MORPHING` slider and `TOL` / `FRC`
(Tolerate / Force) buttons. It morphs **patch parameters**, not waves. Worth noting
as a separate idea: a second, coarser map over the whole patch, which is something
Kyklophoria's payload lanes gesture at.

### What PPG gets right, and where it stops

**Steal.** (a) **The path as the primary control object**, separable from the
content — "Any Path can be used with any Multi setup". Kyklophoria's rotation matrix
is the analogous separable object: one space, many traversals. Making the traversal
a saveable, swappable thing in its own right is a strong instrument idea. (b)
**Typed path segments** (`Linear Path` vs `Point to Point`) — the stepped/continuous
choice made per *segment* rather than per patch, the finest-grained version of that
switch in this survey. (c) **`Wave Offset` / `HORIZ` / `KEY`** — live *translation*
of a fixed path through the map, including key-tracked translation, which is a
real, shipped, non-trivial transform of the control frame. (d) **`BLEND` 0…1 as a
continuous hard-switch-to-smooth control** rather than a mode menu. (e) **Zones with
per-zone octave and gain trim** so heterogeneous material coexists — a lattice
assembled from mixed generators needs the same normalisation. (f) **Bitmap import**
with three named interpretations (WaveGenerator's Image Transformer: `Wave`,
`Spect D`, `Spect H`) — a 2-D source for a 2-D store, and unlike Falcon, PPG
actually traverses both axes; note that `Spect D` reads image rows as **spectra**,
which is directly usable by a magnitude-vector engine. (g) **`S-PATH`** — "saves a
new wavetable, which only contains the points which are in the current path", i.e.
**bake the traversal down to a flat table for export**. The analogue for us is
baking an orbit through a `.kyk` space into a 1-D wavetable other synths can load,
which is a cheap and very shareable feature. (h) **`Audio 1` / `Audio 2` as a
timbre choice, not a quality setting** — PPG exposes band-limiting as "softer,
better for high pitches" rather than as a hidden correctness knob.

**Where it stops.** Two dimensions, not N. And note that in **both** products the
2-D map is re-collapsed to 1-D at playback: an envelope drives a **scalar** position
along the path, so the player has one degree of freedom through a two-dimensional
field. The path's *shape* cannot be modulated at all — only its offset. The map is a **zoned grid**, not a
uniform lattice: the horizontal axis is a list of loaded resources, so "one step
right" is not a metric move. The path is **authored offline and fixed at play
time** — the only live transform is translation (`Wave Offset` / `HORIZ` / `KEY`),
never rotation, and there is no angle parameter anywhere. And the traversal is
driven by **one scalar** (an envelope walking the path), so the player has one
degree of freedom through a 2-D map, not two. There is no topology control
(no wrap, no sphere) and no payload travelling with position.

---

## Waldorf — the 64-entry heritage, Nave, Largo

### The inherited model (Microwave II / XT / XTk, and PPG before it)

Primary source: *MicroWave II • MicroWave XT • XTk User's Manual* (Waldorf), PDF
mirrored at [waldorfmusic.com downloads](https://waldorfmusic.com/nave-plugin-en/).
Quoted because every Waldorf software instrument inherits it, and because it is
the closest historical relative of Kyklophoria's sparse-lattice model:

> "A wavetable is a list made up of **64 entries**. Each entry represents one wave
> ... **A wavetable itself contains no wave data**, but is in fact a collection of
> up to 64 entries referencing up to 64 waves. **Not all entries of the wavetable
> have to contain entries.** When one or several sequential entries contain no
> reference, the MicroWave II/XT/XTk **calculates the waves for these locations
> automatically. The algorithm producing these 'imaginary' waves uses an
> interpolation scheme that crossfades the 'real' ones.** E.g. when a wavetable
> contains entries in entry 1 and 5, the positions 2 to 4 are generated based on
> interpolation between the existing waves in entry 1 and 5."

One correction to the folklore, from Waldorf's current
[M User Manual, p. 77](https://files.kraftmusic.com/media/ownersmanual/Waldorf_M_User_Manual.pdf):
the 64 slots are not all table. "You will notice, that the **upper three entries in
the wavetable (position 61, 62 and 63) consist of the classic analog type waveforms
triangle, pulse and sawtooth. These three waves are identical in every wavetable.**
You can always use these classic synthesizer waves, independent of which wavetable
is currently selected." The Microwave II parameter range prints as
`Startwave 00...60 / triangle / square / sawtooth`. So it is **61 table slots plus
three universal reserved waveforms**. Sparseness is a separate fact about those 61:
the [Microwave 1 plug-in
manual](https://www.lootaudio.com/_media/images/loot/waldorf/mw1/Microwave_1-Manual.pdf)
(pp. 20–21) says "Only a few positions are typically defined with waveforms. The
other ones are interpolated. **The first position 0 needs always to be defined**",
the minimum is five references, and "the biggest part of the ... wavetables contain
between 8 and 16 waves."

Three things to take from that. First, **the table is a list of references, not
content** — a wavetable is cheap and waves are shared. Second, **the stored table
is deliberately sparse and the gaps are filled by interpolation at load** — which
is precisely the `side^N` lattice argument (store few corners, interpolate the
rest), stated in 1997. Third, **reserving a few fixed, universal entries in every
table** (always-available triangle/square/saw) is a cheap way to guarantee a
familiar sound is reachable from anywhere in the space — worth considering for a
`.kyk` corner convention.

One architectural warning from the same lineage: on the Waldorf M "**both
oscillators of one M's voice use a common wavetable.** However each oscillator can
play a different waveform inside the list", and the Microwave II modulation
destination list contains `Wave 1 Pos` and `Wave 2 Pos` but **no destination for
wavetable select** — the table index cannot be modulated at all. That is the
opposite of what a player wants and is worth avoiding.

### Waldorf Nave

Primary source: *Nave Reference Manual* (Waldorf), PDF via
[waldorfmusic.com](https://waldorfmusic.com/nave-plugin-en/); product page lists
"Two independent advanced wavetable oscillators" plus an "Osc module with (up to)
8-oscillator Überwave".

**1. Table shape and limits.** The `Wave` parameter runs **0.0 … 64.0** — "This
parameter defines the startpoint of the selected wavetable ... A setting of 0
selects the first wave, the maximum setting selects the last wave of the
wavetable" — i.e. the inherited **64-wave table**, addressed **continuously**
(decimal values are accepted and the position draws as "a thin red line" on the
display). **Two** wavetable oscillators. Samples per wave not documented —
**unverified**.

**2. Interpolation and morphing.** Not documented directly; inherited crossfade
interpolation of unfilled entries is the Microwave behaviour. What Nave *does*
document is a genuinely spectral read: **`Spectrum`** (−1.00 … +1.00) "transposes
the spectrum of a sound, specifically the **spectral envelope** ... The default
setting is 0, where no transposition happens. **This is the behaviour of the
classic wavetable synthesis.**" With **`Keytrack`** (0–100 %): "The pitch doesn't
affect the spectrum, when Keytrack is set to 0%. This setting is recommend for
speech or singing, so that the formants are not influenced by the pitch. **Based on
this, we have included a speech synthesizer for wavetables.**" Plus `Noisy` ("adds
a noisy sound character ... The spectrum is unaffected") and `Brilliance` ("only
audible when Spectrum is transposed ... Higher settings result in narrow peaks").

**3. Named modes / traversal.** No warp-mode menu. Instead **`Travel`**
(−1.00 … +1.00): "Travel allows the **cyclic** moving through a wavetable. Positive
values allow a forward movement, negative values a backward movement ... Cyclic
means, that a wavetable starts automatically again from beginning when the end is
reached." With a `Clocked / Sync` switch — Clocked slaves Travel to host tempo
("The highest amount is 1024, where one turn needs 1024 beats"), Sync makes "all
triggered notes of a patch behave as a single triggered note. Travel is started
simultaneously for all triggered notes."

**4. Authoring — Nave's real distinction.** The **Wave Detail Section** is a
spectral region editor over the (harmonic × table-position) plane:
- `Partial` slider — "select a frequency area. Click on front or the back section
  of the slider to limit the frequency area."
- `Position` slider — "select a **wavetable area** for editing", limited the same
  way.
- `Amount` slider — "direct access to the spectrum of the selected area."

So the selection is a **rectangle in (partial, position)** and the operations act
on that rectangle: `Level`, `Expand/Contract` ("works nearly similar as contrast
setting"), `Permute` ("re-arranges the spectral components of the selected area up
to progressive chaos"), `Rotate Waves` ("moves the waves of the selected area in a
cyclic way. The part that leaves the one end will be pushed back at the other
end"), `Shift Waves` ("In opposite to Rotate, the level of the pushed back spectrum
is 0"), `Rotate Partials` ("moves the spectrum of the selected area to a new
position of the **frequency axis**"), `Shift Partials`, `Gyrate` ("rotates the
selected area"), `Random`. "Sequentially changes of different areas and different
edit options will be add up."

Tools menu: **`Talk`** — "allows you to enter one or more words with your computer
keyboard. These words will be **automatically synthesized as a new wavetable**";
**`Analyze Audiofile`** — "enables you to import a WAV file with any sample rate
and bit rate ... automatically synthesized as a new wavetable"; `Export Wavetable`;
and a `Load` button. Also a `Ribbon Band` for scrubbing/auditioning the table,
whose last position "will be automatically assigned to the Wave and the Travel
parameter".

**5. Anti-aliasing.** Not documented. **Unverified.**

**6. What to steal.** (a) **The 2-D *editing* selection.** Nave's `Partial` ×
`Position` rectangle is the closest thing in commercial software to editing a
spectrum-versus-position field, and it is exactly the shape of a Kyklophoria
lattice slice. Operations like `Rotate Partials` (cyclic shift along the harmonic
axis) and `Rotate Waves` (cyclic shift along position) are one-line operations on
our magnitude array and would be strong `kykspace` primitives. **`Gyrate` — "Gyrate
rotates the selected area" — is the only genuinely non-axis-aligned operation
Waldorf ships anywhere**, and it is an offline edit on the stored table, not a
playback traversal. Worth knowing: rotation as an *authoring* operation has
precedent; rotation as a *performance* traversal does not. (b) **`Spectrum` +
`Keytrack` as a decoupled formant axis** — a second continuous timbre axis that is
independent of table position and costs one multiply on the harmonic index; note
the manual's own reasoning that Keytrack 0 % is what makes speech tables work.
(c) **`Travel` with `Clocked`/`Sync`** — a cyclic scanner with tempo lock and a
"all voices move together" mode; the direct 1-D ancestor of our orbit LFOs, and the
`Sync` idea (one shared orbit phase across voices vs per-voice) is worth copying.
(d) **`Talk`** — text-to-wavetable, shipped in 2013, entirely offline and local
(contrast Vital's server-side TTWT).

**7. Where the 1-D table limits it.** One `Wave` axis per oscillator; the "3D
Wavetable display" is a rotatable *view* of a 1-D stack ("Click and hold on the
Wavetable display and move the mouse ... to turn the wavetable representation in
all three dimensions"), not a 3-D table — this is the clearest example in the
survey of "3D" meaning graphics, and the manual settles it by listing the display's
*rendering styles*: `Peaks`, `Smooth Plane`, `Hard Plane`, `Strips`, `Lines`,
`Cross Lines`, `Flow`, plus `cut peaks` and `color` sliders. 87 factory wavetables
(000 Resonant … 086 "Almost anything"). Note also that Nave has **no waveform or
harmonic drawing tool at all** — only the region-based spectral editing above. The `Partial × Position` rectangle is an *edit*
selection, not something the player can navigate; there is no per-position
addressing of the harmonic axis at play time.

### Waldorf Largo

Waldorf's own Largo manual link 404s and the current one is behind a JS-gated
Nextcloud share, so parameter naming specific to Largo is **unverified**; the
following is from official product pages plus the Blofeld manual, whose engine
Largo mirrors.

Three oscillators, of which **only 1 and 2 load wavetables** (oscillator 3 is
virtual-analogue only). Largo 1's official spec lists a strikingly small
complement — the Q `Alt 1` and `Alt 2` tables plus "a selection of waves from the
PPG and Waldorf Wave stored in two Wavetables", i.e. roughly four tables;
[Largo 2](https://waldorfmusic.com/largo-2/) closes the gap with "68 wavetables
sourced from Microwave, Wave, Q, and Blofeld synthesizers", matching the Blofeld.

The Blofeld convention is worth recording because it is an unusual answer to
"where does the position control live". From the
[Blofeld User's Manual, p. 37](https://files.kraftmusic.com/media/ownersmanual/Waldorf_Blofeld_Users_Manual.pdf):
"When a wavetable is selected, **the parameters Pulsewidth and PWM serve to select
the start point of the waves.** Furthermore, the modulation sources O1PW and O2PW
are active subject to which Oscillator is set to the wavetable. Please note that the
Wavetables are only available for Oscillator 1 and 2." So wave position rides on a
reused pulse-width slot — 1-D, continuous. The canonical Waldorf sentence is on the
same page: "A wavetable is a list with 64 waves, among which you can move at will."

Largo is the most conservative instrument in this survey: the classic 1-D model,
none of Nave's spectral axis, none of PPG's grid or path, no authoring at all — it
plays factory tables only. Anti-aliasing **unverified**. It adds nothing to the
dimensionality question.

---

## Tone2 Icarus — what "3D wavetable synthesizer" actually means

Included because Icarus is the product that markets the dimensionality claim most
directly, and the answer matters for ours. Primary source: *ICARUS 2 Manual*
(Tone2).

Verbatim, twice: "We call Icarus a **'3D wavetable synthesizer'**, because we
expanded the wavetable concept, **allowing you to cross-blend waveforms, with an
additional dimension for morphing**." And, in the Morphmodes chapter: "The morph
modes are different playback modes for the wavetables. The morph knob varies the
way how the morph mode affects the wavetable. **Next to the WAVE knob it provides
another dimension** to change the oscillator's sound ... All morph modes are
processed in realtime ... you can route any modulation source to MORPH and WAVE
within the matrix."

So Icarus's third dimension is **`WAVE` (table position) + `MORPH` (depth of one of
64 named morph modes)** — a 1-D table plus a modifier axis, i.e. the same
architecture as Vital's spectral morph and Serum 2's warp, marketed as 3-D. The
morph modes the manual's overview lists are "Waveshaping, time-stretching,
pitch-shifting, granulizing, stacking, PWM, formant shifting, BPM syncing, phase
distortion, reversing, ringmodulation, sync, rearranging, FM, spectral editing,
looping, denoising, mixing"; the menu itself reads `None/Partial`, `PW`,
`PhaseDist Squ`, `PhaseDist X2`, `Mirror`, `Bandlimit`, `Ringmod`, `Bandpass`,
`Harmonic Key`, `Hollow1–4 Key`, `formant`, `granulize`, `time-stretch`,
`FM<-Osc1`, … — i.e. a **DSP effect list**, which is the point. A full verbatim
enumeration of all 64 was not extracted — **unverified**. Three oscillator blocks, each up to 10 detuned hypersaw
oscillators; resynthesis is a one-click "select a wav file — wait for some seconds
and Icarus will automatically create a patch"; there is a wavetable editor with
`Wave / Spectrum / Phase` display modes.

**Why it matters here.** Icarus is the strongest counter-example to check before
claiming novelty, and it does not survive contact with its own manual: the "extra
dimension" is a process parameter, not a stored coordinate. Anyone assessing
Kyklophoria will raise Icarus; the answer is the sentence above, quoted.

---

## Adjacent software: morph pads, vector synthesis, and wave terrain

None of these is a wavetable synth in the Serum/PPG sense, but each is the sort of
product someone will raise against a novelty claim, so each is settled here.

### Camel Audio / Apple Alchemy — a 4-source mix surface, not table navigation

Primary source: [Logic Pro User Guide, Alchemy morph
controls](https://support.apple.com/guide/logicpro/morph-controls-lgsib359659f/mac).
Four named modes, verbatim: **`XFade XY`**, **`XFade Linear`**, **`Morph XY`**,
**`Morph Linear`**. The mechanism is a mix: "Use the X knob to control the mix
levels of sources A and C versus sources B and D." Four sources, two coordinates.
Two caveats worth having: with the **Elements** button on there are five
independent X/Y points (Additive, Spec/Gran, Pitch, Formant, Envelope) — ten morph
coordinates in total, but that is **five separate 2-D morphs over the same four
corners**, not one table with three indices. And `Morph Linear` traverses a
**fixed A→B→C→D path** through the square, not a user-rotatable diagonal.
Verdict: **2-D maximum, mix surface, no oblique control.**

### Korg Wavestation / wavestate and the Prophet VS — vector synthesis

Primary source: [Korg wavestate
manual](https://cdn.korg.com/us/support/download/files/629595d96c362967378a6d6b8ce253ec.pdf):
"Vector Synthesis … works by moving around a point on a two-dimensional plane, both
left-right and up-down … positioned on two different lines at once: a left-right
line (the X axis), and an up-down line (the Y axis)." Two coordinates over a
**four-source mixer**, not a table index — so it fails the dimensionality question
twice over.

But it is the **strongest historical precedent for oblique traversal**, and honesty
requires saying so: the original Wavestation SysEx spec stores `Mix_X0..Mix_X4` and
`Mix_Y0..Mix_Y4` — **five arbitrary (X, Y) breakpoints**, so every Vector Envelope
segment is a diagonal of arbitrary angle. wavestate adds: "When the Vector Envelope
is in use, the Vector Joystick offsets the Vector Envelope position by up to halfway
across either axis" — i.e. a live **translation** of an authored diagonal path,
exactly as in PPG WaveMapper. The Prophet VS has the same architecture (four
oscillators A–D on two axes, a five-stage mix envelope with X–Y endpoints); its
owner's manual is an image-only scan with no OCR layer, so the **verbatim wording is
unverified** though the dimensionality is not in doubt.

### Audio Damage Continua — the one genuine 3-parameter timbre space in software

Primary source: [Continua
manual](https://audio-damage-inc.github.io/manuals/Continua_Manual.pdf). It
disqualifies itself from the wavetable question outright: "Rather than using fixed
tables of waves, these oscillators mathematically calculate signals from scratch,
in real time." But its `Shape` / `Skew` / `Warp` knobs are genuinely
interdependent — "the Skew knob has a different effect depending on the settings of
the Shape and Warp knobs" — so Continua is **one waveform function of three
continuous coordinates**.

This is the sharpest test of how we word our claim. Continua qualifies if the
criterion is "3+ continuous coordinates into one timbre space"; it does not qualify
if the criterion is "**one stored table indexed by 3+ coordinates**". Kyklophoria's
claim must be the second, and should say so explicitly: our space is *stored
content* that can be authored, imported and browsed, not a closed-form function of
three knobs.

### Wave terrain synthesis — where rotation actually does ship, in software

This is the closest existing art to the rotation idea and must be cited carefully.

- **Terrain** (Aaron Anderson), free and open source, VST3/AU —
  [github.com/aaronaanderson/Terrain](https://github.com/aaronaanderson/Terrain).
  Verbatim: "In wave terrain synthesis, a sound is produced via a **2D trajectory
  scanning over a 3D surface, or terrain**"; "The peaks and troughs traversed by
  the trajectory determine the output; **translation** adjusts which peaks and
  troughs are traversed"; and, decisively, "**changes in phase and the balance of
  harmonics may be heard as the trajectory is rotated**." It also ships
  **`Meanderance`**, which lets "trajectories displace about the terrain on their
  own accord. Both the speed and the scale of this meanderance can be controlled
  and automated" — an autonomous wandering traversal, conceptually adjacent to our
  orbit LFOs.
- **Scaler Music Carbon Electra 2** (2026) —
  [product page](https://scalermusic.com/products/carbon-electra-2/): "What's the
  next evolution of the wavetable? Carbon Electra's Wave Terrain synthesis
  **calculates a 3D surface over which a 2D harmonic curve moves and modulates**."
  Four-oscillator engine with "classic, wavetable and Waveterrain oscillators", and
  "Upload PNG images to create your own Waveterrain oscillators."

**What this does and does not concede.** Rotating a traversal path over a timbre
surface *has* shipped in software. But wave terrain is a **2-D trajectory over a
computed (or image-derived) 3-D height field**: the surface is a function
z = f(x, y), read at audio rate along a closed path, and the rotation is of a 2-D
path in a 2-D domain — a single Givens rotation in the one available plane. It is
not a rotation of an N-dimensional *control frame* before indexing a stored
lattice, there is no per-axis topology, and the "third dimension" is the output
value, not a third coordinate. Our claim has to be worded to survive this, and the
version in the cross-cutting section below is.

### Everything else checked and cleared

| Product | >2-D stored timbre space? | Oblique/rotated traversal? | Source |
|---|---|---|---|
| Tone2 Nemesis | no | no | [tone2.com](https://www.tone2.com/nemesis.html) — makes no dimensional claim |
| NI Absynth 5 | no | no | [manual §7.5](https://docs.native-instruments.com/pdf-guides/Absynth_Manual_English.pdf) — "The Wave Morph function allows you to dynamically blend two waveforms into a new shape… determined by the position of the Morph Mod parameter." 1-D between two endpoints |
| NI Form | no | no | 1-D sample scan position |
| NI Kontour | no | no | two sine oscillators with PM + waveshaping, no table; its Motion Recorder is generic automation |
| NI Razor | no | no | additive, "up to 320 partials"; its own copy boasts "beautiful 3D graphics" — graphics |
| NI Skanner XT | no | no | scanned synthesis, 1-D scan of a modelled string |
| Madrona Aalto | no | no | [madronalabs.com](https://madronalabs.com/products/aalto) — "made with dynamic calculation, not static wavetables" |
| Madrona Kaivo | no (2-D, and X is time) | no | [manual](https://madronalabs.com/media/kaivo/KaivoManual.pdf) — granulator "X/Y offset sliders… X affects their place in time, and Y affects their position in the stack of audio channels", Y capped at 4 channels. Worth noting for a different reason: Kaivo ships **`LFO 2D`** — "'2D' stands for 'two-dimensional'… LFO 2D traces shapes in two dimensions and outputs [both]" — the closest shipped diagonal *generator* in software, but a patchable modulator, not an oscillator control |
| iZotope Iris 2 | no | no | [manual](https://help.izotope.com/docs/izotope-iris-2-reference-manual.pdf) — spectrogram-drawn spectral filter; a 2-D mask, not a traversal |
| Synclavier | no | no | [Vol. 2 Sound Design](https://www.synclavier.com/wp-content/uploads/Volume-2-Sound-Design.pdf) — timbre frames are a **time-ordered crossfade chain**, not a coordinate space: "Like the separate frames of a motion picture, each timbre frame plays back in order when you press a key." No knob or CV indexes frame position |
| Spectrasonics Omnisphere 2/3 | no | no | [oscillator page](https://support.spectrasonics.net/manual/Omnisphere2/25/en/topic/layer-page-oscillator-index) — wavetable is a discrete menu pick, then Shape/Symmetry/Hard Sync reshape it; the Orb is a 2-D performance pad |
| Applied Acoustics Chromaphone 3 | no | no | modal physical modelling, no wavetable |

---

## What to steal — consolidated

Ranked by value to Kyklophoria, with the milestone each lands in.

1. **Named interpolators with an honest CPU ranking.** Hive's four —
   `switch` / `crossfade` ("smoothly interpolates waveform magnitudes") /
   `spectral` ("also interpolates the phases of each partial. CPU-hungry!") /
   `zero phase` ("forces the phase of each partial to zero first") — with the
   manual saying outright which is expensive and which to prefer. Serum's Morph
   menu is the same four ideas. **Our magnitude-only store with seeded phases *is*
   `zero phase`**; the docs should say so, and at minimum a `step` vs `smooth`
   switch should be exposed per space. **M2/M6.**
2. **Phase alignment as named, one-click import fixes.** Phase Plant names three
   distinct strengths — `Align Fundamentals` ("sets the phase of the first harmonic
   in all frames to be identical to the selected frame"), `Align All Phases`,
   `Align Frames` ("phase-shifts each frame **without changing the phase ratio of
   its harmonics**, so that they get maximum correlation") — with the stated
   purpose of making *position modulation* behave. Ableton does it silently at
   load; Bitwig exposes it as `Aligned` / `Diffuse` / `Original`. Every product in
   this survey solves this problem, and an N-D lattice has more neighbour
   directions in which to go incoherent. **M4 (WAV import).**
3. **Per-edge morph rules.** Zebralette 3: `Crossfade`, `Point By Point`,
   `Closest X`, `Closest X & Y`, `Peaks & Valleys`, set *per adjacent curve pair*,
   plus **Morph Vectors** (drag a point to make it "pretend" to be elsewhere for
   the morph calculation) and `Ease In / Out`. Hive's UHM has the same idea offline
   (`Interpolate Type=morph1|morph2` with `Snippets`, `Threshold`, `Weighting`).
   Nothing else lets you author *what happens between* stored points. Even one
   eased-vs-linear bit per axis is a real instrument change on a lattice, and costs
   a curve evaluation on the fold fraction. **M2.**
4. **A text authoring format with separate time / magnitude / phase parsers.**
   Hive's UHM: `Wave`, `Spectrum` ("magnitudes of frequency domain"), `Phase`, each
   with `Start=`/`End=` frame ranges, `Blend=` (replace/add/sub/multiply/min/max…),
   `Direction=`, and `Target=main|aux1|aux2`; plus `Import`, `Move`, `Interpolate`,
   `Normalize` (`Metric=RMS|Peak|Average|Ptp`), `Envelope`, seeded
   `rand`/`randf`/`rands`, arbitrary frame accessors `main_fi(frame,index)`, and
   in-formula `lowpass()`/`bandpass()`/`highpass()` — **stored in presets by
   reference**, so a table is kilobytes of text regenerable at any resolution.
   `tools/kykspace` should be UHM generalised to N indices. **M4.**
5. **A stepped/continuous switch on the position axis.** Pigments' `Morph` button
   (governing knob *and* modulation), Serum 2's right-click `Smooth Interpolation`,
   Bitwig's disable-interpolation option, Hive's `switch`, PPG WaveMapper's per-
   segment `Linear Path` vs `Point to Point`, and its continuous `BLEND` (1 = hard
   switching, 0 = smoothest). One control, large character change, near-zero cost.
   **M2.**
6. **Position spread across voices.** Serum's Unison WT Pos and Unison Warp;
   Ableton's `Position spread` and `Random note`; Falcon's `WAVE SPREAD` ("sets the
   range of Wave Index values for each voice"); Vital's `kUnisonFrameSpread` and
   `kUnisonSpectralMorphSpread`; Bitwig's stereo `Table Index`. Our stereo pair
   spreads only rotation angle (spec §9.1); a per-axis ± spread **vector** is the
   N-D generalisation and is nearly free. **M1/M2.**
7. **A built-in scanner on the position axis.** Hive's `WT Auto Mode`
   (`One Shot` / `Loop >` / `Loop ><`) with `Tempo`, `Reverse` and `Cyclic`;
   Nave's `Travel` with `Clocked` (tempo-locked, "one turn needs 1024 beats") and
   `Sync` ("all triggered notes of a patch behave as a single triggered note").
   The simplest gesture should not cost a modulation slot, and Nave's `Sync` —
   one shared orbit phase across voices versus per-voice — is a decision our orbit
   LFOs must make anyway. **M2.**
8. **Render rate as an exposed parameter with a documented trade.** Zebra2's
   `Resolution` (4 s down to <1 ms, plus the honest note that "low resolution can
   actually make transitions smoother"); Zebralette 3's **200 / 800 / 2000 Hz**;
   Ableton's "modulation is calculated every 32 samples"; Phase Plant's audio-rate
   vs 64-sample control rate. **Kyklophoria's block rate at 48 kHz / 24 samples is
   exactly 2000 Hz — u-he's top setting.** That is independent confirmation that
   the M0 baseline is right and that `render_div` = 2 or 4 (1000 / 500 Hz) stays
   musical. Zebralette 3 also separates *precision* from *rate*
   (`Maths` = `Precise`/`Fast`/`Rough`); two orthogonal knobs is the right shape
   for an embedded target. **M1/M6.**
9. **Order each axis by a spectral property.** Serum 2's SORT menu: peak frequency
   bin, spectral centroid, overall peak, spectral spread, highest bin containing
   spectra, fundamental energy — plus Serum 1's rationale, that reordering makes
   travel "feel like you are traveling in a straight line, instead of some zig-zag
   fashion, spectrally speaking". An N-D lattice needs each axis monotone in
   *something*, and this is the shortlist. **M4 (generators).**
10. **Cheap, legible spectral operations as generative axes and payload lanes.**
    Vital's `Formant Scale`, `Harmonic Stretch`, `Inharmonic Stretch`, `Smear`,
    `Phase Disperse`; Zebra2's `Odd for Even`, `Brilliance`, `Registerizer`,
    `Formanzilla`, `Scale`, `ChopLift`, `Smear`; Zebralette 3's `Spectral Decay`
    ("high values along the curve mean longer decay"), `Sparse`, `Log Clusters`
    ("distributed to ensure equal energy across the spectrum ... 10 clusters ...
    spaced precisely octaves apart"); Nave's `Rotate Partials` / `Shift Partials`.
    All are a few operations on a magnitude vector. **M3/M4.**
11. **Reshape as a parameter.** Hive's `Tables`, which folds a 256-frame list into
    an n × 256/n grid — "some would even say three-dimensional" — with the
    deliberate, documented weirdness of non-divisible values. Changing the fold
    changes which cells are neighbours: a rotation-adjacent gesture for the cost of
    an integer.
12. **Import conventions that need no metadata**, and 2-D sources. Frame size in
    the filename (Hive's `-WT512`, Falcon's `_128`); Bitwig honouring the WAV `clm`
    chunk; Falcon's and PPG WaveGenerator's **image import** (Falcon: "each row of
    pixels as the wave cycle, with one slice per row ... brightness to amplitude").
    An image is already a 2-D array — the natural authoring surface for a 2-D slice
    of an N-D lattice, which is exactly what Falcon's importer *cannot* traverse.
    **M4.**
13. **Nave's `Partial` × `Position` edit rectangle.** Select a harmonic range and a
    table range, then apply `Level`, `Expand/Contract`, `Permute`, `Rotate Waves`,
    `Shift Waves`, `Rotate Partials`, `Shift Partials`, `Gyrate`, `Random` — the
    closest commercial thing to editing a spectrum-versus-position field, and the
    exact shape of a `.kyk` lattice slice. **M4.**
14. **The traversal as a separable, saveable object.** PPG WaveMapper: "the Path is
    part of the sound program ... **Any Path can be used with any Multi setup!**"
    Kyklophoria's rotation `R` is the analogous object — one space, many traversals
    — and making it saveable and swappable independently of the `.kyk` file is a
    strong instrument idea. **M2/M6.**

## Gaps Kyklophoria can fill

1. **A stored table with more than one position axis.** Almost every product
   surveyed stores a 1-D list. Hive 2 gets a second axis by *folding* the same
   256-item list — no extra content, the grid is always `n × 256/n`, and the y-axis
   is restricted to crossfade. PPG's WaveMap is a genuine 2-D layout, but its
   horizontal axis is a list of loaded resources (zones), so "one step right" is
   not a metric move. Kyklophoria's `side^N` lattice stores a genuinely
   N-dimensional corpus with the *same* interpolator on every axis.
2. **Interpolating between tables, not mixing them.** The universal workaround for
   "one axis isn't enough" is a second oscillator and a crossfade — Serum 2 (three
   oscillators), Ableton (two), Pigments (two engines), Falcon and Phase Plant
   (layers), Bitwig (unlimited Grid modules but, in its own documentation, no
   primitive for interpolating between tables). Mixing two points is not reading
   the point between them: mixing gives you both spectra at once, interpolation
   gives you neither.
3. **A modifier axis that varies with position.** Every warp/morph/spectral-morph
   stage in this survey is 0-D — a scalar applied to whatever frame you landed on.
   Kyklophoria's payload vector *is* per-hyperpoint, so cutoff, FM index, drive and
   CV out are functions of position by construction (spec §3.1, §3.5). Nobody else
   has this.
4. **Per-axis topology.** Only Hive's `Cyclic` ("adds a copy of the first frame to
   the end of the table ... lets loops cross wavetable boundaries") and Nave's
   `Travel` address wrap at all, and each is one behaviour on one axis. Massive's
   manual documents the clamp artefact precisely — spread voices "will not be able
   to follow this move" — without offering a fix. Kyklophoria has `Clamp` / `Wrap`
   / `Sphere`, mixable per axis (spec §3.3).
5. **Rotation of the control frame.** See below.

---

## Cross-cutting question: has any commercial software synth shipped a genuinely 3-D-or-higher wavetable space, or an oblique/rotated traversal?

**Three or more dimensions: no.** No software synthesizer in this survey stores a
wavetable in three or more dimensions. The recurring "3D" label is always a
*display*: Serum's 3D view of its frame stack ("The 3D view, in contrast, displays
all frames"), Pigments' 3D visualiser (the manual: "shows the wavetables in two or
three dimensions ... the 3D view has the advantage of showing you all the different
waveforms in the current table"), Nave's rotatable "3D representation of the
selected wavetable", Ableton's linear/polar projections, Bitwig's "tilted 3D layout"
in the browser, Falcon's waterfall, and Vital's `wavetable_3d` renderer class. In
every case the store underneath is a list. The one vendor who even flirts with the
word about the *data* is u-he, and only as an aside — Hive's "two-dimensional (some
would even say three-dimensional)" is describing a 2-D fold.

The one product that markets the claim outright is **Tone2 Icarus**, "a '3D
wavetable synthesizer', because we expanded the wavetable concept, allowing you to
cross-blend waveforms, **with an additional dimension for morphing**" — and its own
Morphmodes chapter defines that dimension as the `MORPH` knob: "Next to the WAVE
knob it provides another dimension to change the oscillator's sound." That is a
1-D table plus a modifier depth, not a 3-D store.

**Exactly two dimensions: yes, and there are two distinct kinds.**

- **u-he Hive 2** — a **fold** of a 1-D store. Verbatim: "Hive's wavetables become
  two-dimensional (some would even say three-dimensional) if the `Tables`
  parameter, which specifies the number of frames in the y-axis, is set to 2 or
  more. The lower `Position` knob then crossfades between frames in the y-axis."
  Two independent, modulatable position knobs (`Position` and `Multi Position`),
  both strictly axis-aligned; the second axis is second-class ("the interpolation
  through Multi Position is always crossfade"); total cells still cap at 256.
- **PPG WaveGenerator and PPG WaveMapper / WaveMapper 2** (Wolfgang Palm) — a
  genuine 2-D **layout**. WaveGenerator: "**256 waves assembled within a wave
  grid**", "**16 rows with 16 places each**", with "**3 oscillators** ... arbitrary
  and independent access". WaveMapper 2: columns are zones of loaded resources and
  "the vertical resolution is always 100 steps regardless of the material loaded in
  the zones".

**Oblique traversal: yes, in three places — and none of them is a rotated control
frame over a stored N-D table.** Taken in order of how close they come.

**(i) PPG's Path.** In both PPG instruments the player does not turn a position knob; they **draw a
path across the 2-D grid** and an envelope walks it. WaveGenerator: "The way you
access these waves depends on the envelope settings and **a path which connects
single waves out of the grid**", with grid modes including `Linear` ("linear paths,
which run through all points of one column" — axis-aligned) and `Path` (a free
polyline — oblique). WaveMapper 2: "you design a **Path**, which is the way the
synthesizer plays the material over time ... you see a green light-point that shows
the exact position the oscillator is playing", with typed segments ("The stroked
line stands for a **Linear Path**, and the normal line for a **Point to Point**
path segment"), and the path is separable from the content ("**Any Path can be used
with any Multi setup!**"). It can also be **translated at performance time**: "The
`Wave Offset` parameter shifts the path you have setup inside the Wave Map in a
vertical direction. When the offset is higher than 16, the vertical shift is reset
but you get an additional horizontal shift", plus `HORIZ` and key-tracked `KEY`
offsets.

A diagonal path across a WaveMap **is** a non-axis-aligned traversal of a
wavetable space, and it ships. That is real prior art and should be cited as such.
What it is not:
- **2-D, not N-D.**
- The path is **authored** — a fixed polyline saved with the program — not a
  transform computed from live control inputs.
- The only live transform is **translation**, never rotation: there is no angle
  parameter, no matrix, no way to make an axis-aligned CV sweep a diagonal, and no
  quasi-periodic orbit.
- The traversal is driven by **one scalar** (an envelope walking the path), so the
  player has one degree of freedom through a 2-D map, not two.

**(ii) Vector synthesis — the Wavestation / Prophet VS Vector Envelope.** The
original Wavestation SysEx spec stores `Mix_X0..Mix_X4` and `Mix_Y0..Mix_Y4` —
**five arbitrary (X, Y) breakpoints**, so every envelope segment is a diagonal of
arbitrary angle, and wavestate lets the joystick offset the whole envelope position
live ("up to halfway across either axis"). Architecturally identical to PPG's
translatable path. But the plane is a **four-source mixer**, not a table index: you
are crossfading four oscillators, not reading a point in a field.

**(iii) Wave terrain synthesis — where the word "rotate" actually appears.** Aaron
Anderson's open-source **Terrain** plugin states it outright: "changes in phase and
the balance of harmonics may be heard **as the trajectory is rotated**", alongside
`Meanderance`, which lets "trajectories displace about the terrain on their own
accord" with controllable speed and scale. Scaler Music's **Carbon Electra 2**
(2026) ships the same idea commercially: "Carbon Electra's Wave Terrain synthesis
calculates a 3D surface over which a 2D harmonic curve moves and modulates." This
is a genuine rotated traversal of a timbre surface in shipping software and **must
be acknowledged**. It differs from Kyklophoria in four specific ways: the domain is
2-D, so there is exactly **one** rotation plane rather than N(N−1)/2; the "3-D" is
the output height z = f(x, y), not a third coordinate; the terrain is a computed or
image-derived function rather than a stored, authored, importable corpus; and there
is no per-axis topology, no payload travelling with position, and no bandlimited
spectral read.

**Rotation of an N-dimensional control frame before indexing a stored lattice —
not found.** No product in this survey applies a rotation to N > 2 control
coordinates before indexing a table, and none exposes N(N−1)/2 rotation angles with
independent orbit rates as performance parameters. Remaining near-neighbours:
- **Zebra 3's 4in1 Vector mode** — handles A–E on an XY square, auto-traversed
  A→B→C→D→E with per-leg time multipliers, `Loop Mode` options (`ABCDE`, `BCDE`,
  `ABCDEDCBA`, …) and live `X Mod` / `Y Mod`. This is an auto-traversed 2-D path,
  the closest thing to an orbit in the survey — but it **mixes four signals**, it
  does not index a space. The same is true of the whole Prophet VS / Wavestation
  vector-synthesis lineage.
- **Bitwig's `XY` modulator** — "Four control sources derived from one continuous
  XY control", patched wherever you like. A 2-D controller and a patch, not a
  rotation of a table's coordinates.
- **Madrona Kaivo's `LFO 2D`** — "'2D' stands for 'two-dimensional' … LFO 2D traces
  shapes in two dimensions and outputs [both]". The closest shipped *diagonal
  generator* in software, but a patchable modulator rather than an oscillator
  control — closer to our orbit LFOs than to our rotation matrix.
- **Vital's spectral-morph axis and Serum 2's dual Warp** — genuinely orthogonal
  second knobs, but they move through a space of *transformations*, not of stored
  timbres.
- **Audio Damage Continua** — three interdependent continuous coordinates
  (`Shape` / `Skew` / `Warp`) into one timbre, but explicitly *not* a table:
  "Rather than using fixed tables of waves, these oscillators mathematically
  calculate signals from scratch, in real time." It is the reason our claim must
  say **stored table**, not just "three parameters".

**The claim we can defend.** *No commercial software wavetable synthesizer stores a
timbre table indexed by more than two coordinates. The only two-dimensional cases
are u-he Hive 2, which folds a 256-frame list into an n × m grid with a
crossfade-only second axis, and Wolfgang Palm's PPG WaveGenerator / WaveMapper,
whose 2-D wave grid is traversed by an authored path that can be translated but not
rotated. Oblique traversal itself is not new — PPG's path, the Wavestation Vector
Envelope's arbitrary X-Y breakpoints, and wave-terrain plugins that rotate a
trajectory all ship it — but all three are two-dimensional, which admits a single
rotation plane, and the last two traverse a mixer or a computed surface rather than
a stored corpus. Applying a product of Givens rotations to N > 2 control
coordinates before indexing a stored lattice — so that one CV sweeps a diagonal no
axis reaches, and several slow rotation LFOs trace quasi-periodic orbits — has no
precedent we could find in software.*

**One more thing to say out loud, because it is the actual shape of the gap:** no
product with a genuinely multi-dimensional table ships a rotation control, and no
product with a rotation control has a multi-dimensional stored table. That
disjunction, not either half alone, is what Kyklophoria closes.

**Hardware is a different answer, and this file must not be read as covering it.**
A parallel pass over Eurorack found genuine 3-D wavetable cubes shipping —
Industrial Music Electronics **Piston Honda mk2/mkIII** (16³ / 8³, with the mkIII
manual explicit that Z interpolates rather than bank-selects), Mutable
**Plaits'** wavetable engine (4 banks × 8 × 8, with trilinear interpolation
verifiable in `wavetable_engine.cc`), and **Ferry Island Four Seas** (8³, 2025,
built on Daisy Seed with open firmware) — and one literal rotation control.

That last one deserves its exact words, because it is the single strongest
precedent anywhere and our wording has to survive it. **Conductive Labs Terrain
Synth** (a standalone hardware synth, not a Eurorack module),
[manual v0.13 p. 28](https://conductivelabs.com/wp-content/uploads/2026/05/The-WTS-User-Manual-v0.13.pdf):
"**`ROTATE`: (Rotation About the X,Y Position) [−180, +180 degrees] — This rotates
the path from −180 degrees to 180 degrees (a complete circle). Rotating a path can
add a nice timbre effect to the waveform especially when LFO'd.**" Sibling path
parameters are `SHAPE` (18 shapes — Line, Ellipse, Triangle, Square, Hourglass,
Star, Hazard, Segments, Lissa Joule 2–5, Cardioid 1–3, Rose 3–5), `SIZE`, `W:H`
(eccentricity), `POS X`, `POS Y`, `PHASE`, `MANGLE`. Its own framing sentence is the
best one-line statement of the thesis found anywhere, and it is the manufacturer
saying it: "**This is a super-set of wavetable synthesis, where the path is always a
straight line that can only be moved in one way and the terrain is frozen.**"

"Especially when LFO'd" means the rotation is modulatable, so **any unqualified
"nobody modulates a rotation" phrasing is dead.** Three distinctions survive, and
they are the ones to use — note that *orthogonality* is not among them, since a 2-D
rotation is orthogonal by construction:
1. The domain is **2-D**, so there is exactly one rotation plane and no
   plane-selection problem. A product of Givens rotations over N = 4–6, with a
   static angle and an orbit rate per plane, is a different object rather than a
   bigger version of the same one.
2. It rotates the **readout path within** the space. It does not transform incoming
   position CVs before indexing — there is no control frame being rotated, because
   the player is not navigating by CV coordinates in the first place.
3. The surface is **computed, not stored** ("17 math-based terrains"), and z is
   output height, not a third index — so there is nothing for a rotated frame to
   index.

These belong in `docs/lit/eurorack.md` and must be read alongside this file before
any novelty claim is made. **No product in either survey has both a
higher-than-2-D stored table and a rotation control**, which is the actual gap.

**What was checked here.** Verified against primary sources and found negative:
Serum 1 and 2, Vital, Massive, Massive X, Ableton Wavetable, Pigments, Bitwig's
Grid `Wavetable` module, Zebra2/Zebralette, Zebralette 3/Zebra 3, Phase Plant,
Falcon, Waldorf Nave and the Microwave 64-entry model, Tone2 Icarus, Tone2 Nemesis,
Apple/Camel Alchemy, Absynth, Form, Kontour, Razor, Skanner XT, Aalto, Kaivo,
Iris 2, Omnisphere, Chromaphone, Synclavier, Korg Wavestation/wavestate, Audio
Damage Continua. Verified positive at 2-D: Hive 2 (fold), PPG WaveGenerator and
WaveMapper (grid + path), Alchemy and the vector-synthesis lineage (4-source mix
surfaces). Only the Prophet VS owner's manual could not be quoted — the scan has no
OCR layer, so its **verbatim wording is unverified**, though its architecture is
not in doubt.

**Two things that would falsify a sloppier version of this claim**, both settled
above and both worth knowing before anyone says "nobody has done this":
Audio Damage **Continua** is a genuine three-continuous-parameter timbre space (but
computed, not stored — "Rather than using fixed tables of waves, these oscillators
mathematically calculate signals from scratch, in real time"); and **wave terrain
synthesis** ships trajectory rotation in software (Aaron Anderson's Terrain:
"changes in phase and the balance of harmonics may be heard as the trajectory is
rotated"), but over a 2-D domain, which admits only one rotation plane, with no
stored lattice and no per-axis topology.

Keep the qualifier "we could find". This file covers software only; the hardware
side of the claim is in `docs/lit/eurorack.md`, and the academic side in spec §10.
