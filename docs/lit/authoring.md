# Filling an N-dimensional space of spectra — a literature review

*Kyklophoria lit review, area: authoring / content generation. Written 2026-09-08.*

## The problem

Kyklophoria's table is a lattice of `side^N` hyperpoints (default N=4, side=4 → 256
cells; up to N=6). Each cell is K=64 harmonic magnitudes plus P=8 payload floats;
phases are global, derived once from `phase_seed`, so a cell is a *magnitude
envelope over harmonics 1..64* and nothing else (docs/space-format.md). At runtime the
engine multilinearly interpolates the 16 corner magnitude vectors once per block and
IFFTs.

Nobody is going to hand-draw 256 spectra, and the one generator we ship
(`core/kyk_gen.h`, the Harmonic family) makes each lattice axis one synthesis
parameter — shape, tilt, formant, hollow, comb2, comb3. That is legible but it has a
specific failure mode that matters for *this* instrument: **if axis `a` is exactly
"formant position" and axis `b` is exactly "spectral tilt", then any diagonal through
the space is just a simultaneous move in formant and tilt, and there is nothing at
45° that is not already a blend of what is at 0° and 90°.** The rotation feature
(spec §3.4) is the novel part of the instrument, and rotation only pays if the space
is *not* a separable product of independent parameters. What we want is a space whose
covariance structure is roughly isotropic — where every direction is as likely to be
interesting as every other, and where distance in the space corresponds to
distance in timbre — filled automatically. This document surveys the techniques that
could produce such a space, and rates each on: can it be baked **offline** into a
regular lattice of 64 magnitudes; what does it cost on a desktop; and does it need
anything at **runtime** on the STM32H750.

Everything below is judged against three hard constraints: the payload is 64
non-negative magnitudes (no phase, so no time-domain detail survives); the space is a
regular grid, so any method must be *resamplable on a grid*, not just *samplable*;
and the module has no ML runtime, so a learned model must be evaluated offline and
baked.

## 1. Wavetable generation techniques and corpora

