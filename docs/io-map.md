# I/O map — agreed with Will 2026-09-08 (stereo out); remapped 2026-09-21 for the resonate mode

**2026-09-21, Combust on the bench with the first resonate worlds:** "everything
needs CV and trigger in modal and accurate CV in wavetable. This is a modular
rack instrument." So: **J4 is the trigger in** under a resonate world and **CV
out A** under a wavetable one (the DG411 switches with the world), and
**J5–J8 are the four control CVs** in both modes — positions 0–3 on a
wavetable world; on a resonate one the body (the instrument of a family, the
body on a row, the voicing on a single note world), the velocity a trigger
strikes with, the decay and the coil, each summed with the **Model page** pot
on its axis (P1–P4) — the Play page's position pots stay the wavetable's, so
switching worlds does not hand the spin to wherever four pots happened to be.
The table below is the map as it was; the jack rows are updated, the rest of
the history stands.

Source of truth for the panel: `../alchemy-sdk/hardware/alchemy-lab/v2/include/alchemy/hw/alchemy_lab_v2_layout.h`
and the SDK README's jack reference. What the design wants versus what the
panel has:

| wants | panel has | resolution |
|---|---|---|
| v/oct + N position CVs + rotate CV + FM in | 6 CV jacks (J3–J8), 2 AC-coupled audio ins (J1/J2) | v/oct + 4 positions + 1 rotate on the CV jacks; FM on J1 (audio-rate, AC-coupled: linear through-zero FM works, DC offsets do not) |
| stereo out | J9/J10 are the codec's two channels | **stereo audio on J9/J10** — the two channels read the space at positions split by a small angle in a chosen rotation plane (see *Stereo* below) |
| 1–2 CV outs | any of J3–J8 can switch to a 12-bit DAC (J7/J8 on the fast STM DAC) | CV out A on J8 (default). Rotate CV is the alternative use of J8, an option. A second CV out would trade position 3 on J7, also an option |
| an encoder for menus / space browsing | none — six pots with rings and three buttons | pages by button (SDK `Pager`); the space browser is a pot on a page; deep options go to Settings (B2+B3 held) and the web page |
| N up to 6 positions | 4 CVs free | axes 4–5 are pot-only: P1/P2 of the **World** page (added when user worlds could put an effect on one), and web-visible |

## Jacks

| Jack | Direction | Function | Notes |
|---|---|---|---|
| J1 | audio in | **FM in** | AC-coupled codec input; linear TZ FM into the phase accumulator (M3). Index from the payload lane × FM depth |
| J2 | audio in | **Sync / clock** | rising edges advance the **world tour** through its division (`core/kyk_tour.h`). Schmitt-triggered with a 5 ms refractory count, because the jack is AC-coupled: a gate arrives as a step that decays and its release dips below zero, so one comparison would count both ends. Resetting the oscillator phase was the original plan for this jack and is still unimplemented — if both ever land they have to be selectable, since resetting the cycle on every clock edge is hard sync and nobody asked for that *and* a world loop |
| J3 | CV in | **v/oct** | 16-bit at audio rate, calibrated `Volts()`; read once per block |
| J4 | trig in *or* CV out | **Trigger** (resonate world) / **CV out A** (wavetable world) | As the trigger: a rising edge past 1 V strikes at the velocity axis 1 holds, 0.5 V hysteresis, 2 ms refractory, read once a block. As CV out A: MCP4728 12-bit via I²C, latched once a control frame; payload lane with depth. The DG411 switches on the world change, on the control thread |
| J5 | CV in | **CV 0**: position 0 / body | ±5 V → −1..+1, summed with Play P3 on a wavetable world, Model P1 on a resonate one |
| J6 | CV in | **CV 1**: position 1 / velocity | summed with Play P4 / Model P2; the velocity a J4 trigger strikes with, 0..1 |
| J7 | CV in | **CV 2**: position 2 / decay | summed with Play P5 / Model P3; 0.5 the world as fitted, a quarter to four times |
| J8 | CV in | **CV 3**: position 3 / coil | summed with Play P6 / Model P4; 0.5 as fitted, half to double. (Was CV out A on the STM DAC; the option of Rotate in stands) |
| J9 | audio out | **Out L** | |
| J10 | audio out | **Out R** | |

Options (Settings or web): J8 as Rotate in; J7 as CV out B (position 3 then comes from its pot only).

