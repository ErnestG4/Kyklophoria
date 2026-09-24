# Literature review: ML and signal-processing techniques for the fitter and the exciters (23 Sept 2026)

This review picks up where `lit-fitting.md` (the fitter's gates and loss) and `lit-runtime.md`
(commuted synthesis, waveguide above the modes, B per note, Weinreich, Bank's Physis)
leave off. It does not repeat those two. It covers the four open questions from
`Kyklophoria/docs/feedback-2026-09-23.md`: neighbouring notes that jump, a waveguide heard as
"a separate wave on top", owning the exciter, and the noise "snare chain". Each claim has a label:
**MEASURED** (a paper, or our own code, gives the number), **REASONED** (it follows from something
measured), or **HUNCH**.

## Top 5 things we are likely missing

### 1. A parametric model of the whole keyboard to fit each note against

**What it is.** The piano literature does not fit notes independently. It fits a few curves over
the whole keyboard and lets each note deviate from them:
- Inharmonicity B(m) as the sum of two exponentials in MIDI note, one asymptote for the bass and
  one for the treble (Rigaud, David & Daudet).
- A tuning curve (octave stretch) derived from B.
- Per note, a damping law that is a function of frequency, σ_k = b1 + b3·f_k² (Chaigne & Askenfelt;
  Bensa et al.), in place of one free decay per mode.

DDSP-Piano (Renault, Mignot & Roebel) builds these curves in explicitly. It freezes the frequency
sub-models at their physical initial values while the rest of the network trains, and adds a
two-string detuner per note.

**Why it matters for us.** The timbre jumps and "resonance groups" that Combust hears are what
independent per-note fits produce. The refit from neighbours that we are piloting fixes the
symptom one note at a time. A backbone would give the neighbour-smoothness prior on the roadmap
something concrete to be smooth in. Two ways to do it:
- (a) Explicit. Fit B(m), b1(m), b3(m) and a smooth spectral envelope over partial index. Then
  penalise each note's free modes for straying from the backbone, with a graph-Laplacian penalty
  between neighbouring notes. Match modes between notes by partial index for strings, or by the
  pairing we already use for interpolation (bars, bells).
- (b) Amortised. Train one small MLP across the set that maps (midi, velocity) to per-partial
  offsets from the backbone. Then bake every semitone from the network rather than interpolating
  at runtime.

MEASURED (DDSP-Piano JAES 2023 and its README): with frequencies taken from the explicit
inharmonicity and detuning models, the second training phase that fine-tunes those frequencies
"does not improve the model quality". Frequency by gradient descent is still called unsolved there.

REASONED: the backbone also gives us B per note, which `lit-runtime` wants for the waveguide above
the modes.

HUNCH: a network-only prior (option b without the backbone) will smooth over real register breaks:
the piano's bass-bridge break, the change from one string to two to three per note, and the
Wurlitzer's reed-size groups. Keep explicit break points, or use option a.

**Cost.** Option a: 2–4 days in `modalfit.py` plus a set-level driver, then a refit (~3 GPU hours
a campaign). Option b: about a week.

**Sources.**
- Rigaud, David & Daudet, "A parametric model and estimation techniques for the inharmonicity and
  tuning of the piano", JASA 133(5) 2013, https://doi.org/10.1121/1.4799806
- Renault, Mignot & Roebel, "DDSP-Piano", JAES 71(9) 2023, https://aes.org/publications/elibrary-page/?id=22231 ;
  code https://github.com/lrenault/ddsp-piano

### 2. A time-domain final stage: variable projection plus an ESPRIT cross-check

**What it is.** Our modes are exactly damped complex exponentials. Given the poles, the complex
amplitudes (level and phase together) are a linear least-squares problem. Variable projection
(Golub–Pereyra) optimises only the poles and solves the amplitudes in closed form. ESPRIT and the
matrix pencil method (Laroche 1993; Badeau, David & Richard, with the ESTER order criterion) give
poles directly from the signal, including closely spaced pairs and fast decays.

Hayes, Saitis & Fazekas show why the time domain matters. Their complex surrogate Re(zⁿ) is a
damped sinusoid, trained on time-domain MSE, and it is what converges.

**Why it matters for us.**
- Our loss is sign-blind (STFT magnitude), and the phases are set afterwards from the residual.
- A final stage that is aware of phase fits phase jointly.
- It turns the cluster problem (large antiphase pairs cancelling) into a conditioning problem that
  ridge least squares controls directly.
- ESPRIT on each note's first ~300 ms after the burst is an independent check that flags the bad
  local optima. Our "strong fast-decaying mode 35 cents off the fundamental" either appears in
  ESPRIT's poles (then it is real, probably a doublet or two-stage decay) or does not (then it is a
  fitting artefact).

MEASURED (Hayes 2023): on time-domain MSE the surrogate approaches the Cramér–Rao bound for one
sinusoid. With a DFT-magnitude loss, the mean squared frequency error stalls near −83 dB, which the
authors attribute to phase being discarded. It beats a real-sinusoid baseline on mixtures of 2, 8
and 32 sinusoids. Failure mode: several components combine to match one target, which is our
cluster penalty's problem.

MEASURED (Laroche 1993): matrix pencil "achieves excellent results" for highly damped signals,
short records and close frequencies.

REASONED: with poles from the STFT fit, the least-squares amplitude step is milliseconds per note
on the GPU.

**Cost.** 1–2 days for varpro with ridge. ESPRIT is ~100 lines of numpy (see
https://arxiv.org/pdf/0909.0885 for the modal version with a filter bank).

**Sources.**
- Hayes et al., ICASSP 2023, https://arxiv.org/abs/2210.14476
- Laroche, JASA 94(4) 1993, https://pubs.aip.org/asa/jasa/article-abstract/94/4/1958/970799/
- Badeau et al., "High-resolution spectral analysis of mixtures of complex exponentials modulated
  by polynomials", IEEE TSP 2006, https://perso.telecom-paristech.fr/grichard/Publications/SP06_Badeau2.pdf.
  Its PACE model (polynomial times exponential) is the principled form of the "non-exponential
  attack" that the fitter builds out of antiphase pairs.

### 3. Loss settings that make the frequency gradient informative

**What it is.** Schwär & Müller trace the frequency-gradient failure of the multi-scale spectral
(MSS) loss mostly to spectral leakage. Three things make it worse:
- Hann or rectangular windows (high sidelobes).
- log(x + ε) compression, which deepens the window's spectral zeros.
- Power-of-two FFT sizes, whose bin grids line up.

They recommend a flat-top window, log(1 + γx) compression, and window sizes that are prime or
otherwise mutually unaligned. Separately, spectral optimal transport (Torres, Peeters & Richard)
uses a 1-D Wasserstein distance along the frequency axis. It still pulls a peak towards its target
when the main lobes do not overlap, where the MSS gradient is zero.

**Why it matters for us.** We use exactly the configuration they fault:
- `torch.hann_window` in `stft_mag`
- `torch.log(mag + 1e-4)`
- sizes 4096/1024/256

The fix is a few lines and needs no rework of the method.

MEASURED (Schwär & Müller): gradient-sign accuracy is near 0.5 (random) for the original MSS at
0.3–3 cent steps and high for "smooth MSS" down to 0.3 cents. In their DDSP autoencoder, raw pitch
accuracy was 0.81 ± 0.09 with smooth MSS, against 0.01 for the original and 0.27 for modified Hann.

REASONED: our phase-vocoder initialisation already puts f within about a bin, so the gain is
largest where a bin is wide in cents: the top octaves, and the 256-point scale everywhere. It will
not by itself fix a mode that has converged to a different job (item 2).

**Cost.** Hours. SOT is pip-installable. Anneal it in over the first ~20% of steps.

**Sources.**
- Schwär & Müller, IEEE SPL 30 2023, https://ieeexplore.ieee.org/document/10319088/
- Torres et al., ICASSP 2024, https://arxiv.org/abs/2312.14507 ; code
  https://github.com/bernardo-torres/spectral-optimal-transport

### 4. Couple the exciter to the bank through feedback, not by summing

**What it is.** Every playable physical model that "sounds like one instrument" closes a loop. The
nonlinearity computes a force from the resonator's own state at the contact point (velocity or
displacement, a weighted sum of the modes) and feeds that force back into the modes. Examples:
- **Mutable Instruments Elements (MIT-licensed code).** Its bowed modes are Essl & Cook *banded
  waveguides*: per mode, a delay line of one period through a narrow band-pass. The bow nonlinearity
  (`BowTable`) reads the sum of those modes and writes back into their shared input. The same
  resonator also takes the exciter's `damping_` while the gate is held. (Checked in
  `elements/dsp/resonator.cc`.)