Storage sanity check for everything below: 256 cells × 64 float32 = **64 KB**, and the
offline conversion is always the same recipe — render one cycle at length L, `rfft`,
take `abs(bin[1..64])`, normalise. That is literally what WaveEdit does internally
(`harmonics[i] = hypotf(re, im) * 2.0` in
[`src/wave.cpp`](https://raw.githubusercontent.com/AndrewBelt/WaveEdit/master/src/wave.cpp)),
so the pipeline is field-proven, not speculative.

### 1.1 WaveEdit (Synthesis Technology / Andrew Belt)

Desktop editor for the E352/E370 Eurorack wavetable oscillators, GPL-3.0,
[github.com/AndrewBelt/WaveEdit](https://github.com/AndrewBelt/WaveEdit), product page
[synthtech.com/waveedit](https://synthtech.com/waveedit/).
[`src/WaveEdit.hpp`](https://raw.githubusercontent.com/AndrewBelt/WaveEdit/master/src/WaveEdit.hpp)
fixes `WAVE_LEN 256`, `BANK_LEN 64`, `BANK_GRID_WIDTH 8`, `BANK_GRID_HEIGHT 8` — **a
bank is literally an 8×8 grid of 256-sample waves**, and the
[manual](https://raw.githubusercontent.com/AndrewBelt/WaveEdit/master/doc/manual.html)
confirms the Grid XY page previews two-dimensional morphing across it. That is
Kyklophoria at N=2, shipped in 2016, and it is the closest existing product analogue.

The part worth stealing is the **Effect page**, not the editor.
`enum EffectID { PRE_GAIN, PHASE_SHIFT, HARMONIC_SHIFT, COMB, RING, CHEBYSHEV,
SAMPLE_AND_HOLD, QUANTIZATION, SLEW, LOWPASS, HIGHPASS, POST_GAIN }` — twelve
continuous scalars per wave, each with a per-bank "Average" slider plus
Randomize/Bake. `Wave::updatePost()` implements most of them in the Fourier domain
(phase/harmonic shift = phase rotation, comb = FFT-domain kernel multiply,
LOWPASS/HIGHPASS = brick-wall bin zeroing, CHEBYSHEV = `sin(n·asin(x))` folding). **A
12-D effect vector applied to a seed wave is a ready-made parametric family
generator**: pick 4 of the 12 as axes, sweep each over 4 values, bake 256 cells.

(b) Yes, trivially — the bank file is a raw `fwrite` of the struct
([`bank.cpp`](https://raw.githubusercontent.com/AndrewBelt/WaveEdit/master/src/bank.cpp)),
and the portable path is `Bank::saveWAV`, a 44.1 kHz mono PCM16 concatenation of
64×256 = 16384 samples; split into 256-sample chunks, FFT, take magnitudes. (c)
negligible — 256 FFTs of length 256 is microseconds; porting the dozen effect
functions into a baker is a day. (d) nothing at runtime. (e) **realistic; the single
highest-leverage item in this section.** (f) Distance/direction: the effects are
separate DSP stages so the axes are independent by construction — a diagonal is a
*product* of effects. Meaningful, but not curved; it does not solve the rotation
problem.

### 1.2 Serum's formula parser — the `q`/`y` idea

Serum's WT Editor has a text field that generates whole wavetables from algebra.
Per the Serum manual §15 (the official PDF ships with the product per
[Xfer support](https://support.xferrecords.com/article/32-where-can-i-find-the-serum-manual);
read here via a [third-party mirror](https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf),
so **page numbers unverified**): variables are `x` (time, −1..1), `w` (time, 0..1),
**`y`/`z` = current table number (0..1 / −1..1)**, `in`/`sel` (existing wave), `rand`,
and critically **`q`**: *"When a 'q' is present in the formula, the function plots to
the FFT bins instead of … the waveform display. q iterates from 1 to 512 for the
respective harmonics/bins."*

**This is the most directly applicable primitive found anywhere in this review.** A
formula in `(q, y)` *is* a function from (harmonic index, position) → magnitude.
Generalise to `(q, y₀..y₃)` and the whole Kyklophoria lattice is a closed form with no
FFT step at all — which is exactly what `HarmonicPoint()` in `core/kyk_gen.h` already
is, only with the axes hard-wired. Making the axis functions data rather than code is
a small change with a large payoff. (b) identity — direct evaluation, 256×64 = 16384
expression evaluations. (c) zero. (d) nothing. (e) **very realistic**: embed a tiny
expression parser in the offline baker, or just make the per-axis functions
table-driven C++ lambdas. Serum is closed-source, so you reimplement the idea, not
the code. (f) **Yes, strongly** — because `y` appears inside the same expression as
`q`, the family is a genuine continuum and a diagonal traverses a smooth manifold
rather than a Cartesian product. Serum's own frame-sort menu (by peak spectrum,
average spectrum, centroid-ish measures) shows they treat spectral *ordering* as the
thing that makes a table coherent.

### 1.3 Vital (Matt Tytel)

GPLv3, [github.com/mtytel/vital](https://github.com/mtytel/vital). The editor is a
stack of *sources* + *modifiers* over a keyframe timeline. From
[`wavetable_component_factory.h`](https://raw.githubusercontent.com/mtytel/vital/main/src/common/wavetable/wavetable_component_factory.h):
sources `kWaveSource`, `kLineSource`, `kFileSource` (plus a deprecated
`kShepardToneSource`); modifiers `kPhaseModifier`, `kWaveWindow`, `kFrequencyFilter`,
`kSlewLimiter`, `kWaveFolder`, `kWaveWarp`. Keyframes interpolate with
`enum InterpolationStyle { kNone, kLinear, kCubic }`
([`wavetable_component.h`](https://raw.githubusercontent.com/mtytel/vital/main/src/common/wavetable/wavetable_component.h)).
Frame size `kWaveformSize = 1 << 11` = 2048, `kNumRealComplex = 1025`
([`wave_frame.h`](https://github.com/mtytel/vital/blob/main/src/synthesis/lookups/wave_frame.h)).
Audio-import styles: `kWavetableSplice`, `kVocoded`, `kTtwt`, `kPitched`
([`wavetable_creator.h`](https://raw.githubusercontent.com/mtytel/vital/main/src/common/wavetable/wavetable_creator.h)).

(b) yes — `WavetableCreator` serialises to JSON (`stateToJson`/`jsonToState`) and
`renderToBuffer()` produces frames, so 256 states can be scripted offline, rendered,
and FFT'd. (c) 256 renders × 2048-point FFTs: seconds. (d) nothing. (e) realistic as
an asset-authoring path, **awkward as a build dependency** (JUCE), and GPLv3 makes
linking its renderer into a proprietary baker a licensing question (offline-only
tooling is a much softer case than shipping it). **Steal the modifier taxonomy, not
the code.** (f) continuous along the keyframe axis and compositional, but still
parameter-product geometry.

### 1.4 Corpora

**AKWF (Adventure Kid Waveforms)** —
[adventurekid.se/akrt/waveforms](https://www.adventurekid.se/akrt/waveforms/adventure-kid-waveforms/),
mirror [github.com/KristofferKarlAxelEkstrand/AKWF-FREE](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE),
**CC0-1.0**. Verified by enumerating the repo tree: **4358 single-cycle WAVs, 600
samples each at 44.1 kHz, 16-bit, in 65 folders** — about 45 *named families*
(`AKWF_bw_saw` = 50 waves, plus `bw_squ`, `bw_tri`, `bw_sin`, `bw_sawbright`,
`bw_sawgap`, `bw_sawrounded`, `bw_blended`, `sinharm`, `overtone`, `epiano`,
`violin`, `hvoice`, `vgame`, `fmsynth`, …) and 20 unsorted numbered batches.
(a/b) FFT a 600-sample cycle, take bins 1..64 (300 available). (c) 256 FFTs of length
600: milliseconds. (d) nothing. (e) very realistic — a free 256-cell fill in an
afternoon. (f) **Mostly no.** Within `bw_saw` the 50 waves are related; *across*
folders distance is meaningless. To make a diagonal mean anything you must **embed**
them — compute descriptors per wave (centroid, odd/even ratio, rolloff, harmonic
entropy), or run the PCA of §3.2, and let the embedding be the axes. That is where
the real design work lives.

**WaveEdit Online** — the community CC0 bank database for the E352/E370 (banks are
64×256, and the manual states uploads are released CC0). **`waveeditonline.com`
failed DNS resolution during this review and web.archive.org was unfetchable, so
treat its availability as unverified**; community context on
[Mod Wiggler](https://modwiggler.com/forum/viewtopic.php?t=197451).

**Plaits / Braids (Mutable Instruments)** —
[github.com/pichenettes/eurorack](https://github.com/pichenettes/eurorack), MIT.
[`plaits/resources/wavetables.py`](https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/resources/wavetables.py)
is the best worked example in existence of exactly our problem. `WAVETABLE_SIZE = 128`;
**3 banks × 8 families × 8 parameter values = 192 waves**, built by an explicit family
generator:

```python
def make_family(fn, arguments):
  return map(fn, arguments)
bank_1 += make_family(sine,   [1,2,3,4,5,6,7,8])
bank_1 += make_family(comb,   [2,3,5,8,13,21,34,55])
bank_2 += make_family(trisaw, [1,1.5,2,3,4,4.5,5,8])
```

Bank 1 is additive, bank 2 formant/waveshaping, bank 3 sampled Braids/Shruthi waves.
Decisively, `make_braids_family` does
`sf = numpy.abs(sf) * numpy.exp(-1j*numpy.pi/2.0)` — **it discards phase and imposes a
single global phase rule, precisely our architecture** (docs/space-format.md §Phases).
The [Plaits manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/)
states HARMONICS picks the bank, TIMBRE the row, and **within a row the waves are
sorted by spectral brightness**. (b) already magnitude-domain. (c) seconds.
(d) nothing. (e) realistic — copy the pattern outright. (f) **Yes, by deliberate
construction**: one axis is a monotone family parameter, sorted by brightness.
**Direction means something because a human sorted it.** That is the load-bearing
lesson of this whole section, and it is the cheapest one to act on.

### 1.5 Parametric family generators from the literature

- **Chebyshev waveshaping** — Le Brun, "Digital Waveshaping Synthesis", *JAES*
  27(4):250–266, April 1979 ([AES](https://www.aes.org/e-lib/browse.cfm?elib=3212)).
  `T_k(cos θ) = cos kθ`, so a polynomial's coefficients **are** the harmonic
  magnitudes and a drive index sweeps a continuum. WaveEdit's `CHEBYSHEV` effect is
  the cheap `sin(n·asin(x))` form. (b) closed-form magnitudes, no FFT. (c) free.
  (d) none. (e) very realistic. (f) **excellent** — the index is a true continuum
  with monotone brightness, so distance is timbral distance.
- **Phase distortion (Casio CZ)** — [US 4,658,691](https://patents.google.com/patent/US4658691A/en)
  (Ishibashi, Casio); [overview](https://en.wikipedia.org/wiki/Phase_distortion_synthesis).
  Warp the phase index with a breakpoint curve before the sine lookup. (b) render a
  2048-sample cycle → FFT → magnitudes. (c) free. (d) none. (e) very realistic; two
  natural axes (knee position, warp amount) times eight CZ shapes. (f) **yes** — a
  smooth, resonant-sounding continuum.
- **Spectral interpolation synthesis** — Serra, Rubine & Dannenberg, "Analysis and
  Synthesis of Tones by Spectral Interpolation", *JAES* 38(3):111–128, March 1990
  ([free PDF, CMU](https://www.cs.cmu.edu/afs/cs/Web/People/rbd/papers/Spectral-JAES-1990.pdf),
  [AES](https://www.aes.org/e-lib/browse.cfm?elib=6049)). Represents a tone as sparse
  amplitude-spectrum keyframes with interpolation between them — the direct
  theoretical justification for the lattice. (e) the synthesis half is trivial; the
  analysis half (extracting keyframes from real tones) is the work. (f) **yes** — this
  literature is precisely about making interpolation paths perceptually meaningful.
- **Additive recipe languages** — Csound [GEN10](https://csound.com/docs/manual/GEN10.html)
  (`f # time size 10 str1 str2 …`, weighted harmonic sums), GEN09 (non-harmonic, with
  phase), GEN19. **A GEN10 line *is* a 64-magnitude cell.** (b) identity mapping.
  (c) free. (e) realistic as a text authoring format for hand-seeded corners that the
  slice/RBF machinery of §5 then fills between.

## 2. Learned / latent timbre spaces

The pitch of this family is exactly ours: train a generative model whose latent space
is low-dimensional and smooth, then *sample the latent space on a regular grid and
bake the decoder output into the lattice*. The decoder never ships; only its output
does. That makes every model here architecturally compatible with `*.kyk` — the
format is generator-agnostic (docs/space-format.md §Versioning). The question is
whether the latent geometry is actually better than a parameter grid.

**Wavespace (Lee, Kim, Lee & Lee, 2024).** *Wavespace: A Highly Explorable Wavetable
Generator*, arXiv:2407.19862 ([abs](https://arxiv.org/abs/2407.19862),
[html](https://arxiv.org/html/2407.19862v1)); also written up on the
[IRCAM Forum](https://forum.ircam.fr/article/detail/wavespace-a-highly-explorable-wavetable-generator-1/).
A VAE over single-cycle waveforms whose latent space is **factorized into several
2-D "style" subspaces** — one 2-D plane per waveform style — with the decoder
additionally conditioned on auxiliary timbral and morphological descriptors, so some
latent axes are *forced* to be a named descriptor rather than learned. Trained on two
corpora: 18 Serum wavetables (256 waveforms of 2048 samples each) and a WaveEdit set
(2572 waveforms of 256 samples), resampled to 1024 samples; the decoder is six
transposed-1-D-conv upsampling/residual blocks emitting a **raw time-domain
waveform**, power-normalised. Reported reconstruction MAE 0.005 (Serum) / 0.035
(WaveEdit), spectral MSE 0.018 / 0.221, and a disentanglement KL of 0.133 against a
baseline 0.393. Model sizes ~1.7 M params (WS) and ~111 K (WS-S); inference 17.2 ms
and 9.7 ms per waveform on an M1 CPU, RTF 1.24 / 2.20. **What it actually
demonstrates:** good waveform reconstruction and a measurably more factorized latent
space than its baseline, plus a working VST prototype. It does **not** report a
perceptual listening test, and the "explorability" claim rests on the factorization
metric and demos, not on human evaluation. Note also that the factorization is into
*2-D style planes* — which is the same separability problem we are trying to escape,
one style per plane rather than one parameter per axis.
*Bakeable?* Yes, and it is the closest match in the literature to what we want: run
the decoder on a 4^4 grid of latent points, FFT each 1024-sample output, keep the
first 64 magnitudes, RMS-normalise. *Desktop cost:* 256 decodes × ~17 ms ≈ **5
seconds**, plus a training run (not stated in the paper; a 1.7 M-param conv VAE on a
few thousand waveforms is single-GPU-hours — unverified). *Runtime cost:* zero. Big
caveat: **no code repository was published**; the paper says the plugin "will be
available online" and gives no location (checked 2026-09-08). Reproducing it is a
from-scratch reimplementation.

**Esling, Chemla-Romeu-Santos & Bitton (2018), generative timbre spaces.**
*Generative timbre spaces: regularizing variational auto-encoders with perceptual
metrics*, DAFx-18, Aveiro ([arXiv:1805.08501](https://arxiv.org/abs/1805.08501)). A
VAE over spectral transforms of instrument tones, with an added regularizer that
pulls latent distances toward the **dissimilarity ratings from the classical MDS
timbre studies** (§3). The paper's own claims, from the abstract: the resulting
space "provide[s] almost similar distance relationships as timbre spaces"; the NSGT
gives the highest correlation to timbre spaces and the best synthesis quality; the
space generalises to novel instruments; and audio descriptors "have an overall
non-linear topology" but "follow a locally smooth evolution" along latent dimensions.
That last sentence is the honest version of the result and the one that matters to
us: **the latent space is locally smooth but globally non-linear in descriptor
terms**, so a straight line in latent space is not a straight line in perceived
brightness. See also the follow-up survey, Esling & Devis et al.,
[*Timbre latent space: exploration and creative aspects*](https://www.researchgate.net/publication/343441571_Timbre_latent_space_exploration_and_creative_aspects)
(2020; venue unverified).
*Bakeable?* In principle — sample the latent grid, decode to a spectral frame, reduce
to 64 harmonic magnitudes. But the model is trained on *instrument tones with
attacks*, not single cycles, so the decoder output is a time-frequency frame of a
real note; harvesting a harmonic magnitude vector from it needs f0-aligned binning
and loses the temporal behaviour that the model is mostly about. *Effort:* research
project for us. Code exists under [acids-ircam](https://github.com/acids-ircam)
(unverified which repo corresponds to this paper).

**FlowSynth / Flow Synthesizer (Esling, Masuda, Bardet, Despres &
Chemla-Romeu-Santos).** *Universal audio synthesizer control with normalizing flows*,
DAFx-19 ([arXiv:1907.00971](https://arxiv.org/abs/1907.00971)); journal version
*Flow Synthesizer: Universal Audio Synthesizer Control with Normalizing Flows*,
Applied Sciences 10(1):302, 2020 ([MDPI](https://www.mdpi.com/10.3390/app10010302),
[project page](https://acids-ircam.github.io/flow_synthesizer/),
[code](https://github.com/acids-ircam/flow_synthesizer)); an IJCAI-2020 demo paper
also exists ([PDF](https://www.ijcai.org/proceedings/2020/0767.pdf)). The trick is a
**normalizing flow giving an invertible map between an audio latent space and a
synthesizer's parameter space**, so you can invert audio → preset, and learn
macro-controls. This is the right *shape* of idea for us — invertibility is exactly
what "resample onto a grid" requires — but it targets *parameters of an existing
synth*, and our "parameters" would be the 64 magnitudes themselves, which makes the
parameter-space half of the model vacuous. Relevant as a technique for learning
macro-controls over a generator we already have, not as a content generator.
*Effort:* research project.

**SPINVAE-2 (Le Vaillant & Dutoit, 2024).** *Latent Space Interpolation of
Synthesizer Parameters Using Timbre-Regularized Auto-Encoders*, IEEE/ACM TASLP 32,
pp. 3379–3392, 2024 ([IEEE](https://ieeexplore.ieee.org/document/10596701),
[accepted PDF](https://orbi.umons.ac.be/bitstream/20.500.12907/49507/1/taslp24_accepted.pdf),
[demos](https://gwendal-lv.github.io/spinvae2/)); predecessor *Synthesizer Preset
Interpolation using Transformer Auto-Encoders*, ICASSP 2023
([arXiv:2210.16984](https://arxiv.org/abs/2210.16984),
[demos](https://gwendal-lv.github.io/spinvae/)). A preset VAE (DX7/Dexed presets)
with an auxiliary mel-spectrogram decoder used as a *timbre regulariser*, so that
straight lines in latent space decode to preset sequences whose **audible** change is
smooth, rather than to linear parameter ramps (which for FM presets are notoriously
non-monotonic). Reported best interpolation performance at **latent size Lz = 512** —
note that is a *large* latent space, not a 4-D one; the paper's contribution is
smoothness of the path, not low dimensionality.
*Relevance to us:* it is a strong argument that **the fix for "interpolation sounds
bad" is to change the representation you interpolate in**, not to add cells. Our
analogue is §6. *Bakeable?* Not directly — it produces DX7 presets, not spectra.
*Effort:* research project; but the *idea* (bake a warped lattice so that plain
linear interpolation gives a perceptually straight path) is cheap and is the
transferable lesson.

**RAVE (Caillon & Esling, 2021).** *RAVE: A variational autoencoder for fast and
high-quality neural audio synthesis*, [arXiv:2111.05011](https://arxiv.org/abs/2111.05011),
code at [acids-ircam/RAVE](https://github.com/acids-ircam/RAVE). Two-stage training
(representation learning, then adversarial fine-tuning) over raw 48 kHz waveforms;
the abstract claims **20× faster than real time on a laptop CPU** and that a
post-training PCA of the latent space lets you trade reconstruction fidelity against
latent compactness. RAVE is a *streaming* model over arbitrary audio; it is not a
single-cycle model and it does not naturally emit a harmonic magnitude vector. It is
also the wrong scale of thing to embed — it is what people run on a Raspberry Pi or
a Bela, not on a Cortex-M7 alongside an oscillator.
*Verdict:* **not relevant** except as evidence that post-hoc PCA on a latent space is
the standard way to get an ordered, resamplable set of axes out of a VAE — which is
exactly the manoeuvre in §3.

**Cross-cutting judgement.** Everything in this section is bakeable in the trivial
sense that a decoder run offline emits spectra. The real question is whether the
*learned* latent geometry beats a *designed* one, and the honest answer from these
papers is: it beats linear parameter interpolation on smoothness (SPINVAE-2), and it
gives a factorized but still axis-aligned space (Wavespace), and it is locally but
not globally descriptor-linear (Esling 2018). None of them demonstrates the property
we actually want — **isotropy**, no privileged directions. A VAE's latent prior is an
isotropic Gaussian, which sounds promising, but the *decoder* is free to be as
anisotropic as it likes, and the disentanglement objectives in these papers actively
push it toward axis-alignment. **Flag: for Kyklophoria these are research projects**,
worth revisiting only once there is a training pipeline and a corpus (§1) and
somebody wants to spend GPU-weeks.

## 3. Dimensionality reduction over a corpus

The manoeuvre: take a corpus of single-cycle waves, reduce their 64-magnitude spectra
to N coordinates, then **resample that reduced space on a regular grid and decode**.
This only works if the reduction has an *inverse*. That is the whole story of this
section, and it divides the field cleanly.

### 3.1 The MDS timbre spaces (the ancestors — and a warning)

- **Grey 1977**, "Multidimensional perceptual scaling of musical timbres", *JASA*
  61(5):1270–1277 ([AIP](https://pubs.aip.org/asa/jasa/article/61/5/1270/626725/Multidimensional-perceptual-scaling-of-musical),
  [PDF](http://sites.music.columbia.edu/cmc/courses/g6610/fall2019/week3/Grey_1977_Multidimensional_perceptual_scaling_of_musical_timbres.pdf)).
  16 analysis-resynthesised orchestral tones, 20 listeners, equalised for pitch,
  loudness and duration. A **3-D** MDS solution: axis I spectral energy distribution,
  axis II synchronicity of harmonic onsets/offsets, axis III low-amplitude HF energy
  during the attack.
- **Wessel 1979**, "Timbre Space as a Musical Control Structure", *Computer Music
  Journal* 3(2):45–52 ([PDF](https://web.mit.edu/9.35/www/archives/2020/auditory/papers/Wessel1979.pdf)).
  Turns the MDS geometry into a control surface: position → interpolated synthesis
  parameters. This is Kyklophoria's idea, from 1979. But Wessel's space is *defined
  only at the ~16 measured points*; everything between is interpolation of analysis
  data — he has no generator either.
- **McAdams, Winsberg, Donnadieu, De Soete & Krimphoff 1995**, "Perceptual scaling of
  synthesized musical timbres: common dimensions, specificities, and latent subject
  classes", *Psychological Research* 58:177–192 ([PubMed](https://pubmed.ncbi.nlm.nih.gov/8570786/)).
  18 timbres, 98 subjects, CLASCAL: **3 common dimensions plus per-timbre
  "specificities" plus 5 latent subject classes**; correlates are log attack time,
  spectral centroid, spectral flux. The specificities result is the sting: a
  substantial part of a timbre's identity is *not* on any shared continuous axis.
- **Caclin, McAdams, Smith & Winsberg 2005**, "Acoustic correlates of timbre space
  dimensions: a confirmatory study using synthetic tones", *JASA* 118(1):471–482
  ([PDF](https://www.mcgill.ca/mpcl/files/mpcl/caclin_2005_jasa_0.pdf)). Orthogonally
  controlled synthetic stimuli. **Attack time and spectral centroid confirmed;
  spectral flux was not** ("Neither of these two dimensions was correlated with
  spectral flux (p > 0.5 in both cases)"); spectral fine structure (even-harmonic
  attenuation) *was* recovered as a dimension.

**What this means for us, plainly.** Of the classical timbre axes, the confirmed ones
are attack time, spectral centroid and spectral fine structure. **Attack time does not
exist in a static single-cycle spectrum.** Our 64 magnitudes can express centroid,
spread and fine-structure/irregularity, and not much else that the literature has
confirmed as perceptually primary. Budget at most two or three perceptually grounded
axes; the remaining axes of an N=4 or N=6 space are ours to invent, and there is no
literature saying what they should be.
*Bakeable?* **No.** MDS gives coordinates for the stimuli only, with no coordinate →
spectrum map. *Cost:* trivial compute, but requires a listening experiment.
*Runtime:* none. *Verdict:* **research project**, and not a content generator at all.

### 3.2 PCA / KLT over spectra — the one with a real inverse

- **Stapleton & Bass 1988**, "Synthesis of musical tones based on the Karhunen–Loève
  transform", *IEEE Trans. ASSP* 36(3):305–319
  ([IEEE](https://ieeexplore.ieee.org/abstract/document/1527)) — resynthesis from ≤5
  KLT basis functions instead of an arbitrary sinusoid count.
- **Sandell & Martens 1995**, "Perceptual Evaluation of Principal-Component-Based
  Synthesis of Musical Timbres", *JAES* 43(12):1013–1028
  ([AES](http://www.aes.org/e-lib/browse.cfm?elib=7919)) — PCA over time-varying
  harmonic amplitude envelopes; reports "nearly identical resyntheses … with a 40–70 %
  data reduction" (cello, trombone, clarinet) across two listening experiments, and
  explicitly builds a PCA **space for interpolation between instruments**.
- **Hourdin, Charbonneau & Moussa 1997**, *Computer Music Journal* 21(2):40–55 and
  21(2):56–68 — MDS applied to the *spectra themselves* rather than to perceptual
  judgements, as an analysis/resynthesis intermediary.

(a) Eigenbasis of the spectrum covariance; project to k dims, reconstruct
`X ≈ mean + Σ cᵢ vᵢ`. (b) **Yes, cleanly** — choose N axes, choose grid extents,
decode each of the 256 cells offline. (c) an SVD of a 4000×64 matrix is ~16 MFLOP,
**milliseconds** in NumPy; the whole pipeline is seconds. (d) **nothing at runtime**
— the firmware still just multilinearly interpolates 64 baked magnitudes.
(e) **entirely realistic for firmware.**

**Negative magnitudes.** A linear reconstruction can go below zero. Three fixes:
clip at zero (cheap, mangles quiet partials); **log-magnitude PCA** — PCA on
`log(|X| + ε)`, decode then `exp`, positivity free and loudness is roughly
logarithmic anyway, at the cost of multiplicative error so grid extremes can produce
wild partial ratios; or **NMF** (Lee & Seung, "Learning the parts of objects by
non-negative matrix factorization", *Nature* 401:788–791, 1999,
[Nature](https://www.nature.com/articles/44565)), whose factors are non-negative and
additive by construction so any non-negative coefficient grid decodes to a valid
magnitude spectrum. NMF is the principled answer; **log-magnitude PCA is the
pragmatic one and is what I would write first.**

### 3.3 t-SNE and UMAP — good maps, bad coordinate systems

van der Maaten & Hinton, "Visualizing Data using t-SNE", *JMLR* 9:2579–2605, 2008
([JMLR](https://www.jmlr.org/papers/v9/vandermaaten08a.html)); McInnes, Healy &
Melville, "UMAP: Uniform Manifold Approximation and Projection", 2018
([arXiv:1802.03426](https://arxiv.org/abs/1802.03426)). Audio precedents:
**AudioStellar** (Garber et al., ICMC 2020;
[NIME writeup](https://nime.pubpub.org/pub/dwtopcue/release/1)) — corpus → features →
PCA/t-SNE/UMAP → a 2-D playable map; and Google's
[Infinite Drum Machine](https://experiments.withgoogle.com/drum-machine)
([source](https://github.com/googlecreativelab/aiexperiments-drum-machine)) — t-SNE
over thousands of everyday sounds. **Both play back existing samples at map
positions; neither synthesises a new sound from a coordinate.** That is the tell.

**Why they cannot be resampled onto a grid.** t-SNE optimises *the embedding
coordinates of the training points themselves*; there is no function, forward or
inverse. scikit-learn's
[`TSNE`](https://scikit-learn.org/stable/modules/generated/sklearn.manifold.TSNE.html)
exposes `fit_transform` and **no `transform`**, which is the API conceding exactly
this. And Wattenberg, Viégas & Johnson,
["How to Use t-SNE Effectively"](https://distill.pub/2016/misread-tsne/), *Distill*
2016, shows that cluster sizes and inter-cluster distances in a t-SNE plot are not
meaningful — so "distance and direction mean something", our whole requirement, fails
at the premise. UMAP does ship
[`inverse_transform`](https://umap-learn.readthedocs.io/en/latest/inverse_transform.html),
but its own documentation says it "can be quite expensive computationally", that it
"operates poorly outside the bounds of that convex hull" with out-of-hull points
"often simply snapping to a particular source high dimensional vector", and that
output in inter-cluster gaps is uninterpretable — which is precisely where a regular
grid's corners land. Inverses do exist via **parametric t-SNE** (van der Maaten,
AISTATS 2009, *JMLR W&CP* 5:384–391,
[PMLR](https://proceedings.mlr.press/v5/maaten09a.html)) and **Parametric UMAP**
(Sainburg, McInnes & Gentner, *Neural Computation* 33(11):2881–2907, 2021,
[arXiv:2009.12981](https://arxiv.org/abs/2009.12981)) — both are neural encoders, and
only the autoencoder variant gives a decoder.

(a) neighbour-graph embeddings optimised for local structure. (b) only via a trained
decoder. (c) t-SNE/UMAP on 4000×64: seconds to a minute; parametric variants: minutes
of training plus a torch/TF dependency. (d) none if baked, but the bake is fragile.
(e) **research project. Skip.** If nonlinearity is wanted, an autoencoder gives a
decoder by construction — see §2, and Fried & Fiebrink, "Cross-modal Sound Mapping
Using Deep Learning", NIME 2013
([PDF](https://nime.org/proceedings/2013/nime2013_111.pdf)).

### 3.4 Cepstral / MFCC spaces

MFCC = truncated DCT of log mel-filterbank energies; inversion = zero-pad, inverse
DCT, exponentiate, pseudoinverse of the mel matrix. See Boucheron & De Leon, "On the
inversion of mel-frequency cepstral coefficients for speech enhancement
applications", ICSES 2008
([IEEE](https://ieeexplore.ieee.org/abstract/document/4673475/)), and the reference
implementation
[`librosa.feature.inverse.mfcc_to_mel`](https://librosa.org/doc/0.11.0/generated/librosa.feature.inverse.mfcc_to_mel.html)
("inverse DCT … then `db_to_power`", returning an "approximate Mel power spectrum").
The loss is by design: MFCCs keep the envelope and discard fine structure.

**For a single-cycle wavetable that is backwards** — our 64 harmonics *are* the fine
structure, and mel warping wastes resolution over 64 bins. Use the **plain real
cepstrum instead**: a DCT of the log magnitudes over the 64 harmonic bins, no mel
warping. Exactly invertible untruncated, positivity free via `exp`, and truncating to
4 coefficients gives smooth envelope axes. It is log-magnitude PCA with a fixed basis
instead of a learned one, and it needs no corpus at all.
(a)–(e): bakeable offline, microseconds, nothing at runtime, **entirely realistic**.

### 3.5 Is a corpus of ~4000 waves enough for 4 axes?

Yes, comfortably. [AKWF](https://www.adventurekid.se/akrt/waveforms/adventure-kid-waveforms/)
is ~4300 single-cycle waves of 600 samples
([mirror](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE)). Estimating a
64×64 covariance from 4000 samples is a 60:1 sample-to-dimension ratio, far above any
rule of thumb. Compute: seconds.

Two warnings. **(i)** AKWF is heavily clustered (many near-duplicates within families)
and skewed toward bright buzzy waves; PC1 will very likely be "overall
brightness/centroid" and PC2 "odd/even balance" (*unverified — this is a prediction,
run the SVD*). Eigenvalues will be steeply unequal, so **scale the per-axis grid
extents by √λᵢ, not uniformly**, or three of four axes will be perceptually inert.
**(ii)** PCA axes are variance-optimal, not perceptually orthogonal; the
McAdams/Caclin "specificities" result predicts that the interesting individual
timbres sit *off* the grid. De-duplicate or whiten the corpus before the SVD, and
consider reserving one or two lattice axes for a hand-designed parameter rather than
letting PCA claim all N.

## 4. Correlated random fields as content

This is the family where I think the answer is. The idea: stop treating the lattice as
a sampling of a parameter function and treat it as **a realisation of a random field
over the space**, band-limited to a chosen correlation length. Then there are no
privileged axes *by construction*, because a stationary isotropic field's covariance
`E[g(x)g(y)] = C(|x − y|)` depends only on distance — which is precisely the property
that makes the rotation feature (spec §3.4) pay, and precisely the property that a
parameter grid lacks.

### 4.1 Procedural noise (Perlin, simplex, and the rest)

**Perlin noise.** K. Perlin, "An Image Synthesizer", *Computer Graphics* (SIGGRAPH '85)
19(3):287–296 — a gradient noise: random unit gradients at integer lattice points,
value 0 at the lattice points, smoothly interpolated between. K. Perlin, "Improving
Noise", *ACM TOG* 21(3):681–682 (SIGGRAPH 2002) replaces the cubic ease with the
quintic `6t⁵−15t⁴+10t³` (continuous second derivative) and swaps the gradient table
for 12 fixed cube-edge directions. The definitive survey is **A. Lagae, S. Lefebvre,
R. Cook, T. DeRose, G. Drettakis, D. S. Ebert, J. P. Lewis, K. Perlin & M. Zwicker,
"A Survey of Procedural Noise Functions", *Computer Graphics Forum* 29(8):2579–2600,
2010** ([Wiley](https://onlinelibrary.wiley.com/doi/abs/10.1111/j.1467-8659.2010.01827.x),
[PDF](https://www.cs.umd.edu/~zwicker/publications/SurveyProceduralNoise-CGF10.pdf),
[HAL](https://memsic.ccsd.cnrs.fr/INRIA-SOPHIA/hal-00920177)) — it formalises noise
as a stochastic process and classifies lattice / sparse-convolution / explicit-spectrum
constructions. *(I could not extract quotable text from the PDF in this session; the
citation is verified, specific claims attributed to it below are marked unverified.)*
Two facts matter here: classic Perlin/value noise is a **lattice** noise, so it is
only approximately band-limited and has known **axis-aligned directional artifacts**
(this is the stated motivation for simplex noise and for Gabor noise — *unverified as
to the survey's exact wording*), and the cost of a lattice gradient noise in n
dimensions is **O(n·2ⁿ)** because you interpolate 2ⁿ corners.

**Simplex noise.** K. Perlin, "Noise Hardware", in *Real-Time Shading* SIGGRAPH 2001
course notes; the clearest exposition is S. Gustavson, "Simplex noise demystified",
2005 ([PDF](https://itn-web.it.liu.se/~stegu76/TNM084-2011/simplexnoise-demystified.pdf)).
Skew the space so the interpolation cell is a simplex (n+1 corners) instead of a
hypercube (2ⁿ corners). Perlin's own patent states the complexity change explicitly:
*"the computational complexity of previous Perlin Noise implementations in n
dimensions is O(n 2ⁿ), whereas the computational complexity of the new Perlin Noise
implementation in n dimensions is O(n²)"* — US Patent 6,867,776 B2, "Standard for
perlin noise", K. Perlin, filed 2002-01-08, granted 2005-03-15
([Google Patents](https://patents.google.com/patent/US6867776B2/en)). **The patent
expired 2022-01-08** (the date is widely reported, e.g. the
[Godot proposal thread](https://github.com/godotengine/godot-proposals/discussions/5007);
treat the exact legal status as *unverified* but it is 20 years from the filing date,
and it is moot for us anyway — we only need noise **offline**, in a desktop tool).
Simplex noise also has less directional bias than Perlin noise in high dimensions,
which is exactly the isotropy property we want.
*Bakeable?* Trivially. `mag[k] at cell x = envelope(k) · f(noise_{N+1}(x, k·s))`.
*Desktop cost:* 256 cells × 64 harmonics = **16,384 noise evaluations**;
microseconds. N=6, side=4 → 4096 × 64 = 262,144; still microseconds.
*Runtime:* nothing. *Realism:* **this is a 100-line generator in `kyk_gen.h` and it
could ship next week.**

### 4.2 Gaussian random fields with a prescribed spectrum

Procedural noise gives you *a* correlation structure; a Gaussian random field lets you
*choose* it. The classic construction is **spectral synthesis / Fourier filtering**:
draw complex white noise in the DFT domain with Hermitian symmetry, multiply by
`sqrt(S(ω))` for a target power spectrum `S`, inverse-transform. Reference: D. Saupe,
"Algorithms for random fractals", ch. 2 of H.-O. Peitgen & D. Saupe (eds.), *The
Science of Fractal Images*, Springer 1988
([Springer](https://link.springer.com/chapter/10.1007/978-1-4612-3784-6_2)) — the
method is named there as "the spectral synthesis method or … the Fourier filtering
method". For non-periodic domains the exact version is **circulant embedding**:
C. R. Dietrich & G. N. Newsam, "Fast and Exact Simulation of Stationary Gaussian
Processes through Circulant Embedding of the Covariance Matrix", *SIAM J. Sci.
Comput.* 18(4):1088–1107, 1997
([ADS](https://ui.adsabs.harvard.edu/abs/1997SJSC...18.1088D/abstract)); and
A. T. A. Wood & G. Chan, "Simulation of stationary Gaussian processes in [0,1]^d",
*J. Comput. Graph. Statist.* 3(4):409–432, 1994. Cost is one FFT of the padded
lattice per realisation, i.e. **O(M log M)**.

**Why this is a particularly good fit for Kyklophoria:** circulant embedding's whole
trick is to *make the domain periodic* so the FFT method is exact. Our `Wrap`
topology (spec §3.3) already **is** a flat torus, so no embedding is needed — on a
wrapped space the plain FFT method is exact, with no padding and no non-negative-
definiteness worry. A wrapped Kyklophoria space filled with a Gaussian field is the
textbook case, not an approximation of it.

**Concrete recipe.** Build an (N+1)-dimensional field over (lattice × harmonic
index): axes 0..N−1 are the lattice, axis N is `k`. Give it an anisotropic covariance
— **isotropic across the N lattice axes** (that is the whole point) but a separately
chosen correlation length `ℓ_k` along the harmonic axis. Large `ℓ_k` ⇒ smooth
spectral envelopes, formant-like. Small `ℓ_k` ⇒ comb/inharmonic-sounding rasp.
Multiply by a deterministic mean envelope (`1/k` or `k^−tilt`) so the result is a
plausible spectrum, exponentiate the field so magnitudes are non-negative
(a log-normal field — which is also the right domain for magnitudes, see §6), then
RMS-normalise per cell as the format requires. Two knobs (`ℓ_space`, `ℓ_k`) plus a
seed give a whole family of spaces.
*Desktop cost:* one 5-D FFT of 4⁴×64 = 16,384 complex points. **Under a millisecond.**
N=6 → 4096×64 = 262,144 points, still milliseconds. This is free.
*Runtime:* nothing. *Realism:* **realistic, immediately.**

### 4.3 Two real problems with side=4

Honest counter-analysis, and it is the most actionable finding in this document.

**(a) There is almost no room for a correlation length.** With `side = 4` and a
wrapped axis, the DFT of that axis has exactly 4 bins (DC, ±1, 2). A field whose
correlation length is 2 cells has essentially *one* feature per axis; a field whose
correlation length is 1 cell is at the lattice Nyquist and is aliased by the
multilinear reconstruction. So on a 4-point axis, "band-limited noise" and "one random
bump per axis" are the same thing. **Recommendation: raise `side` for noise-filled
spaces.** The format already allows it and the cost is nothing: `side=8, N=4, K=64,
P=8` is `64 + 4096·72·4` = **1.18 MB**, in a 64 MB SDRAM (docs/space-format.md
§Sizes); interpolation cost is unchanged (still 2^N = 16 corners). Only load time and
file size grow.

**(b) Multilinear interpolation attenuates a random field at cell centres.** For a
zero-mean field whose corner values are near-independent (which is what a correlation
length of ~1 cell means), the multilinear interpolant at the centre of a cell is the
mean of 2^N corners with weight 2^−N each, so its variance is `2^N · (2^−N)² = 2^−N`
of the corner variance. For **N=4 that is 1/16, i.e. −12 dB**, and for N=6 it is
−18 dB. Direction matters too: along an axis the interpolant is a 2-point tent
(variance at the midpoint = 1/2, −3 dB), along the main diagonal it is the 16-corner
average (−12 dB). So **multilinear interpolation of a random field is itself
anisotropic**, and it makes cell centres and diagonals quieter and duller than
lattice points — the same "crossfade dip" as §6, arriving by a different route.
Two fixes, both cheap: correlate the field over ≥2–3 cells (needs (a), a larger
`side`), and/or **renormalise the interpolated spectrum's RMS at runtime** — 64
multiply-accumulates plus one reciprocal square root per render, which is noise next
to the 1024-point IFFT already in the block budget (spec §4).

### 4.4 Wave terrain synthesis

Wave terrain synthesis is the 2-D ancestor of this whole idea: store a function of two
variables `z = f(x,y)` and read it along an orbit `(x(t), y(t))` to produce the
waveform. Primary sources: J. Bischoff, R. Gold & J. Horton, "A Microcomputer-Based
Network for Live Performance", *Computer Music Journal* 2(3), 1978 (the first
realisation, per James's survey below); Y. Mitsuhashi, "Audio Signal Synthesis by
Functions of Two Variables", *JAES* 30(10):701–706, 1982; and A. Borgonovo & G. Haus,
"Sound Synthesis by Means of Two-Variable Functions: Experimental Criteria and
Results", *Computer Music Journal* 10(3):57–71, 1986
([JSTOR](https://www.jstor.org/stable/3680260)), with an earlier ICMC-1984 version
([ICMC](https://quod.lib.umich.edu/i/icmc/bbp2372.1984.006/1/--musical-sound-synthesis)).
Modern work: S. James, "Possibilities for Dynamical Wave Terrain Synthesis", in
*Converging Technologies: Proceedings of the Australasian Computer Music Conference*,
2003, pp. 58–67
([Academia](https://www.academia.edu/19578558/Possibilities_for_Dynamical_Wave_Terrain_Synthesis_2003_)),
and his PhD, S. James, *Spectromorphology and Spatiomorphology: Wave terrain synthesis
as a framework for controlling timbre spatialisation in the frequency domain*, Edith
Cowan University, 2015 ([open access](https://ro.ecu.edu.au/theses_ebooks/4/)) —
notable because James's terrain is read **in the frequency domain**, in frames, which
is structurally the same move Kyklophoria makes.

**What this buys us.** The terrain literature is the empirical evidence that reading a
smooth random-ish field along a trajectory is musically productive, and it is the
source of the practical lore: terrains built from smooth analytic functions (Chebyshev
products, sums of sinusoids, Gaussians) give predictable, tonal results; the orbit's
shape matters more than the terrain's fine detail; and a terrain that changes slowly
under the orbit is more interesting than a static one. Kyklophoria is a wave terrain
where the terrain is N-dimensional, the value is a *spectrum* rather than a sample,
and the orbit is the rotation LFO. **That framing is worth putting in the manual**;
and if the noise generator lands, "terrain" is the right name for the family.
*Bakeable?* Yes — an analytic terrain evaluated on a grid is the cheapest generator
imaginable. *Runtime:* none. *Realism:* realistic; it is the same code path as §4.1
with a deterministic function instead of noise.

## 5. Tomographic / slice-based construction

The authoring fantasy: draw two or three 2-D grids of spectra, press a button, get a
4-D volume. There is real prior art, but **not** in tomography — the useful branch is
scattered-data interpolation from slice *values*, and the closest match in the
literature is Turk & O'Brien 1999.

### 5.1 Tomography proper — rule it out, and here is why

The 2-D Radon transform is `p_θ(t) = ∬ f(x,y) δ(x cosθ + y sinθ − t) dx dy`; inversion
is filtered back-projection, `f = ∫₀^π (p_θ ∗ h) dθ` with a ramp filter `|w|`. The
free primary source is **A. C. Kak & M. Slaney, *Principles of Computerized
Tomographic Imaging*, IEEE Press 1988** (reissued SIAM 2001), online at
[slaney.org/pct](http://www.slaney.org/pct/index.html) — ch. 3 (algorithms) and ch. 5
(aliasing and sampling) are the relevant ones
([Ch.3 PDF](https://engineering.purdue.edu/~malcolm/pct/CTI_Ch03.pdf),
[Ch.5 PDF](https://engineering.purdue.edu/~malcolm/pct/CTI_Ch05.pdf)).

**Projection-slice (Fourier-slice) theorem:** the 1-D Fourier transform of `p_θ`
equals the 2-D Fourier transform of `f` restricted to the radial line at angle θ. The
n-dimensional generalisation (the k-plane / hyperplane transform) is in
**F. Natterer, *The Mathematics of Computerized Tomography*, SIAM Classics in Applied
Mathematics 32, 2001** (orig. 1986); a serviceable summary is
[Wikipedia: projection-slice theorem](https://en.wikipedia.org/wiki/Projection-slice_theorem)
(secondary source).

**How many projections?** The standard rule of thumb is **m ≈ πN/2 views for N
detector samples across the object**, from matching angular to radial Nyquist
sampling; stated explicitly in
[Waygate/Baker Hughes, "How many projections do we need in CT?"](https://www.bakerhughes.com/waygate-technologies/blog/how-many-projections-do-we-need-ct)
and used as standard in the CT literature. *(Attributing that exact formula to a
specific page of Kak & Slaney is **unverified**.)* For a 4-point axis that is about 6
views per plane pair — **no saving at all** over just authoring the cells.

**Few projections:** with a limited angular range the operator has a large null space
and FBP streaks along the missing wedge; this is rigorously characterised in
**J. Frikel & E. T. Quinto, "Characterization and reduction of artifacts in limited
angle tomography", *Inverse Problems* 29(12):125007, 2013**
([PDF](https://sites.tufts.edu/tquinto/files/2021/01/LimitedAngleArtefacts_main.pdf),
[IOP](https://iopscience.iop.org/article/10.1088/0266-5611/29/12/125007)).
Regularisation recovers the null space only if the object is sparse in some basis —
**Candès, Romberg & Tao**, *IEEE Trans. Inf. Theory* 52(2):489–509, 2006
([DOI](https://dl.acm.org/doi/10.1109/TIT.2005.862083)) and **Donoho**, *IEEE Trans.
Inf. Theory* 52(4):1289–1306, 2006
([ITSoc](https://www.itsoc.org/publications/papers/compressed-sensing)); the CT
workhorse is TV-constrained reconstruction, **Sidky & Pan**, *Phys. Med. Biol.*
53(17):4777–4807, 2008
([IOP](https://iopscience.iop.org/article/10.1088/0031-9155/53/17/021)).

(a) line/plane integrals → volume. (b) yes, trivially bakeable. (c) milliseconds at
our size. (d) nothing at runtime. (e) **the blocker is conceptual, not
computational: a projection is the *average spectrum along a line* through the timbre
space, and nobody can hear an integral. Asking a musician to author one is not a
musical act. Rule this out.**

### 5.2 Scattered-data interpolation from slice values — the useful branch

**Variational implicit / RBF interpolation from constraint planes.** *Turk & O'Brien,
"Shape Transformation Using Variational Implicit Functions", SIGGRAPH '99, pp.
335–342* ([PDF](https://www.cs.jhu.edu/~misha/Fall05/Papers/turk99.pdf),
[project page](http://graphics.berkeley.edu/papers/Turk-STU-1999-08/)) is literally
this problem: they place 2-D data constraints in parallel planes embedded in N+1
dimensions and solve one scattered-data (thin-plate / RBF) interpolation to get a
single function over the whole volume. Kernel theory: **H. Wendland, *Scattered Data
Approximation*, Cambridge Monographs on Applied and Computational Mathematics 17,
CUP 2004**
([front matter](https://assets.cambridge.org/97805218/43355/frontmatter/9780521843355_frontmatter.pdf)).
(b) yes — evaluate at the `side^N` lattice sites and bake 64 magnitudes each.
(c) **measured during this review: a dense 256×256 RBF solve with 64 right-hand sides
is 0.73 ms single-threaded in NumPy; the N=6 worst case, 4096×4096 with 64 RHS, is
about 1 s.** (d) nothing. (e) **fully realistic** — roughly 50 lines of offline
Python or C++ in `tools/kykspace`.

**Tensor-product / separable outer product.** Two authored 2-D grids →
`f(a,b,c,d) = g(a,b) ⊙ h(c,d)` (multiply magnitudes, or add log-magnitudes).
(b) yes; (c) microseconds; (d) none; (e) the cheapest thing that works and the most
*predictable* for a player — but it can only express rank-1 structure, so cross-slice
surprise is impossible by construction, and **a diagonal is again just a blend**,
which is the failure mode we started from.

**Laplace / harmonic and Poisson diffusion inpainting.** Solve `∇²f = 0` (or
`∇²f = div v`) on the lattice with the authored slices as Dirichlet boundary
conditions. Canonical graphics reference: **Pérez, Gangnet & Blake, "Poisson image
editing", *ACM TOG* 22(3):313–318 (SIGGRAPH 2003)**
([ACM](https://dl.acm.org/doi/10.1145/882262.882269)). (b) yes; (c) 256 unknowns per
channel — a sparse solve, or 100 Jacobi sweeps, well under a millisecond; (d) none;
(e) very realistic, and it degrades gracefully with only *one* authored slice. The
downside is structural: by the maximum principle a harmonic interpolant has no
interior extrema, so **the middle of the cube is guaranteed to be the blandest part of
the space** — the opposite of what we want.

**Gaussian process / kriging.** The same linear algebra as RBF, but it also returns a
posterior variance you can *show the user*: "this region is unconstrained, go author a
slice here". **Rasmussen & Williams, *Gaussian Processes for Machine Learning*, MIT
Press 2006**, free at [gaussianprocess.org/gpml](http://gaussianprocess.org/gpml/chapters/RW.pdf).
Same cost as RBF. Realistic, and the variance map is genuinely useful UI for the
authoring drawer (spec §8). Note the direct link to §4: a GP *conditioned on nothing*
is exactly the Gaussian random field of §4.2, so **slice authoring and noise fill are
the same machine** — author what you care about, and let the field's prior fill the
rest with correlated material instead of with blandness. That is the single most
attractive synthesis of ideas in this document.

**Shape-based interpolation.** Interpolate a *feature/distance transform* rather than
raw values, so features move instead of cross-fading: **S. P. Raya & J. K. Udupa,
"Shape-based interpolation of multidimensional objects", *IEEE Trans. Med. Imaging*
9(1):32–42, 1990** ([PubMed](https://pubmed.ncbi.nlm.nih.gov/18222748/)). The spectral
analogue is interpolating in a warped or feature domain (formant position,
log-magnitude, cepstral) rather than in linear magnitude — cheap, and the same
conclusion §6 reaches from the other end.

### 5.3 Author-2-D-get-3-D in graphics — the closest real prior art

**Kopf, Fu, Cohen-Or, Deussen, Lischinski & Wong, "Solid Texture Synthesis from 2D
Exemplars", *ACM TOG* 26(3) (SIGGRAPH 2007), article 2**
([project page](https://johanneskopf.de/publications/solid/),
[ACM](https://dl.acm.org/doi/10.1145/1276377.1276380)). Texture optimisation plus
histogram matching over a multiresolution pyramid with 8×8 neighbourhoods; it
demonstrates exactly the authoring act we want. From the paper: synthesising a 128³
solid texture with 3 channels per texel takes 10–90 minutes on a 2.4 GHz CPU, and
they also synthesise a 128³ volume of **6-D** texels — so many channels are fine.
Scaled down to 256 cells × 64 channels that is seconds, not minutes. (b) yes;
(d) none; (e) realistic but it is **the most code of anything in this document**
(mean shift, histogram matching, PCA on neighbourhoods).

The procedural shortcut for the same idea: fit a noise model to a 2-D exemplar's
power spectrum, then evaluate it in n dimensions for free — **Gilet, Sauvage,
Vanhoey, Dischler & Ghazanfarpour, "Local random-phase noise for procedural
texturing", *ACM TOG* 33(6) (SIGGRAPH Asia 2014)**
([Semantic Scholar](https://www.semanticscholar.org/paper/Local-random-phase-noise-for-procedural-texturing-Gilet-Sauvage/87a653eb904abd21b32e7a79654148aec4a81dd6));
see also "Solid texture synthesis based on 2D exemplar and procedural noise"
([IEEE](https://ieeexplore.ieee.org/document/6001915/)). Dimension-agnostic by
construction, and it is §4's machinery with the spectrum *fitted* rather than
dialled. Useful when you want texture and variation rather than controlled timbre.

### 5.4 Verdict on this area

"Author 2-D slices, get a 4-D volume" is **tractable and cheap** — the solvers are
sub-millisecond at our size, and Turk & O'Brien did exactly this construction in
1999. The cost is not compute, it is **UI**: a musician must be able to draw a 4×4
grid of spectra and pin it to two chosen axes. That, not the solver, is the project.
Ship the tensor product as the predictable default, RBF/GP-from-slices as the "fill
the rest" button, and skip Radon entirely.

## 6. Interpolation quality between spectra

This section is not about *what* fills the space but about whether the space is
*readable* once filled, and it produced the two most immediately actionable findings
in the review. Every citation below was checked.

### 6.1 The crux: cancellation and cross-fade are different failures, and only one is ours

**(i) Complex spectra or waveforms.** Blending `X_t = (1−t)X_A + tX_B` gives per-bin
`|X_t|² = (1−t)²|A|² + t²|B|² + 2t(1−t)|A||B|cos Δφ`. Where `Δφ → π` you get true
cancellation; when the sources differ by a time shift τ, `Δφ = ωτ` sweeps linearly
with frequency and you get periodic notches — comb filtering, the audible "phasey"
crossfade. **This does not apply to Kyklophoria's lattice.** Our phases are global
(docs/space-format.md §Phases), so all corners share them.

**(ii) Magnitude-only with shared phases — ours.** The interpolant is
`m_t = (1−t)m_A + t m_B` with `m ≥ 0`, so bin-by-bin `min(m_A, m_B) ≤ m_t`: **no
cancellation, no comb filtering, ever.** The failure is purely structural: a peak at
bin 10 and a peak at bin 20 become *two peaks at half height*, never one peak at bin
15. This is the **cross-fade (mixture) versus transport (displacement interpolation)**
distinction. Linear averaging of measures is the Euclidean/mixture average; the
transport answer is McCann's **displacement interpolation** (R. J. McCann, "A
convexity principle for interacting gases", *Advances in Mathematics* 128:153–179,
1997, [ScienceDirect](https://www.sciencedirect.com/science/article/pii/S0001870897916340)),
generalised to many inputs as the **Wasserstein barycenter** (M. Agueh & G. Carlier,
"Barycenters in the Wasserstein space", *SIAM J. Math. Anal.* 43(2):904–924, 2011,
[doi:10.1137/100805741](https://doi.org/10.1137/100805741)).

The direct evidence that (ii) is our problem: **Slaney, Covell & Lassiter, "Automatic
Audio Morphing", ICASSP 1996**
([PDF](https://engineering.purdue.edu/~malcolm/interval/1995-061/AudioMorphingPaper.pdf))
document this failure **on magnitude spectrograms with no phase at all** —
"Conventional spectrograms can represent any sound, but cross-fading spectrograms does
not produce convincing morphs," and when the pitches differ "the morphed sound has two
separate pitches, causing the sound to be perceived as two different auditory objects,
destroying the illusion of a continuous morph." Henderson & Solomon (below) frame the
same contrast as **"fade" versus "portamento."**

### 6.2 The loudness dip — do this first

Because harmonics are orthogonal over the 1024-sample period, Parseval makes the
rendered frame's RMS **exactly proportional to the ℓ² norm of the magnitude vector**,
which is what the generator normalises (`Σm² = 2`). Let the corners be unit-norm.

**Two corners:** `‖(1−t)a + tb‖² = 1 − 2t(1−t)(1 − ⟨a,b⟩)`, so the blend always dips,
worst at `t = ½` where `‖m‖ = √((1+⟨a,b⟩)/2)`. Because magnitudes are **non-negative**,
`⟨a,b⟩ ≥ 0` always, so the two-corner dip is bounded at **−3.01 dB**, hit exactly when
the supports are disjoint (odd-only against even-only harmonics — which is *precisely*
what our `hollow` axis does).

**Sixteen corners — the real problem.** `‖Σ wᵢaᵢ‖ ≤ Σ wᵢ‖aᵢ‖ = 1` by convexity
(Jensen). At the centre of a cell `wᵢ = 1/16`, and with mutually orthogonal corners
`‖m‖² = 16·(1/16)² = 1/16`, so **‖m‖ = ¼: a −12.04 dB hole at the centre of every
lattice cell.** In general the floor is `−10 log₁₀(M)` dB for M equally-weighted
mutually orthogonal corners; N=6 gives −18 dB. This is almost certainly a large part
of why interior cells sound dull, and it compounds with the *variance* attenuation
derived in §4.3(b) — that argument is about how much the field varies, this one about
how loud it is; they are separate effects with the same `2^−N` shape and they add.

**Two fixes, both cheap, and orthogonal to everything else in this section:**
1. **Renormalise after interpolation:** `m ← m · (E_target / ‖m‖)` with
   `E_target = Σ wᵢ‖aᵢ‖ = 1`. Cost: 64 MAC + one `vsqrt`/rsqrt + 64 multiplies ≈
   **200–300 cycles, well under 1 µs**. Exact.
2. **Power-domain interpolation:** store `aᵢ²` **offline** in the `.kyk` file,
   interpolate linearly, `sqrt` per bin. Then `‖m‖² = Σ wᵢ‖aᵢ‖² = 1` automatically.
   Cost ≈ 64 × ~16 cycles ≈ **1 k cycles ≈ 2 µs**. It is a storage change, not an
   algorithm change, and it biases each bin toward the louder corner, which sounds
   less washed out than the amplitude mean. This is the equal-power crossfade
   principle (`sin² + cos² = 1`; see
   [CCRMA, *Multichannel Intensity Panning*](https://ccrma.stanford.edu/guides/planetccrma/Multichannel_Intensity_Pann.html),
   and V. Pulkki, "Virtual sound source positioning using vector base amplitude
   panning", *JAES* 45(6):456–466, 1997,
   [Aalto](https://research.aalto.fi/en/publications/virtual-sound-source-positioning-using-vector-base-amplitude-pann/)).

**Do one of these regardless of anything else in this document.** It is the cheapest,
largest and most certain win available.

### 6.3 Optimal transport, and the one trick that makes it free

Treat the 64 normalised magnitudes as a 1-D measure on bin index; the barycenter
*moves* mass instead of fading it. **Henderson & Solomon, "Audio Transport: A
Generalized Portamento via Optimal Transport", DAFx-19**
([DAFx PDF](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_56.pdf),
[arXiv:1906.06763](https://arxiv.org/abs/1906.06763)) do exactly this per STFT frame,
with a greedy monotone-rearrangement plan in **O(|X|+|Y|)**, placing mass `π*ᵢⱼ` at
frequency `(1−k)ωᵢˣ + kωⱼʸ`; they normalise before transport and interpolate scale
linearly afterwards.

**The key fact.** In 1-D, optimal transport *is* monotone rearrangement, so the
barycenter is exactly the weighted average of quantile (inverse-CDF) functions.
Peyré & Cuturi, *Computational Optimal Transport*, FnT ML 11(5–6)
([arXiv:1803.00567](https://arxiv.org/abs/1803.00567)), **Remark 9.6**: "For 1-D
distributions, the `W_p` barycenter can be computed almost in closed form… in
`O(n log n)` operations, using a simple sorting procedure." For `p = 2` the averaging
operator is the plain weighted mean, and **the multi-marginal case — our 16 corners —
is covered, not just the two-measure case** (which is their Eq. 7.7,
`C⁻¹_{α_t} = (1−t)C⁻¹_{α₀} + t C⁻¹_{α₁}`). Same result in Bonneel, Rabin, Peyré &
Pfister, "Sliced and Radon Wasserstein Barycenters of Measures", *JMIV* 51:22–45, 2015
([Springer](https://link.springer.com/article/10.1007/s10851-014-0506-3)).

**Therefore — the best single finding in this review for the firmware:** multilinear
interpolation is a *linear* operator on stored values, and the 1-D barycenter *is* a
linear average **in quantile coordinates**. So **store the inverse CDF `Q(r)` on a
fixed r-grid of R points in each cell instead of (or beside) the magnitude vector,
plus one scalar for total energy.** Then the existing runtime multilinear
interpolator computes the *exact* 16-marginal W₂ barycenter — not an approximation,
with no change to the interpolation code. The only added runtime work is one
quantile → histogram scatter per block, and its cost is **independent of N and of the
corner count**.

(c) Desktop bake: microseconds per cell, seconds for a lattice. Sinkhorn (Cuturi,
NIPS 2013, [arXiv:1306.0895](https://arxiv.org/abs/1306.0895)), Bregman projections
(Benamou, Carlier, Cuturi, Nenna & Peyré, *SIAM J. Sci. Comput.* 37(2):A1111–A1138,
2015, [arXiv:1412.5154](https://arxiv.org/abs/1412.5154)) and convolutional
barycenters (Solomon et al., *ACM TOG* 34(4), SIGGRAPH 2015,
[code](https://github.com/gpeyre/2015-SIGGRAPH-convolutional-ot)) are **not needed** —
those are for 2-D and up. Tooling: [POT](https://pythonot.github.io/).

(d) **Runtime, doing it online:** with quantiles precomputed offline, per block you
need (1) 16×R MACs for the quantile average (R=128 → 2048 MACs, ~1–2 k cycles),
(2) a 128-iteration scatter with linear splatting into 64 bins (~1 k cycles; 64 floats
= 256 B, fits in DTCM), (3) 16 MACs for energy. **≈3–6 k cycles ≈ 6–13 µs at
480 MHz.** Even building and inverting CDFs at runtime only roughly triples that.
Against the 350 µs budget (spec §2) — of which a 1024-point `arm_rfft_fast_f32`
plausibly eats 30–80 µs (**estimate, unverified — measure it in M1**) — **1-D OT of
16 corners × 64 bins fits with an order of magnitude to spare.** (e) realistic; the
quantile-storage variant is a data-format change.

**Caveat to design around:** on a strictly harmonic 64-bin grid a partial "sliding"
from h10 to h20 occupies fractional positions, renderable only by splitting energy
between adjacent integer harmonics. So OT here gives a **spectral envelope / formant
that walks up the harmonic series**, not a continuous portamento (Henderson & Solomon
get true glides only because a phase vocoder supports non-integer frequencies). Still
a large qualitative win, but sell it as envelope transport. Consider applying OT to a
smoothed envelope and linear interpolation to the fine structure.

### 6.4 Cepstral / log-magnitude morphing

Interpolate `log|X|` (or cepstral coefficients) and exponentiate. Slaney et al. split
the sound into an MFCC-derived **smooth spectrogram** (log filterbank → DCT, i.e.
homomorphic separation of envelope from fine structure) and a residual pitch
spectrogram, and warp *before* cross-fading. The cepstral envelope estimator of record
is A. Röbel & X. Rodet, "Efficient Spectral Envelope Estimation and its Application to
Pitch Shifting and Envelope Preservation", DAFx-05
([PDF](https://www.dafx.de/paper-archive/2005/P_030.pdf)) — the "true envelope"
method.

(b) fully bakeable — store log-magnitudes, one `expf` per bin at runtime (64 ×
~15–25 cycles ≈ 2–3 µs), or a 64-entry `exp2` polynomial. (d/e) trivial, a one-line
change, realistic. **But be honest about what it buys:** log interpolation yields the
*geometric* mean, which does not slide peaks either — it is **not** a substitute for
transport. Two hazards: a bin that is exactly 0 in any corner forces the product to 0
everywhere, so **floor the log at about −90 dB**; and geometric means bias toward
valleys, thinning sparse harmonic spectra. Slaney's split — log/cepstral for the
smooth envelope, linear or OT for the fine structure — is the right shape.

### 6.5 Sinusoidal-model morphing with partial matching

All four references verified real:

- X. Serra & J. Smith, "Spectral Modeling Synthesis: A Sound Analysis/Synthesis System
  Based on a Deterministic plus Stochastic Decomposition", *Computer Music Journal*
  14(4):12–24, 1990
  ([PDF](https://www.audiolabs-erlangen.de/content/05_fau/professor/00_mueller/02_teaching/2024s_sarntal/02_group_SYNTH/1990_SerraS_SpectralModelingSynthesis_CMJ.pdf))
  — the substrate that makes partial-level morphing possible.
- E. Tellman, L. Haken & B. Holloway, "Timbre Morphing of Sounds with Unequal Numbers
  of Features", *JAES* 43(9):678–689, September 1995
  ([AES e-lib #7931](https://aes.org/e-lib/browse.cfm?elib=7931)) — handles the hard
  case where A and B have different partial counts, morphing amplitude *and frequency*
  of matched components (Lemur analysis).
- N. Osaka, "Timbre Interpolation of Sounds Using a Sinusoidal Model", ICMC 1995
  ([proceedings](https://quod.lib.umich.edu/i/icmc/bbp2372.1995.119/1)).
- T. Ezzat, E. Meyers, J. Glass & T. Poggio, "Morphing Spectral Envelopes Using Audio
  Flow", Interspeech 2005
  ([PDF](https://www.isca-archive.org/interspeech_2005/ezzat05_interspeech.pdf)) —
  optical flow applied to log-magnitude envelopes, i.e. learned formant *displacement*
  rather than amplitude cross-fade. Conceptually the closest non-OT relative of
  transport.

(b) **partially bakeable and worth doing.** Offline, match partials between adjacent
lattice corners and resample each corner's spectrum onto a common warped frequency
axis, or subdivide the lattice with true morphed intermediates. (c) desktop seconds
to minutes. (d) zero extra at runtime if baked. (e) full runtime partial tracking is a
research project; **the offline bake is not.**

Also relevant and real: Cazelles, Robert & Tobar, "The Wasserstein–Fourier Distance
for Stationary Time Series", *IEEE TSP*
([arXiv:1912.05509](https://arxiv.org/abs/1912.05509)) — OT geometry on normalised
PSDs; and Valdivia, Renaud, Cazelles & Févotte, "Audio Signal Interpolation Using
Optimal Transportation of Spectrograms", 2025
([arXiv:2502.15430](https://arxiv.org/abs/2502.15430)).

## Candidates for Kyklophoria, ranked

Value = what it does for the *instrument*, specifically for making direction and
distance in the space mean something and making rotation pay. Effort = desktop tooling
plus firmware change, for one person.

| # | Technique | § | Value | Effort | Runtime cost | Verdict |
|---|---|---|---|---|---|---|
| 1 | Fix the cell-centre loudness hole (power-domain storage, or renormalise after interpolation) | 6.2 | **High** — removes a measured −12 dB dip that makes every interior cell dull | **Hours** | <1–2 µs/block | **Build first** |
| 2 | Correlated-random-field / procedural-noise fill (Gaussian field via FFT on the wrapped lattice; simplex noise as the cheap version), at `side` 6–8 | 4.1–4.2 | **High** — the only technique here that is *isotropic by construction*, which is exactly what the rotation feature needs | **Days** — one generator in `kyk_gen.h`, no format change | none | **Build second** |
| 3 | Axis functions as data: a `(q, y₀..y_{N−1})` closed-form recipe per family, Serum-`q`-style, plus Plaits' `make_family` + brightness sorting | 1.2, 1.4 | **High** — generalises the existing Harmonic family, makes new families cheap, and *sorting* is what makes direction meaningful | **Days** | none | Build third |
| 4 | Log-magnitude PCA over AKWF (4358 CC0 single cycles), grid extents scaled by √λᵢ | 3.2, 3.5 | **High** — real material, real timbral distance, a genuine inverse; the only corpus method that resamples onto a grid | **Days** (offline Python) | none | Strong M4 candidate |
| 5 | Store per-cell quantile functions so the existing multilinear interpolator computes the exact 16-marginal W₂ barycenter | 6.3 | **High** — turns cross-fade into transport, the qualitative fix for "the middle sounds like a mix" | **Weeks** — format change + a scatter in the audio path | ~6–13 µs/block | Best long shot; do after 1–2 prove out |
| 6 | Slice authoring: draw 2-D grids, fill the volume by RBF/GP (Turk & O'Brien) with the §4 field as the GP prior | 5.2 | **High** *if the UI exists* — solver is 0.73 ms, the work is entirely the authoring drawer | **Weeks** (mostly web UI) | none | The right M4/M6 story |
| 7 | WaveEdit's 12 effect scalars as axes; Vital's modifier taxonomy | 1.1, 1.3 | Medium — proven, legible, but strictly parameter-product geometry | Days | none | Cheap extra families |
| 8 | Chebyshev waveshaping and Casio-CZ phase distortion families | 1.5 | Medium — closed-form, monotone brightness, genuinely continuous | Hours each | none | Free wins, add opportunistically |
| 9 | Real-cepstrum axes (DCT of log magnitudes, no mel warping), truncated to N coefficients | 3.4 | Medium — smooth envelope axes with an exact inverse and no corpus needed | Hours | none | Good sanity baseline |
| 10 | Offline partial-matching morph to subdivide the lattice (Tellman/Haken/Holloway, Ezzat audio flow) | 6.5 | Medium | Weeks | none | Only if 5 is rejected |
| 11 | Solid texture synthesis from 2-D exemplars (Kopf et al.) | 5.3 | Medium — literally "author 2-D, get N-D", but the most code in this document | Weeks | none | Research-flavoured |
| 12 | Wavespace and other wavetable VAEs, baked offline | 2 | Medium — no published code; latent space is factorized into 2-D planes, i.e. our own separability problem restated | **Months** (reimplement + train) | none | Research project |
| 13 | Esling 2018 perceptual-VAE, FlowSynth, SPINVAE-2, RAVE | 2 | Low for content — wrong output domain (tones, presets, streams), right ideas | Months | none | Research project; mine the ideas, not the models |
| 14 | t-SNE / UMAP over a corpus | 3.3 | **Low** — no usable inverse, and Distill's result says inter-cluster distance is not meaningful, which fails our premise at the root | Days | none | **Skip** |
| 15 | Radon / filtered back-projection authoring | 5.1 | **Low** — a projection is the average spectrum along a line; nobody can hear an integral | Days | none | **Skip** |
| 16 | MDS timbre spaces (Grey/Wessel/McAdams) | 3.1 | Low as a generator — no coordinate → spectrum map at all | Months (listening study) | none | Cite it, don't build it |

### What I would build first, and why

**Build (1) this week: the loudness fix.** It is hours of work, it is provably
correct (§6.2 gives the arithmetic), and it is a *precondition for judging every other
item on this list*. Right now a −12 dB hole sits at the centre of every cell in the
space; any A/B test of a new content generator is being run through that hole, so
none of the results mean anything until it is gone. Store squared magnitudes in the
`.kyk` file (a `flags` bit, per docs/space-format.md §Versioning) or renormalise after
interpolation — either, not both. The renormalise version is the one that needs no
format change, so do that first and consider power-domain storage later.

**Then build (2): the correlated random field family.** This is the actual answer to
the question this document was written to ask. A parameter grid cannot make rotation
pay, because a separable space has privileged axes by definition; a stationary
isotropic random field has *no* privileged axes, because its covariance depends only
on distance. That is not a heuristic, it is the defining property, and it means a
rotation of the control frame lands on material that is statistically identical and
concretely different — which is precisely the experience the instrument promises
(spec §3.4). It is also nearly free: one 5-D FFT of 16 K complex points at bake time,
zero at runtime, one new function in `core/kyk_gen.h`, and no format change. Two knobs
(`ℓ_space`, `ℓ_k`) plus a seed give an unlimited supply of spaces, and the wrapped
topology we already implement is exactly the domain on which the FFT construction is
*exact* rather than approximate.

Two things must ship with it or it will disappoint. **Raise `side` to 6 or 8 for noise
spaces** — with only 4 points per axis there is no room for a correlation length
(§4.3a), and the cost is 1.18 MB in a 64 MB SDRAM. And **keep the field's correlation
length at 2–3 cells**, so that multilinear interpolation is reconstructing a
band-limited field rather than aliasing a white one (§4.3b).

After those two, the ordering is: (3) make the axis functions data so new families
cost an afternoon; (4) the AKWF log-magnitude PCA family, which is where *real*
material enters the instrument; and then (5) quantile storage, which is the one change
that would make the space's interior sound like transport rather than mixture. Item
(6), slice authoring, is the right long-term story for the web surface, and note the
happy fact from §5.2 that a Gaussian process conditioned on authored slices and the
Gaussian random field of §4.2 are **the same machine** — so (2) and (6) are one
codebase, entered from two ends.
