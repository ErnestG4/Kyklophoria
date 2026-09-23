"""armcount.py — instructions in each function of an objdump, and in each of
its loops' bodies (a backward branch and its target), so a per-sample and a
per-band / per-section cost can be read off. Used by `make -C proto armcost`."""
import re
import sys

text = open(sys.argv[1]).read().splitlines()
funcs = {}
cur = None
for l in text:
    m = re.match(r'^[0-9a-f]+ <(\w+)>:', l)
    if m:
        cur = m.group(1); funcs[cur] = []; continue
    m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$', l)
    if m and cur:
        funcs[cur].append((int(m.group(1), 16), m.group(2), m.group(3)))
for f, ins in funcs.items():
    if not f.startswith('c_'):
        continue
    print('  %-10s %4d instructions' % (f, len(ins)))
    addrs = [a for a, _, _ in ins]
    for j, (a, op, arg) in enumerate(ins):
        if op.startswith('b') and not op.startswith('bl') and not op.startswith('bx') and not op.startswith('bic'):
            m = re.match(r'([0-9a-f]+)', arg)
            if not m:
                continue
            t = int(m.group(1), 16)
            if t < a and t in addrs:
                body = ins[addrs.index(t):j + 1]
                fp = sum(1 for _, o, _ in body if o.startswith('v'))
                print('      loop %x..%x: %3d instructions (%d VFP)' % (t, a, len(body), fp))