- **The Sound Design Toolkit.** Its interactors (Hunt–Crossley impact, elasto-plastic friction)
  sit between modal resonators.
- **Clarinet synthesis from a modal fit of the bore impedance** (Taillard, Silva, Guillemain &
  Kergomard). The reed's nonlinearity is driven by the sum of the modes' pressures.
- **Energy-quadratised modal networks** (Ducceschi, Bilbao & Webb, DAFx 2023), and the
  open-source `nlm` externals (Diaz, 2026). These are explicit, stable, and real-time.

**Why it matters for us.** Our waveguide is summed beside the modes, which is why Combust hears "a
separate wave" on the Wurlitzer and "a spring" on the piano. REASONED: in a feedback coupling the
nonlinearity's harmonics excite only what the resonator passes. The result is a change in the
note's timbre that follows its amplitude, which is what "modulates" means.

For the Wurlitzer "Beooooo", the pickup gap form we already fit is the right place. HUNCH: it will
only sound right if it is driven by the physical reed displacement, a position-weighted sum of the
modes, rather than a sum of levels, so that its harmonics fall as the reed decays.

For bow, blow and reed on the Daisy, Elements shows the cost: 8 banded waveguides next to 64
filters on a Cortex-M4 at 32 kHz. REASONED: the M7 can afford it on at least one voice.

