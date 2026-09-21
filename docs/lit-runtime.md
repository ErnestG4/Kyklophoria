# What the literature has that the runtime does not, and what to take

Written after the first evening on the bench (20 Sept 2026): the worlds
overran, a piano's bass notes fell off a cliff behind their burst, a wash
put white noise under a note, and dynamics were duplicate points. Each of
those was a rule, not a note, and each has a shape the literature has met.
`lit-fitting.md` is the fitter's review; this is the synthesis side — what a
modal voice on an M7 is missing, ranked by what it would fix here. Each
item is labelled *measured* (we have the number), *reasoned* (it follows
from what we measured) or *hunch*.

## The gaps, in the order they were heard

1. **A piano bass note has more partials than the bank has modes.** Measured:
   48 modes on an E1 stop at 1.7 kHz, the recording's hundred partials above
   that die over half a second, and from the burst's end on every band above
   2 kHz is 60–77 dB short. The burst now runs to 400 ms on those notes; the
   cliff is still there, 40 dB down.
2. **The voice is one strike model.** Reasoned: a pizzicato's dynamics differ
   most in their attack, a piano's in how many partials the hammer excites,
   and both are one modes-set with a swing and a burst crossfade.
3. **What is heard as "wack" on a keyboard is often level, not timbre.**
   Measured: the layered violin's notes sit within 13 dB across the keyboard,
   the recordings' own; the piano's treble is the sampler's, dead in 0.2 s.
4. **Cost.** Measured on the desktop and reasoned for the M7: the bank is
   ~400 instructions a sample, four per cent; `At()` was 60 µs and the
   render under it the whole wavetable cost. Fixed, but a pitch sweep still
   pays `At()` every block.

## Sources, and what each is worth to us

