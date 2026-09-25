"""fitcheck.py fitdir [fitdir ...] [--all] [--over DB] [--list out.txt]

How far each record's ring is from its recording, heard the way it is played:
the fitted modes resynthesised and their loudness held against the
recording's, in windows from 0.1 s on — the attack before that is the stored
burst's, not the modes' — as the mean absolute difference in dB. One number,
against the recording itself.

Why: the fitter's decay ratio follows the top partial tracks, and it read
"fine" on the Iowa grand's A5 while the note as a whole rang twice as long as
its recording (4.4 s against 2.1); a refit scored on it chose that. And the
audit's neighbour comparison flagged a good A#5 because that A5 beside it had
gone wrong. The recording is the one reference that cannot move.

By default only the records a world plays modes from — in a layered set the
loudest dynamic at each note; the quieter takes give only their attacks.
--all checks every record. --over lists the records past DB (set, ids) for
tools/refitn.py; --list writes that list.
"""
import argparse
import collections
import csv
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))

RANK = {'pp': 0, 'p': 1, 'mp': 2, 'mf': 3, 'f': 4, 'ff': 5, 'fff': 6}
WINDOWS = [(0.10, 0.16), (0.16, 0.25), (0.25, 0.40), (0.40, 0.60), (0.60, 0.90), (0.90, 1.35), (1.35, 2.0), (2.0, 3.0)]


def envelope_error(y, x, sr, floor_db=-60.0):
    """mean |dB(y) - dB(x)| over the windows the recording fills, a window
    left out where the recording is under floor_db of its own peak (its tail
    is room noise by then, and holding a mode to noise is not the point)"""
    peak = float(np.max(np.abs(x))) or 1.0
    errs = []
    for a, b in WINDOWS:
        i, j = int(a * sr), int(b * sr)
        if j > len(x) or j > len(y):
            break
        ex = 10 * math.log10(float(np.mean(x[i:j] ** 2)) / peak ** 2 + 1e-20)
        if ex < floor_db:
            break
        ey = 10 * math.log10(float(np.mean(y[i:j] ** 2)) / peak ** 2 + 1e-20)
        errs.append(abs(ey - max(ex, floor_db)))
    return float(np.mean(errs)) if errs else float('nan')


def read_modes(path):
    modes, src, secs = [], None, None
    import re
    with open(path) as f:
        for line in f:
            m = re.search(r'([\d.]+) s analysed', line)
            if line.startswith('#') and m:
                secs = float(m.group(1))
            if line.startswith('source '):
                src = line.split(' ', 1)[1].strip()
            elif line.startswith('mode '):
                t = line.split()
                hz, zeta, ph = float(t[3]), float(t[5]), float(t[7])
                g = float(t[9:][-1]) if len(t) > 9 else 0.0
                modes.append((hz, zeta, ph, g))
    return modes, src, secs


def load_target(src, secs, onset=-40.0):
    import modalfit
    for on in (onset, -65.0, -80.0):
        try:
            return modalfit.load(src, secs, on)
        except ValueError:
            continue
    return None, None


def check_record(path):
    import modalfit
    modes, src, secs = read_modes(path)
    if not modes or not src or not os.path.exists(src):
        return float('nan')
    x, sr = load_target(src, secs or 4.0)
    if x is None:
        return float('nan')
    f = np.array([m[0] for m in modes]); r = np.array([m[1] * 2 * math.pi * m[0] for m in modes])
    a = np.array([m[3] for m in modes]); ph = np.array([m[2] for m in modes])
    y = modalfit.resynth(f, r, a, len(x), sr, ph)
    return envelope_error(y, x, sr)


def carriers(rows):
    grp = collections.defaultdict(list)
    for r in rows:
        grp[r.get('value')].append(r)
    return [max(g, key=lambda r: RANK.get(r.get('dynamic', ''), -1)) for g in grp.values()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fitdirs', nargs='+')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--over', type=float, default=3.0)
    ap.add_argument('--list', default='')
    ap.add_argument('--tsv', default='', help='every record checked: set, id, note, dB')
    a = ap.parse_args()
    listing, allrows, every = [], [], []
    for d in a.fitdirs:
        try:
            rows = list(csv.DictReader(open(os.path.join(d, 'fits.tsv')), delimiter='\t'))
        except FileNotFoundError:
            continue
        if not rows or rows[0].get('param') != 'midi':
            continue
        with open(os.path.join(d, rows[0]['id'] + '.mmr')) as f:
            if any(l.startswith(('shaper', 'take')) for l in f):
                print('%-22s shaped records: not this check' % os.path.basename(d.rstrip('/')))
                continue
        use = rows if a.all else carriers(rows)
        errs = []
        for r in use:
            e = check_record(os.path.join(d, r['id'] + '.mmr'))
            errs.append((e, r))
            every.append((os.path.basename(d.rstrip('/')), r['id'], r['value'], e))
        good = [e for e, _ in errs if not math.isnan(e)]
        bad = sorted([(e, r) for e, r in errs if not math.isnan(e) and e > a.over], key=lambda t: -t[0])
        allrows += good
        if bad:
            listing.append((d.rstrip('/'), [r['id'] for _, r in bad]))
        print('%-22s %3d records: envelope error median %.1f dB, p90 %.1f, %d over %.0f dB%s'
              % (os.path.basename(d.rstrip('/')), len(good), float(np.median(good)) if good else 0, float(np.percentile(good, 90)) if good else 0,
                 len(bad), a.over, (': ' + ' '.join('%s@%s(%.1f)' % (r['id'], r['value'], e) for e, r in bad[:6])) if bad else ''))
    if allrows:
        print('all: %d records, envelope error median %.1f dB, p90 %.1f, %d over %.0f dB'
              % (len(allrows), float(np.median(allrows)), float(np.percentile(allrows, 90)), sum(len(ids) for _, ids in listing), a.over))
    if a.list:
        with open(a.list, 'w') as f:
            for d, ids in listing:
                f.write('%s %s\n' % (d, ','.join(ids)))
    if a.tsv:
        with open(a.tsv, 'w') as f:
            f.write('set\tid\tnote\tenvelope_db\n')
            for s, rid, note, e in every:
                f.write('%s\t%s\t%s\t%.2f\n' % (s, rid, note, e))


if __name__ == '__main__':
    main()
