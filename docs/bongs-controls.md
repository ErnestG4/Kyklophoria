# Bongs: the panel and the page, reworked for modal

Combust, 29 September: "The Modal firmware is named Bongs. Let's fully rework
the panel controls and web controls to be optimized for modal. Page 1 is fine,
everything else needs thought."

A proposal, not built. Done so far: the name (firmware, binary `bongs.bin`,
page), and on the page the wavetable's play tab removed with model renamed
play (2011bf5).

## What Bongs has today

Eight pages of six pots, B1 steps through them; B2 taps a strike, B3 flips the
pitch lock, J4 strikes.

| page | P1 | P2 | P3 | P4 | P5 | P6 | on a resonator |
|---|---|---|---|---|---|---|---|
| 1 Play | Coarse | Fine | Body | Velocity | Decay | Coil | all live; **keep** |
| 2 Rotate | 6 plane angles | | | | | | **dead**: the rotation leaves a resonator alone |
| 3 Stereo | Spread | Stereo plane | — | — | Level | Exciter (J1) | Spread and plane place the two ears along the string; two dead |
| 4 Orbit | 6 plane rates | | | | | | live: each spins a pair of the four axes |
| 5 Kepler | Gravity | Eccentricity | Plane | Softening | Damping | Radius | live: a Kepler orbit moves the axes |
| 6 Couple | Coupling | Reach | Rate | Bodies | Company | — | live (the orbits' coupling); one dead |
| 7 World | Position 4 | Position 5 | Voices | — | — | | Bongs' worlds have four axes: only Voices is live |
| 8 Exciter | Type | Timbre | Position | Noise | Mass | | live; P6 empty |

Engine controls with **no pot**, reachable only from the page (or not at all):
the release of a stolen voice (5-1000 ms), "dig in" (velocity tracking), a
family's body axis morphing its members or switching, and the pitch lock
(B3 only). Nine pots are dead or empty.

## Proposed panel: six pages, nothing dead

| page | P1 | P2 | P3 | P4 | P5 | P6 |
|---|---|---|---|---|---|---|
| 1 **Play** (as now) | Coarse | Fine | Body | Velocity | Decay | Coil |
| 2 **Exciter** | Type | Timbre | Position | Noise | Mass | J1 in |
| 3 **Voices** | Voices (1 / 2 / 4) | Release | Dig in | Family (switch / morph) | Pitch (lock / bend) | Level |
| 4 **Space** | Spread (the ears apart) | Listen (where along the string) | Ear orbit (which plane spins them) | — | — | — |
| 5 **Motion** | Body·Velocity | Body·Decay | Body·Coil | Velocity·Decay | Velocity·Coil | Decay·Coil |
| 6 **Kepler** | Gravity | Eccentricity | Plane | Damping | Radius | Coupling |

- **Exciter gets J1's amount** (from Stereo P6): J1 is now a force into the
  exciter's loop, so it belongs with the exciter.
- **Voices collects the playing controls** that had no pot. Pitch as a pot
  and B3 both flip the lock. Level moves here from Stereo: the page you adjust
  a performance on.
- **Space is the ears.** Listen is new: the ears' centre set directly, where
  today it only moves with a rotation angle. P4-P6 are free; Space could fold
  into Voices if six pages is too many.
- **Motion is Orbit**, its planes named by the axes they turn (on a
  four-axis world the six planes are exactly the six pairs).
- **Kepler takes Couple's one essential pot** (Coupling). Softening, Reach,
  Rate, Bodies and Company go. They are fine-tuning for the wavetable's
  many-body orbits.
- **Gone:** Rotate, World, and the dead pots (Morph sharp, Render div, World
  morph, Tour glide and rate).

Order is a question: Exciter second because it is what you reach for after
the note, Motion and Kepler last because they are set-and-leave.

## Proposed page (the play tab)

Today's rows (pitch, strike, body, ring, voices, exciter) become **one row per
panel page, in the panel's order and names**, each showing the pots as sliders
that follow the module. Moving a slider moves the pot's value; the pot picks
it up when turned, the same soft takeover the pager does between pages. So
the page and the panel are one instrument. It needs one new command, "set
this pot's value" (page, pot, value), in place of a special action per
control. The exciter has none today, which is why it cannot show.

The axis pad, the modes view, the instrument chips and the card loader stay
where they are.

## Questions for Combust

1. Six pages, or five (Space folded into Voices)?
2. Keep Kepler at all on Bongs? It is a wavetable exploration toy; on a
   struck instrument the Motion page may be enough.
3. Pitch lock as a pot on Voices as well as B3, or B3 only?
4. The page mirroring every pot through one "set pot" command: yes?
