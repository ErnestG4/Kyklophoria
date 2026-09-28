#!/usr/bin/env bash
# Host test suite. Run from the repo root: tests/run.sh [--update]
#   1. core_check   — unit suite under ASan/UBSan (space, interp, fft, osc, engine)
#   1b. resonate_check — the modal mode's runtime (kyk_resonate.h): a mode's
#                     frequency, decay and level; a fitted EP world's pickup
#                     barking with velocity at the level the fit found; a
#                     Wurlitzer world struck across its keyboard and
#                     interpolated between its points
#   1c. resonate_engine_check — the resonate path inside the engine: equal to
#                     the standalone voice bit for bit, a wavetable world's frame
#                     untouched by its existence, arriving equal to starting,
#                     a strike at a new pitch a new note, a retune ringing on
#   2. rotate_check — rotation identity/permutation, the stereo pair, telemetry
#   3. morph_check  — morph linearity, level across a cell, band-limit continuity,
#                     and a diversity report on the generated space
#   4. cont_check   — every axis of every world must be continuous. Measures
#                    the same sweep at two step sizes: a smooth axis halves
#                    its largest step when the step halves, a cliff does not.
#   5. tour_check   — a loop of worlds on a clock: who is in which buffer, what
#                     a step is allowed to overwrite, and the measurement that
#                     says why the ring has three buffers and not two.
#   6. alias_check  — spec §7 aliasing sweep, fails above -80 dBFS on the
#                     bright lattice cell and -60 on the worst cell there is.
#                     KYK_SLOW=1 adds the whole 22-world scan (95 s)
#   7. link_check   — python3 tests/link_check.py: the HostLink extension over stdio
#                     and through tools/bridge/bridge.py (stdlib only; KYK_NODE=1 adds
#                     the node selftest, which needs node but never npm)
#   8. golden       — kykdesk renders tests/scripts/*.txt and diffs against
#                     tests/golden/*.wav (tolerance 1e-6; --update rewrites).
#                     m4_sweep_note is the EP (a pickup world) under a two-
#                     octave glide, retriggers and a velocity run; m4_sweep_index
#                     the percussion row struck at every body and walked under
#                     a ring — the two scripts that found the coil's clicks
# Same params + same seed must give the same CRC on every machine of the same
# arch; the CRC is printed so it can be compared against the module (M1).
set -u
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/kyklophoria-tests
mkdir -p "$OUT"
fail=0
CXX=${CXX:-g++}
CORE_FLAGS="-std=gnu++17 -fno-exceptions -fno-rtti -ffp-contract=off -Wall -Wextra -Icore"
SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
update=0
[ "${1:-}" = "--update" ] && update=1

echo "== core_check =="
$CXX $CORE_FLAGS $SAN tests/core_check.cpp -o "$OUT/core_check" || fail=1
"$OUT/core_check" || fail=1

echo "== resonate_check =="
$CXX $CORE_FLAGS $SAN tests/resonate_check.cpp -o "$OUT/resonate_check" || fail=1
"$OUT/resonate_check" || fail=1

echo "== resonate_engine_check =="
$CXX $CORE_FLAGS $SAN tests/resonate_engine_check.cpp -o "$OUT/resonate_engine_check" || fail=1
"$OUT/resonate_engine_check" || fail=1

echo "== exciter_check =="
$CXX $CORE_FLAGS $SAN tests/exciter_check.cpp -o "$OUT/exciter_check" || fail=1
"$OUT/exciter_check" || fail=1

echo "== regions_check =="
$CXX $CORE_FLAGS $SAN -Ishell/common tests/regions_check.cpp -o "$OUT/regions_check" || fail=1
"$OUT/regions_check" || fail=1

echo "== rotate_check =="
$CXX $CORE_FLAGS $SAN tests/rotate_check.cpp -o "$OUT/rotate_check" || fail=1
"$OUT/rotate_check" || fail=1

echo "== morph_check =="
$CXX $CORE_FLAGS $SAN tests/morph_check.cpp -o "$OUT/morph_check" || fail=1
"$OUT/morph_check" || fail=1

echo "== switch_check =="
$CXX $CORE_FLAGS $SAN tests/switch_check.cpp -o "$OUT/switch_check" || fail=1
"$OUT/switch_check" > "$OUT/sw.txt" || { cat "$OUT/sw.txt"; fail=1; }
grep -q "same arrived at as started in" "$OUT/sw.txt" || { cat "$OUT/sw.txt"; fail=1; }

echo "== morphworld_check =="
$CXX $CORE_FLAGS -Ishell/common $SAN tests/morphworld_check.cpp -o "$OUT/morphworld_check" || fail=1
"$OUT/morphworld_check" > "$OUT/mw.txt" || { cat "$OUT/mw.txt"; fail=1; }
grep -q "all passed" "$OUT/mw.txt" || { cat "$OUT/mw.txt"; fail=1; }

