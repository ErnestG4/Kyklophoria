#!/usr/bin/env bash
# Host test suite. Run from the repo root: tests/run.sh [--update]
#   1. core_check   — unit suite under ASan/UBSan (space, interp, fft, osc, engine)
#   2. rotate_check — rotation identity/permutation, the stereo pair, telemetry
#   3. morph_check  — morph linearity, level across a cell, band-limit continuity,
#                     and a diversity report on the generated space
#   4. cont_check   — every axis of every world must be continuous. Measures
#                    the same sweep at two step sizes: a smooth axis halves
#                    its largest step when the step halves, a cliff does not.
#   5. alias_check  — spec §7 aliasing sweep, fails above -80 dBFS
#   6. link_check   — python3 tests/link_check.py: the HostLink extension over stdio
#                     and through tools/bridge/bridge.py (stdlib only; KYK_NODE=1 adds
#                     the node selftest, which needs node but never npm)
#   5. golden       — kykdesk renders tests/scripts/*.txt and diffs against
#                     tests/golden/*.wav (tolerance 1e-6; --update rewrites)
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

echo "== rotate_check =="
$CXX $CORE_FLAGS $SAN tests/rotate_check.cpp -o "$OUT/rotate_check" || fail=1
"$OUT/rotate_check" || fail=1

echo "== morph_check =="
$CXX $CORE_FLAGS $SAN tests/morph_check.cpp -o "$OUT/morph_check" || fail=1
"$OUT/morph_check" || fail=1

echo "== cont_check =="
$CXX $CORE_FLAGS $SAN tests/cont_check.cpp -o "$OUT/cont_check" || fail=1
"$OUT/cont_check" > "$OUT/cont.txt" || { cat "$OUT/cont.txt"; fail=1; }
tail -1 "$OUT/cont.txt"

echo "== alias_check =="
$CXX $CORE_FLAGS -O2 tests/alias_check.cpp -o "$OUT/alias_check" || fail=1
"$OUT/alias_check" --csv "$OUT/alias.csv" || fail=1

echo "== golden =="
make -s host || fail=1
$CXX -std=gnu++17 -O2 -Icore tests/wavdiff.cpp -o "$OUT/wavdiff" || fail=1
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
    esac
    build/host/kykdesk $args --script "$script" --out "$OUT/$name.wav" --telemetry "$OUT/$name.csv" || { fail=1; continue; }
    if [ $update = 1 ] || [ ! -f "tests/golden/$name.wav" ]; then
        cp "$OUT/$name.wav" "tests/golden/$name.wav"
        echo "  wrote tests/golden/$name.wav"
    else
        "$OUT/wavdiff" "$OUT/$name.wav" "tests/golden/$name.wav" 1e-6 || fail=1
    fi
done

echo "== link_check (python, stdio + bridge.py) =="
python3 tests/link_check.py || fail=1
if command -v node >/dev/null 2>&1 && [ "${KYK_NODE:-0}" = 1 ]; then echo "== web selftest (node, optional) =="; node web/selftest.mjs || fail=1; fi

if [ $fail = 0 ]; then echo "ALL PASSED"; else echo "FAILURES"; fi
exit $fail