**Cost.** 1–2 weeks for a first bow/reed/strike set in the runtime, plus an offline prototype.

**Sources.**
- Elements code, https://github.com/pichenettes/eurorack/tree/master/elements/dsp
- Essl & Cook, "Banded waveguides", ICMC 1999, https://soundlab.cs.princeton.edu/publications/1999_icmc_bar.pdf
- Sound Design Toolkit, https://sdt.lim.di.unimi.it/
- Taillard et al., Applied Acoustics 141 2018,
  https://www.sciencedirect.com/science/article/abs/pii/S0003682X1730350X
- Ducceschi, Bilbao & Webb, DAFx 2023,
  https://www.research.ed.ac.uk/en/publications/real-time-modal-synthesis-of-nonlinearly-interconnected-networks/
- nlm, https://arxiv.org/abs/2603.10240

### 5. A mode budget based on how much error the ear tolerates

**What it is.** Measured limits of what the ear notices, used to decide where modes are spent:
- Decay: MEASURED (Järveläinen & Tolonen 2001), 25–40% variations in the decay time constant of
  plucked-string tones were inaudible.
- Inharmonicity is audible mainly in the bass (already in `lit-runtime`).
- Interactive modal synthesis culls modes that fall under the masking threshold of louder ones
  (Raghuvanshi & Lin 2006).
- Gabrielli et al. used a psychoacoustic model to decide which Rhodes attack partials matter.

**Why it matters for us.**
- It sets the tolerance our `decay_ratio` gate should accept (about 0.75–1.35×), which is REASONED
  from the 25–40% figure.
- It shows how coarse the decay byte can be.
- It gives a principled per-voice cull, so a piano E1 could have 96 modes while a treble note has
  12, inside the same cycle budget.

