# Quirks found in libDaisy and the Alchemy SDK

Things that cost us time on Kyklophoria, written down so they cost it once. The
480 MHz clock patch is documented elsewhere and is not repeated here.

Each entry says what happens, how we know, and what to do. Line numbers are
against alchemy-sdk as of 2026-09-10. Two of these hard-fault on boot, two are
silent, and the rest are design properties with consequences rather than bugs.

---

## Boot: nothing in `.sdram_bss` may have default member initialisers

**Symptom.** The module flashes, then hard-faults before `main` and drops to the
bootloader. Every time, deterministically.

**Cause.** A default member initialiser makes a type non-trivially
constructible, so the compiler emits a static initialiser for it in
`.init_array`. Those run before `hw.Init()` has configured the FMC, so the
store lands on unconfigured SDRAM.

**Evidence.** Visible in the ELF as a literal load of the SDRAM base followed by
a store through it:

```
ldr r0, [pc, #564]   @ 0xc0000000
str r7, [r0, #8]
```

**What to do.** Guard every SDRAM declaration:

```cpp
static_assert(std::is_trivially_default_constructible<solids::VertexTable>::value,
              "SDRAM types must not have default member initialisers");
```

We keep two of these in `shell/alchemy/main.cpp`. Verify such an assert is
non-vacuous by re-adding an initialiser and watching the build fail — ours was
checked that way.

**Worth knowing:** the struct that bit us had `n = 4, k = 64, sigma = 0.20f`,
which looks entirely innocent.

---

## Boot: `Pager::SetStored` dereferences `phys` with no null check

**Symptom.** Deterministic hard fault on every boot. This is what our 0.3.0
shipped with.

**Cause.** `pager.cpp:521` bounds-checks `page` and `pot`, then calls
`InitCatch(s, phys[pot])` unconditionally. `LockPage` (`:529`) does the same.
The parameter has no default, but passing `nullptr` compiles cleanly.

On this part a null read lands at `0x8`, inside ITCM. Firmware that never writes
a byte of ITCM leaves it uninitialised ECC RAM, and reading it raises a
double-bit ECC error.

**What to do.** Always pass a real array:

```cpp
float phys[kNumPots];
for (uint8_t i = 0; i < kNumPots; i++) phys[i] = hw.pots[i].Value();
pager.SetStored(page, pot, value, phys);
```

**Suggested fix upstream:** an early `if (!phys) return;`, or make the parameter
non-optional in a way the compiler can enforce.

---

## Descriptor: `Host::Pages()` must be called or no panel metadata is published

**Symptom.** A web page can see the module's jacks but has no idea what any knob
does, even though every knob declares `.Ident()`, `.Name()` and `.Unit()`.

**Cause.** Registering pages with `loop.Use(page_a).Use(page_b)` makes them
work. It does **not** put them in the descriptor. That needs a separate,
easily-missed call:

```cpp
host.Pages(page_play, page_rotate, page_stereo, page_orbit, page_kepler, page_couple);
```

We ran for months assuming the page was ignoring metadata that was never sent.

**Cost.** About 7.6 KB of SRAM for six pages of six knobs. Cap is
`kMaxPageRefs = 8` (`host.h:205`).

---

## HostLink: `Send` drops responses silently

**Symptom.** The host hangs for its full timeout — two seconds in our client —
with no error anywhere.

**Cause.** `host_link.cpp:474-479`, and the comments say it outright:

```cpp
const size_t n = w.Encode(wire_);
if (n == 0u) return;                       /* response overflow — drop */
if (transport_.WriteSpace() < n) return;   /* queue full — host times out */
```

An over-large reply and a momentarily full TX ring are both indistinguishable
from a dead module.

**What to do.** Keep every reply under `kMaxBody`, and page anything that can
grow. Our `GET_WORLDS` had to be paged once the list reached eighteen entries;
the hang is documented at `shell/common/kyk_ext.h:149`.

**Suggested fix upstream:** reply `BUSY` or `TOO_LARGE`. The protocol already
defines `BUSY` as transient, so a host could retry immediately instead of
waiting out a timeout.

---

## HostLink: two avoidable milliseconds per round trip

Neither is a bug; both are easy to fix and worth about half the latency.