echo "== user_check =="
$CXX $CORE_FLAGS $SAN tests/user_check.cpp -o "$OUT/user_check" || fail=1
"$OUT/user_check" > "$OUT/user.txt" || { cat "$OUT/user.txt"; fail=1; }
grep -q "all passed" "$OUT/user.txt" || { cat "$OUT/user.txt"; fail=1; }

echo "== kepler_check =="
$CXX $CORE_FLAGS $SAN tests/kepler_check.cpp -o "$OUT/kepler_check" || fail=1
"$OUT/kepler_check" > "$OUT/kep.txt" || { cat "$OUT/kep.txt"; fail=1; }
grep -q "all passed" "$OUT/kep.txt" || { cat "$OUT/kep.txt"; fail=1; }

echo "== cont_check =="
$CXX $CORE_FLAGS $SAN tests/cont_check.cpp -o "$OUT/cont_check" || fail=1
"$OUT/cont_check" > "$OUT/cont.txt" || { cat "$OUT/cont.txt"; fail=1; }
tail -1 "$OUT/cont.txt"

echo "== tour_check =="
$CXX $CORE_FLAGS $SAN tests/tour_check.cpp -o "$OUT/tour_check" || fail=1
"$OUT/tour_check" > "$OUT/tour.txt" || { cat "$OUT/tour.txt"; fail=1; }
tail -2 "$OUT/tour.txt"

echo "== alias_check =="
$CXX $CORE_FLAGS -O2 tests/alias_check.cpp -o "$OUT/alias_check" || fail=1
"$OUT/alias_check" --csv "$OUT/alias.csv" || fail=1
# The default cell is a regression guard on one world at one position, and it
# reads as a stronger claim than it is: a scan of every world at every corner
# and midpoint puts the honest worst at -64.6 dBFS, on Field. That takes 95 s,
# so the suite checks the cell the scan found worst and KYK_SLOW=1 runs the lot.
"$OUT/alias_check" --world 5 --pos 1 1 0.5 0 --limit -60 || fail=1
if [ "${KYK_SLOW:-0}" = 1 ]; then "$OUT/alias_check" --scan || fail=1; fi

