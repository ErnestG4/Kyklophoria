# I/O map — agreed with Will 2026-09-08 (stereo out; CV out on J8)

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
| J2 | audio in | **Sync** | rising edge resets phase (`RisingEdge()`, AC-coupling passes edges) |
| J3 | CV in | **v/oct** | 16-bit at audio rate, calibrated `Volts()`; read once per block |
| J4 | CV in | **Position 0** | ±5 V → −1..+1, summed with the offset pot, then rotation and fold |
| J5 | CV in | **Position 1** | |
| J6 | CV in | **Position 2** | |
| J7 | CV in | **Position 3** | |
| J8 | CV out *or* in | **CV out A** (default) / Rotate in (option) | STM DAC, 12-bit, <1 µs; payload lane with curve/slew/depth. As Rotate: drives the plane picked on the rotation page |
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

Seven pages ship: Play, Rotate, Stereo, Orbit, Kepler, Couple and World. Lanes
arrives with M3.

**Page World** is two knobs: P1 and P2 are the offsets for axes 4 and 5. Those
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
Couple page, where lock is emergent rather than chosen. **B3** tap: next
space on the card · hold: reload / reset phases (M4). **B2+B3** 2 s: Settings
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
