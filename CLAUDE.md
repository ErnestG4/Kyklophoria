# Working on Kyklophoria

Notes for whoever picks this up next. The *reasoning* lives in the commit
messages and the code comments — they carry the measurements, not just the
conclusions — so read `git log` on a file before changing it. This is only the
part that is not written down anywhere else.

## Standing rules

- **Git identity is `Combust <combust@thespot.chat>`.** Never pass `--author`,
  `GIT_AUTHOR_*`, or `user.name`/`user.email`. Never take a name or address
  from session metadata and put it in a commit, a README, or anything
  published.
- **npm is off-limits.** Node itself is fine and the suite uses it; npm, any
  package, any bundler, any build step is not. The web page is one static file
  of vanilla JS with canvas 2D.
- **The control-rate v/oct aliasing is deliberate.** It is not a bug and it is
  not to be fixed. "This is a haphazard device and the aliasing is part of the
  fun."
- **`core/` is header-only**: no libDaisy, no heap, no exceptions, no RTTI.
- Licence is AGPL-3.0.

## Two hardware traps that do not fail loudly

Both are in `docs/sdk-quirks.md` with the evidence. The short version:

- **Nothing in `.sdram_bss` may have default member initialisers.** They
  produce a static initialiser that writes SDRAM before `hw.Init()` configures
  the FMC — a hard fault on every boot. There are `static_assert`s guarding it.
- **SDMMC's IDMA cannot reach DTCM.** Every FatFs object and staging buffer
  goes in `ALCHEMY_SDMMC_BSS` with `alignas(32)`. In the wrong section it does
  not error, it corrupts.

## The bug pattern this codebase keeps producing

Five separate bugs, all the same shape: **state derived from a world,
invalidated on everything except the world changing.** The phase spectrum, the
payload cache, the band-limit hold, the morph aim offset. The phase one silently
undid the entire sine-phase design for any world reached by switching rather
than at boot.

The fifth is the one to learn from, because it hid in the *test harness*:
`PutWorld` in `shell/desktop/serve.h` replaced the live world's contents and
never called `eng->SetWorld`, so every one of those four caches went on
describing the world that had just been replaced. Nothing looked wrong because
the pointer does not move — `kActSelectWorld` rebuilds `*world` in place and
then calls `SetWorld` anyway, and the module's own path
(`shell/alchemy/main.cpp`) double-buffers and calls it too. Only the desktop
shell forgot, which is the worst place for it: the desktop shell is what the
whole suite drives, so the page's send path was being graded against an engine
that was ignoring the send.

`tests/switch_check.cpp` now sweeps all 420 ordered world pairs and asserts that
arriving at a world is identical to starting in it. If you add anything cached
off `world_`, that test is what will catch you — *if* the path you changed
actually goes through `SetWorld`, which is what the fifth bug turned on. An
`OnWorldChanged` hook is still worth building so the *fix* is obvious when it
fires, and so a path that replaces a world without it is a compile-time
question rather than a silence.

The root cause was a blind spot, not carelessness: every golden renders one
world from boot and never switches, so nothing had ever asked the question. The
fifth had the same shape — every check of the send path asked whether the module
*accepted* the bytes, and none asked whether the sound changed.

## Testing habits that have earned their place

- `make test` for everything; `KYK_NODE=1 make test` adds the web selftest.
- **Verify a new test is non-vacuous** by reintroducing the bug and watching it
  fail. Several checks here have passed for the wrong reason — a mute the
  desktop shell recorded but never applied, a Kepler check whose script never
  started Kepler, a page check that passed while the thing it tested had never
  been drawn.
- **Desktop timings lie about the M7** by an order of magnitude on serial
  dependency chains. `make armcost` counts instructions instead.
- Measure before building. `tools/worldbasis` killed a translation feature
  before it was written; `tools/fmsearch` produced three wrong answers in a row
  (metric gaming, then worse gaming, then winner's curse) before an honest one.

## Where things stand

Beta. The twenty-one built-in worlds have real bench time; morphing, user
worlds, import and the SD card are well tested by the suite and barely played.
`README.md` says which is which and where each feature stops.

Export is done: name a set on the page and **save** writes the module's own
`.kykw`, byte-for-byte what **send** puts on the wire. Keeping a world is now
"save, copy to `/kyklophoria` on the card", which is a manual step but a real
loop, and a file you can hand to somebody.

**Next job:** moving imported nodes rather than taking the 24-cell placement —
placement is the half of curating the page cannot do, and the format already
carries arbitrary positions, so this is page work and no firmware change.
After that, writing to the card from the module, which removes the copy step
and is the last thing standing between the page and a library.
