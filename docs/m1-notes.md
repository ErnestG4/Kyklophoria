# M1 notes — hardware sound (2026-09-08, desktop side done; module numbers pending a flash)

What exists beyond M0: rotation (`core/kyk_rotate.h`), the stereo pair
(`core/kyk_stereo.h`), the telemetry encoder (`core/kyk_telemetry.h`), the
shared HostLink extension (`shell/common/kyk_ext.h`), `kykdesk --serve`
(HostLink on stdio, real time), the Alchemy Lab shell
(`shell/alchemy/main.cpp`, builds to `build/kyklophoria.bin`), the bridge
(`tools/bridge/bridge.mjs`) and the page (`web/`).

## Rotation and stereo, measured on the desktop
- Trig from the shared table with linear interpolation, then one Newton
  step to put (sin, cos) back on the unit circle: max error vs libm
  1.2e-6, and a 15-plane product (N=6) stays orthonormal to 1e-6. Without
  the step the chord error compounded to 1e-5 on the diagonal — visible in
  the test, inaudible, but free to fix.
- All-zero angles skip the matrix entirely: the identity path is
  bit-identical to the M0 mono engine over a 1000-block random wander, and
  after a rotation is set and cleared again (from the second block on —
  the first block still crossfades out of the rotated frame).
- A quarter turn in plane (i,j) is an exact axis permutation about pivot
  0, and within 1e-7 about pivot 0.5 (the pivot subtraction rounds).
- Stereo: δ = 0 gives L = R = mono bit for bit; δ = 0.02 turns gives a mean
  |L−R| of 0.007 at unit gain on the test cell; switching δ on from zero
  steps by < 0.05 because the idle voice adopts the live voice's frame and
  phase first. Payload comes from the centre position, so the CV out does
  not wobble with the spread.
- Cost: with δ ≠ 0 the block does two interpolations and two IFFTs; on the
  desktop 7.4 µs → ~14 µs per block at frame 1024. `render_div` applies to
  both voices.

## The module build
`cd shell/alchemy && make` against `../alchemy-sdk` v0.11 (+ the local boost
patch). 224 KB binary, BOOT_SRAM app:

| region | used | of |
|---|---|---|
| DTCM (.data/.bss + stack) | 79 KB | 128 KB (61 %) |
| AXI SRAM (code + .axi_bss: the engine, 38 KB) | 263 KB | 480 KB (53 %) |
| SDRAM (space blob 74 KB + SDK host buffers) | 136 KB | 64 MB |

DTCM is the constraint to watch as pages and lanes grow (Audiothurgist sits
at 65 %). The two 1 KB telemetry buffers and the SDK's CDC rings are there.

**Not yet measured (needs the module on the bench):** cycles per block
(GET_STATS reports last/max/avg against the 240 000-cycle budget), whether
`render_div` 1 fits with stereo on, and the desktop-vs-module CRC of the
golden scripts (the module runs the same portable IFFT, so the expectation
is bit-identical apart from `exp2f` in the shell's pitch path, which is
not in the core).

## What the shell does (M1 scope)
- J3 v/oct → `261.63 · 2^(V + coarse + fine/12)`; J4–J7 ±5 V → ±1 added to
  the Play-page offset pots; J8 CV out A = payload lane 4 × depth × 5 V;
  J9/J10 stereo out. Angles from the Rotate page pots (0..1 turn), spread
  and stereo plane from the Stereo page. Pot reads are the SDK's ISR-safe
  `Norm()/Value()`; CV reads are the calibrated `Volts()` smoothed at the
  1 kHz control poll (as Audiothurgist does).
- Telemetry snapshot in the callback only when the control loop asked
  (~5 µs); stats from DWT->CYCCNT.
- The boot space is generated at start (Harmonic family, seed 1). SD
  loading is M4; presets manage only pager + settings for now.

## Web and bridge
- `kykdesk --serve` runs the engine by the wall clock and speaks HostLink on
  stdio; the bridge is a dependency-free WebSocket/HTTP server (Python
  stdlib; a node twin beside it, neither touches npm) that pipes bytes to it. The page's transport layer is Audiothurgist's
  `hostlink.js` ported with a transport abstraction (Web Serial, WebSocket,
  stdio for the selftest).
- Verified headless (Playwright, chromium): 60 frames/s at 28 KB/s with
  spectrum and frame, L/R dots, fading trail, active cell, six angle
  gauges, frame and spectrum with the cutoff, stats strip. The stereo
  split is visible as two dots either side of the centre cross once the
  script opens the spread.
- `python3 tests/link_check.py`: the extension over stdio and through the
  Python bridge, part of `tests/run.sh`; `node web/selftest.mjs` is the
  optional node twin (33 checks).

## Surprises
- The SDK's HostLink wire layer (`frame.h`, `cobs.h`, `crc32.h`,
  `extension.h`) is header-only and hardware-free, so the desktop speaks
  the exact protocol with zero SDK .cpp files. The same `KykExt` object is
  compiled into both shells.
- `Space::BlobSize` had to become `constexpr` to size a static SDRAM
  array; `ControlLoop::OnFrame` takes a `void()` (time comes from
  `System::GetNow()`).
- The bridge's serve loop initially wrapped the script by rewinding the
  engine clock, which rendered a whole script pass instantly at each
  wrap; fixed by keeping the engine clock absolute and offsetting the
  script start.

## Open
- Bench numbers (above). Flash with `make program-dfu` from `shell/alchemy`
  or Hermetic's `/program`.
- The page's rings/panel mirror (spec §8 view 4) and attract mode are not
  started; the dev drawer only shows on the bridge.
- Name: **Kyklophoria** (Will, 2026-09-08); prefix `kyk`, files `kyk_*.h`, spaces `*.kyk`.