## Stereo
Both channels come from the same control frame `c`. The right channel's
rotation adds a spread angle `+δ` in the *stereo plane* (default plane
(0,1), selectable), the left channel `−δ`: `p_L = R(θ−δ)·c`, `p_R = R(θ+δ)·c`.
At δ=0 the output is mono; as δ grows the ears read cells further apart and
an orbit moves them through the space out of phase. The payload (cutoff,
drive, CV out) is taken at the centre `R(θ)·c` so the filter and the CV stay
single-valued. Cost: two interpolations and two IFFTs per render — the bench
measurement decides whether that fits at `render_div` 1 or needs 2. Spread
and the stereo plane are P1/P2 of the Stereo page.

## Pots and pages (B1 taps through the pages)

Eight pages ship — the SDK's limit: Play, Rotate, Stereo, Orbit, Kepler,
Couple, World and Model. Lanes would have to take a page from one of them.

**Page Model** is a resonate world's own six (`docs/modal-mode.md`), one plane
each after the way Rings lays a resonator out: P1 **Body** (the instrument of
a family, the body on a row, the voicing on a note world), P2 **Velocity**
(what J4's trigger and B2's tap strike with), P3 **Decay** (every T60, a
quarter to four times), P4 **Coil** (the pickup resonance, half to double), P5
**Voices** (1, 2, 4, round-robin), P6 **Exciter** (how much of J1's audio is
driven into the bank). J5–J8 add to the first four. Under a resonate world
**B2** taps a strike at the Velocity pot and **B3** flips the pitch lock
(locked, the default: the nearest semitone, taken at the strike, and a ring
keeps its note; free: the ring follows the pitch by the cent). Under a
wavetable world the page's pots do nothing.

**Page World** is five knobs. P1 and P2 are the offsets for axes 4 and 5; P3 to
P5 are the world tour's division, glide and free-run rate (`core/kyk_tour.h`),
which are the three things about a loop of worlds that belong under a finger
rather than in a list on the page. Those
axes have no jack and had no knob, so until a world could put a frame effect on
one (`core/kyk_userworld.h`) they were reachable only from the web page. A world
with n=4 — which is every built-in — does not read them at all: the engine
writes the whole control frame but only slews `World::N()` of it.

| | Page Play | Page Rotate | Page Stereo | Page Orbit | Page Kepler | Page Couple | Page Lanes (M3) |
|---|---|---|---|---|---|---|---|
| P1 | Coarse pitch (±3 octaves) | plane (0,1) angle | Spread δ (0–0.1 turn) | plane (0,1) rate | Gravity (bottom = off) | Coupling | cutoff depth |
| P2 | Fine (±1 semitone) | plane (0,2) | Stereo plane (6 zones) | plane (0,2) | Eccentricity | Reach (1–5) | resonance depth |
| P3 | Position 0 offset | plane (0,3) | Morph (smooth to stepped) | plane (0,3) | Orbit plane (6 zones) | Rate ×, detent at 1 | FM depth |
| P4 | Position 1 offset | plane (1,2) | Render div (2–6) | plane (1,2) | Softening | — | drive depth |
| P5 | Position 2 offset | plane (1,3) | Level | plane (1,3) | Damping | — | CV out A curve |
| P6 | Position 3 offset | plane (2,3) | CV out A depth | plane (2,3) | Radius | — | CV out A slew |

The Couple page is three knobs rather than six, and is not padded out. Reach
caps the integers a locking ratio may use: at 1 the only lock is unison, at 5
the whole staircase is open. Rate × scales all six orbit rates at once, three
octaves either side of unity with a detent at the centre — its stored value is
seeded at that detent, because a stored zero would run every orbit at an
eighth speed on a fresh boot and read as the orbit doing nothing.

N=4 has exactly six rotation planes, one per pot. For N=5–6 the extra
planes and position axes get a second Rotate/Orbit page each.

Buttons: **B1** tap: next page (ring tints per page). **B2** tap: ratio lock
on/off · hold + turn an Orbit pot: pick the rational — superseded by the
Couple page, where lock is emergent rather than chosen; under a resonate
world, a strike. **B3** tap: next space on the card · hold: reload / reset
phases (M4) — under a resonate world, the pitch lock. **B2+B3** 2 s: Settings
(SDK). Param locks (SDK, B1 hold + turn) come for free on every pot.

## Rings
Play page: P1/P2 pitch as a level; P3–P6 show the *folded, rotated* position
of that axis, not the pot — so the ring is a readout of where the CV put you
(a pip marks the pot's own value, SDK catch pip). Rotate page: angle as an
arc. Orbit page: a comet spinning at the orbit rate; a lock pip when the
ratio is locked. Lanes page: level = lane output.

## Web mirror
The panel mirror (spec §8 view 4) reads the jack list from the descriptor
(the SDK `Jacks()` component on the module, the `iomap` array from
`kykdesk --serve`), so it cannot drift from the shell.
