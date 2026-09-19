# Fitting modal models to recordings: what the literature says, and what we took

Written for the fitter's second pass, after the first pass put partials into
the resynthesis of a guitar and the top Wurlitzer notes that the instruments
never made — a whistle, feedback. This is what was read, what each source
contributes, and which of it is now in `tools/modalfit.py`.

## The problem in one sentence

A sum of decaying sinusoids fitted by gradient descent will happily explain
noise, hum, mp3 artefacts and room resonance as modes, because each of those is
a narrow ridge in a spectrogram and a sinusoid that never decays is the best
narrow ridge there is. The fitter has to be told what a mode *is* before it is
allowed to fit one, and the loss has to stop rewarding tone where the recording
has none.

## Sources

**Diaz, Mizrahi, Cao, Peterson, Rodriguez — "Rigid-body sound synthesis with
differentiable modal resonators", ICASSP 2023 (arXiv 2210.15306;
github.com/rodrigodzf/neuralresonator).** Differentiable bank of second-order
IIR resonators, trained end to end against FEM-synthesised strikes. What we
took: the loss. Linear mel-spectrogram L1 with weight 1.0 plus log mel L1 with
weight 0.1. Our first pass weighed log magnitude equally with spectral
convergence, and the floor's two thousand bins outvoted the partials: the
optimiser filled the floor with tone because that moved the mean of a log
error, and the loud modes' decay went wrong by 4× because two loud bins do not
move a mean over two thousand. The 0.1 is their number and it is now ours.
Also from them: parameterise decay and frequency in the log domain, which we
already did, and expect frequency to be the hard axis — see the last section.

