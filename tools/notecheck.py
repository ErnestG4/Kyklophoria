#!/usr/bin/env python3
"""notecheck.py <world.kykm> [lo hi]   — every semitone through the engine

Combust, on the module: "wurli's notes are OFF badly. A2 to B2 and C2 in
particular is an octave wrong." The records were the cause (a C2 labelled
C3; two bass fits without their fundamental), and this is the check that
would have said so before the card did: every semitone from lo to hi
struck once in isolation through Kyklophoria's own engine (kykdesk
--resonate, the same runtime the module runs), and its pitch read back by
tools/pitchman.py's detector. A note that reads more than half a semitone
from what was asked is listed; the exit code says whether any did.

    ~/fmexplorer/bin/python tools/notecheck.py out/worlds/wurli.kykm 36 96
"""
import os, subprocess, sys, tempfile
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(__file__))
from pitchman import pitch, NAMES

KYK = os.path.join(os.path.dirname(__file__), '..', '..', 'Kyklophoria')


def main():
    world = sys.argv[1]
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 36
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 96
    kykdesk = os.path.join(KYK, 'build', 'host', 'kykdesk')
    if not os.path.exists(kykdesk):
        print('no %s: make -C %s build/host/kykdesk' % (kykdesk, KYK)); return 2
    bad = []
    with tempfile.TemporaryDirectory() as d:
        for m in range(lo, hi + 1):
            f0 = 440.0 * 2 ** ((m - 69) / 12)
            sc = os.path.join(d, 'n.txt'); wav = os.path.join(d, 'n.wav')
            open(sc, 'w').write('0.8 dur\n0.0 f0 %.4f\n0.05 strike 0.9\n' % f0)
            subprocess.run([kykdesk, '--gen', '--seed', '1', '--resonate', world, '--script', sc, '--out', wav],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            x, sr = sf.read(wav, always_2d=True); x = x.mean(axis=1)
            f = pitch(x[int(0.05 * sr):], sr, 21, 108)
            got = 69 + 12 * np.log2(f / 440)
            name = lambda v: NAMES[int(round(v)) % 12] + str(int(round(v)) // 12 - 1)
            if abs(got - m) > 0.5:
                bad.append('  %-4s asked, %-4s heard (%+.1f st, %.1f Hz)' % (name(m), name(got), got - m, f))
    print('%s: %d semitones %s..%s, %d off' % (world, hi - lo + 1, NAMES[lo % 12] + str(lo // 12 - 1), NAMES[hi % 12] + str(hi // 12 - 1), len(bad)))
    for b in bad: print(b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
