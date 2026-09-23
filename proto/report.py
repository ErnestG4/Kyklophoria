"""report.py — bench/threeway/results.json as the tables in proto/README.md.

    ~/fmexplorer/bin/python proto/report.py [--md]
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RP = os.path.join(HERE, '..', 'bench', 'threeway', 'results.json')


def f(v, fmt='%.1f'):
    if v is None or (isinstance(v, float) and (math.isnan(v) or math.isinf(v))):
        return '-'
    return fmt % v


def win(r, j, key):
    w = r['windows'][j] if j < len(r['windows']) else None
    return None if not w else w[key]


def row(r):
    """the numbers a variant gets, in the README's column order"""
    e = r['early']; env = r['env']; h = r['high']
    return [f(e[0]), f(e[1]), f(e[2]),
            f(env[0][1]), f(env[1][1]),
            f(r['seam'][0], '%+.1f'), f(r['seam'][1], '%+.1f'),
            f(h[1], '%+.1f'), f(h[2], '%+.1f'), f(h[3], '%+.1f'),
            f(win(r, 0, 'static'), '%+.0f'), f(win(r, 1, 'static'), '%+.0f'), f(win(r, 2, 'static'), '%+.0f'),
            f(win(r, 2, 'missing'), '%d'), f(win(r, 3, 'missing'), '%d')]


HEAD = ['early 0-50', '50-150', '150-400', 'env |max| 0-.5s', '.5-2s', 'seam', 'seam >top',
        'top .05-.3', '.3-1', '1-2s', 'static 0-50', '50-300', '.3-1s', 'missing .3-1', '1-2s']


def show(res, md=False):
    sets = []
    for r in res['notes']:
        if r['set'] not in sets:
            sets.append(r['set'])
    for s in sets:
        rs = sorted([r for r in res['notes'] if r['set'] == s], key=lambda r: r['note'])
        print('\n## %s' % s)
        if md:
            print('| note | | ' + ' | '.join(HEAD) + ' |')
            print('|' + '---|' * (len(HEAD) + 2))
        else:
            print('%-6s %-2s ' % ('note', '') + ' '.join('%11s' % h[:11] for h in HEAD))
        for r in rs:
            for v in 'abc':
                cells = row(r['variants'][v])
                name = r['name'] if v == 'a' else ''
                if md:
                    print('| %s | %s | ' % (name, v) + ' | '.join(cells) + ' |')
                else:
                    print('%-6s %-2s ' % (name, v) + ' '.join('%11s' % c for c in cells))
        # medians over the notes
        for v in 'abc':
            cols = np.array([[float(c) if c != '-' else np.nan for c in row(r['variants'][v])] for r in rs])
            med = np.nanmedian(cols, axis=0) if len(cols) else []
            cells = [('%+.1f' % m if j in (5, 6, 7, 8, 9) else '%.1f' % m) if not np.isnan(m) else '-' for j, m in enumerate(med)]
            if md:
                print('| median | %s | ' % v + ' | '.join(cells) + ' |')
            else:
                print('%-6s %-2s ' % ('median', v) + ' '.join('%11s' % c for c in cells))
        # the waveguide
        print('\n  waveguide (c):')
        for r in rs:
            i = r['info']; c = r['variants']['c']
            d = i.get('disp') or {}
            dbl = c.get('double')
            print('    %-5s top mode %6.0f Hz, crossover %6s, f0 %7s B %8s (bank %8s), band to %6s Hz: %2s+%-2s sections, j %3s, series err %5s spacing; '
                  'input %s, tilt %s; waveguide %s dB re modes; doubling worst %s dB (beat %s dB); noise %s dB re modes' % (
                      r['name'], r['ftop'], f(i.get('fc'), '%.0f'), f(i.get('f0'), '%.2f'), f(i.get('B'), '%.2e'), f(i.get('B_bank'), '%.2e'),
                      f(i.get('wg_top'), '%.0f'), d.get('M1', '-'), d.get('M2', '-'), d.get('j', '-'), f(d.get('err_cpp'), '%.2f'),
                      f(i.get('gin'), '%.3g'), f(i.get('tilt'), '%.0f'), f(c.get('wg_rms_db')),
                      f(dbl[0] if dbl else None), f(dbl[2] if dbl else None, '%.3f'), f(c.get('noise_rms_db'))))
            if i.get('wg'):
                print('          %s' % i['wg'])
    for p in res.get('passages', []):
        print('\n## %s passage (4 voices, 28 strikes at 4 Hz, velocity 0.8, around %s)' % (p['set'], p['mid']))
        for k, v in p['pops'].items():
            print('    %-18s pops: strikes %d, over 30 dB %d, worst %.1f dB, median %.1f dB; peak %.3f' % (k, v['n'], v['over30'], v['worst'], v['median'], p['peak'][k]))
        if p.get('kykdesk_a'):
            print('    (a) against kykdesk (the engine itself): %s' % p['kykdesk_a'])
        if p.get('missing'):
            print('    points without params: %s' % p['missing'])


if __name__ == '__main__':
    show(json.load(open(RP)), md='--md' in sys.argv)
