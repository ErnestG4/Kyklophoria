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

## The play view is a readout

Every mark in it comes from the module. The build tab's set is a draft, and the
two are the same world only between a successful send and the next edit — so
the play view draws the page's record of what it *sent* (`held`), never
`imported`. The record is only as good as the rules for dropping it, so there
is one function that drops it and five callers. If you add a sixth way for the
module to end up holding something else, it goes through `heldLost`.

The page cannot ask what the module is *holding* — the world list reports "not
one of the built-ins" and nothing more, and `GET_BASIS` gives a lock world's
count and sigma with no positions — but it does know *which* world, because
every telemetry frame now carries the live index (0xFF for a user world). So a
world changed from the module's own panel drops the record within a frame, and
the world bar corrects itself instead of naming whatever the page last asked
for. Absent is not 0xFF: firmware older than the field sends nothing, and
reading that as "a user world" would be a lie about every module that predates
it.

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

## A long-standing annoyance, and what it was

- **Clicking twice to change world.** The action only queues the switch; the
  module performs it on its control loop and a tabulated world is expanded
  first, so reading the world list straight back returned the old value. Fixed
  by waiting for the module to agree — now by watching the live world index in
  telemetry rather than asking for the list every 60 ms, which cost a round
  trip per attempt at exactly the moment the module was busy expanding.

## Testing habits that have earned their place

- `make test` for everything; `KYK_NODE=1 make test` adds the web selftest.
- **Verify a new test is non-vacuous** by reintroducing the bug and watching it
  fail. Several checks here have passed for the wrong reason — a mute the
  desktop shell recorded but never applied, a Kepler check whose script never
  started Kepler, a page check that passed while the thing it tested had never
  been drawn.
- **Desktop timings lie about the M7** by an order of magnitude on serial
  dependency chains. `make armcost` counts instructions instead.
- **A race is not a delay, and reading twice is not waiting.** A world change
  is applied on the request but only reaches telemetry on the next *render*,
  and a render is not every block. Two attempts at this failed intermittently —
  "read twice" about one run in four, "read until two agree" about one in five,
  because a throttled render looks exactly like a settled picture from outside.
  What works: drive a few blocks, then require three consecutive identical
  frames. Idle reads are bit-identical, which is what makes agreement mean
  anything at all.
- **Waiting longer can make a check vacuous.** Settling generously hid the very
  bug the check was written for: without `SetWorld` the world's contents are
  still replaced, so something else eventually forces a render and the spectrum
  changes anyway — late, and at the wrong phase convention. The fix was to
  assert something the bug breaks *permanently*: arriving at a sent world from
  three different worlds must give byte-identical frames. 0 with the fix, 173
  of 255 without, every run either way. When a check needs a timing window to
  be right, it is usually asking the wrong question.
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

Building a world is its own view now — the **build** tab — because placement
had been bolted onto the space pane, which is a readout of the module and not
somewhere to edit something the module is not holding. It carries the set as
drawn cards, two placement squares (the pair on screen and its complement, so
four dimensions are reachable without switching), and an inspector showing what
you imported against what the engine will actually render from it.

**Next job:** writing to the card from the module, which removes the copy step
and is the last thing between the page and a library. It is the first thing
here that *writes* to the SD card, so re-read `docs/sdk-quirks.md` on SDMMC
before starting: the IDMA cannot reach DTCM, and in the wrong section it does
not error, it corrupts.

One thing the placement work turned up that is worth keeping: `web/pagecheck.mjs`
now records any draw call given a coordinate that is not a number. A stub
canvas swallows `arc(NaN, …)` and so does a real one — it draws nothing, which
looks exactly like a mark that is off screen — so this is a class of bug whose
only symptom is something quietly missing from the picture. It caught a node
being drawn on an axis it does not have the first time it was armed.
