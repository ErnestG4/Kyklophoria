#!/usr/bin/env python3
"""earcheck.py — the numbers that can see what a listener hears at once.

    earcheck.py out/wav/*.wav

A rendered file that is a rail reads as silence; a note cut off while it
rings reads as a pop; a level ten dB off reads as a mistake. None of those
showed in a bark table or a spectral convergence, and both happened. So:

    rail      more than 5% of samples within 1e-3 of full scale, or the
              envelope flat to 1% for over a second at more than -20 dBFS
    click     a broadband burst: a 1 ms frame of the signal above 12 kHz
              that stands 20 dB over the median of the 200 ms around it and
              above -50 dBFS, and short: 20 dB down within 3 ms on both
              sides — a note cut off while ringing, or one starting at full
              amplitude, and broadband: its energy above 12 kHz within a
              tenth of its 4-12 kHz energy. A partial up there is not isolated,
              an attack arrives fast but stays, a stored 40 ms hammer burst
              that fades out is broad on one side, and a bright partial dying
              at 8 kHz leaks through the high-pass at a hundredth of itself
    silence   more than 50% of 50 ms frames under -80 dBFS
    level     RMS outside -40 .. -6 dBFS
    nan       any

Prints one line a file and exits 1 if any file fails. `make renders` and
the fitted renders run it; a wav that fails is a wav nobody should listen
to."""
import sys

import numpy as np
import soundfile as sf


def check(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    bad = []
    if np.isnan(x).any():
        bad.append('nan')
    rail = np.mean(np.abs(np.abs(x) - 1.0) < 1e-3)
    if rail > 0.05:
        bad.append('rail %.0f%%' % (100 * rail))
    hop = int(0.05 * sr)
    env = np.array([np.sqrt(np.mean(x[i:i + hop] ** 2)) for i in range(0, max(hop, len(x) - hop), hop)])
    if len(env) > 20:
        flat = 0
        for i in range(len(env) - 20):
            seg = env[i:i + 20]
            if seg.mean() > 0.1 and (seg.max() - seg.min()) < 0.01 * seg.mean():
                flat += 1
        if flat:
            bad.append('flat envelope %d s' % (flat // 20 + 1))
    # a click is a broadband burst: the signal above 12 kHz, its envelope on
    # 1 ms frames, and a frame 20 dB above the median of the 200 ms around it
    # — isolated, which a partial up there is not — and above -50 dBFS, which
    # a tail's own noise is not. A note cut off inside a mix leaves one;
    # an attack that ramps in over 3 ms does not
    from scipy import signal as sg
    sos = sg.butter(6, 12000.0, 'highpass', fs=sr, output='sos')
    hfx = sg.sosfiltfilt(sos, x) if len(x) > 100 else x
    fh = max(1, int(0.001 * sr))
    henv = np.array([np.sqrt(np.mean(hfx[i:i + fh] ** 2)) for i in range(0, max(fh, len(hfx) - fh), fh)])
    # and a click is broadband: its energy above 12 kHz is of the order of
    # its energy in 4-12 kHz. A bright partial dying at 8 kHz leaks through
    # the high-pass at a hundredth of its own band and is not one
    sos2 = sg.butter(6, [4000.0, 12000.0], 'bandpass', fs=sr, output='sos')
    mfx = sg.sosfiltfilt(sos2, x) if len(x) > 100 else x
    menv = np.array([np.sqrt(np.mean(mfx[i:i + fh] ** 2)) for i in range(0, max(fh, len(mfx) - fh), fh)])
    steps = 0
    k = 100
    for i in range(len(henv)):
        if henv[i] < 10 ** (-50 / 20):
            continue
        around = np.concatenate([henv[max(0, i - k):i], henv[i + 1:i + 1 + k]])
        if len(around) and henv[i] > 10 * (np.median(around) + 1e-9):
            # and it ends: 20 dB down again within 3 ms. An attack's high
            # band arrives fast too, but it stays
            after = henv[i + 1:i + 4]
            before = henv[max(0, i - 3):i]
            if len(after) and after.min() < 0.1 * henv[i] and (len(before) == 0 or before.min() < 0.1 * henv[i]) \
               and henv[i] > 0.1 * menv[i]:
                steps += 1
    if steps:
        bad.append('%d clicks' % steps)
    silence = np.mean(env < 1e-4)
    if silence > 0.5:
        bad.append('silence %.0f%%' % (100 * silence))
    rms = 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)
    if not -40 <= rms <= -6:
        bad.append('level %.0f dBFS' % rms)
    return bad, rms, len(x) / sr


def main():
    fails = 0
    for p in sys.argv[1:]:
        bad, rms, secs = check(p)
        fails += bool(bad)
        print('  %-44s %6.1fs %6.1f dBFS  %s' % (p.split('/')[-1], secs, rms, 'ok' if not bad else 'FAIL: ' + ', '.join(bad)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
