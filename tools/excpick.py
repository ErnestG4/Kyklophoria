#!/usr/bin/env python3
"""excpick.py <out.tsv> <results.tsv> <level.txt> [<results.tsv> <level.txt> ...]

A keyboard's trained hammers chosen note by note, level first. Each pair is
excfit's results and exclevel's report on the world baked from them (the
smoothed bake, the raw one, ...). A note takes its hammer from the first pair
in which exclevel passed it; a note no pair passed gets no hammer, and plays
its recorded attack (format 9: a point without a trained exciter).

Why: retrained five times overnight on 29 September, the Iowa grand's level
gate kept failing on a handful of keys — different keys smoothed and raw —
while the rest passed. A card is offered only once it is level-safe, so the
safe keys are trained and the others keep what they had. Bake the output with
excbake and gate the whole world again with exclevel before offering it.

Standard library only.
"""
import sys


def failed(level_path):
    bad = set()
    for line in open(level_path):
        f = line.split()
        if len(f) >= 3 and f[0] == 'FAIL' and f[1] == 'note':
            bad.add(round(float(f[2])))
    return bad


def main():
    if len(sys.argv) < 4 or len(sys.argv) % 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    pairs = [(sys.argv[i], sys.argv[i + 1]) for i in range(2, len(sys.argv), 2)]
    chosen, source = {}, {}
    for n, (res, lev) in enumerate(pairs):
        bad = failed(lev)
        for line in open(res):
            f = line.split('\t')
            if len(f) < 14:
                continue
            m = round(float(f[0]))
            if m in chosen or m in bad:
                continue
            chosen[m] = line
            source[m] = n
    every = set()
    for res, _ in pairs:
        for line in open(res):
            f = line.split('\t')
            if len(f) >= 14:
                every.add(round(float(f[0])))
    with open(out, 'w') as w:
        for m in sorted(chosen):
            w.write(chosen[m] if chosen[m].endswith('\n') else chosen[m] + '\n')
    none = sorted(every - set(chosen))
    counts = [sum(1 for m in source if source[m] == n) for n in range(len(pairs))]
    print('%s: %d notes trained (%s), %d on their recorded attack: %s' %
          (out, len(chosen), ', '.join('%d from %s' % (c, pairs[n][0]) for n, c in enumerate(counts)),
           len(none), ' '.join(str(m) for m in none) or '-'))


if __name__ == '__main__':
    main()