**Smith & Van Duyne — "Commuted piano synthesis", ICMC 1995; Van Duyne &
Smith, "Developments for the commuted piano", ICMC 1995.** Linear,
time-invariant parts commute, so the soundboard's impulse response is folded
into the excitation table and fed into a waveguide string; the hammer, being
nonlinear, does not commute and is kept as a filter applied to the excitation
per velocity. What this is to us: our stored burst *is* commuted synthesis —
the recording's attack minus the model is the excitation-times-body the modes
do not carry — and it says the burst should be *filtered by velocity*, not
only crossfaded: a hammer at low velocity is a low-pass on the excitation.
*Reasoned:* the take crossfade approximates this where we have takes; where we
have one take, a velocity low-pass on the burst (one pole, cutoff from the
swing) would give a soft strike a soft attack instead of a quiet copy of the
hard one. Cheap, and no refit.
[Commuted piano synthesis](https://quod.lib.umich.edu/i/icmc/bbp2372.1995.097/1/--commuted-piano-synthesis?page=root;size=+75;view=text),
[Van Duyne's publications](https://ccrma.stanford.edu/~savd/pubs.html).

**Jaffe & Smith — "Extensions of the Karplus–Strong plucked-string
algorithm", CMJ 1983; and the waveguide string generally.** A delay line at
the period with a loss filter gives every partial of a string for the price
of one delay and one filter, inharmonicity by an allpass in the loop. This
is the answer to gap 1 that the whole piano literature converges on: the
low notes are waveguides, the high notes are modes, because a bass string's
hundred partials are one delay line and a treble string's ten are ten
resonators. *Reasoned:* a hybrid voice — modes for the partials the fit
found, a Karplus–Strong loop above them tuned to the fitted f0 with the
fitted inharmonicity — would fill the 2–8 kHz cliff on the bass notes at
the cost of a 1,200-sample delay line and a biquad. The loop's partials
would not be *fitted*, they would be the string's; their decay comes from
the loop filter, which can be set from the fitted modes' decay-versus-
frequency law. This is the one item here that changes the runtime's shape
and the one that would close the largest measured gap.
[Karplus–Strong string synthesis](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis),
[Dispersion modulation using allpass filters (DAFx 2008)](http://legacy.spa.aalto.fi/dafx08/papers/dafx08_36.pdf).

**Fletcher, Blackham & Stratton — "Quality of piano tones", JASA 1962; and
the inharmonicity literature after it (Järveläinen, Välimäki, Karjalainen on
its audibility).** f_k = k·f0·√(1 + B·k²). Fletcher found inharmonicity
necessary for a tone to sound like a piano; Järveläinen et al. measured where
it is audible (the bass, mostly) and where it is not. *Measured, ours:* the
piano E1's deficit lines sit 55 Hz apart at 2–4 kHz where f0 is 41 Hz —
B ≈ 1.5e-4 — and in doublets 6 Hz apart, the three strings a note. What to
take: fit B per note from the fitted modes (a one-parameter regression of
f_k/k against k²) and carry it in the world, so anything that generates
partials above the fit — the waveguide's allpass, or a parametric extension
of the bank — is stretched right. The doublets say the same for the three
strings: a per-note detune, which is the beat pairs the fitter already
finds, kept as a parameter rather than as pairs of modes.
[Perception and adjustment of pitch in inharmonic string instrument tones](https://www.researchgate.net/publication/228933837_Perception_and_Adjustment_of_Pitch_in_Inharmonic_String_Instrument_Tones),
[Audibility of the timbral effects of inharmonicity](https://pubs.aip.org/asa/arlo/article/2/3/79/123666/Audibility-of-the-timbral-effects-of-inharmonicity).

**Bank, Avanzini, Borin, De Poli, Fontana, Rocchesso — "Physically informed
signal-processing methods for piano sound synthesis: a research overview",
EURASIP JASP 2003; Bank, Zambon & Fontana — "A modal-based real-time piano
synthesizer", IEEE TASLP 2010 (the Viscount Physis engine).** The 2010
paper is the closest published thing to what we are building: a piano
where every string is a bank of second-order resonators, run in parallel,
with the hammer as a nonlinear excitation and the longitudinal (phantom)
partials as a second bank driven by the transverse one. What it says about
cost: they run hundreds of modes a note in real time on a DSP by keeping the
resonators in a form that is stable and cheap at low frequencies and by
processing them as a vector — the same two-multiply resonator we use, but
arranged so the coefficients stay in registers across a block. *Reasoned:*
our bank loops sample-outer, mode-inner; loop mode-outer, sample-inner over
the 24-sample block and the compiler keeps c1/c2/y1/y2 in registers for 24
samples, which on the M7 is the difference between a load-store-bound loop
and an FMA-bound one. Roughly halves the bank's cost; no format change.
What it says about the hammer: the excitation's spectrum depends on
velocity through the felt's stiffening (Chaigne & Askenfelt), which is
gap 2 again from the physics side — a soft hit has fewer partials, not a
quieter copy.
[Physically informed signal processing methods for piano synthesis (EURASIP 2003)](https://home.mit.bme.hu/~bank/publist/jasp03.pdf),
[A modal-based real-time piano synthesizer (TASLP 2010)](https://home.mit.bme.hu/~bank/publist/taslp-piano/index.html).

**Weinreich — "Coupled piano strings", JASA 1977.** Two or three strings a
note, slightly mistuned, coupled through the bridge: the sum decays fast
while they are in phase (the bridge moves, energy leaves) and slowly once
they are out of phase (the bridge is still). That is the two-stage decay
the fitter models as a knee, and the beat pair, and it is why a piano note
has a "prompt sound" and an "aftersound". Nothing to take that the fitter
does not already find; what it gives is the reason the beat pairs must
survive export as pairs (they do) and the reason a decay-tune knob that
scales every T60 alike sounds wrong on a piano (*hunch*: the knob should
scale the aftersound and leave the prompt sound).

**The Rhodes and Wurlitzer, modelled: Gabrielli et al., "Real-time physical
model of a Wurlitzer and Rhodes electric piano", DAFx 2017; and the JASA
2020 Rhodes tine analysis.** Their pickup is our pickup — a tine as a
single sine, the magnetic field as 1/(1 + ((u − h)/w)²), Faraday's d/dt,
the coil's resonance — and their finding that the tine's harmonics come
from the field is the finding that made `fit_shaped`. What they have that
we do not: the hammer's contact as a nonlinear spring with a velocity-
dependent contact time, which is what makes a hard Rhodes hit *bark* in
the first 10 ms before the pickup has anything to do with it. *Reasoned:*
our burst carries that bark per take; a contact model would carry it per
velocity without takes. Not worth a refit until the takes run out.
[DAFx-17 paper 79](https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf),
[The Rhodes electric piano: analysis and simulation of the inharmonic overtones (JASA 2020)](https://pubs.aip.org/asa/jasa/article/148/5/3052/631688/The-Rhodes-electric-piano-Analysis-and-simulation).

**High-resolution modal analysis: ESPRIT and the exponentially damped
sinusoid (EDS) models (Badeau, David, Richard; Ege, Boutillon & David on the
piano soundboard).** The Fourier transform's resolution is the window; ESPRIT
projects onto a signal subspace and resolves modes closer than a bin, with
their damping, from a short record. What this is to us: the fitter's
initialiser is a phase vocoder with a 65536-point doublet split, and it
found the piano's doublets; where it will fail is dense inharmonic bodies —
the tam-tam kept 26–49 of hundreds of modes because the gates could not
tell them apart. *Hunch:* an ESPRIT pass on the residual of a poorly-fitted
body (loss > 2) would find the modes the vocoder cannot, at the price of
a model order to choose. The gong and the cymbals are the only place this
would pay, and they are the place the wash was made for; it may be that the
wash is the honest answer there and ESPRIT is not.
[High-resolution modal analysis (Ege, Boutillon, David)](https://arxiv.org/pdf/0909.0885),
[Perceptual audio modeling with exponentially damped sinusoids](https://www.researchgate.net/publication/222820695_Perceptual_audio_modeling_with_exponentially_damped_sinusoids).

**Sample-plus-synthesis instruments (Yamaha SY99's AFM+AWM, Roland's
modelled attack over PCM, the Nord's sampled attacks) — the industrial
answer, unpublished but heard.** The attack is a sample and the sustain is
synthesis, because the attack is where a model is worst and a sample is
best, and the sustain is where a sample loops and a model does not have to.
That is the burst, and Combust named this lineage when the burst was
proposed. What they do that we do not, *reasoned from listening*: the
sample runs longer than 60 ms and crossfades over a hundred or more; the
crossfade is spectral (the sample's high end is faded out where the
synthesis's high end fades in) rather than a plain amplitude ramp. Our
`--ms auto` is the first of these; a crossfade that hands over band by band
would be the second, and it is what would hide the cliff on the piano's
bass notes without a waveguide: the burst's top fades on the burst's own
decay while its bottom hands to the modes at 60 ms.

## What to take, in order

1. **Mode-outer, sample-inner bank loop** (Bank 2010). Halves the bank's
   cost, no format change, an afternoon. *Reasoned.* Do first, because
   everything else here spends the budget it frees.
2. **Velocity low-pass on the burst** (commuted synthesis). One pole on the
   burst's playback, cutoff from the swing, for one-take worlds. A soft
   strike gets a soft attack. An afternoon, no refit. *Reasoned.*
3. **Band-wise burst crossfade** (the SY99 lineage). The burst's high end
   fades on its own decay, the low end hands to the modes. Hides the cliff
   on the piano's five lowest notes and on the guitar's low E at the cost of
   a crossover in the burst player. A day. *Reasoned.*
4. **B per note in the world**, from the fitted modes. A column in
   fits.tsv, a float in the point. Costs nothing now and is what 5 needs.
   *Measured* on the piano.
5. **A waveguide above the modes for string worlds** (Jaffe–Smith with the
   fitted B). The structural fix for gap 1, and the one that needs the
   bench: a delay line a voice in AXI, a loop filter from the decay law, an
   allpass for B. A week, and a new world kind or a flag. *Reasoned*, and
   the only item here I would not do before hearing 1–3.
6. **Decay knob that scales the aftersound** (Weinreich). *Hunch*; try it on
   the Wurlitzer, which has the knees.
7. **ESPRIT on the residual of the gong and cymbals.** *Hunch*; only if the
   wash is heard to be wrong there, which nobody has said yet.

## What was looked for and not found

A published account of a modal synthesizer with a *stored residual burst*
per note. Commuted synthesis is the nearest and it stores the excitation-
times-body, which is what our burst is by construction; nobody seems to
have written up subtracting the model from the recording to get it. That
is either because it is obvious or because it does not survive the
retune — and the retune is where ours is weakest: the burst is the nearest
point's, at the nearest point's pitch, and a note between points plays a
burst a semitone off. A pitch-shifted burst (a resampled read, the same as
a sampler's) is the fix, and it is not in any of the above because none of
them have the problem.
