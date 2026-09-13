# HostLink extension — kyklophoria, `"kyk":{"ext":5}`

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
f32 ctl[n]           control frame after slew; the Kepler attractor
f32 centre[n]        after rotation, before folding
f32 posL[n]          left voice, folded (what was interpolated)
f32 posR[n]          right voice, folded (= posL when mono)
f32 angle[planes]    turns
f32 payload[p]       interpolated at the centre, 0..1
u8  mags[k]          if flags&1: left voice, pre-bandlimit; 0 = ≤ −96 dB, 255 = +6 dB
i8  frame[256]       if flags&2: left voice's frame, decimated, ×40 clipped
u8  kep_running      if flags&4 ─┐
u8  kep_plane                    │
f32 kep_x, kep_y                 │  how the position is moving on its own:
f32 kep_rush         0..1, 1 at periapsis
f32 couple           Kuramoto coupling strength as set
f32 lock             0..1, how closed the orbit figure is
u8  sharp            the **Morph** knob (Stereo P3), 0..255: how tight the
                     basins are. Not the same control as `morph` below
u8  kep_bodies       how many are falling; body 0 is kep_x/kep_y above
f32 x, y             per body 1..kep_bodies-1 — only the perturbers
u8  page             which pager page the panel is showing
u8  morph            the **World morph** knob (Couple P6), 0..255: how far
                     towards `morph_world`
u8  morph_world      what it blends towards; 0xFF: nothing, so it does nothing
u16 mute             muted motions; bits 0..14 planes, bit 15 Kepler
u8  aimed            whether the morph reads its target somewhere else
u8  pots[6]          the live page's knob positions
u8  vals_valid       whether the six below mean anything
u8  vals[6]          what the live page's six knobs are *worth* — not where
                     they are sitting; the panel catches
