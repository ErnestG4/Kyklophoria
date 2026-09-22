#!/usr/bin/env python3
"""fitvel.py — one note at several velocities, fitted as metal into a pickup.

    fitvel.py tine out/gen/tine out/fit/tine-vel --bar --steps 1500
    fitvel.py ep samples/EP out/fit/ep-vel --order MED,MAX --octave 1 --normalised --bar

The folders layout: <indir>/<velocity>/<note>.wav, one folder per velocity,
softest first (alphabetical, or --order). Every note that exists in every
folder is fitted jointly by modalfit.fit_shaped: the metal's partials shared,
one swing gain per take, and the pickup — a bell field with a voicing offset,
Faraday, the coil — shared. --bar keeps only inharmonic partials in the metal
(a tine or reed is a clamped bar; its harmonics are the pickup's). --normalised
gives each take an output gain, for a library that levelled its samples.

The record is a modal record with the stage after it:

    mode k hz F zeta Z gains A...          the metal, unit swing
    shaper bell H W K FC Q                 field offset, width, coil gain, coil f, Q
    shaper gap 0 G K FC Q                  an electrostatic plate: rest gap G (--form gap)
    take v030 swing G level L              one line per take

The packer reads the modes and ignores the rest; the stage is for the
runtime. Beside the record: the resynthesis and the target of every take, and
a bark table — h2..h4 against h1, target and model, per take — in fits.tsv
and in the log, because the whole point is what changes with velocity.
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
from fitset import midi_of  # noqa: E402


def harmonics(x, sr, f0, ks=(1, 2, 3, 4), N=65536):
    X = np.abs(np.fft.rfft(x[:N] * np.hanning(min(len(x), N)), N))
    out = []
    for k in ks:
        lo, hi = int(k * f0 * 0.96 * N / sr), int(k * f0 * 1.04 * N / sr)
        out.append(20 * np.log10(X[lo:hi].max() + 1e-12) if hi > lo and hi < len(X) else -120.0)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('family')
    ap.add_argument('indir')
    ap.add_argument('outdir')
    ap.add_argument('--order', help='velocity folders softest first, comma separated (default alphabetical)')
    ap.add_argument('--octave', type=int, default=0)
    ap.add_argument('--modes', type=int, default=24)
    ap.add_argument('--steps', type=int, default=1200)
    ap.add_argument('--seconds', type=float, default=3.0)
    ap.add_argument('--bar', action='store_true', help='the metal is a clamped bar: only inharmonic partials')
    ap.add_argument('--normalised', action='store_true', help='takes were levelled: an output gain per take')
    ap.add_argument('--form', default='bell', choices=['bell', 'gap'], help='the transducer: a magnetic pole (bell) or an electrostatic plate (gap)')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--from', dest='lo', type=int, default=0, help='first midi note')
    ap.add_argument('--to', dest='hi', type=int, default=127, help='last midi note')
    ap.add_argument('--only', default='', help='comma-separated note keys (g1,g#1) to fit again in place, the rest of fits.tsv kept')
    ap.add_argument('--coil-from-set', action='store_true', help="hold every note's coil to the median fc and Q of the set's last fit (fits.tsv here): one coil per set")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    dirs = a.order.split(',') if a.order else sorted(d for d in os.listdir(a.indir) if os.path.isdir(os.path.join(a.indir, d)))
    notes = None
    for d in dirs:
        here = {}
        for f in os.listdir(os.path.join(a.indir, d)):
            m = re.match(r'^([a-gA-G][#sb]?-?\d+)', os.path.splitext(f)[0])
            if m and f.lower().endswith(('.wav', '.flac', '.mp3', '.aif', '.aiff')):
                here[m.group(1).lower()] = os.path.join(d, f)
        notes = here if notes is None else {k: v for k, v in notes.items() if k in here}
        files = {k: [] for k in notes}
    for d in dirs:
        for f in os.listdir(os.path.join(a.indir, d)):
            m = re.match(r'^([a-gA-G][#sb]?-?\d+)', os.path.splitext(f)[0])
            if m and m.group(1).lower() in files:
                files[m.group(1).lower()].append(os.path.join(d, f))
    keys = sorted((k for k in files if a.lo <= midi_of(k) + 12 * a.octave <= a.hi), key=lambda k: midi_of(k))
    if a.limit:
        keys = keys[:a.limit]
    print('  %s: %d notes in every one of %d velocity folders (%s)' % (a.family, len(keys), len(dirs), ', '.join(dirs)))
    device = 'cuda' if modalfit.torch.cuda.is_available() else 'cpu'
    # a refit of some notes in place: the manifest's other rows are kept as
    # they are, the chosen ones rewritten where they stand (the ids are the
    # notes' places in the full list, so the list is walked whole)
    only = set(k.lower() for k in a.only.split(',')) if a.only else None
    old = {}
    if only:
        for l in open(os.path.join(a.outdir, 'fits.tsv')).read().splitlines()[1:]:
            old[l.split('\t')[0]] = l
    coil_prior = None
    if a.coil_from_set:
        rows = [l.split('\t') for l in open(os.path.join(a.outdir, 'fits.tsv')).read().splitlines()]
        hdr, rows = rows[0], rows[1:]
        ifc, iq = hdr.index('coil_hz'), hdr.index('coil_q')
        fcs = [float(r[ifc]) for r in rows if float(r[ifc]) > 0]
        qs = [float(r[iq]) for r in rows if float(r[iq]) > 0]
        coil_prior = (math.log(float(np.median(fcs))), math.log(float(np.median(qs))))
        print('  one coil for the set: fc %.0f Hz, Q %.2f (the medians of %d notes)' % (math.exp(coil_prior[0]), math.exp(coil_prior[1]), len(fcs)))
    with open(os.path.join(a.outdir, 'fits.tsv' if not only else 'fits.new.tsv'), 'w') as man:
        man.write('id\tfamily\tparam\tvalue\tdynamic\tsource\tmodes\tloss\th_over_w\tcoil_hz\tcoil_q\tswings\tbark_target\tbark_model\n')
        for n, key in enumerate(keys):
            midi = midi_of(key) + 12 * a.octave
            f0 = 440.0 * 2 ** ((midi - 69) / 12)
            if only and key.lower() not in only:
                mid = '%s%03d' % (a.family, n)
                if mid in old:
                    man.write(old[mid] + '\n')
                continue
            xs = []
            for rel in files[key]:
                try:
                    x, sr = modalfit.load(os.path.join(a.indir, rel), a.seconds, -90.0, normalise=a.normalised)
                except ValueError as e:
                    print('  %-6s skipped: %s' % (key, e))
                    xs = None
                    break
                xs.append(x)
            if not xs:
                continue
            m = min(len(x) for x in xs)
            xs = [x[:m] for x in xs]
            peak = max(float(np.abs(x).max()) for x in xs) or 1.0
            xs = [x / peak for x in xs]
            # a take 60 dB under the loudest is noise, not a velocity: leave
            # it out, and initialise the metal from the loudest take — the
            # bar prior takes its harmonics away again; the top octave's
            # softest takes sit near -90 dBFS and initialised a metal of
            # nothing, which opened the field flat
            rms = [float(np.sqrt(np.mean(x ** 2))) for x in xs]
            keep = [i for i, v in enumerate(rms) if v > max(rms) * 1e-3]
            xs = [xs[i] for i in keep]
            used = [dirs[i] for i in keep]
            loud = int(np.argmax([rms[i] for i in keep]))
            # a bar metal is found among many more candidates than it keeps:
            # on the loudest take the sixteen loudest peaks are all the
            # pickup's harmonics and the fundamental sits 20 dB under h2,
            # so it must be looked for wider, and put in by hand if it is
            # still not there — a tine without its fundamental is nothing
            init = modalfit.initialise(xs[loud] / (np.abs(xs[loud]).max() or 1.0), sr, 4 * a.modes if a.bar else a.modes, f0=f0)
            if a.bar:
                init = modalfit.bar_metal(init, f0)
                if not any(abs(m[0] / f0 - 1) < 0.03 for m in init):
                    top = max((m[2] for m in init), default=0.1)
                    init.append((f0, 1.0, 0.3 * top, 0.0))
                init = sorted(init, key=lambda m: -(m[2] * (1e6 if abs(m[0] / f0 - 1) < 0.04 else 1.0)))[:a.modes]
                # the decay is read off the softest usable take, where the
                # pickup is near-linear and the output decays as the metal
                # does; on the loudest take the field compresses the
                # envelope and the metal came out decaying 2.7x too fast
                # (Epi's E2: 12 s against 33)
                soft = modalfit.initialise(xs[0] / (np.abs(xs[0]).max() or 1.0), sr, 4 * a.modes)
                fixed = []
                for m in init:
                    near = [q for q in soft if abs(q[0] / m[0] - 1) < 0.03]
                    fixed.append((m[0], min(near, key=lambda q: abs(q[0] - m[0]))[1], m[2]) + tuple(m[3:]) if near else m)
                init = fixed
            if not init:
                print('  %-6s no partials' % key)
                continue
            f, r, amp, ph, g, (h, w, K, fc, Q), ys, loss = modalfit.fit_shaped(xs, sr, init, a.steps, device, verbose=False, normalised=a.normalised, form=a.form, coil_prior=coil_prior)
            # the tine's own note stays whatever the level test says: the EP's
            # G2 came out of the fit without it and played a fifth low
            keep = modalfit.keep_fundamental(modalfit.audible(amp, r) & (amp > 0), f, f0)
            order = [i for i in np.argsort(f) if keep[i]]
            mid = '%s%03d' % (a.family, n)
            bt, bm = [], []
            for k, (x, y) in enumerate(zip(xs, ys)):
                hx, hy = harmonics(x, sr, f0), harmonics(y, sr, f0)
                bt.append('/'.join('%+.0f' % (v - hx[0]) for v in hx[1:]))
                bm.append('/'.join('%+.0f' % (v - hy[0]) for v in hy[1:]))
                lvl = float(np.sqrt(np.mean(x ** 2)))
                sf.write(os.path.join(a.outdir, '%s-%s-target.wav' % (mid, used[k])), np.clip(x * 0.9, -1, 1), sr)
                sf.write(os.path.join(a.outdir, '%s-%s-resynth.wav' % (mid, used[k])), np.clip(y / (np.abs(y).max() or 1) * 0.9 * (np.abs(x).max()), -1, 1), sr)
            with open(os.path.join(a.outdir, mid + '.mmr'), 'w') as o:
                o.write('# modalfit shaped record via fitvel: %s at %d velocities, loss %.4f\n' % (key, len(xs), loss))
                o.write('source %s\nfitted 1\nshaped 1\nloss %.5f\npositions 12\nmodes %d\n' % (files[key][0], loss, len(order)))
                for k, i in enumerate(order):
                    o.write('mode %d hz %.6f zeta %.9g phase %.5f gains %s\n' % (k, f[i], r[i] / (2 * math.pi * f[i]), float(np.remainder(ph[i], 2 * math.pi)), ' '.join('%.9g' % amp[i] for _ in range(12))))
                o.write('shaper %s %.6g %.6g %.6g %.6g %.6g\n' % (a.form, h, w, K, fc, Q))
                for k, d in enumerate(used):
                    o.write('take %s swing %.6g level %.6g\n' % (d, g[k], float(np.sqrt(np.mean(xs[k] ** 2)))))
            man.write('%s\t%s\tmidi\t%d\tall\t%s\t%d\t%.4f\t%.3f\t%.0f\t%.2f\t%s\t%s\t%s\n' % (
                mid, a.family, midi, files[key][0], len(order), loss, h / w, fc, Q,
                ','.join('%.2f' % v for v in g), ' '.join(bt), ' '.join(bm)))
            man.flush()
            print('  %-6s %-8s midi %3d  %2d metal  loss %.3f  h/w %.2f coil %.0f Hz Q %.2f  swings %s' % (mid, key, midi, len(order), loss, h / w, fc, Q, ','.join('%.2f' % v for v in g)))
            for k, d in enumerate(used):
                print('         %-5s target h2/h3/h4 %-14s model %s' % (d, bt[k], bm[k]))
    if only:
        os.replace(os.path.join(a.outdir, 'fits.new.tsv'), os.path.join(a.outdir, 'fits.tsv'))
    print('  done: %s' % a.outdir)


if __name__ == '__main__':
    main()
