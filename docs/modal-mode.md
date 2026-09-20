# The modal mode: resonators, a hammer, a pickup

What `core/kyk_resonate.h` is, what it is measured to cost, and how it goes
into the instrument — which is the part this branch deliberately does not do,
because it needs the module on the bench and a hand on the pots. The fitting,
the worlds and the reasons are in ModalBake (`docs/findings.md` there); this
is the runtime side only.

## What it is

A fitted world is a note-by-note list of modes — frequency, decay, gain —
and, for an electric world, a pickup per note. The runtime is a bank of
second-order resonators struck by a short pulse and read through the pickup:

    bank      y[n] = 2r cos w · y[n−1] − r² · y[n−2] + x[n]      three MACs a mode a sample
    strike    a 0.1 ms raised cosine of the velocity's swing into every mode
    pickup    u → phi = 1/(1 + ((u−h)/w)²)  (magnetic)  or  1/(1 − u/g)  (electrostatic)
              → first difference (Faraday) → the coil's second-order low-pass

The metal of a tine or reed piano is one sine; every harmonic and all of the
velocity behaviour is the pickup's (ModalBake, from Muenster & Pfeifle's
38 kfps measurement and a fit that rediscovered it). So an electric world is
a handful of modes and five numbers a note, and velocity comes out of the
physics rather than out of a sample layer.

Worlds arrive as `.kykm` blobs (ModalBake `tools/export.py`): four bytes a
mode, the stage and the loudest mode's absolute gain per point. The EP is
38 KB as 48 harmonic slots, 7 KB as metal plus pickup; the Wurlitzer with
eleven recorded notes is 2.5 KB and the runtime interpolates between them by
slot. They attach where they lie in SDRAM; nothing is copied.

## Measured

- `tests/resonate_check.cpp`: a 440 Hz mode within 0.01 cent and 0.1% in
  T60, at the record's amplitude; the EP's C3 barks +8.5 → +20.2 dB (h2 re
  h1) from the softest to the hardest strike, monotonic, where the fit's own
  model of the same note said +7 → +19; the Wurlitzer struck across its
  keyboard without a NaN and interpolated halfway between two points to
  within a cent of the log midpoint. The check found a real bug on its first
  run (the export's level scale applied after the bank had taken the gains:
  the pickup driven 2.4× too hard, barking 15 dB early).
- `make armcost`: `c_resonate` 68 instructions for the whole function, about
  fifteen a mode a sample in the loop; `c_pickup` 121 with the electrostatic
  form's `tanh` as an inline clamped Padé (it was 93 with libm's `tanh`,
  which the count saw as one instruction — a `bl` — and which cost about a
  hundred cycles a sample: the count misleads on a call). Forty-eight modes at 48 kHz is
  about 36 M instructions a second, under a tenth of the M7; an electric
  world is four to twelve modes.
- Both structs are trivially constructible (`static_assert` in the check),
  so a voice and a world may sit in `.sdram_bss`.

## How it goes in

The engine renders a *frame* from `World::Evaluate` and the oscillator plays
it. A resonator bank is not a frame: it rings in real time and is excited in
real time. So it does not enter through `Evaluate`; it enters `Engine::Process`
after `osc_.Process(out, n)` as a stage on `out`, gated by a new
`World::Kind::Resonate` (9 is already `Modal`, the harmonicised one, and stays
what it is). Concretely:

1. **The world.** A `.kykm` is a user world of a new kind. `SetWorld`
   attaches the blob (`ResonatorWorld::Attach`) and calls `At(param)` into
   the live voice — through `SetWorld`, and only there, because a voice is
   state derived from a world and CLAUDE.md's five bugs were all that. Add it
   to `switch_check`'s sweep so that arriving at it equals starting in it.

2. **The parameter.** A pitched world's parameter is a midi note; `At()`
   takes it as a float and interpolates, so the v/oct input maps straight
   onto it — with the deliberate control-rate aliasing, which is a feature
   and is not to be fixed. The position `c_[0]` is the natural carrier; the
   other axes are free for the pickup (below).

3. **The exciter.** Three candidates, all of which the header supports
   because it only needs `Strike(velocity)` and a sample stream in:
   - *A gate.* J2 already has a Schmitt edge detector (`ReadClock`); a rising
     edge strikes at a velocity from a pot. The simplest, and the one a
     struck instrument wants.
   - *The oscillator.* Feed `out` into the bank as `x[n]` instead of the
     pulse: the wavetable becomes the mallet and the bank a resonant filter
     on it. Sustained rather than struck, and every existing world becomes
     an exciter — the roadmap's original idea. Costs nothing extra.
   - *Both.* The gate strikes; the oscillator hums underneath at a level.
   Which one, and which pot is velocity, is decided by playing it. Do not
   decide it here.

4. **The pickup's knobs.** `h` is the voicing screw (ModalBake's sweep says
   the fitted number *is* the screw: 0.27 at −0.85 on Epi's scale, 0.01 at
   the centreline where the octave-up appears). One pot on `h` around the
   fitted value, one on the swing (velocity scale), one on `fc`. These are
   the world's axes 1–3, applied at `At()` time on top of the point's fitted
   stage, so a world sent from the page keeps its own voicing as the centre.

5. **Level.** The pickup's `K` is in volts at a coil; the bank's sum is a
   displacement. Both are absolute and neither is dBFS. Level the *output* the
   way `Process` already levels frames — the crest-factor headroom logic —
   and leave the chain linear; `tests/alias_check` will say if the pickup's
   nonlinearity aliases at full rate (it is a smooth 1/(1+u²), differentiated,
   then low-passed at the coil's fc, so the expectation is that it does not
   at the swings the fits found — measure it).

6. **Memory.** Voices and worlds in `.sdram_bss`, `alignas(32)` if they are
   ever staged through SDMMC, `Init()` after `hw.Init()` and never a default
   member initialiser (both traps in `docs/sdk-quirks.md`).

7. **Telemetry and the page.** The world list needs the kind; the play view
   needs nothing new to start (the position is the note). The bark is the
   thing to show later: h2/h1 per strike is one DFT the desktop shell can do.

## What is not done, and why

Nothing above the header is wired. The engine change is small in lines and
large in consequence — it puts a second signal model behind `Process`, and
the notes on this repo say what happens to state derived from a world when
it is invalidated on everything except the world changing. That change is
made with `switch_check` extended first and the module on the bench, not
overnight and blind. The header, the check and the count are what can be
believed without hands; the rest is a session with them.
