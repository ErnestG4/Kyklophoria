# Kyklophoria

An N-dimensional morph oscillator for the Hermetic Modular Alchemy Lab.
A wavetable whose table is a 4-D (up to 6-D) space of spectra, indexed by
position CVs that pass through an N-D rotation first; a WebSerial page shows
where you are in the space. **Status: M1 — the firmware builds and the
page runs against the desktop engine; the module has not been on the bench
yet.** Design: `docs/spec.md`; milestone notes in `docs/m*-notes.md`.

```sh
make host                                   # build/host/kykdesk, build/host/kykspace
make test                                   # unit + aliasing + golden WAVs + link check (python)
build/host/kykspace gen demo.kyk --seed 7    # a 4-D lattice, 256 cells
build/host/kykdesk --space demo.kyk --script tests/scripts/m0_sweep.txt --out sweep.wav --telemetry sweep.csv
```

- `core/` — header-only engine, no hardware, no allocation (`kyk_engine.h` is the entry)
- `shell/desktop/` — the desktop main(): param script → WAV
- `shell/alchemy/` — the Alchemy Lab main() + Makefile (builds against `../alchemy-sdk`)
- `shell/common/` — the HostLink extension both shells compile (`docs/hostlink.md`)
- `tools/bridge/` — WebSocket/HTTP bridge to `kykdesk --serve` (`bridge.py`, stdlib only; a node twin beside it)
- `tools/kykspace/` — generate / inspect space files (`docs/space-format.md`)
- `tests/` — `run.sh`, unit suite, aliasing sweep, golden renders
- `docs/` — spec, io-map, space format, hostlink, per-milestone notes
- `web/` — the instrument view: one page, canvas, Web Serial or the bridge (`web/README.md`)

Siblings expected next to this repo, as for Audiothurgist: `alchemy-sdk`,
`DaisySP`, `Audiothurgist` (shared shell pieces).

License: to be confirmed (Audiothurgist is AGPL-3.0).
