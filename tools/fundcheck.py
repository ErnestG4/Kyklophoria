import os, sys, math
sets = sys.argv[1:] or ['piano','guitar','bass','violin','viola','banjo','mandolin','wurli','ep','ep-vel','tine-vel','reed-vel']
for s in sets:
    d = 'out/fit/' + s
    if not os.path.exists(d + '/fits.tsv'): continue
    rows = [l.split('\t') for l in open(d + '/fits.tsv').read().splitlines()]
    hdr = rows[0]; rows = rows[1:]
    ip = hdr.index('param'); iv = hdr.index('value')
    bad = []
    for r in rows:
        if r[ip] != 'midi': continue
        midi = float(r[iv]); f = 440 * 2 ** ((midi - 69) / 12)
        modes = []
        for w in (l.split() for l in open(d + '/' + r[0] + '.mmr')):
            if w and w[0] == 'mode':
                hz = float(w[3]); g = max(abs(float(x)) for x in w[w.index('gains') + 1:])
                modes.append((hz, g))
        if not modes: continue
        loud = max(g for _, g in modes)
        at = [g for hz, g in modes if abs(hz / f - 1) < 0.04]
        rel = max(at) / loud if at else 0
        if rel < 0.01: bad.append('%s midi %d (%.0f Hz): fundamental %s' % (r[0], midi, f, ('%.1f dB under the loudest' % (20*math.log10(rel))) if rel > 0 else 'ABSENT'))
    print('%s: %d note records, %d without a fundamental' % (s, sum(1 for r in rows if r[ip]=='midi'), len(bad)))
    for b in bad: print('   ', b)