echo "== golden =="
make -s host || fail=1
$CXX -std=gnu++17 -O2 -Icore tests/wavdiff.cpp -o "$OUT/wavdiff" || fail=1
$CXX -std=gnu++17 -O2 -Icore tests/earcheck.cpp -o "$OUT/earcheck" || fail=1
for script in tests/scripts/*.txt; do
    name=$(basename "$script" .txt)
    args="--gen --seed 1"
    case "$name" in
        m2_field*|m2_orbit*) args="--gen --seed 1 --family field --side 8";;
        m3_solid*|m3_kepler*|m3_couple*) args="--gen --seed 1 --world 1";;
        m3_fm*) args="--gen --seed 1 --world 9";;
        m3_vowel*) args="--gen --seed 1 --world 10";;
        m3_shapes_ring*) args="--gen --seed 1 --world 12";;
        m3_shapes*) args="--gen --seed 1 --world 11";;
        m3_lock*) args="--gen --seed 1 --world 13";;
        m3_unison*) args="--gen --seed 1 --world 14";;
        m3_plate*) args="--gen --seed 1 --world 15";;
        m3_bar*) args="--gen --seed 1 --world 16";;
        m3_drum*) args="--gen --seed 1 --world 17";;
        m3_saw*) args="--gen --seed 1 --world 18";;
        m3_pulse*) args="--gen --seed 1 --world 19";;
        m3_edge*) args="--gen --seed 1 --world 20";;
        m3_grit*) args="--gen --seed 1 --world 21";;
        m4_resonate*) args="--gen --seed 1 --resonate tests/data/piano.kykm";;
        m4_sweep_note*) args="--gen --seed 1 --resonate tests/data/tine.kykm";;
        m4_sweep_index*) args="--gen --seed 1 --resonate tests/data/bodies.kykm";;
    esac
    build/host/kykdesk $args --script "$script" --out "$OUT/$name.wav" --telemetry "$OUT/$name.csv" || { fail=1; continue; }
    # the resonate renders are graded by ear as well as by bit: a rail, a
    # click, silence — the check that found the coil's step and the ramp's
    case "$name" in m4_*) "$OUT/earcheck" "$OUT/$name.wav" || fail=1;; esac
    if [ $update = 1 ] || [ ! -f "tests/golden/$name.wav" ]; then
        cp "$OUT/$name.wav" "tests/golden/$name.wav"
        echo "  wrote tests/golden/$name.wav"
    else
        "$OUT/wavdiff" "$OUT/$name.wav" "tests/golden/$name.wav" 1e-6 || fail=1
    fi
done

# The wavetable firmware builds the core without the resonator
# (KYK_RESONATOR=0, shell/alchemy MODE=wavetable): every wavetable golden again,
# through that core, bit for bit — the firmware that ships is the core that is
# tested. And a resonator through it is silence: the flag really takes the
# resonator out, or this proves nothing.
echo "== golden, the wavetable firmware's core =="
for script in tests/scripts/m[0-3]_*.txt; do
    name=$(basename "$script" .txt)
    args="--gen --seed 1"
    case "$name" in
        m2_field*|m2_orbit*) args="--gen --seed 1 --family field --side 8";;
        m3_solid*|m3_kepler*|m3_couple*) args="--gen --seed 1 --world 1";;
        m3_fm*) args="--gen --seed 1 --world 9";;
        m3_vowel*) args="--gen --seed 1 --world 10";;
        m3_shapes_ring*) args="--gen --seed 1 --world 12";;
        m3_shapes*) args="--gen --seed 1 --world 11";;
        m3_lock*) args="--gen --seed 1 --world 13";;
        m3_unison*) args="--gen --seed 1 --world 14";;
        m3_plate*) args="--gen --seed 1 --world 15";;
        m3_bar*) args="--gen --seed 1 --world 16";;
        m3_drum*) args="--gen --seed 1 --world 17";;
        m3_saw*) args="--gen --seed 1 --world 18";;
        m3_pulse*) args="--gen --seed 1 --world 19";;
        m3_edge*) args="--gen --seed 1 --world 20";;
        m3_grit*) args="--gen --seed 1 --world 21";;
    esac
    build/host/kykdesk-wavetable $args --script "$script" --out "$OUT/wt_$name.wav" > /dev/null || { fail=1; continue; }
    "$OUT/wavdiff" "$OUT/wt_$name.wav" "tests/golden/$name.wav" 1e-6 > /dev/null || { echo "  FAIL $name differs through the wavetable core"; fail=1; continue; }
    echo "  ok   $name"
done
build/host/kykdesk-wavetable --gen --seed 1 --resonate tests/data/piano.kykm --script tests/scripts/m4_resonate.txt --out "$OUT/wt_m4.wav" > /dev/null 2>&1
if python3 - "$OUT/wt_m4.wav" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read()
i = b.find(b'data'); n = struct.unpack('<I', b[i + 4:i + 8])[0]; d = b[i + 8:i + 8 + n]
fmt = struct.unpack('<H', b[20:22])[0]
x = struct.unpack('<%df' % (len(d) // 4), d) if fmt == 3 else struct.unpack('<%dh' % (len(d) // 2), d)
sys.exit(0 if max(abs(v) for v in x) == 0 else 1)
PY
then echo "  ok   a resonator through the wavetable core is silence"
else echo "  FAIL a resonator sounds through the wavetable core: the resonator is not compiled out"; fail=1; fi

# Every fitted world carries burst audio — a window of the recording it was
# fitted from — so publishing one publishes that. tests/data/SOURCES.md is the
# record of which recording, and a fixture that is not in it is one nobody has
# said we may redistribute. The check is a reminder, not an audit: it cannot
# tell whether an entry is true, only that somebody had to write one.
echo "== fixture sources =="
# every TRACKED .kykm, not only the ones in tests/data. Tracked is the scope
# because tracked is what gets published, and a world dropped anywhere else
# in the tree would have gone out with it: ModalBake's first pass at this
# swept .wav and left seventeen worlds committed, because a world looks like
# numbers and is a recording. Without git (a tarball) it falls back to the
# whole tree, which over-reports rather than under-reports.
for f in $(git ls-files '*.kykm' 2>/dev/null || find . -name '*.kykm' -not -path './build/*'); do
    [ -e "$f" ] || continue
    if grep -q "\`$(basename "$f")\`" tests/data/SOURCES.md; then
        echo "  ok   $(basename "$f") is in SOURCES.md"
    else
        echo "  FAIL $f has no entry in tests/data/SOURCES.md — say where its audio came from"; fail=1
    fi
done

echo "== link_check (python, stdio + bridge.py) =="
python3 tests/link_check.py || fail=1
if command -v node >/dev/null 2>&1 && [ "${KYK_NODE:-0}" = 1 ]; then echo "== web selftest (node, optional) =="; node web/selftest.mjs || fail=1; fi
# pagecheck needs no module and no bridge, so it runs whenever node is present:
# it is the only thing here that executes index.html's drawing code at all, and
# the bug it was written for froze the page for a whole test session.
if command -v node >/dev/null 2>&1; then echo "== web pagecheck (node) =="; node web/pagecheck.mjs || fail=1; fi

if [ $fail = 0 ]; then echo "ALL PASSED"; else echo "FAILURES"; fi
exit $fail
