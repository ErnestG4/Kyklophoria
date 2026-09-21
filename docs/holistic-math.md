# The mathematics as one system — a review, 21 September 2026

Read-only pass over ModalBake (`tools/modalfit.py`, `fitset.py`, `fitvel.py`,
`bursts.py`, `noise.py`, `export.py`, `runtime/modal_bank.h`, `runtime/world.h`)
and Kyklophoria's `modal` branch (`core/kyk_resonate.h`, `core/kyk_engine.h`,
`shell/alchemy/main.cpp`), with one question: do the stages agree with each
other about what a number means? Not whether a line is wrong — whether the
thing the fitter hands the export is the thing the export hands the runtime,
and whether a quantity computed in two places is the same quantity.

Every number below was produced this evening by scripts under the session's
scratch directory (`kykm.py`, a Python port of `world.h`'s `Decode`/`At` and
the bank's closed form; `q1_*` to `q9_*`; two C++ harnesses against
`runtime/modal_bank.h` compiled with g++), reading the worlds in `out/worlds/`
and the records in `out/fit/` as they stood at 00:48. `export.py` and
`bursts.py` were being edited while this was written (an `intune` step and a
band-wise burst fade landed during the pass); both are accounted for where
they matter. Each claim is labelled MEASURED (a number came out), REASONED
(it follows from the code) or HUNCH.

## The chain, and what agrees

Scale. A recording is normalised to peak 1 in `modalfit.load` and every
record's gains are in that scale; the target wav is that excerpt at half
scale; `bursts.py` reads it back at `x / 0.5` and writes the burst at record
scale; the world stores each mode's level as quarter-dB under the point's
`loudest` and `loudest` itself as a float; `World::At` multiplies the level
back by `loudest`, the bank rings at record scale times the swing, the burst
is dequantised by its own `scale` and scaled by swing over its swing, and
the engine multiplies the whole by `gain × PhaseTrim` (0.23 on the desktop,
0.23 × the level knob on the module). Every link in that agrees with its
neighbour; the `resonate_check` found the one that did not (the level scale
applied after the bank) and it is fixed. Decay: the record's ζ is r/ω, the
byte is −10 ln ζ, the runtime's pole radius is exp(−ζ w); the byte's step is
a tenth of a neper, so every T60 in a world is within ±5% of its record, and
the ζ ≤ 1 clamp can only bite a thump under 220 Hz, which `--thump` removes
first. Phase: the fit's sin(2πft + φ) at the window's t = 0, the byte's 256
steps of a turn, the strike state A sin(φ − w)/r and A sin(φ − 2w)/r² — I
checked by hand that the recursion's first output from that state is
A sin φ, and the runtime check confirms it. Frequency: cents from 20 Hz, the
runtime's 20 × 2^(c/1200). Time: the fit's 3 ms raised cosine on the sum and
the strike bank's 144-sample weight table are the same curve; the burst's
1 ms fade-in, its 30 ms fade-out, the 60 ms `time_weight` in the loss.
Sample rate: every shaped set is at 48 kHz, so the pickup's per-sample first
difference and its fitted K mean the same thing in the fit and on the
module; the pitched sets at 44.1 and 50 kHz have their bursts resampled and
their modes in hertz, and the wash's band power is rate-free by
construction.

Two quantities computed two ways that agree, MEASURED:

- The retune. `ResonatorBank::Set`'s keep branch solves y1, y2 under the old
  pole for A sin φ and A cos φ and rewrites them under the new one. For a
  damped pole (780 Hz, ζ 0.018, r = 0.9982) retuned to 300 Hz, ζ 0.002 after
  100 ms of ring, the carried state matches the closed form A sin φ,
  A sin(φ − w′)/r′ to six figures and the 100 ms after it to 2 × 10⁻³ RMS,
  which is the float recursion's own drift (the state *before* the retune
  was already 6 × 10⁻⁴ off the closed form). A retune to the same pole moves
  y2 by 3 × 10⁻⁷. A sweep of 600 two-cent steps from 300 to 600 Hz decays
  as the mean damping says it should. The algebra is right for r ≠ 1.
