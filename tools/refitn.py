"""refitn.py fitdir [--only id,id] [--auto] [--steps 1200] [--dry]

A record fitted alone can settle in a bad optimum: piano-iowa3's A#5 ff put a
strong, fast-dying mode 35 cents sharp of its fundamental and two junk
partials an octave and a half below it, and rang 12x too short (decay ratio
0.085); its level then sat 13 dB under what the notes either side of it say,
and the keyboard seesawed around it (Combust, 2026-09-23: "a few notes are off
and the jumps are bigger near them ... Piano A#5, G5").

This fits such a record again from its NEIGHBOURS: the same dynamic's nearest
good records below and above, their modes transposed to this note, each used
as the fitter's starting point (tools/modalfit.fit), validated and polished
exactly as tools/fitset.py does. The candidate kept is the one with the best

    score = loss x (1 + |ln decay_ratio|)

— the loss the fitter reports, and how far the ring's length is from the
recording's — and only if it beats the record as it stands by 5 %. The old
record goes to fitdir/prev/ first, and fits.tsv is updated in place.

--auto picks the records to try: a decay ratio outside 0.4..2.5, a loss over
1.3, or fewer than 4 modes. A neighbour is good by the same measure.

Uses the fitter's torch (~/fmexplorer/bin/python), like fitset.
"""
import argparse
import math
import os
import re
import shutil
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(__file__))
import modalfit  # noqa: E402

GOOD_DR = (0.4, 2.5)
GOOD_LOSS = 1.3


def read_manifest(fitdir):
    rows, head = [], None
    with open(os.path.join(fitdir, 'fits.tsv')) as f:
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue
            if head is None:
                head = line.split('\t')
                continue
            rows.append(dict(zip(head, line.split('\t'))))
    return head, rows


def read_record(path):
    """modes (hz, zeta, phase, gain), the source, the seconds analysed, and the
    lines after the modes (a body, if the set has one) kept as they are"""
    modes, src, secs, tail = [], None, None, []
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
                gains = [float(x) for x in t[9:]]
                modes.append((hz, zeta, ph, gains[-1] if gains else 0.0))
            elif line.startswith('body') and not line.startswith('body '):
                tail.append(line)
            elif line.startswith('bodymode') or line.startswith('bmode'):
                tail.append(line)
    return modes, src, secs, tail


def good(row):
    try:
        dr, loss, n = float(row['decay_ratio']), float(row['loss']), int(row['modes'])
    except (KeyError, ValueError):
        return False
    return GOOD_DR[0] <= dr <= GOOD_DR[1] and loss <= GOOD_LOSS and n >= 4


def score(loss, dr):
    return loss * (1.0 + abs(math.log(max(dr, 1e-3))))


