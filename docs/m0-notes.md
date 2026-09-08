# M0 notes — skeleton (2026-09-08)

What exists: `core/` (types, space view, interpolation, IFFT, oscillator,
engine, generator), `shell/desktop/kykdesk` (script → WAV, telemetry CSV),
`tools/kykspace` (gen/info), `tests/run.sh` (unit under ASan/UBSan, aliasing
sweep, golden WAVs). `make host && make test`.

## Measurements

### Aliasing sweep (`tests/alias_check`, spec §7)
Brightest cell (saw, no tilt, formant at the top, all harmonics), f0 from
55 Hz over five octaves in third-octaves, 2 s each, 65536-point 4-term
Blackman-Harris analysis. "Worst" = loudest bin that is not within ±6 bins
of a harmonic of f0, in dBFS. All at K=64 unless noted.

| frame | read | render every | worst non-harmonic bin | verdict (−80 dBFS bar) |
|---|---|---|---|---|
| 256 (K=32) | cubic | 1 block | −62.6 dBFS | fail |
| 512 | cubic | 1 block | −68.3 dBFS | fail |
| 512 | cubic, 8-bin raised-cosine rolloff | 1 block | −73.2 dBFS | fail |
| 1024 | linear | 1 block | −71.2 dBFS | fail |
| **1024** | **cubic** | **1 block** | **−88.1 dBFS** | **pass** |
| 1024 | cubic | 4 blocks | −88.1 dBFS | pass |
| 2048 | cubic | 1 block | −92.2 dBFS | pass |

The failures are not content above Nyquist (the spectrum is truncated
before the IFFT; a kcut-0 frame is exactly silent) — they are the frame
read interpolator's images. At low pitch all 64 harmonics are live and the
top one has only frame/64 table samples per cycle: 8 at 512, 16 at 1024.
Cubic Hermite at 16 samples/cycle puts the top harmonic's image around
−52 dB below it; the harmonic itself sits near −36 dBFS in a saw, hence
the −88. The worst bins in the passing configuration all sit below 350 Hz
f0, i.e. where K is the limiter, not Nyquist. Above 440 Hz kcut < K and the
frame is oversampled for its content; everything drops to −92…−119 dBFS.

Consequences: **frame 1024, K 64, cubic reads is the baseline** (spec
§9.2). Raising K to 128 for fuller bass would push the top harmonic back to
8 samples/cycle and need a 2048 frame (or a better interpolator). Rendering
every 4th block costs nothing in aliasing — the crossfade is per sample —
so the IFFT rate is free to trade against CPU.

### Cost (desktop, single thread, x86, scalar — a ratio guide only)

| frame | whole block (interp + IFFT + 24 reads) | IFFT alone |
|---|---|---|
| 512 | 3.7 µs | 3.0 µs |
| 1024 | 7.4 µs | 6.5 µs |
| 2048 | 14.8 µs | 15.3 µs |

The IFFT is ~90 % of the block. The portable radix-2 does a full complex
1024-point transform on the positive bins; it is deliberately simple. On the
M7 at 480 MHz expect roughly 10–20× the x86 figure for this code, so
75–150 µs of the 500 µs block at 1024 rendered every block — inside the
budget but not the 30 % headroom with the rest of the signal path on top.
Two ways down, both M1 measurements: `render_div` 2–4 (frame every 1–2 ms,
still a per-sample crossfade) and CMSIS `arm_rfft_fast_f32` (half-size
real transform, assembly-tuned; typically 15–25 µs for 1024 on an M7).
The interpolation itself (16 corners × 72 floats) is under a microsecond.

### Determinism
Two runs of the golden scripts produce byte-identical WAVs (crc32 printed
by kykdesk: `m0_static` 21a74da9, `m0_sweep` 362c0224 with this compiler).
Two engines fed the same random wander of 2000 blocks are bit-identical.
Rules: every trig value comes from `core/kyk_tables.h` (a generated float
table, hex literals, 2048 points); harmonic phases are a seeded xorshift
quantised to that table; the phase accumulator is Q32; `-ffp-contract=off`
everywhere; no libm in the core (the generator's `pow` is a series, used
only offline). What is *not* guaranteed: identical bits between the desktop
and the module once the ARM IFFT is CMSIS (different butterfly order and
twiddles) — M1 measures that drift with the same scripts and reports it.

### Levels
Unit-RMS spectra with the +12 dB formant bump peak at ~2.4 (engine gain 0.5
→ 1.2 in the sweep golden). Fine in float; the hardware shell needs a
final gain/limiter stage anyway (drive stage, M3).

## Surprises
- **Block size is 24 samples.** Per-block frame rendering means 2000
  IFFTs per second; the "IFFT per block" of the brief was written with a
  bigger block in mind. `render_div` exists for this.
- **No encoder** on the Alchemy Lab. The brief's "multiplex via the
  encoder/pages" becomes pages via B1 and Settings via B2+B3 (docs/io-map.md).
- **HostLink cannot push.** Telemetry is a polled reply; that is fine at
  30 Hz (Audiothurgist's scope proves it) but the "stream" wording in §8 is
  really "poll a snapshot".
- `Fract()` and the clamp are libm-free so the fold is identical on both
  targets; worth keeping that discipline as rotation lands (Givens needs
  cos/sin of the angles → the table again, or a deterministic
  polynomial).
- ASan/UBSan clean on the first run; the only failure was the test's own
  off-by-epsilon on the Nyquist guard (fixed: a harmonic landing exactly on
  Nyquist is excluded, nothing else is).

## Decisions (spec §9), same evening
1–4 confirmed by Will, with stereo output added: J9/J10 are L/R, CV out A
moves to J8, and the stereo mechanism is an angular spread ±δ in a rotation
plane (docs/io-map.md *Stereo*). That doubles the per-render cost, which
makes the M1 cycle count the gate for `render_div`. 5 (the name) is open.