- The coil. `fit_shaped` applies H(s) = 1/(1 − s² + js/Q) in the frequency
  domain; the runtime and `playvel.note` use the RBJ bilinear biquad. Over
  the EP, reed and tine sets at each note's h1–h4, the runtime's magnitude is
  within 0.03 dB of the fit's at the median and its h2/h1 bark within
  0.02 dB — except where the fit's Q is not a coil (below).
- The velocity law. `playvel`'s `g0 (g1/g0)^x` and `ModalVoice::Strike`'s
  `soft (hard/soft)^v` are the same line; the burst crossfade's weights and
  scaling reproduce, at any swing between two takes, "each take's attack
  rescaled to this swing, crossfaded".
- The bytes. Resynthesised from bytes against floats over 0.5 s at the
  recorded points: spectral convergence median 0.017 (bass) to 0.060
  (mandolin), first-40 ms energy within ±2 dB everywhere. The 63.75 dB level
  floor clips 0 to 5 real modes per world and raises them by under 5 dB;
  the −60 dB audibility gate gets there first. (What the floor *does* do to
  ghosts is below.)

Everything after this is where the stages do not agree.

## 1. The record hides cancellation, and everything that mixes two records un-hides it

Look at the Wurlitzer's C4 (`out/fit/wurli/wurlz008.mmr`): modes 7 and 8 are
780.68 Hz at amplitude 6.60, phase 4.68, and 783.86 Hz at 7.71, phase 1.57.
They are 3.1 rad apart in phase and 3 Hz apart in frequency; at t = 0 they
sum to 1.1, and both die inside 100 ms (T60 0.095 and 0.079 s). The
fundamental of that note is 0.28. So the record's `loudest` — the reference
for every level byte at that point and the `st[7]` the runtime multiplies
back in — is one member of a pair that cancels to a seventh of itself.

MEASURED, over the worlds, "loudest over its own cluster's phasor sum at
t = 0" (a cluster being the modes within 1% of the loudest): Wurlitzer
median 14.4 dB, max 28.1; piano median 5.0, max 45.6; banjo max 36.3;
mandolin max 31.9; percussion max 21.3; bass 0.0 everywhere (its records
have no clusters). The fit's loss is STFT magnitude at 4096/1024/256, and a
pair that cancels leaves every magnitude where it was; the decay prior
holds each member near its track's slope and nothing holds their sum. The
fitter has learned to make a partial's non-exponential attack out of two
large antiphase sines, which is a valid decomposition and a fragile one.

