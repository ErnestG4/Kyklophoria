#!/usr/bin/env python3
"""armloops.py <object.o> <function> [...] — the loops in a function, as the
Cortex-M7 compiler emitted them: every backward branch is a loop, and its body
is the instructions from the branch target to the branch. The innermost loops
of a modal voice are its per-mode work, so their bodies are its instructions a
mode a sample — the number `make armcost` reports for the coupled exciters,
whose loop is sample-outer (docs/exciters.md, cost). Instructions, not cycles:
the M7 dual-issues some pairs and stalls on some FP chains, so a count is the
right order, not the exact figure.

Standard library only, as the rest of the suite's Python.
"""
import re
import subprocess
import sys


def disasm(obj):
    out = subprocess.run(['arm-none-eabi-objdump', '-d', '-C', '--no-show-raw-insn', obj],
                         capture_output=True, text=True, check=True).stdout
    funcs, cur = {}, None
    for line in out.splitlines():
        m = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
        if m:
            cur = m.group(1)
            funcs[cur] = []
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$', line)
        if m and cur is not None:
            funcs[cur].append((int(m.group(1), 16), m.group(2), m.group(3)))
    return funcs


def loops(body):
    """(start, end, instructions) for every backward branch, innermost first"""
    addrs = [a for a, _, _ in body]
    found = []
    for i, (a, op, args) in enumerate(body):
        if not op.startswith('b') or op.startswith(('bl', 'bx', 'bic')):
            continue
        m = re.match(r'([0-9a-f]+)\b', args)
        if not m:
            continue
        t = int(m.group(1), 16)
        if t <= a and t in addrs:
            j = addrs.index(t)
            found.append((t, a, i - j + 1))
    # innermost first: a loop containing no other loop
    inner = [l for l in found if not any(o != l and l[0] <= o[0] and o[1] <= l[1] for o in found)]
    return sorted(found, key=lambda l: l[2]), inner


def main():
    obj, names = sys.argv[1], sys.argv[2:]
    funcs = disasm(obj)
    for n in names:
        body = funcs.get(n)
        if body is None:                          # a demangled name: the first function that starts with it
            hit = next((k for k in funcs if k.startswith(n)), None)
            body = funcs.get(hit) if hit else None
        if body is None:
            print(f'    {n:14s} not found')
            continue
        _, inner = loops(body)
        fp = lambda l: sum(1 for a, op, _ in body if l[0] <= a <= l[1] and op.startswith('v'))
        desc = ', '.join(f'{l[2]} ({fp(l)} FP)' for l in sorted(inner, key=lambda l: l[0]))
        print(f'    {n[:14]:14s} {len(body):4d} in all; innermost loops, instructions a pass: {desc or "none"}')


if __name__ == '__main__':
    main()
