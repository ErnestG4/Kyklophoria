# Bongs: the panel and the page, reworked for modal

Combust, 29 September: "The Modal firmware is named Bongs. Let's fully rework
the panel controls and web controls to be optimized for modal. Page 1 is fine,
everything else needs thought."

Combust's answers (29 September): six pages named Play, Exciter,
Resonator, Space, Motion, Kepler; Kepler stays ("an orbital decay or
resonance twister ... much better than the motion/orbits themselves for
modal"); the pitch pot on the Resonator page; the page mirrors every pot
through one "set pot" command.

Built: the name (firmware, `bongs.bin`, the page) and the page's single play
view (2011bf5); Listen in the engine (53a9fc9); the six-page panel (below).
The page's rows of sliders over the "set pot" command (0x70) are built too.

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
| 3 **Resonator** | Voices (1 / 2 / 4) | Release | Dig in | Family (switch / morph) | Pitch (lock / bend) | Level |
| 4 **Space** | Spread (the ears apart) | Listen (where along the string) | Ear orbit (which plane spins them) | — | — | — |
| 5 **Motion** | Body·Velocity | Body·Decay | Body·Coil | Velocity·Decay | Velocity·Coil | Decay·Coil |
| 6 **Kepler** | Gravity | Eccentricity | Plane | Damping | Radius | Coupling |

- **Exciter gets J1's amount** (from Stereo P6): J1 is now a force into the
  exciter's loop, so it belongs with the exciter.
- **Resonator collects the playing controls** that had no pot. Pitch as a pot
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

## As built (the panel)

As the table above, with the Resonator page's name. Details:
- Knobs with no page on Bongs (Rotate's angles, Softening, Reach, Rate,
  Bodies, Company, morph sharpness, render divider, axes 4-5, the tour's
  glide and rate, World morph) are held at the value their stored default
  had, so the engine hears what it heard before the pages went.
- The Resonator page's pots take effect when moved, so a setting made from
  the page holds until a hand moves the pot. B3 still flips the lock and
  moves the Pitch pot's stored value with it, to be caught.
- Boot defaults: Exciter Trained; release 40 ms; no dig-in; family switch;
  pitch locked; Listen at its centre (a quarter of the string, as before).
- **Saved Bongs presets disappear**: the pager's schema includes its page
  count, so the old eight-page slots read as empty instead of loading into
  the wrong pots.

The page (29 September, later): with the panel rows showing, the play tab's
planes that repeated a pot are gone — the lock's chips (now a readout),
trigger velocity, dig in, the morph chip, decay and coil, voices, release,
and the exciter note. Two hands on one setting disagreed: the chips sent
actions the pot never heard of, so the pot and the page differed until the
pot was next turned. Left: the axis pad, the strike pads and their velocity,
the family's members (a readout of which one sounds), the instruments and
the card. The page for both firmwares and the bridge keep every plane.

## Backport to Kyklophoria (the wavetable firmware)

Kept as the Bongs work goes:
- **The "set pot" command is built** (0x70 POTS, both firmwares answer it;
  KykExt claims 0x60-0x70). The page's rows are shown on Bongs only
  (`renderPanelRows`, `fwMode === 'modal'`); for the wavetable, lift that
  condition and find the rows a place on its play view.
- **The descriptor's page list** carried every page Bongs has; check the
  wavetable one keeps listing all of its own if a page is ever added (the
  Exciter was missing from it on Bongs).
- **Axis-named orbit and plane labels** (Body-Velocity...): the wavetable's
  worlds name their axes too (the page knows them), so its Orbit, Kepler and
  Stereo plane labels could say which axes they turn instead of "0,1".
- **A Listen-like direct control** is Bongs-only (the wavetable's stereo is
  a rotation), nothing to port.

## Gates (29 September)

Combust: "How about gated exciters?" A sustained exciter (Bow, Reed, Lips)
has a gate. While it is open, the exciter drives the newest note with the
Play page's P4 (velocity) as its energy. Shut, the exciter lifts and the note
rings free.

- **J4 held high** is a gate. Its rising edge strikes, as before, and it
  holds while J4 stays over 1 V (closing under 0.5 V).
- **B2 held** is a gate: a tap still strikes; held, it blows or bows.
- **The page's pad held** is a gate (`ACTION gate`, 24).
- **Nothing gating:** with J4 not high in the last 10 s and neither held,
  the gate stays open, and energy alone articulates as before.

A struck exciter takes no notice of the gate.

Fixed later that night: B2 and the pad owned the gate only while held, so
letting go gave it straight back to open and the bow played on. Now all
three own it for the ten seconds, and a note whose gate has shut stays
lifted until the next strike, so the fallback to open ten seconds later
does not put the bow back on it. The page's strike pads are gates while
held, as the axis is (pressed: gate open and strike; let go: shut).