The gate already fails far outside that band ("ring 12× too short"). The fix is a tolerance, not a
new metric.

**Cost.** 1 day offline (a masking pass at export sets a per-point count). 1–2 days runtime (skip
ghosts, count-driven loop).

**Sources.**
- Järveläinen & Tolonen, JAES 49(11) 2001, https://www.aes.org/e-lib/browse.cfm?elib=10173
- Raghuvanshi & Lin, https://www.researchgate.net/publication/215514876
- Gabrielli et al., JASA 148(5) 2020, https://pubs.aip.org/asa/jasa/article/148/5/3052/631688/

## 1. Differentiable fitting of damped sinusoids

- **Hayes, Saitis & Fazekas 2023**, "Sinusoidal frequency estimation by gradient descent",
  https://arxiv.org/abs/2210.14476. Covered in Top 5 #2. Also MEASURED: the surrogate solves
  mixtures "one component at a time", so the loss falls to a series of plateaus. A plateau is not
  convergence, which is relevant to our step counts and our early stopping.
- **Turian & Henry 2020**, "I'm sorry for your loss: spectrally-based audio distances are bad at
  pitch", https://arxiv.org/abs/2012.04572. The original diagnosis: spectral distances give no
  stable frequency gradient.
- **Schwär & Müller 2023**: Top 5 #3.
- **Torres, Peeters & Richard 2024** (SOT): Top 5 #3.
- **Vahidi et al. 2023**, "Mesostructures: beyond spectrogram loss", https://arxiv.org/abs/2301.10183 ;
  code https://github.com/cyrusasfa/meso-dtfa. Joint time–frequency scattering (JTFS) as a loss
  that tolerates time shifts beyond the window. REASONED: marginal for us. Our targets are aligned
  single notes, and JTFS is expensive.
