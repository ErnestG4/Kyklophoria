# HostLink extension — kyklophoria, `"kyk":{"ext":1}` (M1)

Base protocol: `../alchemy-sdk/docs/hostlink-protocol.md` (v1: COBS + CRC32
frames, stop-and-wait, host-polled, `max_body` announced in HELLO). Both
shells build with `ALCHEMY_HOSTLINK_MAX_BODY=1024`. Standard commands
(HELLO, descriptor, presets, live state, filesystem 0x50–0x5F) come from
the SDK on the module; the desktop `kykdesk --serve` answers HELLO and
GET_DESCRIPTOR itself. Our commands claim **0x60–0x6F** through one
`IHostlinkExtension` shared by both shells (`shell/common/kyk_ext.h`), so
the bytes are identical over USB and over the bridge.

The protocol has no unsolicited frames. "Streaming" telemetry is the page
polling GET_TELEMETRY at up to 60 Hz with a keyed request (one in flight,
never queued twice). On the module the audio callback encodes a snapshot
into a double buffer only when the control loop has asked for one, so the
link can never stall audio and a slow host simply sees fewer frames.
Measured over the bridge: 60 frames/s at 28 KB/s with spectrum and frame.

All replies start with `u8 status` (0 OK, 1 UNSUPPORTED, 2 BAD_ARGS,
3 BAD_STATE). Multi-byte fields are little-endian.

## Commands

### 0x60 GET_TELEMETRY
Request: `u8 flags` (bit0 spectrum, bit1 frame, bit2 motion).
Reply (`core/kyk_telemetry.h`, `EncodeTelemetry`):

```
u32 block            audio block counter (left voice)
f32 f0               Hz
u8  n, k, kcut, p    dims, harmonics, harmonics below Nyquist, payload lanes
u8  planes, flags    n(n-1)/2, the flags echoed
u8  stereo           1 when spread ≠ 0 (two positions)
u8  spread_plane
f32 spread           turns
f32 ctl[n]           control frame as set (pots + CVs)
f32 centre[n]        after rotation, before folding
f32 posL[n]          left voice, folded (what was interpolated)
f32 posR[n]          right voice, folded (= posL when mono)
f32 angle[planes]    turns
f32 payload[p]       interpolated at the centre, 0..1
u8  mags[k]          if flags&1: left voice, pre-bandlimit; 0 = ≤ −96 dB, 255 = 0 dB
i8  frame[256]       if flags&2: left voice's frame, decimated, ×40 clipped
u8  kep_running      if flags&4 ─┐
u8  kep_plane                    │
f32 kep_x, kep_y                 │  how the position is moving on its own:
f32 kep_rush         0..1, 1 at periapsis
f32 couple           Kuramoto coupling strength as set
f32 lock             0..1, how closed the orbit figure is ─┘
```
N=4, K=64, P=8 with spectrum and frame: 460 bytes; with motion as well, 482.

The motion block is deliberately last. A host written against an earlier
firmware reads every field it knows by fixed offset and ignores the tail, so
the block could be added without a protocol version bump — which is the whole
reason for putting growth at the end rather than in the middle.

### 0x61 GET_SPACE_INFO
Reply: the 64-byte `SpaceHeader` (docs/space-format.md), `u32 crc32` of the
whole blob, `u16 stride` (floats per point).

### 0x62 GET_CELL
Request: `u32 idx`. Reply: `u8 k, u8 p, u8 mags[k]` (dB-scaled as above),
`f32 payload[p]`. BAD_ARGS past `point_count`. The page fetches the 2^N
corner cells of the active hypercube once and caches by index.

### 0x63 GET_STATS
Reply: `u32 cycles_last, cycles_max, cycles_avg` (per block; the module
counts DWT cycles at 480 MHz, the desktop reports nanoseconds), `u16
overruns` (blocks over budget since boot), `u16 dropped` (telemetry
requests that arrived before the previous snapshot was taken), `u8
render_div`, `u32 cycles_budget` (240 000 on the module, 500 000 ns on the
desktop). Max and avg are over the last one-second window.

### 0x64 ACTION
Request: `u8 op [, args]`. 0 reset phases · 3 set render_div `u8` (desktop
only; the pot owns it on the module) · 1 next space, 2 load space `u8 len,
name` reserved for M4. Reply: status only.

### 0x65 GET_WORLDS
Request: empty. Reply: `u8 count`, `u8 current`, then per world `u8 kind`
(1 tabulated, 2 analytic), `str name`, `str note`. The list is built into the
firmware (`core/kyk_worlds.h`), so it needs no card.

### 0x66 GET_BASIS
Request: `u8 world, u32 offset, u16 max`. Reply: `u32 total, u32 offset,
u16 n, bytes[n]`; chunk until `total` bytes are in hand. UNSUPPORTED when the
world is tabulated, because it has no formula to send.

The blob is the whole world in about 1.3 KB: `u8 n, u8 k, f32 extent,
f32 floor, f32 mean[k], f32 comp[n][k]` (row-major, component `a` weight for
harmonic `i` at `comp[a*k + i]`). A host that has it can evaluate the space
anywhere without asking again, which is how the page draws the terrain rather
than a scatter of sampled dots:

    coord[a] = (2·p[a] − 1) · extent
    y[i]     = mean[i] + Σ_a coord[a] · comp[a·k + i]
    m[i]     = max(0, exp(y[i]) − floor),  then scale so Σ m² = 2

### 0x64 ACTION op 4 — select world
`u8 index`. Analytic worlds switch on a pointer write. A tabulated one has to
be expanded first, on the control thread, and answers BUSY (9) if another
expansion is already running.

### 0x6E SET_CONTROL (desktop bridge only)
Request: `f32 f0, u8 n, f32 c[n], u8 planes, f32 angle[planes], f32 spread`.
Once received, the host owns the controls and the script stops driving
them. The module answers UNSUPPORTED.

## Descriptor
The root object carries `"kyk":{"ext":1,"telemetry":96,"space":97,"cell":98,
"stats":99,"action":100,"control":110}` from `DescriptorRootJson()`. The
desktop adds `"space":{name,n,side,k,p}` and `"iomap":[{jack,id,name,sig}…]`
(docs/io-map.md); the module's descriptor carries the same jacks through the
SDK's `Jacks()` component and its pages/knobs through the normal
auto-description, so the page's panel mirror follows the firmware.

## Desktop bridge
`python3 tools/bridge/bridge.py [--port 8765] -- --gen --seed 1 --script
tests/scripts/m1_rotate.txt --loop` serves `web/` over HTTP and pipes a
WebSocket at `/link` byte-for-byte to `kykdesk --serve` on stdio. Python
standard library only — no pip, no node, no npm. `tests/link_check.py`
exercises both paths and is part of `tests/run.sh`. A node twin
(`tools/bridge/bridge.mjs`, `web/selftest.mjs`) exists for anyone who
prefers it; it uses only node's built-in modules, never npm.

## Versioning
Any change to a reply layout bumps `"ext"`; the page feature-detects. `ext` is
2 as of the world commands.
