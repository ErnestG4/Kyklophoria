# Exciters coupled to the modes — design

Item 8 of the overnight list (docs/overnight-2026-09-27.md). Combust, 23
September: "modeling our own strikes and perfecting the waveguide seems like
the way to go ... a control surface similar to elements would be nice to
have, and that means owning the exciter range from blow to bow to reed to
buzz (brass) to pluck to strike". Grounded in ModalBake's three-way
prototype (proto/kyk_exciter.h, bench/threeway: the synthesised strike at
par with the recording by ear) and its literature review
(docs/litreview-ml-2026-09-23.md, top five #4).

## What is wrong now, and why coupling fixes it

Everything that excites a resonator today is **feed-forward**: the recorded
attack is played beside the modes; the strike state is written into the
bank once; the prototype's hammer computes its force at strike time; the
external exciter (J1) is summed into every mode. Nothing the modes do
reaches back into what drives them. That is enough for a strike — a hammer
leaves the string in a few ms — and it is the whole reason nothing can be
**bowed or blown**: a bow's force depends on the string's velocity under
it (stick, slip), a reed's opening on the pressure the bore sends back. It
is also why the prototype's waveguide sounded like "a separate wave on top"
and "a spring playing along": it was summed beside the modes, a second
instrument, where the ear wanted the note itself to change.

**Coupled** means: every sample, the exciter reads the resonator at the
contact point, computes a force from that and its own state, and puts the
force back into the modes at the same point.

```
   contact velocity  v_c = Σ_k  w_k · (y_k[n] - y_k[n-1]) · sr      (read)
   force             F   = exciter(v_c, its own state, the controls)
   into each mode    y_k += w_k · F · g_k                            (write)
   w_k = sin(π · p · h_k)   the mode's shape at the contact position p
```

The same weights read and write (reciprocity), which is what keeps the
pair passive: the exciter can only put in energy its own law says it has.
This is Elements' structure (the exciter's output is the resonator's input
and the resonator's state is the exciter's input), the Sound Design
Toolkit's, and the modal clarinet's; the literature review found all three
agree.

## The exciters

Each is a force law on the contact velocity (or displacement) and a few
controls. Velocity, pressure and position are the Elements surface.

| exciter | force law | controls | what it proves |
|---|---|---|---|
| **strike** | Hunt–Crossley felt: F = K·d^α (1 + μ·ḋ) while the hammer is in contact, the hammer a mass moving under −F; it leaves when d < 0 | velocity (hammer speed), felt hardness (K, α), mass | the contact shortens and brightens with velocity by itself (T ∝ v^-(α-1)/(α+1)); a restrike on a ringing string hits a moving string |
| **pluck** | a displaced point pulled to the finger's position through a stiff spring, released when F passes a threshold | force, position, stiffness (plectrum/finger) | the release is where the string actually is |
| **bow** | friction on the slip velocity v_rel = v_bow − v_c: F = f_N · μ(v_rel), μ a hyperbolic friction curve (static μs, dynamic μd); stick when the resonator can match v_bow | bow velocity, bow pressure f_N, position | Helmholtz motion: a steady tone at the resonator's fundamental above a minimum pressure, raucous above a maximum (Schelleng) |
| **blow / reed** | a reed: opening x = x0 − p_d/k (clamped at 0), flow u = w·x·sqrt(2|Δp|/ρ)·sign(Δp), Δp = p_mouth − p_bore, the bore pressure the resonator's contact output | mouth pressure, reed stiffness, embouchure | an oscillation threshold in pressure; below it silence, above it a tone |
| **buzz (lips)** | a lip mass-spring driven by Δp, the flow through the lip opening as the reed's | pressure, lip tension (its own frequency) | locks to the nearest resonance, as a brass player's lips do |

All five share the loop above and differ only in the force law — one
interface, `float Force(float v_c)` plus state, so the Elements surface is
one page: type, and three knobs (energy, timbre, position).

## The noise that goes with them

The prototype's twelve noise bands, each decaying on its own, stacked into
a "snare chain" in fast passages (Combust). Tied to the contact instead:
the noise's amplitude is the contact force (or, for a bow, the slip
velocity while slipping; for a reed, the flow), so it lives exactly while
the contact does, milliseconds for a strike and as long as the bow moves,
and a steal takes it with the voice. No independent decays at all.

## The waveguide

Dropped as a separate path. What the Wurlitzer's "Beooooo" is — the
timbre moving as the reed's swing decays — is the pickup's nonlinearity,
which the runtime already has (the bell and gap forms) and which the fits
do not yet drive hard enough; the dispersion the waveguide added is the
modes' own inharmonicity, which the fits carry. If a waveguide comes back it
comes back inside the loop, as the resonator a bow or reed drives, not
beside the modes.

## Cost, and where it runs