**Sinusoidal modelling, from McAulay–Quatieri (1986) through the IRCAM partial
trackers (Depalle, Garcia, Rodet; and Lagrange, Marchand, Rault, "Enhancing
the tracking of partials", IEEE TASLP 2007).** A peak in one frame is not a
partial. A partial is a peak whose *phase advances consistently* with its
frequency from frame to frame, and whose amplitude follows a trajectory. The
phase vocoder's frequency estimate is the deviation of the phase advance from
the bin's expected advance; a stable sinusoid has a stable deviation, and noise
has a deviation that wanders the full circle. What we took: the coherence
gate. A candidate has to keep its phase-advance deviation within a third of a
radian (standard deviation, over the frames it is above its own floor) to be
admitted. That gate alone removed most of the guitar's ghosts: on the E5 the
initialiser rejected 619 candidates and kept 12, and the twelve are the
guitar.

**High-resolution modal analysis — Ege, Boutillon, David, "High-resolution
modal analysis", J. Sound Vib. 2009 (arXiv 0909.0885); and Badeau, David,
Richard on ESPRIT and the ESTER criterion (IEEE TSP 2006).** Two things. First,
ESPRIT-type subspace methods estimate frequency *and damping* directly from the
signal's covariance, at resolutions well past the Fourier limit, and are the
standard for closely spaced, fast-decaying modes (a piano's doublets, a bell's
warble). We did not adopt ESPRIT — the gradient fit already gets frequency to
under a cent on synthetic tests and the failure was not resolution — but it is
the method of record if two modes within a bin ever matter, which for the
Rhodes doublet they may. Second, and taken: **model order selection**. ESTER
picks the number of sinusoids by where the signal subspace stops being one;
the untrained equivalent is "stop admitting candidates when they stop looking
like modes", which is what the prominence gate is — a candidate must stand
8 dB above the median of the two octaves around it in the mean spectrum, so a
noise ridge that is only loud relative to its neighbours' noise is not
admitted. The first fitter admitted the loudest forty peaks regardless of what
they were.

**Sines + transients + noise decomposition for piano, arXiv 2409.06513 (2024).**
A piano note, and any struck note, is three things: the modes, a short
broadband attack, and a residual that is noise. Fitting the modes to the whole
signal forces them to explain the other two, and the attack in particular
produces spurious high-frequency partials with very short decay times — which
is exactly what the initialiser saw in the guitar's first 30 ms. What we took:
the decay gate. A candidate's log amplitude over its above-floor frames has to
fit a line with negative slope and r² ≥ 0.5; a ridge that is flat (hum, room)
or erratic (noise) is not a mode. What we have not taken yet: fitting the
attack as its own thing. The residual after the modal fit is the exciter, and
the roadmap's exciter question and this paper's transient layer are the same
question.

**Modal analysis of room impulse responses, DAFx 2018 (Rasmussen / the
Aalborg group); and "Differentiable modal synthesis for physical modelling of
planar string", arXiv 2407.05516 (2024).** The first is the reminder that
recording rooms have modes too, at low frequency, with long decays, and that a
fitter cannot tell a room mode from an instrument mode; the 50 Hz component in
the Wurlitzer set, which scrambled the rank alignment until the packer placed
partials by harmonic number, is the hum equivalent. We have no fix beyond the
gates; a second recording in a second room is the fix. The second paper is the
reason to be careful with the loss's frequency axis: it shows the same
non-convexity we hit — a mode initialised more than half a bin from its target
sees a gradient that points the wrong way — and it, like Diaz, lands on
initialising from an analysis and moving frequency slowly. Our frequency
learning rate is 1/40 of the others' (5e-4 vs 2e-2) and the phase-vocoder
initialisation is what makes that enough.

**Asymmetric spectral losses** are folklore more than literature — they appear
in speech enhancement (over-suppression penalties) and in some DDSP variants —
but the idea is simple and it is the third thing the pass added: penalise the
model's log magnitude *above* the target's, only in cells where the target is
at its floor. Energy the instrument never made is the definition of a whistle,
and the symmetric losses were indifferent to it because a few cells of excess
cost less than a few cells of missing partial. The term is `relu(log|Y| −
floor)` averaged over the target's quiet cells, weight 1.0, and it is also the
metric: the record carries the resynthesis's mean excess in dB, so a whistle is
a number before it is a listening test.

## What the pass is

1. **Initialise** from the phase vocoder (8192-point, hop 512): local maxima of
   the mean spectrum, each admitted only if it passes prominence (8 dB over
   the two-octave median), coherence (phase-advance deviation σ ≤ 0.33 rad),
   and decay (negative slope, r² ≥ 0.5 over ≥ 6 frames).
2. **Fit** by Adam with cosine decay, log frequency at 5e-4 and the rest at
   2e-2, against three STFT scales (4096/1024/256): spectral convergence +
   0.1 × floored log L1 + 1.0 × asymmetric excess, the floor being the target's
   20th percentile per scale.
3. **Validate**: drop any mode the recording does not show 6 dB above its floor
   at that frequency during the mode's own first half-second.
4. **Polish** (the batch driver): refit the survivors for half the steps, since
   removing a spurious mode changes the optimum for the rest.

Measured on the offending files, excess over the target's floor: Wurlitzer C7
3.08 → 0.73 dB, guitar E5 3.71 → 0.08 dB, guitar E2 0.82 → 0.20 dB; a note that
was fine (Wurlitzer C2, 0.18 dB) stayed fine.

## What is not done, in order of how much it would matter

- **Attack as a separate layer.** The residual after the fit is the exciter.
  Fitting it — as a short filtered burst — would take the pressure off the
  high modes and give the runtime its mallet for free.
- **ESPRIT for doublets.** When two modes sit within a bin (Rhodes tine +
  tonebar, bell warble), the gradient fit will find one at the mean. A
  subspace estimate on the residual would find the pair.
- **Frequency-dependent floor.** The 20th-percentile floor is one number per
  scale; a floor per band would let quiet high partials in on instruments
  whose spectrum falls steeply, which mp3 encoding makes every Philharmonia
  sample.
- **Position.** One recording is one strike position; the record's twelve
  gains are copies. Two microphones, or two strike points, would give the
  shape block something to say.
