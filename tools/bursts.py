#!/usr/bin/env python3
"""bursts.py — the attack as a stored burst: what the modes are not.

    bursts.py out/fit/wurli [--ms 40] [--thump 0.06]
    bursts.py out/fit/ep-vel --shaped

A struck note is modes, a hammer, and noise; the modes are fitted and the
other two were being imitated by sines with a 50 ms decay, which is a thock
and not a hammer. The MT-32, the SY99 and the Nord Wave all did the same
thing about it: a short stored attack under a synthesised sustain. Here the
attack is measured from the note it sits under: the recording minus the
modal resynthesis at the fitted phases, over the first --ms milliseconds
(60), crossing into the ringing modes on a raised cosine over its last
--fade ms (30) — long, because the model carries nothing above the partials
it kept and the recording's brightness must not stop at a wall. The
modes that only existed to make the thump (T60 under --thump seconds) come
out of the record, since the burst carries them now.

Writes <id>-burst.wav beside the record (mono float, the target's rate; the residual is not bounded by the recording's peak) and adds a
`burst <file>` line; for a shaped set (fitvel), one burst a take and a
`burst <take> <file>` line each. Export packs them into the world at 48 kHz;
the runtime plays them at strike time after the pickup, scaled by the swing
— for a shaped world, the two takes' bursts crossfaded by velocity."""
import argparse
import math
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modalfit  # noqa: E402
import playvel  # noqa: E402


def read(path):
    head, modes, tail = [], [], []
    for line in open(path):
        w = line.split()
        if w and w[0] == 'mode':
            modes.append(line.rstrip('\n'))
        elif w and w[0] in ('burst', 'signed'):
            continue
        elif modes:
            tail.append(line.rstrip('\n'))
        else:
            head.append(line.rstrip('\n'))
    return head, modes, tail


def parse(line):
    w = line.split()
    return float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0


def burst(x, modes, sr, ms, thump_s, fade_ms=30.0, signed=False):
    keep = [m for m in modes if 6.91 / (parse(m)[1] * 2 * math.pi * parse(m)[0]) >= thump_s]
    f = np.array([parse(m)[0] for m in keep]); z = np.array([parse(m)[1] for m in keep])
    a = np.array([parse(m)[2] for m in keep]); ph = np.array([parse(m)[3] for m in keep])
    if ms is None:
        # as long as it matters: the burst runs until what the bank cannot
        # play — the recording above its highest fitted partial — has fallen
        # 30 dB under the recording, on 10 ms windows; at least 60 ms and at
        # most 400. A piano's E1 has a hundred partials above the bank's 48
        # that die in half a second; cut at 60 ms they fell off a cliff
        # behind the burst, every band above 2 kHz 60-77 dB under the
        # recording from 60 ms on, which Combust heard as a fizz on the
        # attack. A treble note the bank covers to 8 kHz stays at 60 ms.
        # (The residual itself is no measure: a partial with its phase a
        # little off leaves a residual as loud as itself for as long as it
        # rings, and every note ran to the cap.)
        m = min(len(x), int(0.4 * sr))
        f_top = float(f.max()) if len(f) else 0.0
        from scipy import signal as sg
        hp = sg.sosfiltfilt(sg.butter(6, min(f_top * 1.05, 0.45 * sr), 'highpass', fs=sr, output='sos'), x[:m]) if f_top > 0 else x[:m]
        hop = int(0.01 * sr)
        peak = max(np.sqrt(np.mean(x[i:i + hop] ** 2)) for i in range(0, min(len(x), int(0.1 * sr)) - hop + 1, hop))
        ms = 60.0
        for i in range(int(0.06 * sr), m - hop, hop):
            r = np.sqrt(np.mean(hp[i:i + hop] ** 2))
            ms = (i + hop) * 1000.0 / sr
            if r < peak * 10 ** (-40 / 20):      # 40 dB under the note's own peak: gone, or the floor
                break
        fade_ms = max(fade_ms, ms / 3)
    n = min(len(x), int(ms * sr / 1000))
    y = modalfit.resynth(f, z * 2 * math.pi * f, a, n, sr, ph) if len(f) else np.zeros(n)
    # the fit's loss is STFT magnitude, which cannot see a sign: every
    # phase plus pi leaves every magnitude where it was, and half the
    # records came back as the negative of their recording (guitar A2:
    # the residual's energy 3.7x the recording's). The residual can see it,
    # so the sign that leaves less residual is the true one, and the
    # record's phases are turned to it
    flipped = False
    if signed:
        pass
    elif len(f) and np.sum((x[:n] + y) ** 2) < np.sum((x[:n] - y) ** 2):
        y = -y
        flipped = True
        keep = [flip(m) for m in keep]
    e = x[:n] - y
    k = min(n, int(fade_ms * sr / 1000))
    e[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(k) / k)
    fade_in(e, sr)
    return e, keep, flipped


