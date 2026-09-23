#!/usr/bin/env python3
"""labelcheck.py <gen dir> [--tol 0.7] — every organised note against its own pitch, BEFORE a fit

The fitter seeds each note's fundamental from its label and keeps it, so a
wrong label is not a naming slip that a later relabel fixes: VCSL names its
octaves with middle C as C3 for most instruments, organise.py read them as
C4, and the fits of fourteen sets each carried a mode an octave under the
note they were told was there (the Knight upright's A4 had one at 216 Hz,
-12 dB, among its four loudest). pitchcheck.py grades a fitted set after the
fact; this runs first. Each <dyn>/<note>.wav is pitched with pitchman's
detector (as pitchcheck does) and one whose sound is more than --tol
semitones from its name is moved into <gen dir>-rejected/<dyn>/ and listed,
so the set is fitted only from notes that are what they say. --tol 0.7 and
not 0.5: a kalimba is not tuned to twelve equal steps.

    ~/fmexplorer/bin/python tools/labelcheck.py out/gen/upright-knight-o
"""
import argparse, os, re, shutil, sys
import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pitchman import pitch

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def midi_of(name):
    m = re.match(r'([A-G])(#?)(-?\d)', name)
    return NOTE[m.group(1)] + (1 if m.group(2) else 0) + 12 * (int(m.group(3)) + 1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gendir')
    ap.add_argument('--tol', type=float, default=0.7)
    a = ap.parse_args()
    d = a.gendir.rstrip('/')
    kept = moved = 0
    for dyn in sorted(os.listdir(d)):
        dd = os.path.join(d, dyn)
        if not os.path.isdir(dd):
            continue
        for f in sorted(os.listdir(dd)):
            lab = midi_of(f)
            if lab is None or not f.endswith('.wav'):
                continue
            x, sr = sf.read(os.path.join(dd, f), always_2d=True)
            hz = pitch(x.mean(axis=1), sr, 21, 108)
            got = 69 + 12 * np.log2(hz / 440.0)
            if abs(got - lab) > a.tol:
                rj = os.path.join(d + '-rejected', dyn)
                os.makedirs(rj, exist_ok=True)
                shutil.move(os.path.join(dd, f), os.path.join(rj, f))
                g = int(round(got))
                print('  %-24s named %-4s sounds %7.1f Hz = %-4s (%+.1f st): moved out' % (
                    dyn + '/' + f, NAMES[lab % 12] + str(lab // 12 - 1), hz, NAMES[g % 12] + str(g // 12 - 1), got - lab))
                moved += 1
            else:
                kept += 1
    print('%s: %d notes kept, %d moved out' % (d, kept, moved))


if __name__ == '__main__':
    main()