A coupled voice runs **sample-outer**: each sample needs every mode's state
before the force, so the bank's mode-outer, sample-inner loop (which is what
makes 48 modes cheap) cannot be used for it. Per sample, for n modes: the
recursion (2 FMA), the read (1 FMA), the write (1 FMA) — about 4n FMA plus
the force law. At n = 24 (a voice's share at two voices) that is ~100 FMA a
sample, 4.8 M a second: roughly 5 % of the M7 if the loads behave, more if
they do not (the bank's own comments measured sample-outer as load-store
bound). So:

- only the voice being bowed or blown runs coupled; the others ring on
  mode-outer as now;
- a strike runs coupled only for its contact (milliseconds), then hands the
  state to the ordinary loop;
- the budget is measured with `make armcost` before anything reaches the
  module.

Measured, 28 September (`make armcost`, which now lists every loop's body
through `tools/armloops.py`; instructions as the M7 compiler emits them, not
cycles): the free bank's paired loop is about 20 instructions a pass, two
modes a sample, so about 10 a mode. The bowed loop reads the contact in 6 a
mode and writes the force and runs the recursion in 15, so about 21 a mode a
sample: twice the free bank, not the 2-3x the desktop showed nor worse. The
hammer, pluck, reed and lips have the same two loops (16-19 a mode for the
write, 6 for the read) plus their own force law once a sample. So one coupled
voice costs about 21 x modes x 24 instructions a block: at 12 modes (a voice's
share at four voices) about 6 000, 2.5 % of the 240 000-cycle budget; at 48
(one voice, all the modes) about 24 000, 10 %. One voice coupled at a time is
affordable at any polyphony; four at once is not the plan.

## Stages

1. **Desktop prototype, one exciter: the bow** — done, 27 September:
   `core/kyk_exciter.h` (Bow, ProcessBowed), not wired into the engine;
   `tests/exciter_check`. Measured on a harmonic string of twelve modes at
   196 Hz bowed at 0.13 of its length, modal mass 10 g: Schelleng's band
   appears by itself — playable 1-3 N at 0.05 m/s, 3-10 at 0.2, 10-20 at
   0.4, both edges scaling with the bow's speed; inside it the tone is
   195.9 Hz, its amplitude halves at half the speed and holds at less
   pressure (x1.03); under the band nothing, over it the motion breaks down
   (0.08). The coupled loop costs 2-3x the free bank per voice on the
   desktop (12 modes 0.51 us a block against 0.17; 48, 1.55 against 0.72),
   for the voice being bowed only. Without the force written back the
   string is silent (six checks fail); without the velocity read it never
   oscillates (three). The tests as planned:
   - passivity: bowed for 10 s at every corner of velocity × pressure ×
     position, the state stays bounded;
   - Helmholtz: within the playable range the tone's fundamental is the
     resonator's (within a few cents) and steady; below the minimum
     pressure, no steady tone;
   - off is silent: zero bow velocity puts no energy in.
2. The strike through the same loop — done on the desktop, 27 September
   (Hammer, ProcessStruck; tests/exciter_check 5). A Hunt-Crossley felt
   (alpha 2.5) on a harmonic string: the contact 6.1, 4.7, 3.8 ms at 0.5,
   1, 2 m/s (x0.78 and x0.80 a doubling, where a rigid wall gives 0.74), the
   harmonics' centroid rising h1.06 -> 1.16 -> 1.26 with nothing but the
   felt, and the string holding 98-100 % of the energy the hammer lost.
   With a linear felt the contact and the brightness do not move at all
   (the mutation the test fails on). Still to do: against today's recorded
   attack on the bench (the three-way, with this as a fourth).
3. The reed — done on the desktop, 27 September (Reed, ProcessBlown;
   tests/exciter_check 6). Kergomard's non-dimensional reed on a bore of
   eight odd modes whose peaks fall and losses rise with frequency: silent
   at 0.3 of the closing pressure, the bore's fundamental exactly (147.0 Hz)
   at 0.5 and 0.9, louder the harder it is blown (0.40, 0.72), silent again
   pressed shut at 1.1, bounded at every impedance 5-200. Two things the
   first cut got wrong and the tests now hold: each mode must be driven so
   its own peak is the bore's impedance (driven raw, a lightly damped mode's
   peak was 5000 and the loop ran away at any pressure), and a bore whose
   peaks do not fall with frequency jumps to its upper registers. And the lips
   (Lips, ProcessLipped; exciter_check 7): a mass on a spring with its own
   frequency, swung open by the pressure. On a brass-like bore (ten harmonic
   resonances at 116.5 Hz) the lip tuned just under the 2nd, 3rd and 4th
   resonance plays the 2nd, 3rd and 4th — 242, 365, 487 Hz — which is how a
   bugle has notes; a frozen lip selects nothing. The pitch sits
   3-5 % sharp of the resonance it locks to at the test bore's losses, and
   it is the lip pulling it, not the loop: with the bore's losses cut to a
   third and a tenth the 2nd resonance plays at 2.08, 2.03, 2.02 of the
   fundamental (the 4th at 4.18, 4.08, 4.03), and driving the modes through
   a zero-phase band-pass (u[n] - u[n-2]) in place of the first difference
   changed nothing (242 Hz either way). How far a real brass bore lets the
   lip pull is its losses', which a fitted bore will bring.
3b. The pluck — done on the desktop, 27 September (Pluck, ProcessPlucked;
   exciter_check 8): a plectrum's spring drawing the string with the finger
   and letting go at a force. Plucked at the middle the even harmonics sit
   66 dB under the odd; a stiffer plectrum lets go in 37 samples rather than
   997 and is brighter (centroid h1.53 against h1.27); the string's energy
   is the finger's work less what the spring still held at release, within
   10 % — the check that fails if the force ignores where the string is (a
   pluck's comb and brightness alone would pass a feed-forward force).
   Every exciter of the table now runs on the desktop, tested.
4. The module: an Exciter page (type, energy, timbre, position), the J1
   input as one more force, the contact noise; the recorded attack stays as
   the default "strike" until the synthesised one has been heard against it.
