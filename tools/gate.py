#!/usr/bin/env python3
"""gate.py <recdir> [...] — does a fitted set deserve to be a world?

export.py's only check was "fewer than 8 live modes", and it let through a
viola E3 fitted from a BOWED note: loss 2.03, ringing 13.6x longer than the
recording, because a sustained bow is not a decaying strike and nothing
asked. Over the arco string sets 162 of 210 points were past a loss of 1.5
and most rang over three times their recording; over the per-string guitar
(plucked, the same pipeline) it was two of 97. The numbers were in every
fits.tsv all along. This reads them.

A point fails when, in its own manifest row:
  loss > --loss (1.5)             the fit does not match the recording
  decay_ratio > --ring (3)        it rings N times longer than the recording
and is THIN (said, not failed) when modes < --modes (8): a marimba bar or an
EP tine at 2 kHz honestly has few partials under Nyquist, so thin alone is
the register talking; thin AND a bad loss is the fit giving up.
A set fails when more than --share (10%) of its points fail. Shaped records
(the pickup fits: ep-vel, tine-vel, reed-vel) carry other columns and a
different notion of modes, and are reported as not graded rather than
failed. Exit status 1 if any set fails, so a script can stop on it.

    python3 tools/gate.py out/fit/viola-sulc          # one set, every bad point
    python3 tools/gate.py --brief out/fit/*/          # one line a set
"""
import argparse, os, sys


def grade(recdir, a):
    """(points, failures [(id, midi, reasons)], graded?) for one set"""
    path = os.path.join(recdir, 'fits.tsv')
    rows = open(path).read().splitlines()
    if len(rows) < 2:
        return 0, [], None                       # being written by a running fitset
    head = rows[0].split('\t')
    col = {k: i for i, k in enumerate(head)}
    if 'decay_ratio' not in col or 'loss' not in col:
        return len(rows) - 1, [], False
    bad = []
    for r in rows[1:]:
        f = r.split('\t')
        why = []
        loss, ring, modes = float(f[col['loss']]), float(f[col['decay_ratio']]), int(f[col['modes']])
        if loss > a.loss:
            why.append('loss %.2f' % loss)
        if ring > a.ring:
            why.append('rings %.1fx' % ring)
        thin = modes < a.modes
        if why or thin:
            bad.append((f[0], f[col['value']], ', '.join(why + (['%d modes' % modes] if thin else [])), bool(why)))
    return len(rows) - 1, bad, True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdirs', nargs='+')
    ap.add_argument('--loss', type=float, default=1.5)
    ap.add_argument('--ring', type=float, default=3.0)
    ap.add_argument('--modes', type=int, default=8)
    ap.add_argument('--share', type=float, default=0.10)
    ap.add_argument('--brief', action='store_true')
    a = ap.parse_args()
    failed = False
    for d in a.recdirs:
        d = d.rstrip('/')
        if not os.path.exists(os.path.join(d, 'fits.tsv')):
            continue
        n, bad, graded = grade(d, a)
        if graded is None:
            print('  %-22s        being fitted' % os.path.basename(d))
            continue
        if not graded:
            print('  %-22s %4d points   not graded (shaped records)' % (os.path.basename(d), n))
            continue
        nbad = sum(1 for b in bad if b[3])
        ok = nbad <= a.share * n
        failed |= not ok
        print('  %-22s %4d points %4d bad %4d thin   %s' % (os.path.basename(d), n, nbad, len(bad) - nbad, 'pass' if ok else 'FAIL'))
        if not a.brief:
            for rid, midi, why, isbad in bad:
                print('      %-10s midi %-4s %s%s' % (rid, midi, why, '' if isbad else '  (thin only)'))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
