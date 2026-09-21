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

    bank      y[n] = 2r cos w · y[n−1] − r² · y[n−2]             three MACs a mode a sample
    strike    a state, not an impulse: every mode set to its swing at its fitted
              phase (the hammer's timing per mode), ramped in over 3 ms in a
              second bank and folded into the first
    pickup    u → phi = 1/(1 + ((u−h)/w)²)  (magnetic)  or  1/(1 − u/g)  (electrostatic)
              → first difference (Faraday) → the coil's second-order low-pass
    wash      what a dense body leaves after its modes and its burst (a tam-tam is
              hundreds of modes; three times the bank bought five per cent): white
              noise through eight octave band-passes, each under its own level and
              T60 fitted to the energy the recording has and the model does not
              (ModalBake tools/noise.py), sixteen numbers a point; nothing on a
              world whose bands are zero
    burst     the attack the modes are not: the recording's first 40 ms minus the
              model's, stored 16-bit in the world, played once at the strike after
              the pickup, scaled by the strike's swing; a shaped world crossfades the
              two takes' bursts that bracket the swing (the MT-32's idea: a stored
              attack under a synthesised sustain, here measured from the note it sits under)

The metal of a tine or reed piano is one sine; every harmonic and all of the
velocity behaviour is the pickup's (ModalBake, from Muenster & Pfeifle's
38 kfps measurement and a fit that rediscovered it). So an electric world is
a handful of modes and five numbers a note, and velocity comes out of the
physics rather than out of a sample layer.

Worlds arrive as `.kykm` blobs (ModalBake `tools/export.py`, version 4): five
bytes a mode — cents, decay, level, phase — the stage and the loudest mode's
absolute gain per point, the wash's eight bands, then the point's bursts (~4 KB each; the EP with two
a note is 650 KB, the Wurlitzer 45 KB). Points are variable-length and walked
at note-on. The EP is
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
- `make armcost`: `c_resonate` 123 instructions for the whole function with
  the strike bank in it (68 without), about fifteen a mode a sample in the
  main loop and the same again in the strike loop for the 3 ms a strike
  ramps in; `c_pickup` 121 with the electrostatic
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

## What is wired now (host-tested)

- `World::Kind::Resonate` (11) and `World::UseResonate(blob, bytes)`: a
  `.kykm` attached where it lies; `N() == 1` (the note), `K() == 1` (a
  silent frame). `Evaluate` gives silence for it, as for any unknown kind.
- `Engine`: a `ResonatorVoice` after `osc_.Process`, built in `SetWorld` —
  and only there — at the current pitch; `Engine::Strike(velocity)` retunes
  the bank with its state ringing on when the pitch has moved (the way the
  oscillator follows the pitch) and strikes; `StereoEngine::Strike` both
  sides. Every other kind renders bit for bit as before: the path is behind
  the world's kind.
- Host: the desktop script has `t strike v`, and the phase-reset action
  strikes at 0.8 until the page has a velocity control.
- `tests/resonate_engine_check`: the engine path equals the standalone voice
  bit for bit; Saw's frame is unchanged by the path's existence; arriving
  at a resonate world equals starting in it past the oscillator's
  crossfade; a strike at a new pitch is the new note (a C4 series with the
  C3's fundamental 52 dB under it), a retune keeps the ring, a re-strike
  adds. The golden renders are unchanged.

- A resonate world is a **card world**. A `.kykm` is a sample library's
  size — 66 KB for the Wurlitzer, a megabyte for the EP with its attack
  bursts — which is past the wire's receiver (`kUserBlobMax`, 6.7 KB) and
  past a slot's store, so it never crosses the wire and the page never
  parses it: the card scan lists `.kykm` beside `.kykw`, card-to-slot keeps
  the whole file, slot-live builds the world from it, and the page has the
  same three moves for both kinds (its CARD **load** button does slot then
  play for a resonator, since the straight-to-live path has nowhere to put
  one). What a resonator cannot do is what a frame world can — be a morph
  target, a tour stop, or a snapshot — and each is refused as `BAD_STATE`
  rather than half-done. The host (`shell/desktop/serve.h`) keeps the file
  in the slot's vector and the world reads it there, so freeing the slot
  that is playing is refused (judged by the blob pointer, not by the live
  index, which no world select clears).
- The module (`shell/alchemy/main.cpp`, *built, not run*): four fixed
  2 MB regions in SDRAM (`gResArena`), a slot holding a resonator keeps its
  region's number in `gSlotRes`; the file is read through the SDMMC
  staging buffer a chunk at a time (IDMA's reach is the staging buffer's
  section); the World reads the region in place, so anything that would
  write a slot whose region is playing — free, card-to-slot, put, snapshot
  — is refused rather than glitched. Slot swap moves the region number and
  copies at most a blob's worth of bytes; a resonate slot reads back over
  the wire from its region; saving one to the card is refused (it came from
  there). The tour will not take a resonate slot as a stop.
- The page: `.kykm` is labelled `· resonator` in both card lists, a slot
  loaded from one says so, the world bar says `resonator` for a 1-D user
  world, and every read of a position on a projection axis the world does
  not have (a resonator has one axis, the projection is a pair) sits at the
  centre — `pagecheck` has an N=1 case, and before the fix it drew 22 marks
  at NaN.
- `link_check.py` (card: list, refuse a truncated file, load, play as 1-D,
  refuse target/snapshot/free-while-live, free after a built-in takes
  over) and the node selftest (the same through `link.js`).

Still the bench's: the hardware trigger (J2's edge, or the oscillator into
the bank), the three pots on `ResonatorWorld::voicing / decay / coil`, the
velocity, the cycle budget, and the module's card path above, which is
built and untested. A retune re-reads the old state at the new frequency,
which lifts or drops the ringing note's amplitude by the frequency ratio at
low frequencies (a tone up: about 1 dB; an octave: 6 dB); if that is heard,
the state can be rescaled at retune.

## What is not done, and why

Nothing above the header is wired. The engine change is small in lines and
large in consequence — it puts a second signal model behind `Process`, and
the notes on this repo say what happens to state derived from a world when
it is invalidated on everything except the world changing. That change is
made with `switch_check` extended first and the module on the bench, not
overnight and blind. The header, the check and the count are what can be
believed without hands; the rest is a session with them.