def fit_from(x, sr, f0, init, steps, polish, cap, device):
    fr, r, amp, y, loss, ph = modalfit.fit(x, sr, init, steps, device, verbose=False)
    r = np.maximum(r, 6.91 / cap)
    keep = modalfit.keep_fundamental(modalfit.audible(amp, r) & modalfit.validate(fr, r, amp, x, sr), fr, f0)
    fr, r, amp, ph = fr[keep], r[keep], amp[keep], ph[keep]
    if polish and len(fr) and (~keep).any():
        fr, r, amp, y, loss, ph = modalfit.fit(x, sr, list(zip(fr, r, amp, ph)), polish, device, verbose=False)
        r = np.maximum(r, 6.91 / cap)
        keep = modalfit.keep_fundamental(modalfit.audible(amp, r) & modalfit.validate(fr, r, amp, x, sr), fr, f0)
        fr, r, amp, ph = fr[keep], r[keep], amp[keep], ph[keep]
    y = modalfit.resynth(fr, r, amp, len(x), sr, ph)
    ex = modalfit.excess_db(y, x, sr)
    dr = modalfit.decay_ratio(fr, r, amp, x, sr) if len(fr) else 0.0
    return fr, r, amp, ph, y, loss, ex, dr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fitdir')
    ap.add_argument('--only', default='')
    ap.add_argument('--auto', action='store_true')
    ap.add_argument('--steps', type=int, default=1200)
    ap.add_argument('--polish', type=int, default=-1)
    ap.add_argument('--onset', type=float, default=-40.0)
    ap.add_argument('--t60-cap', type=float, default=8.0)
    ap.add_argument('--dry', action='store_true', help='fit and report, write nothing')
    a = ap.parse_args()
    polish = a.steps // 2 if a.polish < 0 else a.polish
    head, rows = read_manifest(a.fitdir)
    byid = {r['id']: r for r in rows}
    targets = [t for t in a.only.split(',') if t]
    if a.auto:
        targets += [r['id'] for r in rows if not good(r) and r['id'] not in targets]
    if not targets:
        print('nothing to refit')
        return
    device = 'cuda' if modalfit.torch.cuda.is_available() else 'cpu'
    changed = 0
    for mid in targets:
        row = byid.get(mid)
        if row is None or row.get('param') != 'midi':
            print('  %-10s not a note record, skipped' % mid)
            continue
        path = os.path.join(a.fitdir, mid + '.mmr')
        with open(path) as f:
            txt = f.read()
        if re.search(r'^(shaper|take|body [1-9])', txt, re.M):
            print('  %-10s carries a shaper, takes or a body: this tool writes plain modes, skipped' % mid)
            continue
        modes, src, secs, tail = read_record(path)
        if not src or not os.path.exists(src):
            print('  %-10s source missing (%s), skipped' % (mid, src))
            continue
        midi = float(row['value'])
        f0 = 440.0 * 2 ** ((midi - 69) / 12)
        dyn = row.get('dynamic', '')
        # the same dynamic's nearest good records either side
        same = [r for r in rows if r.get('dynamic', '') == dyn and r['id'] != mid and good(r)]
        below = max((r for r in same if float(r['value']) < midi), key=lambda r: float(r['value']), default=None)
        above = min((r for r in same if float(r['value']) > midi), key=lambda r: float(r['value']), default=None)
        secs = secs or 4.0
        x, sr = modalfit.load(src, secs, a.onset)
        cap = a.t60_cap * secs
        old = score(float(row['loss']), float(row['decay_ratio']))
        best = None
        for nb in (below, above):
            if nb is None:
                continue
            nm, _, _, _ = read_record(os.path.join(a.fitdir, nb['id'] + '.mmr'))
            k = f0 / (440.0 * 2 ** ((float(nb['value']) - 69) / 12))
            init = [(hz * k, zeta * 2 * math.pi * hz * k, g, ph) for hz, zeta, ph, g in nm if hz * k < 0.45 * sr]
            if not init:
                continue
            res = fit_from(x, sr, f0, init, a.steps, polish, cap, device)
            s = score(res[5], res[7])
            print('  %-10s midi %3d %-3s from %-10s (midi %3d): %2d modes loss %.3f decay x%.2f score %.3f (was %.3f)'
                  % (mid, midi, dyn, nb['id'], float(nb['value']), len(res[0]), res[5], res[7], s, old))
            if len(res[0]) >= 1 and (best is None or s < best[0]):
                best = (s, nb['id'], res)
        if best is None or best[0] > 0.95 * old:
            print('  %-10s kept as it was' % mid)
            continue
        s, seed, (fr, r, amp, ph, y, loss, ex, dr) = best
        changed += 1
        if a.dry:
            print('  %-10s would take the fit from %s: score %.3f -> %.3f' % (mid, seed, old, s))
            continue
        os.makedirs(os.path.join(a.fitdir, 'prev'), exist_ok=True)
        shutil.copy2(path, os.path.join(a.fitdir, 'prev', mid + '.mmr'))
        order = np.argsort(fr)
        with open(path, 'w') as o:
            o.write('# modalfit record via refitn (seeded from %s): %s, %.2f s analysed, loss %.4f, excess %.2f dB, decay ratio %.2f\n'
                    % (seed, row.get('source', ''), secs, loss, ex, dr))
            o.write('source %s\nfitted 1\nloss %.5f\nexcess_db %.3f\ndecay_ratio %.3f\npositions 12\nmodes %d\nbody 0\n' % (src, loss, ex, dr, len(order)))
            for kk, i in enumerate(order):
                w = 2 * math.pi * fr[i]
                o.write('mode %d hz %.6f zeta %.9g phase %.5f gains %s\n' % (kk, fr[i], r[i] / w, ph[i], ' '.join('%.9g' % amp[i] for _ in range(12))))
        sf.write(os.path.join(a.fitdir, mid + '-resynth.wav'), np.clip(y / (np.max(np.abs(y)) or 1) * 0.5, -1, 1), sr)
        row['modes'], row['loss'], row['excess_db'], row['decay_ratio'] = str(len(order)), '%.4f' % loss, '%.3f' % ex, '%.3f' % dr
        print('  %-10s refitted from %s: score %.3f -> %.3f' % (mid, seed, old, s))
    if changed and not a.dry:
        tmp = os.path.join(a.fitdir, 'fits.new.tsv')
        with open(tmp, 'w') as f:
            f.write('\t'.join(head) + '\n')
            for r in rows:
                f.write('\t'.join(r.get(h, '') for h in head) + '\n')
        os.replace(tmp, os.path.join(a.fitdir, 'fits.tsv'))
    print('  %d of %d refitted%s' % (changed, len(targets), ' (dry run)' if a.dry else ''))


if __name__ == '__main__':
    main()
