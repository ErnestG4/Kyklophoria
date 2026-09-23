#!/usr/bin/env python3
"""velocity.py — one note struck at four velocities, today's runtime and the
synthesised exciter, and how bright each strike is.

    ~/fmexplorer/bin/python proto/velocity.py      (after threeway.py has fitted)

The fits are at the swing the recording was taken at (velocity 1); every
other velocity is the model's: for (a) a one-take world's burst low-passed
from 1 kHz to 13 kHz and scaled, a layered world's takes crossfaded; for
(b) the Hertzian felt's contact length, T ~ v^-3/7, and 1 / (1 + (f T)^2)
over every mode and band against the reference strike's. Writes
bench/threeway/<set>_<Note>_velocity_{a-recorded,b-synth}.wav and prints
each strike's level and spectral centroid over its first 200 ms."""
import json
import math
import os
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kp  # noqa: E402
import threeway as tw  # noqa: E402

VELS = (0.25, 0.5, 0.75, 1.0)
GAP = 1.5


def main():
    rows = []
    for setname, note in (('piano-iowa', 60), ('piano-vcsl', 60), ('wurli', 48)):
        cfg = tw.SETS[setname]
        W = kp.World(os.path.join(tw.CARD, cfg['world']))
        i = W.near(note)
        pj = os.path.join(tw.OUT, 'params', '%s_m0_p%d.json' % (setname, i))
        if not os.path.exists(pj):
            print('%s: no fit at %s' % (setname, note)); continue
        pb = np.array(json.load(open(pj))['pb'], np.float32)
        W.set(i, pb)
        t = [0.1 + GAP * k for k in range(len(VELS))]
        n = int((t[-1] + GAP) * kp.SR)
        for v, name in ((0, 'a-recorded'), (1, 'b-synth')):
            y = W.passage(t, [W.params[i]] * len(t), list(VELS), v, 4, n)
            sf.write(os.path.join(tw.OUT, '%s_%s_velocity_%s.wav' % (setname, tw.nn(W.params[i]), name)), (tw.GAIN * y).astype(np.float32), kp.SR, subtype='FLOAT')
            for tt, vel in zip(t, VELS):
                a = int(math.ceil(tt * kp.SR / 24.0)) * 24
                seg = y[a:a + int(0.2 * kp.SR)]
                P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
                f = np.fft.rfftfreq(len(seg), 1 / kp.SR)
                rows.append((setname, tw.nn(W.params[i]), name[0], vel, 10 * math.log10(np.mean(seg ** 2) + 1e-30), float((P * f).sum() / P.sum())))
    print('%-11s %-4s %s  %s   (level dB, centroid Hz over the first 200 ms of each strike)' % ('set', 'note', 'v', '  '.join('   vel %.2f     ' % v for v in VELS)))
    for s in sorted(set((r[0], r[1], r[2]) for r in rows)):
        rs = [r for r in rows if (r[0], r[1], r[2]) == s]
        print('%-11s %-4s %s  %s' % (s[0], s[1], s[2], '  '.join('%6.1f dB %5.0f Hz' % (r[4], r[5]) for r in rs)))


if __name__ == '__main__':
    main()
