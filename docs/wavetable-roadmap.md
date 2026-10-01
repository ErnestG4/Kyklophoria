# Kyklophoria (the wavetable firmware): the roadmap

The live list for the wavetable firmware, as of 30 September 2026. It
replaces `docs/checklist.md` as the place to start; that file keeps the
history, the measurements and the reasons, and its open items are drawn
from here. The Bongs list is `docs/bongs-roadmap.md`.

## Where it stands

- **Beta.** Twenty-one of the twenty-two built-in worlds have had bench time;
  Grit is measured and not played. Morphing, user worlds, import, the card
  and the tour are tested by the suite and barely played (README, "Not built
  yet").
- **Ready to share** as `build-wavetable/kyklophoria-wavetable.bin` with the
  page at https://ernestg4.github.io/Kyklophoria/?page=wavetable (one repo,
  one page, both firmwares; publishing steps in the README's "The web page").
- **Tidied on 30 September** before sharing, every one with a test that fails
  without it: the morph target is no longer read while it is rewritten (it
  was, for ~10 ms at full level); a new morph target clears the aim; a slot's
  world made the target is withdrawn while written; `Space::Attach` refuses a
  non-finite coefficient and sizes in 64 bits; the page clears its readouts on
  a disconnect, runs one set of poll loops across a reconnect, sends one
  world at a time, and the audition stops when what it plays goes.

## Owed a bench session (needs hands)

1. **The max CPU reading** after the render-deadband fixes (checklist §1):
   the 33% average / 97% max was measured before them. Render divider 1 is off
   the knob until this is known.
2. **The orbit by ear** (checklist "Next"): it renders continuously by nature.
3. **The dark stereo circle and trail** on a wavetable world: a screenshot,
   the world, whether the ground is dark too.
4. **The build view's footer** may sit in row 2 above the pane (derived from
   the grid spec, never seen): open the build tab and look.
5. **Morph and the tour by ear**: built and tested, barely played.

## Decisions for Combust

- **`cycleToNode`'s sign**: the engine plays −Σ m·sin(hθ) and import stores
  +sine, so every imported wave plays inverted. Inaudible alone; fixing it
  inverts every `.kykw` already written.
- **Eigenspace or field** in the rack (checklist §1).
- **The backports from Bongs** (`docs/bongs-controls.md`): the panel as rows
  on this page too (one hand on each setting, the pots moved from the page),
  and the orbit, Kepler and stereo-plane labels named by the world's axes
  rather than "0,1".

## Not built yet

- FM and sync on J1 and J2 (wired to nothing).
- The filter, drive and FM-index payload lanes (only CV out A is routed).
- Wrap and sphere on the panel (sphere is not implemented at all).
- Per-axis LFO shapes; scattered (non-lattice) spaces.
- A morph between two shaped worlds runs only the live world's shapers, and a
  tour step between worlds that render the frame differently is a switch:
  128 of 462 pairs seamless. The honest fix is a second inverse FFT per
  render, which the CPU does not have.
- Writing a `.kyk` space to the card (only `.kykw` worlds are written).

## Known, not to fix

- v/oct is read at control rate. The aliasing is wanted (Combust: "this is a
  haphazard device and the aliasing is part of the fun").
- The Morph knob at the very top can render digital silence on a few pairs;
  the corner stays open as preferable to the alternative tried.

## Budget

Wavetable SRAM 83.1% (about 81 KB free).