u8  world            which world is playing; 0xFF: a user world, no index ─┘
```
`morph_world` carries one more value than it used to: **0xFE** means the target
is a world of yours rather than a built-in, since it has no index. 0xFF still
means no target at all, and *absent* means firmware too old to say — three
different things.
```
```
N=4, K=64, P=8 with spectrum and frame: 460 bytes; with motion as well and one
body, 504.

`pots` and `vals` are two different numbers and the difference is the point.
The panel catches rather than jumps: arriving on a page leaves each pot where
the hand left it while the parameter keeps the value it had, and turning the
pot through that value is what picks it up. A host with only `pots` is drawing
the one thing that is not what the instrument is doing. `vals_valid` is 0 where
a shell has no pager to ask — six zeroes and six knobs that really are at zero
are not the same claim.

The motion block is deliberately last, and everything after `sharp` was added
to it later — the bodies, the pager page, the Morph knob and its target, the
mute mask, the aim flag, the knob positions, and the live world. A host written
against an earlier firmware reads every field it knows by fixed offset and
ignores the tail, so none of them needed a protocol version bump. That is the
whole reason for putting growth at the end rather than in the middle.

It runs the other way too, and a host has to mean it: every one of those fields
is optional on the way in, and absent is not the same as zero. `world` in
particular — a host that read a missing byte as 0xFF would conclude that every
module older than this field was playing a user world.

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
Request: `u8 op [, args]`. Reply: status only.

| op | args | what it does |
|---|---|---|
| 0 | — | reset phases |
| 1 | — | next space (reserved) |
| 2 | `u8 len, name` | load space (reserved) |
| 3 | `u8` | set render divider (desktop only; the pot owns it on the module) |
| 4 | `u8 world` | select a built-in world. A tabulated world must be expanded first, so this answers BUSY until it is done and the switch itself happens on the control loop — the reply returning does not mean the world has changed. Watch the live world index in the telemetry tail. |
| 5 | `u8 world` | which world the Morph knob blends towards; 0xFF clears it. Formula worlds only. Choosing is setup and lives on the host; how far is a knob and lives on the panel. |
| 6 | — | re-scan the card's world folder |
| 7 | `u8 index` | load a card world by its index in the 0x68 list |
| 8 | `u8` | phase override: 0 the world's own convention, 1 force sine, 2 force cosine — a cosine twin of any world without doubling the world list |
| 9 | `u16 mask` | mute motions: bits 0..14 a rotation plane's rate, bit 15 Kepler. A mute rather than a zero, so the knob keeps its value and unmuting restores it |
| 11 | `u8 slot` | play the world in that slot |
| 12 | `u8 slot` | morph towards that slot; 0xFF clears the target. This is the one thing the morph could not do before — the target index space was the built-ins, so a world you made could never be one end of a blend |
| 13 | `u8 slot` | forget a slot |
| 14 | `u8 a, u8 b` | exchange two slots, contents and names, with `live` and `target` following the contents rather than the numbers. Here rather than on the host because a host does not have the blob for a slot it did not store. Safe in the handler: what is playing is an expanded World and the morph target is another, so neither reads a blob except when loading one |
| 10 | — | aim the morph: search the target world for the position whose spectrum is nearest the one playing, and read it there. Refused with status 1 when no target is set |

### 0x65 GET_WORLDS
Request: `u8 start`, optional — an empty body means zero, which is what every
host sent before the list was paged. Reply: `u8 count` (worlds in all),
`u8 current` (the live one; **0xFF** means a user world, which has no index),
`u8 start`, `u8 sent`, then `sent` entries of `u8 kind`, `str name`, `str note`.

**The list is paged and a host must walk it.** Names and notes came to about
2.9 KB at twenty-one worlds against a 1024-byte body, so the module sends as
many whole entries as fit and the host asks again from `start + sent` until it
has `count` of them. A host that assumes one page gets the first seven worlds
and no error.

`kind` is `World::Kind` (`core/kyk_world.h`), not a two-valued flag: the
shipping worlds use 1 through 10. A host only needs `kind != Lattice` to know a
world has a formula it can fetch with 0x66; the specific value says which
formula, and 0x66's own reply carries a marker for the ones that are not an
eigenbasis.

The list is built into the firmware (`core/kyk_worlds.h`), so it needs no card.

### 0x67 PUT_WORLD
Request: `u32 total, u32 offset, bytes`. Reply: status, then `u32 accepted` —
how much of the blob the module now holds, which is `offset + len` on success.

A user world (`core/kyk_userworld.h`, about 6.5 KB for twenty-four nodes)
chunked host to module. Offsets go in order from zero and the module refuses
anything else: random access would have a host choosing the indices the module
writes at. Nothing loads until the last byte lands, so an interrupted transfer
costs the transfer and not the sound that is playing, and a blob that fails any
check leaves the live world untouched rather than half-written.

### 0x69 PUT_SLOT
Request: `u8 slot, u32 total, u32 offset, bytes`. Reply: status, `u32 accepted`.

A user world into one of `kSlotCount` (32) slots, chunked exactly as 0x67 is.
Separate from PUT_WORLD rather than a destination byte added to it, because
PUT_WORLD's request is eight fixed bytes followed by payload and there is no
room in it to say anything new without a rule for telling the two shapes apart.

Slots hold *blobs*, not expanded worlds: a `World` is 7 KB and needs DTCM or
AXI, a blob is 6.7 KB and lives in SDRAM, and parsing one into a playing world
is a memcpy into a LockField with no FFT and no lattice expansion. All 32 cost
216 KB of a 64 MB SDRAM.

### 0x6A SLOTS
Request: empty. Reply: status, `u8 count`, `u8 live` (0xFF none), `u8 target`
(0xFF none), then one entry per *filled* slot: `u8 index, u8 len, name`.

Only filled slots appear. `count` says how many exist, so a host fills the rest
in as empty — and an empty slot then arrives as *absent* rather than as a blank
name, which are different things. Same budget idiom as the other two lists: the
reply stops before it would overflow, because one that does not fit is dropped
silently and the host waits out its timeout.

### 0x68 CARD_WORLDS
Request: empty. Reply: status, `u8 count`, then `count` entries of
`u8 len, name` — the `.kykw` files in `/kyklophoria` on the card.

The count comes before the names so a host can size its list even when the body
ran out before the names did. Like the world list, the reply stops before it
would overflow rather than after: a reply that does not fit is dropped silently
and the host waits out its timeout with nothing to show for it. Use ACTION 6 to
re-scan the folder and ACTION 7 to load one by index.

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
The root object carries `"kyk":{"ext":5,"telemetry":96,"space":97,"cell":98,
"stats":99,"action":100,"worlds":101,"basis":102,"control":110}` from
`DescriptorRootJson()`. The
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
Any change to a *command map* bumps `"ext"`; the page feature-detects. `ext` is
**5**.

The telemetry frame is the exception and grows without a bump. Everything after
the Kepler block was added later — the bodies, the pager page, the Morph knob
and its target, the mute mask, the aim flag, the knob positions, the knob
values, the live world — and every one of them is optional on the way in and
length-guarded on the way out. A host reads what it knows by offset and ignores
the tail. That runs both ways and a host has to mean it: absent is not zero.
A page reading a missing `world` byte as 0xFF would conclude that every module
older than that field was playing a user world.