def fade_in(e, sr):
    """a stored attack starts from silence: the analysed window of a fast
    note can begin mid-attack (the EP's G4 at 0.85 on its first sample), and
    played at a strike that is a step. One millisecond up on a raised cosine."""
    k = min(len(e), int(0.001 * sr))
    e[:k] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k)


def flip(line):
    w = line.split()
    i = w.index('phase') + 1
    w[i] = '%.5f' % ((float(w[i]) + math.pi) % (2 * math.pi))
    return ' '.join(w)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir')
    ap.add_argument('--ms', default='60', help='the burst\'s length in ms, or "auto": until the residual is 24 dB under the recording, 60 to 400')
    ap.add_argument('--fade', type=float, default=30.0, help='the burst\'s last this many ms cross into the modes on a raised cosine')
    ap.add_argument('--thump', type=float, default=0.06, help='modes with a T60 under this are the hammer, and leave')
    ap.add_argument('--shaped', action='store_true', help='a fitvel set: a burst a take')
    a = ap.parse_args()
    rows = [l.split('\t') for l in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]]
    done = 0
    flips = 0
    for r in rows:
        rid = r[0]
        rp = os.path.join(a.recdir, rid + '.mmr')
        if not os.path.exists(rp):
            continue
        head, modes, tail = read(rp)
        signed = any(l.startswith('signed') for l in open(rp))
        lines = ['signed 1']
        if a.shaped:
            takes = [t.split() for t in tail if t.startswith('take ')]
            # the burst is the residual against the model AS THE RUNTIME PLAYS
            # IT — the metal at the take's swing through the field and the
            # coil (playvel's chain, which is the runtime's) — with the
            # target scaled onto the model's level over the first 100 ms,
            # since a levelled library's level means nothing and a burst
            # against a mis-scaled model cancels the wrong thing. The sign
            # belongs to the record (the phases are shared by every take),
            # decided once over all takes and written into the phases
            shaper = [q.split() for q in tail if q.startswith('shaper ')]
            if not shaper:
                continue
            sh = [shaper[0][1]] + [float(v) for v in shaper[0][2:7]]
            pm = [parse(m) for m in modes]
            pairs = []
            e_plus = e_minus = 0.0
            for t in takes:
                tp = os.path.join(a.recdir, '%s-%s-target.wav' % (rid, t[1]))
                if not os.path.exists(tp):
                    continue
                x, sr = sf.read(tp)
                swing = float(t[3])
                y = playvel.note(pm, sh, swing, 0.2, sr)
                n = min(len(x), int(a.ms * sr / 1000))
                m = min(len(x), int(0.1 * sr))
                g = float(np.dot(np.abs(x[:m]), np.abs(y[:m])) / (np.dot(np.abs(x[:m]), np.abs(x[:m])) + 1e-12))   # level, sign-blind
                pairs.append((t[1], g * x[:n], y[:n], sr))
                e_plus += np.sum((g * x[:n] - y[:n]) ** 2)
                e_minus += np.sum((g * x[:n] + y[:n]) ** 2)
            # the sign is decided once and kept: on a shaped record the
            # pickup's output correlates weakly with the target and the
            # choice is near a coin-toss, which flipped back and forth on
            # every run until the record said it had been decided
            sign = 1.0 if signed else (-1.0 if e_minus < e_plus else 1.0)
            keep = [flip(m) for m in modes] if sign < 0 else modes
            flips += sign < 0
            for t, x, y, sr in pairs:
                e = x - sign * y
                k = min(len(e), int(a.fade * sr / 1000))
                e[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(k) / k)
                fade_in(e, sr)
                bp = '%s-%s-burst.wav' % (rid, t)
                sf.write(os.path.join(a.recdir, bp), e, sr, subtype='FLOAT')
                lines.append('burst %s %s' % (t, bp))
        else:
            tp = os.path.join(a.recdir, rid + '-target.wav')
            if not os.path.exists(tp):
                continue
            x, sr = sf.read(tp)
            e, keep, flipped = burst(x / 0.5, modes, sr, None if a.ms == 'auto' else float(a.ms), a.thump, a.fade, signed)   # the target wav is the analysed excerpt at half scale
            flips += flipped
            bp = rid + '-burst.wav'
            sf.write(os.path.join(a.recdir, bp), e, sr, subtype='FLOAT')   # at the record's scale, not the target wav's half
            lines.append('burst %s' % bp)
        out = [h if not h.startswith('modes ') else 'modes %d' % len(keep) for h in head]
        out += ['mode %d %s' % (i, ' '.join(m.split()[2:])) for i, m in enumerate(keep)]
        out += tail + lines
        open(rp, 'w').write('\n'.join(out) + '\n')
        done += 1
    print('  %s: %d records with a burst of %s ms, %d sign-flipped%s' % (a.recdir, done, a.ms, flips, '' if a.shaped else ', thumps under %.0f ms removed' % (1000 * a.thump)))


if __name__ == '__main__':
    main()
