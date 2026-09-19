#!/usr/bin/env python3
"""fitset — a whole sample set through the fitter, into a fitted family.

    fitset.py <family> <sample dir> <out dir> [--layout philharmonia|folders]
              [--articulation normal] [--modes 48] [--steps 800]
              [--max-seconds 4.0] [--limit N]

Two layouts. `philharmonia`: <instrument>_<note>_<length>_<dynamic>_<articulation>.mp3,
note as C4 / Cs4, one directory. `folders`: one subdirectory per dynamic layer
(MAX, MED, ...), files named by note — "c#3 max.wav" — which is how the EP
multisample came. Every single-note file of the articulation asked for is
fitted — tools/modalfit.py's fit, imported rather than run, so torch starts
once — and the family gets a manifest (id, family, param=midi, value, dynamic)
that tools/pack.py takes through --fits. Both dynamics go in as separate models
of the same family at the same pitch: the bake finds the dynamic direction on
its own, which is what makes a keyboard into a space rather than a curve.

The analysed window is the file's own ring: from the onset until the level
has fallen 40 dB or --max-seconds, whichever is first. A guitar note is three
seconds of ring; a banjo's is under one.
"""
import argparse
import math
import os
import re
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modalfit  # noqa: E402

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def midi_of(token):
    """C4, Cs4, c#4, Db4 -> midi"""
    m = re.match(r'^([A-Ga-g])(s|#|b)?(-?\d)$', token)
    if not m:
        return None
    acc = {'s': 1, '#': 1, 'b': -1, None: 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + NOTE[m.group(1).upper()] + acc


def discover(indir, layout, articulation):
    """(path, midi, dynamic, label) for every single-note file"""
    out = []
    exts = ('.mp3', '.wav', '.flac', '.aif', '.aiff')
    if layout == 'philharmonia':
        for f in sorted(os.listdir(indir)):
            if not f.lower().endswith(exts):
                continue
            parts = os.path.splitext(f)[0].split('_')
            if len(parts) < 5 or parts[4] != articulation:
                continue
            midi = midi_of(parts[1])
            if midi is not None:
                out.append((os.path.join(indir, f), midi, parts[3], f))
    else:
        for dyn in sorted(os.listdir(indir)):
            d = os.path.join(indir, dyn)
            if not os.path.isdir(d):
                continue
            for f in sorted(os.listdir(d)):
                if not f.lower().endswith(exts):
                    continue
                m = re.match(r'^([a-gA-G][#sb]?-?\d+)', os.path.splitext(f)[0])
                midi = midi_of(m.group(1)) if m else None
                if midi is not None:
                    out.append((os.path.join(d, f), midi, dyn, os.path.join(dyn, f)))
    return out


def ring_seconds(x, sr, max_s):
    """from the onset until 40 dB down, on a 50 ms RMS"""
    hop = int(0.05 * sr)
    env = np.array([np.sqrt(np.mean(x[i:i + hop] ** 2)) for i in range(0, len(x) - hop, hop)])
    if not len(env):
        return max_s
    top = env.max()
    below = np.where(env < top * 10 ** (-40 / 20))[0]
    after = below[below > env.argmax()]
    end = (after[0] * hop / sr) if len(after) else len(x) / sr
    return max(0.5, min(max_s, end))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('family')
    ap.add_argument('indir')
    ap.add_argument('outdir')
    ap.add_argument('--articulation', default='normal')
    ap.add_argument('--layout', default='philharmonia')
    ap.add_argument('--onset', type=float, default=-40.0)
    ap.add_argument('--octave', type=int, default=0,
                    help='octaves to add to the parsed note: +1 for sets that call middle C "C3", which the EP does')
    ap.add_argument('--modes', type=int, default=48)
    ap.add_argument('--steps', type=int, default=800)
    ap.add_argument('--max-seconds', type=float, default=4.0)
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rows = discover(a.indir, a.layout, a.articulation)
    if a.limit:
        rows = rows[:a.limit]
    print('  %s: %d single-note files, articulation %s' % (a.family, len(rows), a.articulation))
    device = 'cuda' if modalfit.torch.cuda.is_available() else 'cpu'
    with open(os.path.join(a.outdir, 'fits.tsv'), 'w') as man:
        man.write('id\tfamily\tparam\tvalue\tdynamic\tsource\n')
        for n, (path, midi, dyn, f) in enumerate(rows):
            mid = '%s%03d' % (a.family, n)
            raw, sr = sf.read(path, always_2d=True)
            raw = raw.mean(axis=1)
            secs = ring_seconds(raw, sr, a.max_seconds)
            try:
                x, sr = modalfit.load(path, secs, a.onset)
                init = modalfit.initialise(x, sr, a.modes)
            except ValueError as e:
                print('  %-10s %s: skipped, %s' % (mid, f, e))
                continue
            if not init:
                print('  %-10s %s: no partials found' % (mid, f))
                continue
            fr, r, amp, y, loss = modalfit.fit(x, sr, init, a.steps, device, verbose=False)
            cap = 3.0 * secs
            r = np.maximum(r, 6.91 / cap)
            keep = amp > amp.max() * 10 ** (-60 / 20)
            fr, r, amp = fr[keep], r[keep], amp[keep]
            order = np.argsort(fr)
            with open(os.path.join(a.outdir, mid + '.mmr'), 'w') as o:
                o.write('# modalfit record via fitset: %s, %.2f s analysed, loss %.4f\n' % (f, secs, loss))
                o.write('source %s\nfitted 1\nloss %.5f\npositions 12\nmodes %d\n' % (path, loss, len(order)))
                for k, i in enumerate(order):
                    w = 2 * math.pi * fr[i]
                    o.write('mode %d hz %.6f zeta %.9g gains %s\n' % (k, fr[i], r[i] / w, ' '.join('%.9g' % amp[i] for _ in range(12))))
            sf.write(os.path.join(a.outdir, mid + '-resynth.wav'), np.clip(y / (np.max(np.abs(y)) or 1) * 0.5, -1, 1), sr)
            sf.write(os.path.join(a.outdir, mid + '-target.wav'), np.clip(x * 0.5, -1, 1), sr)
            man.write('%s\t%s\tmidi\t%d\t%s\t%s\n' % (mid, a.family, midi + 12 * a.octave, dyn, f))
            man.flush()
            print('  %-10s %-40s midi %3d %-6s %.2fs  %2d modes  loss %.3f' % (mid, f, midi, dyn, secs, len(order), loss))
    print('  done: %s' % a.outdir)


if __name__ == '__main__':
    main()