- **Han, Lostanlen & Lagrange 2023**, "Perceptual-Neural-Physical sound matching",
  https://arxiv.org/abs/2301.02886. A quadratic approximation of a JTFS loss, precomputed in
  parameter space, trained on drum-like physical models. Relevant if we amortise (Top 5 #1b).
- **Salimi, Hindle & Zaiane 2025**, https://arxiv.org/abs/2506.22628. MEASURED: which loss works
  best depends strongly on the synthesiser; there is no universal winner. So we should test our own
  loss choices on our own synthetic modal notes, which `modalfit_check.py` can host.
- **Diaz et al. 2023** (differentiable modal resonators) and **Lee et al. 2024** (planar string)
  are already in `lit-fitting.md`. New from the same group: **Diaz & Sandler, DAFx 2025**, "Fast
  differentiable modal simulation of non-linear strings, membranes and plates" (JAX),
  https://arxiv.org/abs/2505.05940. MEASURED claim: it recovers tension, stiffness and geometry,
  but fitting physical parameters "is more sensitive to initialisation" than fitting free modes.
  That supports keeping free modes with a physical prior rather than fitting physics directly.
- **High-resolution estimators.**
  - Laroche 1993 (matrix pencil), Badeau et al. 2006/2008 (ESPRIT, PACE, ESTER), and Ege, Boutillon
    & David 2009 (https://arxiv.org/abs/0909.0885). Use these as initialisers or cross-checks, and
    on residuals for doublets. Top 5 #2.
  - Karjalainen et al., JAES 50 2002, "Estimation of modal decay parameters from noisy response
    measurements", https://aes.org/publications/elibrary-page/?id=9992. Fits an exponential plus a
    stationary noise floor by nonlinear least squares. REASONED: this is the right estimator for the
    *measured* T60 in our `decay_ratio` gate on mp3 and noisy sources. A line fit biases long decays
    short once they hit the floor.
- **Lu & Reiss 2026**, "Band-count dense modal estimation…", https://arxiv.org/abs/2608.00667, for
  the DAFx-25 Parameter Estimation Challenge (https://github.com/LOGUNIVPM/1st-DAFx-Challenge).
  MEASURED: predicting mode counts per band first, then refining decay and gain with frequency fixed,
  cut the challenge error by ~66% against peak picking. Decay and gain remained the largest error
  sources. REASONED: this separates "how many modes" from "fit them", which is our 48-vs-96
  question. The challenge repo is also a free benchmark for our fitter.

## 2. Fitting across the keyboard

- **Rigaud, David & Daudet 2013** (above). Two-asymptote B(m) and a tuning model, estimable from
  isolated notes *or chords*. DDSP-Piano initialises from its constants (bass αB = −0.0847,
  βB = −5.82; treble αT = 0.0926, βT = −13.64).
- **Renault, Mignot & Roebel 2022/2023**, DDSP-Piano (above; also DAFx 2022). Explicit
  inharmonicity plus a two-string detuner per note (tanh-bounded), a per-piano embedding, a
  recurrent network for envelopes, filtered noise for hammer and pedal noise, and an L1-regularised
  reverb. MEASURED claim: the reverb module "tried to model the notes' sustain" until regularised.
  REASONED: that is our cluster and aftersound problem under another name. A free component absorbs
  whatever the structured ones cannot.
- **Simionato, Fasciani & Holm 2024**, "Physics-informed differentiable method for piano modeling",
  Frontiers in Signal Processing, https://riccardovib.github.io/Physics-Informed-Differentiable-Piano_pages/.
  Partials from physical formulas with parameters learned across keys and velocities. The editorial
  reports that it generalises across the range, but high partials at loud dynamics are the weak
  spot. The follow-up is the sines/transient/noise paper already cited in `lit-fitting`.
- **Chaigne & Askenfelt 1994**, "Numerical simulations of piano strings I/II", JASA 95.
  https://pubs.aip.org/asa/jasa/article/95/2/1112/830873/ . And **Bensa, Bilbao, Kronland-Martinet
  & Smith 2003**, JASA, https://hal.science/hal-00088329/document. The loss law σ = b1 + b3·ω² makes
  decay-versus-frequency two numbers per note, so it can be the smoothness variable for decays.
- **Jin et al. 2024**, "DiffSound", SIGGRAPH, https://arxiv.org/abs/2409.13486. **Ren, Yeh & Lin
  2013**, "Example-guided physically based modal sound synthesis", ACM TOG. Both infer material
  parameters (a damping law) from one recording and transfer them to other shapes. This is the
  "fit a law, not modes" idea for bodies and bars.
- HUNCH: for bars and bodies (marimba, xylophone, perc), the natural backbone is the
  Euler–Bernoulli ratio series of a bar with a tuning undercut, using the FEM we already have in
  `modalfem`, rather than a harmonic comb.
- REASONED: the marimba F3 "roll" and F6+ repeats in the feedback are data problems (a roll
  recorded as a note). A keyboard prior would flag them as outliers against their neighbours.

## 3. Piano and EP modelling

- **Bank & Chabassier 2019**, "Model-based digital pianos: from physics to sound synthesis", IEEE
  SPM 36(1), https://hal.inria.fr/hal-01894219. The survey to start from. It covers hammer,
  string, soundboard, phantom partials and real-time trade-offs.
- **Bank & Sujbert 2005**, "Generation of longitudinal vibrations in piano strings", JASA 117(4),
  https://home.mit.bme.hu/~bank/publist/jasa-longitud/index.html. Phantom partials at sums of
  transverse mode frequencies, from a one-way modal model that runs in real time. REASONED: in our
  bank these are cheap extra modes at fk+fl with levels tied to the product of the parents' levels.
  They are what makes a low piano note "piano", and a fitter with no concept of them will spend
  free modes on them or miss them.
- **Aramaki, Bensa, Daudet, Guillemain & Kronland-Martinet 2001**, "Resynthesis of coupled piano
  string vibrations", JNMR 30(3), https://www.tandfonline.com/doi/abs/10.1076/jnmr.30.3.213.7472.
  Doublets, beats and double decay from two coupled strings. Weinreich is already in `lit-runtime`.
  HUNCH: our "fast mode 35 cents off the fundamental, ringing 12× too short" is the fitter building
  a two-stage decay out of a spurious third mode. A per-partial doublet parameterisation (two
  frequencies, a split and a coupling) would give it a legitimate way to do it.
- **Pfeifle, DAFx 2017**, "Real-time physical model of a Wurlitzer and Rhodes electric piano",
  https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf. FPGA models. The Wurlitzer
  pickup is a time-varying capacitor between the grounded reed and a charged plate (measured 130 V,
  against 170 V in the manual). The Rhodes pickup's field geometry sets the timbre through
  tine-to-magnet offset. Pfeifle & Münster's book chapter "Tone production of the Wurlitzer and
  Rhodes E-pianos" has the measurements.
- **Gabrielli, Cantarini, Castellini & Squartini 2020**, JASA 148(5). The Rhodes' inharmonic attack
  modes, measured by laser vibrometry and ranked by a psychoacoustic model. Companion code:
  https://github.com/LOGUNIVPM/rhodes-companion-files.
- **Transients and noise.** Verma & Meng 2000, "Extending SMS with transient modeling synthesis",
  CMJ 24(2), https://direct.mit.edu/comj/article-abstract/24/2/47/93437/. Shier, Caspe et al. 2023,
  "Differentiable modelling of percussive audio with transient and spectral synthesis",
  https://arxiv.org/abs/2309.06649, which uses a temporal convolutional network (TCN) for the
  transient on top of sinusoids and noise. NoiseBandNet 2023, https://arxiv.org/abs/2307.08007.
  REASONED, for the snare chain: in all of these the noise has its own short envelope tied to the
  event. The fix the lead proposes (noise lives while the contact does, and is choked with its
  voice) matches the literature. The better form is to feed contact noise *into* the modes, as
  Elements does, rather than keeping twelve free bands with their own decays beside them.

## 4. Exciters and waveguides (Rings/Elements-style)

- **Elements** (code above): exciter models are bow, blow (a `tube.cc` waveguide with a reed-like
  nonlinearity), strike (a mallet pulse through a velocity-set SVF), plectrum, particles, granular
  sample, and noise. The exciter also outputs a *damping* signal to the resonator. REASONED: this is
  our velocity low-pass on the burst, plus contact damping, and it is the template for Combust's
  "blow to bow to reed to pluck to strike".
- **Essl & Cook 1999/2000**, banded waveguides, and "Measurements and efficient simulations of
  bowed bars", JASA 108(1), https://soundlab.cs.princeton.edu/publications/2000_jasa_bowedbar.pdf.
  The standard way to bow a modal bank, because a friction nonlinearity needs each mode to have a
  round-trip delay.
- **Smith & Van Duyne** commuted synthesis is already in `lit-runtime`. HUNCH: our stored burst
  plus a feedback exciter can coexist. The burst carries the recorded body and hammer thump. The
  exciter supplies playability.
- **Ducceschi, Bilbao & Webb 2023; Bilbao, Ducceschi & Zama 2023** (JCP, explicit energy-conserving
  methods); **Zheleznov, Bilbao, Wright & King 2026**, "Stable differentiable modal synthesis for
  learning nonlinear dynamics", https://arxiv.org/abs/2601.10453. Scalar auxiliary variable (SAV)
  schemes make nonlinear coupled modal systems explicit and stable. The 2026 paper *learns* the
  nonlinearity from data while keeping the modes analytic. HUNCH: this is how to learn a pickup or
  contact nonlinearity from recordings without giving up our modal bank.
- **Differentiable waveguides.** Nothing mature and open found beyond DDSP guitar and
  Karplus–Strong variants. REASONED: for our purposes, feedback coupling matters more than
  differentiability. Fit the modes offline, and hand-design the exciter's nonlinearity with a few
  fitted constants: bow-table slope, reed stiffness, hammer felt exponent (Chaigne & Askenfelt;
  Stulov).
- **Bank 2007**, "Direct design of parallel second-order filters for instrument body modeling",
  ICMC, https://home.mit.bme.hu/~bank/parfilt/. Fixed log-spaced poles, zeros by least squares,
  robust to order ~1000. REASONED: a cheap, fittable body or coil EQ, the "one coil per set"
  generalised, and a candidate for the body a synthetic exciter needs once the recorded burst is
  gone.

## 5. Cheap wins on the embedded engine

- **Perceptual tolerances and culling**: Top 5 #5.
- **Bonneel et al. 2008**, "Fast modal sounds with scalable frequency-domain synthesis", SIGGRAPH,
  https://inria.hal.science/inria-00607249/file/FastModalSounds.pdf. MEASURED 5–8× speedups by
  summing modes in the STFT domain. REASONED: not worth it at 48 modes × 4 voices with 24-sample
  blocks, but it matters if a voice ever runs 200+ modes (gong, cymbal).
- **Langlois, An, Jin & James 2014**, "Eigenmode compression for modal sound models", ACM TOG,
  https://dl.acm.org/doi/10.1145/2601097.2601177. Compresses per-position mode shapes. Relevant if
  the neck and position axes grow to many positions per world.
- **Interpolating between notes.** Tellman, Haken & Holloway 1995, "Timbre morphing of sounds with
  unequal numbers of features", JAES 43(9). Henderson & Solomon 2019, "Audio transport: a
  generalized portamento via optimal transport", DAFx, https://arxiv.org/abs/1906.06763. Our
  pairing, linear in frequency and geometric in decay and level, is the Tellman/Loris line.
  Optimal transport is the principled version when mode counts differ: mass splits instead of
  unmatched partials fading. REASONED: use OT offline to *choose* the pairing (a small Sinkhorn per
  pair of neighbouring points). Keep the cheap blend at runtime.
- **Quantisation.** REASONED from Järveläinen & Tolonen: a decay byte whose step is ≤ 10% in T60 is
  far below audibility. Check `.kykm` v6's decay-byte step, and if it is finer than needed, spend
  the bits elsewhere, for example on a doublet split.

## Already doing it / validated

- **Initialise from analysis, move frequency slowly.** Phase-vocoder init and a 1/40 learning rate
  on frequency are what Diaz 2023, Lee 2024 and DDSP-Piano do. DDSP-Piano goes further and freezes
  frequency. Validated.
- **Log-domain parameters** (log f, log r, log a). Standard.
- **Multi-resolution STFT with a linear plus log term**, weights from Diaz 2023. Standard; the
  window and compression details are Top 5 #3.
- **Cluster penalty.** Hayes 2023 names the same failure: several components cancelling or merging
  to match one. Validated as a real phenomenon. Varpro with ridge (Top 5 #2) is the other cure.
- **Phases from the residual, because the loss is sign-blind.** Hayes 2023 MEASURED that magnitude
  losses lose the phase-dependent accuracy. Our workaround is sound; a time-domain polish would
  replace it.
- **Burst as commuted excitation, velocity-filtered.** Smith & Van Duyne; Elements' mallet through
  an SVF. Validated.
- **Sines, burst and noise decomposition.** Verma & Meng TMS; Simionato 2024; Shier 2023. Same
  architecture.
- **Monotonic velocity swings, one coil per set.** These are physical priors of the same kind as
  DDSP-Piano's explicit sub-models and the L1 regulariser on its reverb.
- **Refit bad notes from good neighbours.** A continuation or multi-start strategy. Sound, and a
  special case of the keyboard backbone (Top 5 #1). The backbone makes it systematic rather than
  gate-triggered.
- **Pickup nonlinearity as the EP's "sound".** Pfeifle 2017 and Gabrielli 2020 agree the pickup
  shapes the timbre. It is correctly placed as an output nonlinearity on the modal displacement.
- **Coherence, prominence and decay gates** (from `lit-fitting`), and **count-first thinking** for
  bass notes (48 → 96). Consistent with Lu & Reiss 2026.
