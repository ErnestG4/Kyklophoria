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
    if layout == 'manifest':
        # an existing fits.tsv names the files and their pitches: for sets whose
        # files carry no note name and were pitched by hand or by ear
        with open(os.path.join(indir, 'fits.tsv')) as m:
            next(m)
            for line in m:
                c = line.rstrip('\n').split('\t')
                for e in exts:
                    path = os.path.join(indir, c[0] + e)
                    if os.path.exists(path):
                        out.append((path, int(float(c[3])), c[4] if len(c) > 4 and c[4] else '-', c[0]))
                        break
    elif layout == 'philharmonia':
        for f in sorted(os.listdir(indir)):
            if not f.lower().endswith(exts):
                continue
            parts = os.path.splitext(f)[0].split('_')
            if len(parts) < 5 or parts[4] != articulation:
                continue
            if parts[2] == 'phrase':      # a run of notes, not one: the double bass has these under pizz-normal
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
    ap.add_argument('--only', default='', help='comma-separated record ids to fit again in place, the rest of fits.tsv kept')
    ap.add_argument('--polish', type=int, default=-1,
                    help='steps of a second fit from the surviving modes after validation (default steps/2, 0 for none)')
    ap.add_argument('--keep-ids', action='store_true', help='name records after their files rather than family+index')
    ap.add_argument('--first-id', type=int, default=0, help='number records from here, for a set fitted in parts')
    ap.add_argument('--body', type=int, default=0, help='fit this many broad modes to the residual after the partials: the body')
    ap.add_argument('--t60-cap', type=float, default=8.0,
                    help='a fitted T60 is capped at this many times the analysed length. It was 3, and a Wurlitzer C3 whose '
                         'fundamental sits flat for 2.5 s was held to 7.6 s where the recording says over ten')
    a = ap.parse_args()
    if a.polish < 0:
        a.polish = a.steps // 2
    os.makedirs(a.outdir, exist_ok=True)
    rows = discover(a.indir, a.layout, a.articulation)
    # what the parameter is: a note, or — a manifest may say so — an index
    # into a row of bodies, which the world carries so the runtime knows to
    # take it from a position and not from the pitch
    param = 'midi'
    if a.layout == 'manifest':
        with open(os.path.join(a.indir, 'fits.tsv')) as m:
            next(m)
            first = m.readline().rstrip('\n').split('\t')
            if len(first) > 2 and first[2] == 'index':
                param = 'index'
    if a.limit:
        rows = rows[:a.limit]
    print('  %s: %d single-note files, articulation %s' % (a.family, len(rows), a.articulation))
    device = 'cuda' if modalfit.torch.cuda.is_available() else 'cpu'
    residuals = []
    # a refit of some records in place: the manifest's other rows are kept
    # as they are, the chosen ones rewritten where they stand
    only = set(a.only.split(',')) if a.only else None
    old = {}
    if only:
        for l in open(os.path.join(a.outdir, 'fits.tsv')).read().splitlines()[1:]:
            old[l.split('\t')[0]] = l
    with open(os.path.join(a.outdir, 'fits.tsv' if not only else 'fits.new.tsv'), 'w') as man:
        man.write('id\tfamily\tparam\tvalue\tdynamic\tsource\tmodes\tloss\texcess_db\tdecay_ratio\n')
        for n, (path, midi, dyn, f) in enumerate(rows):
            mid = os.path.splitext(os.path.basename(path))[0] if a.keep_ids else '%s%03d' % (a.family, n + a.first_id)
            if only and mid not in only:
                if mid in old:
                    man.write(old[mid] + '\n')
                continue
            f0 = 440.0 * 2 ** ((midi + 12 * a.octave - 69) / 12) if param == 'midi' else None
            raw, sr = sf.read(path, always_2d=True)
            raw = raw.mean(axis=1)
            secs = ring_seconds(raw, sr, a.max_seconds)
            try:
                x, sr = modalfit.load(path, secs, a.onset)
                init = modalfit.initialise(x, sr, a.modes, f0=f0)
            except ValueError as e:
                print('  %-10s %s: skipped, %s' % (mid, f, e))
                continue
            if not init:
                print('  %-10s %s: no partials found' % (mid, f))
                continue
            fr, r, amp, y, loss, ph = modalfit.fit(x, sr, init, a.steps, device, verbose=False)
            cap = a.t60_cap * secs
            r = np.maximum(r, 6.91 / cap)
            keep = modalfit.keep_fundamental(modalfit.audible(amp, r) & modalfit.validate(fr, r, amp, x, sr), fr, f0)
            fr, r, amp, ph = fr[keep], r[keep], amp[keep], ph[keep]
            if a.polish and len(fr) and (~keep).any():
                # the survivors, fitted again without the modes that were
                # taking energy they had no claim to, from their own phases
                fr, r, amp, y, loss, ph = modalfit.fit(x, sr, list(zip(fr, r, amp, ph)), a.polish, device, verbose=False)
                r = np.maximum(r, 6.91 / cap)
                keep = modalfit.keep_fundamental(modalfit.audible(amp, r) & modalfit.validate(fr, r, amp, x, sr), fr, f0)
                fr, r, amp, ph = fr[keep], r[keep], amp[keep], ph[keep]
            order = np.argsort(fr)
            nbody = 0
            if a.body:
                # the body is fitted once for the set, from the mean residual
                # spectrum of every note (below); here, keep this note's
                # residual spectrum and what it needs to place the body
                res = x - y[:len(x)]
                residuals.append((mid, modalfit.residual_spectrum(res, sr), sr, len(x)))
            y = modalfit.resynth(fr, r, amp, len(x), sr, ph)
            ex = modalfit.excess_db(y, x, sr)
            dr = modalfit.decay_ratio(fr, r, amp, x, sr)
            with open(os.path.join(a.outdir, mid + '.mmr'), 'w') as o:
                o.write('# modalfit record via fitset: %s, %.2f s analysed, loss %.4f, excess %.2f dB, decay ratio %.2f\n' % (f, secs, loss, ex, dr))
                o.write('source %s\nfitted 1\nloss %.5f\nexcess_db %.3f\ndecay_ratio %.3f\npositions 12\nmodes %d\nbody %d\n' % (path, loss, ex, dr, len(order), nbody))
                for k, i in enumerate(order):
                    w = 2 * math.pi * fr[i]
                    o.write('mode %d hz %.6f zeta %.9g phase %.5f gains %s\n' % (k, fr[i], r[i] / w, ph[i], ' '.join('%.9g' % amp[i] for _ in range(12))))
            sf.write(os.path.join(a.outdir, mid + '-resynth.wav'), np.clip(y / (np.max(np.abs(y)) or 1) * 0.5, -1, 1), sr)
            sf.write(os.path.join(a.outdir, mid + '-target.wav'), np.clip(x * 0.5, -1, 1), sr)
            man.write('%s\t%s\t%s\t%d\t%s\t%s\t%d\t%.4f\t%.3f\t%.3f\n' % (mid, a.family, param, midi + 12 * a.octave, dyn, f, len(order), loss, ex, dr))
            man.flush()
            print('  %-10s %-40s midi %3d %-6s %.2fs  %2d modes  loss %.3f  excess %.2f dB  decay x%.2f' % (mid, f, midi, dyn, secs, len(order), loss, ex, dr))
    if a.body and residuals:
        # the body: the peaks of the mean residual spectrum over the set —
        # note-specific errors average out, the instrument's resonances do
        # not — placed into every record with the note's own residual level
        # there, clear of the note's partials
        body = modalfit.set_body([sp for _, sp, _, _ in residuals], a.body)
        print('  body of the set: %s' % ', '.join('%.0f Hz Q%.0f' % (f, q) for f, q in body))
        for mid, sp, sr, n in residuals:
            modalfit.append_body(os.path.join(a.outdir, mid + '.mmr'), body, sp, sr)
    if only:
        os.replace(os.path.join(a.outdir, 'fits.new.tsv'), os.path.join(a.outdir, 'fits.tsv'))
    print('  done: %s' % a.outdir)


if __name__ == '__main__':
    main()
