#!/usr/bin/env bash
# renderpack — a pack of wavs to listen to before flashing anything.
#
# The test suite proves the instrument still does what it did. It cannot say
# whether the thing sounds good, and every measurement in tools/kykworlds is a
# proxy for that at best. So: one slow diagonal through every world, and one
# render of each way the position can move on its own, all at the same pitch
# and level so they can be played back to back and compared by ear.
#
#   tools/renderpack.sh [outdir]      (default build/renders)
set -e
cd "$(dirname "$0")/.."
OUT=${1:-build/renders}
DESK=build/host/kykdesk
[ -x "$DESK" ] || make -s host
mkdir -p "$OUT" "$OUT/.scripts"

# name:index, in registry order (core/kyk_worlds.h)
WORLDS="braids:0 cell24:1 cell16:2 tesseract:3 stack:4 field:5 fieldII:6 torus:7 harmonic:8 fm:9 vowel:10 shapes:11 shapesring:12 lock:13 unison:14 plate:15 bar:16 drum:17 saw:18 pulse:19 edge:20"

# A slow diagonal from one corner of the cube to the opposite one, at a pitch
# low enough that most of the K harmonics are below Nyquist and the spectrum
# is actually heard rather than filtered off.
cat > "$OUT/.scripts/sweep.txt" <<'EOF'
0.0 f0 82.4
0.0 pos 0.03 0.97 0.03 0.97
0.0 glide pos 0.97 0.03 0.97 0.03 10.0
10.0 glide pos 0.03 0.97 0.97 0.03 6.0
16.5 dur
EOF

echo "== worlds: a 16 s diagonal through each =="
for w in $WORLDS; do
    name=${w%%:*}; idx=${w##*:}
    args="--gen --seed 1 --world $idx"
    case $name in field|fieldII|torus) args="$args --side 8";; esac
    $DESK $args --script "$OUT/.scripts/sweep.txt" --out "$OUT/world-$name.wav" >/dev/null
    echo "  $OUT/world-$name.wav"
done

# ── the ways the position moves on its own ──────────────────────────────
# All on the torus world, because it is the most isotropic space we have and
# so the least likely to flatter or flatten any particular motion, and
# because it is the one where gravity wraps.
#
# None of them start at the centre of the cube, and that is not decoration.
# The rotation pivots about 0.5 on every axis, so a control frame sitting
# exactly on the pivot is a fixed point of every rotation there is: the angles
# turn, the orbit accumulates, and the position does not move at all. The
# first draft of this pack put all five renders at dead centre and produced
# five files that were bit-identical to the uncoupled one. Gravity is the
# exception, since it adds an offset rather than turning the frame, which is
# why the Kepler renders did move.
mk() { cat > "$OUT/.scripts/$1.txt"; }

mk orbit <<'EOF'
# Ptolemaic: three planes turning at unrelated rates. The path never closes.
0.0 f0 82.4
0.0 pos 0.72 0.38 0.55 0.44
0.0 rate 0 0.041
0.0 rate 1 0.067
0.0 rate 3 0.113
0.0 couple 0
30.0 dur
EOF

mk orbit-coupled <<'EOF'
# The same three rates, but coupled. Ten seconds in they start pulling each
# other toward simple ratios and the figure closes; at twenty the reach drops
# to two and only the octave and the unison are left to settle on.
0.0 f0 82.4
0.0 pos 0.72 0.38 0.55 0.44
0.0 rate 0 0.041
0.0 rate 1 0.067
0.0 rate 3 0.113
0.0 couple 0
10.0 couple 0.30 5
20.0 couple 0.30 2
30.0 dur
EOF

mk kepler-mild <<'EOF'
# A near-circular fall. Even speed, so the timbre glides.
0.0 f0 82.4
0.0 pos 0.72 0.38 0.55 0.44
0.0 kepler 0.35 0.15 0
30.0 dur
EOF

mk kepler-eccentric <<'EOF'
# The same orbit wound out to high eccentricity: the body rushes through
# periapsis and lingers at apoapsis, so the timbre dwells unevenly. That
# second-law unevenness is the whole reason this mode exists alongside the
# other one.
0.0 f0 82.4
0.0 pos 0.72 0.38 0.55 0.44
0.0 kepler 0.35 1.0 0
30.0 dur
EOF

mk both <<'EOF'
# Gravity and coupled rotation at once, which is the state the instrument is
# actually likely to be played in.
0.0 f0 82.4
0.0 pos 0.72 0.38 0.55 0.44
0.0 rate 0 0.037
0.0 rate 2 0.061
0.0 couple 0.30 3
0.0 kepler 0.30 0.8 1
0.0 spread 0.035
30.0 dur
EOF

echo "== motion: 30 s each, on the Torus world =="
for s in orbit orbit-coupled kepler-mild kepler-eccentric both; do
    $DESK --gen --seed 1 --world 7 --side 8 --stereo \
          --script "$OUT/.scripts/$s.txt" --out "$OUT/motion-$s.wav" >/dev/null
    echo "  $OUT/motion-$s.wav"
done

echo
echo "wrote $(ls "$OUT"/*.wav | wc -l) files to $OUT"
