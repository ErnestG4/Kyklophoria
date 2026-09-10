# Kyklophoria — web surface

`index.html` is the instrument's readout: where the CV has put you in the
space, what the voice is doing, and how hard the module is working. It is
read-mostly (spec §8): everything a knob or CV does is *shown*, never
duplicated as a control. The only writes are the dev drawer over the
bridge, which stands in for the panel while there is no hardware.

Three static files, no build step, no dependencies:

| file | what |
|---|---|
| `index.html` | the page: Space view, Sound view, status strip, dev drawer, polling |
| `link.js` | HostLink v1 framing (CRC32/COBS, ported from Audiothurgist's `hostlink.js`), the priority `Link`, three transports (Web Serial, WebSocket, node stdio), the KykExt parsers/builders, and `evalBasis` — the same coordinate-to-spectrum formula the module runs, so the page can evaluate a world locally |
| `selftest.mjs` | node: codec vectors, an end-to-end run against `kykdesk --serve` over stdio, then HELLO + telemetry through the bridge |

## Running it

**Headless (no module):** build the desktop shell, start the bridge, open the page.

```sh
make host
python3 tools/bridge/bridge.py -- --gen --seed 1 --script tests/scripts/m1_rotate.txt --loop
# → http://localhost:8765  (auto-connects to the bridge)
```

The bridge (`tools/bridge/bridge.py`, Python standard library only — it
implements the RFC 6455 server itself; `bridge.mjs` is a node twin with the
same behaviour, no npm packages) serves `web/` over HTTP and upgrades
`/link` to a WebSocket that pipes bytes to a fresh `build/host/kykdesk --serve …`
child per connection. Everything after `--` goes to kykdesk; `--port` is
the bridge's own option. One client at a time.

**Module:** serve the page from a secure context (`http://localhost` or
`https://`), press **Serial**, pick the Alchemy Lab (VID:PID `0483:5740`,
identified by HELLO). Close Hermetic's `/program` first — one page holds
the port. `python3 -m http.server 8080 -d web` does for local use.

```sh
python3 tests/link_check.py      # needs build/host/kykdesk; stdio + the Python bridge (part of make test)
node web/selftest.mjs            # optional node twin; --no-bridge skips the WebSocket half
```

## Views

- **World strip** (top): the worlds the module can be in, from
  `GET_WORLDS`, the live one marked; click to switch. A world marked `ƒ`
  is *analytic* — a formula, so switching to it is instant — and one
  without is *tabulated*, a lattice the module has to expand, which takes
  a moment and shows as `expanding…`. The note beside the list describes
  the current world. On the right, what the terrain is shaded by.
- **Space** (left, the hero): a 2-D projection of the N-D space on a
  selectable pair of axes (buttons top-left, or `[` / `]`).

  When the world is analytic the page fetches its basis once (`GET_BASIS`,
  about 1.3 KB for a whole world) and **shades the plane as a terrain**,
  evaluating the formula itself on a 72² grid — the other axes held at the
  live position, each grid point's spectrum computed the way the module
  computes it (`core/kyk_world.h`). Shade by spectral centroid, flatness,
  or the energy above K/4; all three are ratios, so the missing
  normalisation cancels.

  The scale is **absolute**, not per-slice. Each measure is logged against
  its own bound — the centroid and K·flatness both live in [1,K], so
  log/log K lands them on [0,1] with no free constant; the high-end
  fraction has no positive lower bound (it is exactly zero over the whole
  of Plate and of Drum) so it takes a stated −40 dB floor. That means two
  slices of one world, and two different worlds, can be compared. It also
  means a slice that really is flat paints one colour and says **flat
  field**, instead of being stretched to fill the ramp — the old
  autoscale made rounding error look like structure. The grid is rebuilt
  only when the world, the projection, the off-plane position, the measure
  or the Morph knob changes, never per frame, and fades while a rebuild is
  pending. A tabulated world has no formula to draw, so it falls back to
  faint lattice dots and says so.

  Over that: the cell containing the left position highlighted (lattice
  worlds only — an analytic one has no cells), the *control frame*
  (pre-rotation) as a cross, and the folded positions as dots — one when
  mono, L a disc and R a ring joined by a line under stereo spread. Both
  voices take the **same** colour, from the same measure that painted the
  ground under them, through a ramp that is monotone in lightness; they
  are one timbre at two points, and separate hues claimed otherwise. Dot,
  ring, cross and connector all carry a dark under-stroke and a light rim,
  because a dot that shares the terrain's colour scheme can otherwise
  vanish into ground of its own value. Radius follows payload lane 3 on an
  envelope that adapts to what the world actually does.

  The trail keeps 600 samples, each holding the timbre it was drawn at,
  with alpha linear in recency. It is **paced by the motion**: the sample
  interval is set from the slowest rotation that is actually moving the
  picture, so the trail holds about one turn of it rather than a fixed ten
  seconds — a plane turning once a minute used to draw a sixth of its
  circle. It says how long a window it covers, and says **strobed** when
  fewer than eight samples land per turn of the fastest plane.

  A clamped axis pushed past the edge of the space gets an arrow at the
  wall, sized logarithmically, because that is the one thing here that is
  invisible and audible at once: the position stops moving while the knob,
  the CV and the rotation all keep going. Nothing is drawn on a wrapped
  axis, which loses no information. Lattice worlds declare their topology;
  for a formula world the page works out which fold happened by watching.

  Wrap axes draw dashed seams. Below the square, one gauge per rotation
  plane shows its angle as a **hand** — an arc from twelve o'clock
  collapsed to nothing at every wrap — each sized by how far the position
  sits from the pivot in that plane, since a rotation about a point you
  are standing on moves you nowhere. The stereo plane is marked. Lock gets
  a two-second sparkline beside its bar: one number cannot say whether the
  rotation is settling, sliding or hunting. No p:q label — the ratio is
  not on the wire and guessing one would state a confidence nothing
  supports. When Kepler is running, an inset draws the orbit in its own
  plane, brightening with `rush` so periapsis is the bright part.
- **Sound** (right): the current single-cycle frame (256 points of the
  left voice) and its K-bin spectrum on a +6…−96 dB scale, with the
  bandlimit cutoff (`nyq k`) marked and moving with pitch; bins above it
  are drawn grey. Payload lanes as small meters. Deliberately untinted:
  colour on this page means position-in-timbre and lives only in the
  Space view.
- **Status strip**: module id and firmware, block counter, f0 in Hz with
  the note name, cutoff bin, spread/plane, link round trip · telemetry
  fps · KB/s, CPU cycles last/avg/max as a percentage of the block
  budget from `GET_STATS` (polled once a second; red when overrunning or
  above 70 %), and the space's name and shape. Under stereo the spread
  readout also gives the geometric width — L and R are the centre turned
  by ∓δ in one plane, so the chord between them is 2·r·sin(2πδ) — and,
  where the world is a formula, how far apart the two voices' centroids
  actually are. The projected separation is zero whenever the spread plane
  is not the plane on screen, so the picture alone said mono.
- **Controls** (bridge only): f0 (log), the N control axes, the
  N(N−1)/2 rotation angles in turns, stereo spread — sent with
  `SET_CONTROL` at most 30 times a second. Hidden on a serial link: the
  panel owns those.

Telemetry is polled with a keyed request at up to 60 Hz (so a slow link
coalesces polls instead of queuing them); drawing runs at
`requestAnimationFrame`. Not connected: the empty lattice is drawn.

## Protocol (docs/hostlink.md)

Standard `HELLO` (0x01) and `GET_DESCRIPTOR` (0x02); the descriptor's
`kyk` block advertises the extension and its `iomap` array feeds the
panel mirror (M2). Extension commands, bodies in `core/kyk_telemetry.h`
and `shell/common/kyk_ext.h`:

| cmd | | page use |
|---|---|---|
| 0x60 | `GET_TELEMETRY u8 flags` → block, f0, n/k/kcut/p/planes, stereo, spread, ctl/centre/posL/posR, angles, payload, `u8 mags[k]`, `i8 frame[256]`, motion block (Kepler, couple, lock, **sharp**) | 60 Hz poll, flags 7 |
| 0x61 | `GET_SPACE_INFO` → 64-byte header, blob CRC, stride | once at connect |
| 0x62 | `GET_CELL u32 idx` → k, p, mags, payload | (ghost spectra, M2) |
| 0x63 | `GET_STATS` → cycles last/max/avg, overruns, dropped, render_div, budget | 1 Hz |
| 0x64 | `ACTION u8 op …` | (space browser, M4) |
| 0x65 | `GET_WORLDS [u8 start]` → total, current, start, sent, then entries | once at connect, paged |
| 0x66 | `GET_BASIS u8 world, u16 off` → an analytic world's formula, chunked | on switching to a formula world |
| 0x6E | `SET_CONTROL f0, c[n], angle[planes], spread` | dev drawer; module answers UNSUPPORTED |

`link.js` exposes everything as `window.KYK` in the browser and
`module.exports` in node.