It is fragile because `World::At` interpolates slot by slot: log frequency,
log decay, linear gain, phase the short way, each mode on its own. Two
members that cancel at point a interpolated separately towards whatever
sits in their slots at point b stop cancelling at every t between.
MEASURED, the interpolated bank's energy over 0.5 s at the midpoint of
each pair of neighbouring points, against the mean of the two endpoints
(transposed to the midpoint's pitch for note worlds):

| world | midpoint over endpoints, median | worst | intervals more than 3 dB above both ends |
|---|---|---|---|
| Wurlitzer | +3.1 dB | +11.8 (between G3 and C4) | 5 of 10 |
| piano | +2.0 | +13.4 | 14 of 33 |
| mandolin | +2.2 | +18.8 | 16 of 38 |
| banjo | +1.5 | +13.7 | 12 of 40 |
| percussion (index) | +1.4 | +13.5 | 6 of 18 |
| guitar | −0.1 | +9.6 | 8 of 37 |
| violin | +0.9 | +9.5 | 5 of 24 |
| bass | −0.1 | +1.5 | 0 of 11 |

Over the first 60 ms the Wurlitzer's median is +6.4 dB and the worst +15.4.
The bass, whose records have no clusters, has no bump; the mandolin, whose
records are courses split into pairs, has the largest. The runtime check
asks that the midpoint's slot 0 sit at the log-midpoint frequency, which it
does; nothing asks what level the midpoint plays at.

A second, smaller mechanism rides on the same cause. `At` interpolates the
*relative* gains linearly and `loudest` linearly, then multiplies. That is
not the linear interpolation of the absolute gain: a slot at 0.01 × 10 at
point a and 1.0 × 0.1 at point b is 0.1 at both ends and 2.55 at the
midpoint. MEASURED: `loudest` differs between neighbouring points by a
median 8–9 dB (Wurlitzer, piano, percussion; max 34.8 dB on the percussion
row) — because it is a cluster member, not a level — and the worst slot
within 40 dB of the loudest at each midpoint sits +5.6 dB (Wurlitzer),
+4.7 (piano), +4.5 (percussion) above the linear-absolute value at the
median, up to +12 to +23 dB. Interpolating absolute gains instead leaves
the *total* bump where it is (Wurlitzer +3.0 vs +3.1 median) because the
clusters dominate the energy; it fixes the quiet slots, not the loud pairs.

REASONED: the interpolation is only wrong because the representation
carries a hidden constraint (the pair must move together) that a slot-wise
interpolant cannot see. Three places to put the fix, in order of depth:
a penalty in the fit on Σ|aᵢ| against |Σ aᵢ e^{jφᵢ}| within a cluster, so a
record's amplitudes are its audible amplitudes (a refit of every set, three
hours on the GPU by the findings' own clock); for note worlds, no
interpolation at all — decode the nearer point and transpose its slots by
2^((param − p)/12), which is exactly what the burst already does and what a
sampler does, and which makes `intune`'s nominal pitch land exactly; and in
`At`, interpolate absolute gains. The second costs a timbre step when a
pitch sweep crosses a midpoint (the state is carried so it does not click,
but the mode set changes) and buys correctness at every point and
coherence with the burst; the index world keeps its glide because the glide
is the feature there, and pays the bump until the fitter stops making pairs.

## 2. The burst stores −y and asks the runtime to reproduce y to the sample

The burst is x − y over the first 60–400 ms, faded on a raised cosine,
where y is the record's own resynthesis. Played on top of the bank the
total is x·fade + y_rt·(1 − fade) + (y_rt − y_fit)·fade: the third term is
what the runtime pays for every way in which what it plays is not what the
burst was cut against. Three ways were measured, each as the energy of
that term over the burst window relative to the recording's energy there.

At the recorded points, from bytes (MEASURED). Wurlitzer, 60 ms windows:
median −15.4 dB, worst −0.4. Percussion, 60 ms: median −21.2, worst −5.7.
Piano, 160–390 ms windows: median **+1.6 dB, worst +3.8** — at a recorded
note, the error term is louder than the note. Ablating one byte at a time
on each point's 0.5 s resynthesis, the cents byte is the one that hurts
(piano's worst points: SC 0.21 from cents alone against 0.01 from decay,
0.01 from level, 0.01 from phase; percussion the same). One cent at 2 kHz
is 1.15 Hz; ±0.5 cent on each member of a 2.4 Hz doublet moves the beat
rate by up to a hertz, and over 390 ms a half-cent error at 2 kHz is 81° of
phase. MEASURED on the records' loud pairs within 1%: the piano's 137 pairs
have their beat rate moved by a median 10%, 60% at the 90th percentile,
and by more than a quarter on 31% of them; the Wurlitzer's 29 pairs by more
than a quarter on 24%. The u16 holds 65536 steps and cents from 20 Hz to
20 kHz uses 11966 of them — 5.5× of the field's range is unused. A step of
0.18 cent would be free.

Between the points (MEASURED). At 0.499 of the way from each point to the
next, the runtime plays the interpolated bank and the nearer point's burst
resampled by the pitch ratio. The term's energy over the burst window:
Wurlitzer median **+9.2 dB**, worst +14.4; piano median +3.5, worst +16.5;
percussion median +4.6, worst +20.2. Using the nearer point's phases
instead of interpolating them helps the Wurlitzer to +5.7 and the piano to
+1.3 at the median and the worst cases not at all, because the slots'
frequencies and gains differ between the points too. For scale: the phases
were kept in the record because a zero-phase resynthesis carried 1.7× the
recording's attack energy; between two Wurlitzer points the attack now
carries about 8× on top of it.

Across the layers (MEASURED). The violin's soft take's burst was cut
against the soft take's own model; the runtime plays the loud take's modes
at the soft swing under it. At the softest take's swing the attack over
the first 60% of the burst is +1.5 dB re the soft recording at the median,
+5.1 at the 90th percentile, +7.7 worst. Across the EP's velocity takes,
struck at the geometric mid-swing with the two bursts crossfaded: median
−2.2 dB, 90th +8.1, worst +12.4 (reed −3.2 / +0.0 / +10.1; tine −4.5 /
+6.4 / +9.8) — the cancellation terms of two takes' bursts, rescaled
linearly in swing, against a model whose attack through the field is not
linear in swing.

And the newest one. `export.intune` now pulls each point's modes onto its
nominal note — MEASURED by running the export: Wurlitzer 7 of 11 points,
median 3 cents, max 9.3; piano 31 of 34, median 13.4 cents, max 109.9, one
refused at +162. The burst is not pulled; it plays at the recording's
pitch. On the piano's worst point the burst's partials sit 110 cents from
the modes they are supposed to hand over to, for 160–390 ms. REASONED: the
same factor k that scales the modes should resample the burst at export,
or ride in `burst_rate`.

REASONED, the structural fix: store x·fade, not (x − y)·fade, and have
the strike bank's output weighted by (1 − fade) over the burst's window
instead of over 3 ms. Then nothing cancels: the runtime plays
x·fade + y_rt·(1 − fade) whatever y_rt is, and the worst case is a dip
inside the fade where a partial's phase disagrees (at 2 kHz and 0.5 cent
over 390 ms, cos 40° = −2.3 dB at the fade's middle) rather than an added
term louder than the note. The fit already weighs the burst window down
(`BURST_FLOOR` 0.3), so the modes are fitted to the sustain either way. It
costs the strike bank's second loop for the burst's length rather than 3 ms
(twice the bank for 60 ms typically, 390 ms on five piano notes) and a
rule for a restrike inside the window: fold the ramping state into the
main bank scaled by the current weight, which keeps the output continuous
and drops the rest of that strike's ramp-in. The band-wise fade that landed
in `bursts.py` tonight (the part above the bank's top partial runs to the
burst's end) is compatible: it is the below-f_top part that is faded and
that the modes take over, and that is the part which needs no cancelling.

## 3. The electric worlds: the velocity axis, the coil, the rest-flux carry

The swings (MEASURED). `fitvel` fits one swing per take; `export` takes the
smallest as `swing_soft`, the largest as `swing_hard`, and places each
take's burst at its fitted swing. In `out/fit/ep-vel/fits.tsv` the MAX
folder's swing is *smaller* than MED's on 39 of 84 notes — every note from
midi 81 up, without exception (81: 1.13/0.63, 83: 1.12/0.27, 120:
0.66/0.37). Above A5 the runtime's velocity axis is therefore inverted: at
velocity 0 it strikes at MAX's swing with MAX's burst, at velocity 1 at
MED's with MED's. The findings say the top octave is where two levelled
takes barely differ and there is little to fit; the fit was free to order
them either way and did. The dynamics labels are ground truth the fit
ignores. REASONED: a prior that g_MAX ≥ g_MED (or the folder order taken
as the layer order at export, with the fitted swings only setting the
spacing) — the export rule costs nothing, the prior a refit.

The coil (MEASURED). fc runs 302–7080 Hz and Q 0.02–55 across the EP set;
13 of 84 records have Q outside 0.3–10 (Q 0.02–0.10 on midi 40–46, 0.22 on
62, Q 12–55 on 48, 49, 53, 54, 61). A coil is one physical thing with one L, one C and
one R, and a Q of 0.03 is a tilt, not a resonance; the fit is using the
coil as a free per-note equaliser. The runtime then interpolates fc and Q
*linearly* between neighbours (midi 48 at Q 12.7 to 49 at Q 55: Q 34 at
the midpoint), and at those Qs the bilinear biquad and the fit's analog H
part company — up to 9 dB at h2 and 60 dB at h3/h4 on those records,
against 0.03 dB at the median. REASONED: one coil per set (fc, Q shared
across the notes in `fit_shaped`, or a smoothness penalty along the
keyboard), and fc, Q, K interpolated geometrically like the modes.

The rest-flux carry (MEASURED, C++ harness against `modal_bank.h`). On a
retune with the voice ringing, `World::At` keeps the pickup's last flux as
its distance from rest: `prev − rest_old + rest_new`. That removes the step
a still tine would see when the pole moves; it is not what a moving tine
sees, which is Φ_new(u_last). The difference is
[Φ_old(u) − rest_old] − [Φ_new(u) − rest_new] at the retune sample, and it
is nonzero whenever u ≠ 0. With epv030's stage (h 1.36, w 4.18, K 79,
fc 1180, Q 1.42) and its one metal mode struck at MAX, a voicing sweep of
+2 widths in the module's 0.02-width deadband steps puts a difference of
−43.9 dB RMS but −23.4 dB *peak* against the signal — a click per step,
one every 20 ms of the sweep. Under a ±1-width LFO at 5 Hz on the voicing
CV (1479 retunes in two seconds) the carry is −28.4 dB RMS from exact;
±0.1 width, −39.2. REASONED, the fix: `Pickup` remembers the last
displacement u as well as the last flux, and the keep branch sets
`prev = Φ_new(u_last)` — one float and one field evaluation per retune, and
the harness's "exact" path is exactly that.

The sign (REASONED, no current victims). `bursts.py --shaped` decides a
record's sign from the residual and writes it into the metal's phases. For
a pitched record, φ + π negates y. For a shaped record it negates u, and
the pickup is not odd: Φ(u − h) under u → −u is a different waveform, and
for h = 0 it is the *same* waveform, so a flipped shaped record's burst
(x + y) would sum with the runtime's (+y) to x + 2y. MEASURED: on ep-vel,
reed-vel and tine-vel no record's burst cancels its model better with the
phases flipped back, so none is currently in that state; the operation is
still the wrong one, and the right one is the sign of K, which the stage
already carries as a float.

## 4. Smaller disagreements, each with its number

The index world's padding (MEASURED). `align` returns index worlds
untouched, "rank order", and `write` pads a body with fewer modes than N
with rows at 20 Hz, ζ 1, gain 0. That is the exact defect the ghosts were
added to cure on the Wurlitzer, alive on the percussion row: the bodies
have 0 to 30 pads each, and between bodies 8 and 9 a 12.6 kHz mode
interpolated against a 20 Hz pad passes through 502 Hz at −1 dB re the
point's loudest at the midpoint; bodies 15–16, 14.3 kHz through 535 Hz at
−5 dB; bodies 1–2, 13 kHz through 509 Hz at −13 dB. `perc-morph.wav` walks
all of them. The ghost rule — an empty slot takes its neighbour's
frequency at zero gain — is one line away from applying by rank.

Ghosts are not silent (REASONED). `level8(0.0, loudest)` is 255, and 255
decodes to −63.75 dB, not to nothing; 182 of the Wurlitzer's 528 slots,
179 of the bass's 372 and 349 of the piano's 1632 are ghosts or pads
ringing at that level (a bass point has ~15 of them at its own f0 with
ζ 0.01, −52 dB combined). Inaudible, and wasteful, and the ep-vel world
runs 37 resonators for a median six metal modes because ghost slots
accumulate along the chain. A sentinel (255 = silent, real modes clipped
at 254) makes the fade actually reach zero and lets the bank skip a slot.

Level (MEASURED). A wavetable cell is unit RMS; a record is peak 1. Over
the first half second the records' RMS at record scale sits at −10 dB
(Wurlitzer) to −18 dB (violin, percussion) re a cell, and within one world
spans 0.053–0.566, twenty decibels from note to note, which is crest
factor, not the instrument (peak normalisation keeps a note's peak and
discards its loudness). `layer()` already reads the sources' RMS back for
dynamics and could fold a per-point level into `loudest`. HUNCH: the
resonate path wants its own headroom convention rather than the wavetable's
0.23, since it is not a bank of unit-RMS cells and its peaks are the
recording's.

Onset (MEASURED). `load` opens the window at the last 5 ms frame 20 dB
under the peak, less a hop, so the strike is 9.1 ms (Wurlitzer), 12.0
(percussion), 12.1 (guitar), 11.4 (violin) into the window at the median —
and that is where the runtime's t = 0 is. A trigger on J4 sounds 9–16 ms
after its edge, plus the block. The sampler sets (piano, EP) start at
0 ms. REASONED: a trim at export — advance the record to the burst's first
sample above a threshold, the amplitudes by e^{−rτ} and the phases by 2πfτ,
which a state-struck bank makes exact — and the burst cut from the same
sample.

Beyond the range (REASONED). `At` clamps t to 0 or 1 outside the points,
so a note below the Wurlitzer's C2 plays C2's modes at C2's pitch, while
`burst_rate` is not clamped and the burst transposes. Transposing the
endpoint's slots by the same ratio is what the nearest-transposed rule of
§1 would do anyway.

The wash's envelope (MEASURED). A tam-tam swells: its 2.8 kHz band rises
from −60 dB at the strike to −30 dB at one second and holds. The wash is a
level at t = 0 and a T60, and a line through a plateau puts the level at
the plateau; inside the burst's first 60 ms the tam-tam's live bands play
+32 and +42 dB above what the recording has there, the Thai gong's top
band +26. A second number a band — a rise time, one more one-pole in
`NoiseLayer` — is what a swelling body needs; and since the burst already
carries the residual over its own window, the wash could start at the
burst's end.

Two velocity laws (REASONED). A layered world strikes at
soft·(hard/soft)^v, a one-take world at v itself — silent at 0, and a
linear amplitude for a control that is a level everywhere else. The
one-take burst is low-passed by velocity (1 kHz at 0 to 13 kHz at 1) and
the modes are not, so a soft strike has a dull attack handing over to a
sustain as bright as a hard one's. HUNCH: a one-take world with
`swing_soft` 0.1 under the same log law, and a tilt on the strike gains
that follows the burst's corner, would make the seam and the axis agree.

Retriggers (REASONED). A strike inside the 3 ms ramp folds the earlier
strike un-ramped — a step of (1 − w) of its output, a quarter of it at the
trigger's 2 ms refractory — and `BurstPlayer::Strike` sets `active = 0`,
cutting a burst in flight to zero on a sample. Both are clicks a fast
trigger can make; the second is the louder.

The 3 ms ramp and the burst's fade-in (REASONED). For the first
millisecond the runtime plays y·ramp + (x − y·ramp)·fadein, which is not x;
the window's 9–12 ms of pre-strike silence hides it on the acoustic sets
and the EP's window opens at the strike.

## 5. The interpolation against an instrument

What `At` does between two notes is what the bake's spaces do: every slot
glides geometrically in frequency and decay and linearly in gain, and the
phases go the short way. Along a keyboard a slot that is a ratio (a
harmonic, a tine's 6.27) does move geometrically with the note, and
`align`'s ratio slots make that exact — a harmonic slot at f = r·f0(param)
for every param, since the parameter is log frequency. Inharmonic
structure does not: a piano's partial k sits at k·f0·√(1 + Bk²) and B
changes along the keyboard, so a slot's ratio drifts by ½k²ΔB between
points; the 1.5% tolerance holds it for k up to ~15 at the bass's ΔB and
loses it above, where the mode fades out through a ghost and in through
another. A body mode fixed in hertz (a soundboard resonance under every
note) is ghosted at every interval, which is the honest thing to do with a
ratio slot and the wrong thing to do with a body. Phases interpolated the
short way are not any instrument's: the hammer's timing per partial is
what it is at each note and nothing in between, and §2 shows the only
place they matter — the burst's cancellation — is where interpolating them
does damage. Nothing physical sits between C4 and C♯4 on a Wurlitzer; the
glide is the space's, not the metal's, and for a note world the honest
model of "between" is the nearer note transposed, with the glide reserved
for the index world where a morph between bodies is the point.

## What would make it cohere — ranked

1. **Interpolate absolute gains in `At`** (REASONED; MEASURED +5 dB median
   and up to +23 dB on individual slots at midpoints). Multiply each
   point's relative gains by its own `loudest` before the lerp and drop
   the `st[7]` multiply after. Two lines in both runtimes; fixes the quiet
   slots, not the loud pairs.
2. **Note worlds: the nearer point transposed, no slot interpolation**
   (REASONED; MEASURED bump +12 to +19 dB at midpoints, and the burst's
   +9 dB term). `At` for kind 0 decodes `near` and scales its hz by
   2^((param − p_near)/12); the burst already does this. Costs a mode-set
   step when a sweep crosses a midpoint (state carried, no click); removes
   ghosts, slides and `align`'s tolerance from the sound of a note world
   and makes `intune` exact. Index worlds keep the glide.
3. **The burst as x·fade with the strike bank weighted by (1 − fade) over
   the burst's window** (REASONED; MEASURED +1.6 to +3.8 dB at the piano's
   own points, +9 to +20 dB between points, +5 to +8 dB across layers and
   takes at the 90th percentile). Nothing to cancel; the strike bank runs
   for the burst's length; a fold rule for restrikes inside it. Also makes
   the phases' role at the seam a dip rather than an addition.
4. **The cents field at full u16 resolution** (MEASURED: beat rates moved
   by more than a quarter on 31% of the piano's pairs and 24% of the
   Wurlitzer's at one cent; 5.5× of the field unused). 0.18 cent, a
   format bump, no more bytes.
5. **`intune` pulls the burst too** (MEASURED: up to 110 cents on the
   piano, 9 on the Wurlitzer). Resample by the same k at export.
6. **Carry u across a pickup retune, not the flux's distance from rest**
   (MEASURED: −23 dB peaks per deadband step, −28 dB RMS under a one-width
   LFO). One float in `Pickup`, one evaluation in the keep branch.
7. **The layer order is the folder order** (MEASURED: 39 of 84 EP notes
   inverted, all of midi 81 and up). An export rule now; a monotonic prior
   on the swings at the next refit.
8. **Ghosts for index worlds** (MEASURED: a 12.6 kHz mode through 502 Hz
   at −1 dB between two percussion bodies). The neighbour's frequency at
   zero gain in place of the 20 Hz pad; one line in `write` or `align`.
9. **A cluster penalty in the fit** (MEASURED: `loudest` 14–46 dB over
   what its cluster sums to). The deep fix for §1 and for `loudest`
   meaning a level; costs a refit of every set. Until then 1, 2 and 8
   contain it.
10. **One coil per set, fc/Q/K interpolated geometrically** (MEASURED:
    Q 0.02–55 on 13 of 84 EP notes, 60 dB fit-vs-runtime there). A refit
    for the sharing; the interpolation is a line.
11. **Level: a loudness-based `loudest`, and a headroom convention of the
    path's own** (MEASURED: −10 to −18 dB re a cell, 20 dB note to note).
12. **A wash rise time** (MEASURED: +32/+42 dB inside the tam-tam's burst
    window). One state a band; or start the wash at the burst's end.
13. **Trim the onset at export** (MEASURED: 9–12 ms of window before the
    strike on every acoustic set).
14. **A ghost sentinel at 255** (REASONED: −63.75 dB is not zero; 34% of
    the Wurlitzer's slots and 48% of the bass's ring at it).
15. **One velocity law and a mode tilt to match the burst's low-pass**
    (HUNCH). The sign of K for a shaped record's sign, and burst slots that
    survive a retrigger (HUNCH, no current victims of the first).

The three that matter most are 2, 3 and 6 — or 9, 3 and 6 if a refit is on
the table — because they are the three places where one stage assumes the
next will reproduce it exactly (a pair that cancels, a resynthesis to the
sample, a flux under a pole that has moved), and exactness is the one
thing a byte, a slot and a pot never deliver.