**`Pump()` runs first in `Poll()`** (`host_link.cpp:72`). A response queued
during poll N therefore does not go out until poll N+1 — a full extra
millisecond on every request. Moving the call to the end of `Poll()` is one
line.

**`kChunk` is 256 bytes** (`cdc_transport.h:95`) and `Pump()` submits one chunk
per call. A full-body reply is ~526 bytes on the wire, so it takes three polls
— three milliseconds — just to transmit. Raising `kChunk` to 528 costs 544 bytes
of RAM.

Practical ceiling as it stands: 256 B/poll ÷ 1 ms = ~256 KB/s, and an RTT floor
of roughly 2 ms small / 4 ms full-body.

---

## HostLink: there is no partial write to live state

**This is the big one, and it is a design property rather than a defect.**

To change one knob a host must rewrite the entire managed blob:
`BLOB_BEGIN` → N × `BLOB_DATA` → `BLOB_COMMIT`. Firmware rejects anything
shorter — `host_link.cpp:286-292` requires `total == presets_.LiveSize()` and
answers `SCHEMA_MISMATCH` otherwise.

With param locks that blob is around 7.2 KB, so one knob move is roughly
**seventeen stop-and-wait round trips**, and reading values back costs sixteen
more. Since only one request may be in flight, a drag saturates the link
completely. There is no `SET_FIELD`, no component-scoped read or write, and no
subscribe.

**What we did instead.** A HostLink extension (`shell/common/kyk_ext.h`,
commands 0x60–0x6F) with one command that returns everything we draw in a single
reply. One request per frame at 60 Hz, and the page stays responsive. If you are
writing a module and the panel feels sluggish from the web side, this is almost
certainly why, and an extension is the sanctioned escape hatch.

**Suggested fix upstream:** a component-scoped write. Components are already
contiguous with `off`/`size` in the descriptor, so `SET_COMPONENT idx, bytes`
would take seventeen round trips to one. New commands answer `UNSUPPORTED` on
old firmware, so it would not break existing hosts.

---

## SDMMC's IDMA cannot reach DTCM

**Symptom.** File operations that do not fail — they corrupt. A `DIR` or
`FILINFO` in ordinary `.bss` gives garbage filenames or silent truncation
rather than an error code.

**Cause.** SDMMC1's internal DMA cannot address DTCM, which is where plain
`.bss` lands on this part. The SDK states this in
`alchemy/storage/sd_card.h` and Audiothurgist found it independently before
that.

**What to do.** Every FatFs object and every staging buffer goes in
`ALCHEMY_SDMMC_BSS`, which is AXI SRAM:

```cpp
ALCHEMY_SDMMC_BSS alignas(32) static DIR     gDir;
ALCHEMY_SDMMC_BSS alignas(32) static FILINFO gFno;
ALCHEMY_SDMMC_BSS alignas(32) static FIL     gFil;
ALCHEMY_SDMMC_BSS alignas(32) static uint8_t gStage[N];
```

The `alignas(32)` matters too — cache-line alignment for the D-cache
maintenance around the transfer.

**Also:** card work is slow and must never happen under the audio callback.
Both modules defer it to the main loop, and hold `SdCard::BusyGuard` across
any filesystem operation so the audio side can see `Busy()`.

---

## Smaller sharp edges

**`Crc32` is bitwise** (`crc32.h:20-33`) — eight iterations per byte, roughly 40
instructions per byte. CRCing a 7.5 KB blob is about 300k instructions, ~0.63 ms
at 480 MHz, inside a 1 ms poll. A 64-byte nibble table would be about five times
faster with identical output.

**Selector knobs are zero-based.** `.Selector(8).Value()` returns 0..7, so a
"1 to 8 bodies" control needs `(int)k.Value() + 1`. Obvious once seen, quiet
until then.

**Desktop and module descriptors diverge.** Ours is hand-written in
`shell/desktop/serve.h` and carries only the io-map, while the module's is built
by the SDK. Anything on the page that reads descriptor content cannot be tested
against the desktop bridge, so it needs a fixture instead — ours lives in
`web/pagecheck.mjs`.

---

## Method note

Desktop timings understate the M7 by an order of magnitude on serial dependency
chains. Our wavefolder measured 2.7 µs on x86 and ran at roughly 150 µs on the
module against a 500 µs block budget. Count instructions for the target instead;
`make armcost` does that here.
