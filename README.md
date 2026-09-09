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
| `link.js` | HostLink v1 framing (CRC32/COBS, ported from Audiothurgist's `hostlink.js`), the priority `Link`, three transports (Web Serial, WebSocket, node stdio), the KykExt parsers/builders |
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

- **Space** (left, the hero): a 2-D projection of the N-D space on a
  selectable pair of axes (buttons top-left, or `[` / `]`). Lattice points
  faint, the cell containing the left position highlighted, the *control
  frame* (pre-rotation) as a faint cross, the folded positions as glowing
  dots — one when mono, L and R joined by a line under stereo spread. The
  dot's hue follows payload lane 0 (cutoff), its size lane 3 (drive). A
  600-point trail fades by age. Wrap axes draw dashed seams. Below the
  square, one gauge per rotation plane shows its angle as an arc; the
  stereo plane is marked.
- **Sound** (right): the current single-cycle frame (256 points of the
  left voice) and its K-bin spectrum on a 0…−96 dB scale, with the
  bandlimit cutoff (`nyq k`) marked and moving with pitch; bins above it
  are drawn grey. Payload lanes as small meters.
- **Status strip**: module id and firmware, block counter, f0 in Hz with
  the note name, cutoff bin, spread/plane, link round trip · telemetry
  fps · KB/s, CPU cycles last/avg/max as a percentage of the block
  budget from `GET_STATS` (polled once a second; red when overrunning or
  above 70 %), and the space's name and shape.
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
| 0x60 | `GET_TELEMETRY u8 flags` → block, f0, n/k/kcut/p/planes, stereo, spread, ctl/centre/posL/posR, angles, payload, `u8 mags[k]`, `i8 frame[256]` | 60 Hz poll, flags 3 |
| 0x61 | `GET_SPACE_INFO` → 64-byte header, blob CRC, stride | once at connect |
| 0x62 | `GET_CELL u32 idx` → k, p, mags, payload | (ghost spectra, M2) |
| 0x63 | `GET_STATS` → cycles last/max/avg, overruns, dropped, render_div, budget | 1 Hz |
| 0x64 | `ACTION u8 op …` | (space browser, M4) |
| 0x6E | `SET_CONTROL f0, c[n], angle[planes], spread` | dev drawer; module answers UNSUPPORTED |

`link.js` exposes everything as `window.KYK` in the browser and
`module.exports` in node.
