#!/usr/bin/env python3
"""modalfit_check — fit a strike whose modes are known, and compare.

    modalfit_check.py out/mmr/tine00.mmr [--position 11]

Renders the corpus model as decaying sines with its own Rayleigh damping (the
same equations tools/render uses), writes it to a WAV, runs modalfit on it, and
reports frequency and decay errors mode by mode. A fitter that cannot recover
a synthetic strike has no business near a recording.
"""
import math
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

path = sys.argv[1]
pos = int(sys.argv[2]) if len(sys.argv) > 2 else 11
modes = []
for line in open(path):
    if line.startswith('mode '):
        w = line.split()
        hz, zeta, g = float(w[3]), float(w[5]), float(w[7 + pos])
        if hz > 40 and hz < 20000:
            modes.append((hz, zeta, g * g / (2 * math.pi * hz)))
sr = 48000
t = np.arange(int(2.5 * sr)) / sr
y = np.zeros_like(t)
truth = []
for hz, zeta, amp in modes:
    w = 2 * math.pi * hz
    rate = zeta * w
    y += amp * np.exp(-rate * t) * np.sin(w * t)
    truth.append((hz, rate, abs(amp)))
y = 0.3 * y / np.max(np.abs(y))
tmp = '/tmp/modalfit_check.wav'
sf.write(tmp, y, sr)
out = '/tmp/modalfit_check.mmr'
subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'modalfit.py'), tmp, out,
                '--modes', str(len(truth)), '--seconds', '2.0', '--steps', '600'], check=True)
got = []
for line in open(out):
    if line.startswith('mode '):
        w = line.split()
        hz, zeta, amp = float(w[3]), float(w[5]), float(w[w.index('gains') + 1])
        got.append((hz, zeta * 2 * math.pi * hz, amp))
truth.sort(); got.sort()
# match each true mode to the nearest fitted one in log frequency
amax = max(a for _, _, a in truth)
print('  %9s %9s %8s   %8s %8s %8s' % ('true Hz', 'fit Hz', 'cents', 'true T60', 'fit T60', 'amp dB'))
worst_c = worst_t = 0.0
for hz, rate, amp in truth:
    if amp < 1e-3 * amax:
        continue
    j = min(range(len(got)), key=lambda k: abs(math.log(got[k][0] / hz)))
    fh, fr, fa = got[j]
    cents = 1200 * math.log2(fh / hz)
    if abs(cents) > 50:
        print('  %9.1f   dropped by the fit (%.1f dB)' % (hz, 20 * math.log10(amp / amax)))
        continue
    t60, ft60 = 6.91 / rate, 6.91 / fr
    worst_c = max(worst_c, abs(cents)); worst_t = max(worst_t, abs(math.log(ft60 / t60)))
    print('  %9.1f %9.1f %8.1f   %8.2f %8.2f %8.1f' % (hz, fh, cents, t60, ft60, 20 * math.log10(amp / amax)))
print('  worst: %.1f cents, T60 off by a factor of %.2f' % (worst_c, math.exp(worst_t)))
