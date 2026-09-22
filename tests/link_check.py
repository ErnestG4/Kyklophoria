#!/usr/bin/env python3
"""link_check — the HostLink extension end to end, stdlib only (no node).

Over stdio against `kykdesk --serve`: HELLO, the descriptor, GET_SPACE_INFO,
GET_TELEMETRY with spectrum + frame (field sanity and size), GET_CELL (and
its refusal), GET_STATS, SET_CONTROL reflected in the next telemetry (f0,
control frame, spread → stereo, posL ≠ posR), ACTION render_div, an unknown
action refused, a bad-CRC frame answered with ERR. Then through
tools/bridge/bridge.py: HTTP serves index.html, the WebSocket upgrade
answers, and HELLO + telemetry round-trip over it.

Run from the repo root after `make host`: python3 tests/link_check.py
"""
import math
import base64, hashlib, os, socket, struct, subprocess, sys, time, zlib, http.client

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KYKDESK = os.path.join(ROOT, 'build', 'host', 'kykdesk')
checks = fails = 0


def check(cond, what):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print(f'  FAIL {what}')


def crc32(b): return zlib.crc32(b) & 0xffffffff


def cobs_encode(d):
    out = bytearray(); idx = 0
    while True:
        try: z = d.index(0, idx)
        except ValueError: z = len(d)
        chunk = d[idx:z]
        while len(chunk) >= 254: out.append(0xff); out += chunk[:254]; chunk = chunk[254:]
        out.append(len(chunk) + 1); out += chunk
        if z == len(d): break
        idx = z + 1
    return bytes(out)


def cobs_decode(b):
    out = bytearray(); i = 0
    while i < len(b):
        c = b[i]; i += 1
        out += b[i:i + c - 1]; i += c - 1
        if c != 0xff and i < len(b): out.append(0)
    return bytes(out)


def frame(t, seq, body=b'', corrupt=False):
    h = struct.pack('<BBHH', 1, t, seq, len(body)) + body
    c = crc32(h) ^ (0xdeadbeef if corrupt else 0)
    return cobs_encode(h + struct.pack('<I', c)) + b'\x00'


def parse(chunk):
    d = cobs_decode(chunk)
    n = struct.unpack('<H', d[4:6])[0]
    ok = d[0] == 1 and crc32(d[:6 + n]) == struct.unpack('<I', d[6 + n:10 + n])[0]
    return d[1], struct.unpack('<H', d[2:4])[0], d[6:6 + n], ok


class Stdio:
    def __init__(self, args):
        self.p = subprocess.Popen([KYKDESK, '--serve'] + args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.seq = 0

    def request(self, t, body=b'', corrupt=False):
        self.seq += 1
        self.p.stdin.write(frame(t, self.seq, body, corrupt)); self.p.stdin.flush()
        acc = bytearray()
        while True:
            b = self.p.stdout.read(1)
            if not b: raise EOFError('kykdesk closed')
            if b == b'\x00': break
            acc += b
        return parse(bytes(acc))

    def close(self):
        self.p.stdin.close(); self.p.wait(timeout=3)


def telemetry(link, flags):
    ty, seq, body, ok = link.request(0x60, bytes([flags]))
    check(ok and ty == 0xE0 and body[0] == 0, 'telemetry reply')
    b = body[1:]
    t = {}
    t['block'], t['f0'] = struct.unpack('<If', b[:8])
    t['n'], t['k'], t['kcut'], t['p'], t['planes'], t['flags'], t['stereo'], t['spread_plane'] = b[8:16]
    t['spread'] = struct.unpack('<f', b[16:20])[0]
    n, at = t['n'], 20
    for name in ('ctl', 'centre', 'posL', 'posR'):
        t[name] = struct.unpack(f'<{n}f', b[at:at + 4 * n]); at += 4 * n
    t['angle'] = struct.unpack(f"<{t['planes']}f", b[at:at + 4 * t['planes']]); at += 4 * t['planes']
    t['payload'] = struct.unpack(f"<{t['p']}f", b[at:at + 4 * t['p']]); at += 4 * t['p']
    if flags & 1: t['mags'] = b[at:at + t['k']]; at += t['k']
    if flags & 2: t['frame'] = b[at:at + 256]; at += 256
    if flags & 4:
        t['kep_run'], t['kep_plane'] = b[at], b[at+1]
        (t['kep_x'], t['kep_y'], t['kep_rush'],
         t['couple'], t['lock']) = struct.unpack('<5f', b[at+2:at+22])
        t['sharp'] = b[at+22] / 255.0; at += 23
        # the company: body 0 is kep_x/kep_y above, so only the perturbers are
        # here, and only as many as are running
        t['bodies'] = b[at] if len(b) > at else 1
        at += 1 if len(b) > at else 0
        t['kep_xy'] = []
        for _ in range(1, t['bodies']):
            t['kep_xy'].append(struct.unpack('<2f', b[at:at+8])); at += 8
        t['page'] = b[at] if len(b) > at else 0
        at += 1 if len(b) > at else 0
        t['morph'] = (b[at] / 255.0) if len(b) > at else 0.0
        t['morph_world'] = b[at + 1] if len(b) > at + 1 else 0xFF
        at += 2 if len(b) > at + 1 else 0
        t['mute'] = struct.unpack('<H', b[at:at+2])[0] if len(b) > at + 1 else 0
        at += 2 if len(b) > at + 1 else 0
        t['aimed'] = (b[at] != 0) if len(b) > at else False
        at += 1 if len(b) > at else 0
        t['pots'] = list(b[at:at+6]) if len(b) >= at + 6 else []
        at += 6 if len(b) >= at + 6 else 0
        # what the live page's six knobs are worth, which is not where the
        # pots are sitting: the panel catches. The validity byte separates
        # "six knobs at zero" from "no pager to ask".
        if len(b) > at:
            valid = b[at] != 0
            t['knobs'] = list(b[at+1:at+7]) if valid and len(b) >= at + 7 else None
            at += 7 if len(b) >= at + 7 else 1
        else:
            t['knobs'] = None
        # which world is live; 0xFF is a user world, which has no index. None
        # from firmware that predates it, which is not the same as 0xFF.
        t['world'] = b[at] if len(b) > at else None
        at += 1 if len(b) > at else 0
        # where the world tour has got to: stops, which one is live, how far
        # towards the next. Zero stops means there is no tour.
        if len(b) >= at + 3:
            t['tour'] = (b[at], b[at + 1], b[at + 2] / 255.0); at += 3
        else:
            t['tour'] = None
        # what kind of world is live (World::Kind; 11 a resonator), so a
        # host does not read the axis count as the kind
        t['kind'] = b[at] if len(b) > at else None
        at += 1 if len(b) > at else 0
    t['size'] = len(b)
    check(at == len(b), f'telemetry body consumed exactly ({at} of {len(b)})')
    return t


def stdio_tests():
    print('stdio')
    link = Stdio(['--gen', '--seed', '1', '--script', os.path.join(ROOT, 'tests/scripts/m1_rotate.txt'), '--loop'])
    ty, seq, b, ok = link.request(0x01)
    check(ok and ty == 0x81 and b[0] == 0, 'HELLO ok')
    dlen, dcrc = struct.unpack('<II', b[25:33]); maxbody = struct.unpack('<H', b[33:35])[0]
    at = 37; strs = []
    for _ in range(5):
        ln = b[at]; strs.append(b[at + 1:at + 1 + ln].decode()); at += 1 + ln
    check(strs[0] == 'kyk' and strs[1] == 'Kyklophoria', f'HELLO identity {strs}')
    check(maxbody == 1024, f'max_body {maxbody}')
    d = b''; off = 0
    while off < dlen:
        ty, seq, r, ok = link.request(0x02, struct.pack('<IH', off, 1000))
        n = struct.unpack('<H', r[5:7])[0]; d += r[7:7 + n]; off += n
    check(crc32(d) == dcrc and len(d) == dlen, 'descriptor CRC/length')
    check(b'"kyk":{"ext":' in d and b'"iomap":[' in d, 'descriptor carries the kyk block and iomap')
    ty, seq, r, ok = link.request(0x61)
    check(ok and r[0] == 0 and len(r) == 1 + 64 + 6, f'space info size {len(r)}')
    check(r[1:5] == b'KYK1' and r[7] == 4 and r[9] == 64 and r[10] == 8 and r[11] == 4, 'space header fields')
    time.sleep(0.2)
    t = telemetry(link, 3)
    check((t['n'], t['k'], t['p'], t['planes']) == (4, 64, 8, 6), 'telemetry dims')
    t7 = telemetry(link, 7)
    # 23 for the original block, 1 for the body count, 8 per perturber, then
    # page, morph+target, mute, aimed, six pots, the knob values behind a
    # validity byte, the live world index, and three for the world tour
    want = 460 + 23 + 1 + 8 * (t7['bodies'] - 1) + 1 + 2 + 2 + 1 + 6 + 7 + 1 + 3 + 1
    check(t7['size'] == want, f"motion block appends {want - 460} bytes (size {t7['size']}, {t7['bodies']} bodies)")
    check(t7['kind'] == 1, f"and ends with the world's kind (a lattice = 1): {t7['kind']}")
    # Which world is playing, in every frame. Without it a host only learns
    # this by asking for the list, and nothing asks unprompted — so a world
    # changed from the module's own panel stayed invisible.
    check(t7['world'] is not None and t7['world'] <= 0xFF, f"telemetry names the live world ({t7['world']})")
    check(t7['knobs'] is None, 'and says it has no knob values rather than sending six zeroes')
    check('world' not in t, 'and it rides in the motion block, not the fixed part')
    check(t7['kep_plane'] < 6 and 0.0 <= t7['kep_rush'] <= 1.0, 'kepler fields sane')
    check(t7['couple'] >= 0.0 and 0.0 <= t7['lock'] <= 1.0, 'coupling fields sane')
    check(t['size'] == 460, f"telemetry size {t['size']}")
    check(t['kcut'] == 64 and 100 < t['f0'] < 120, f"f0 {t['f0']} kcut {t['kcut']}")
    check(max(t['mags']) > 200 and any(v != 0 for v in t['frame']), 'spectrum and frame present')
    ty, seq, r, ok = link.request(0x62, struct.pack('<I', 0))
    check(ok and r[0] == 0 and r[1] == 64 and r[2] == 8 and len(r) == 3 + 64 + 32, 'GET_CELL 0')
    ty, seq, r, ok = link.request(0x62, struct.pack('<I', 999))
    check(r[0] == 2, 'GET_CELL out of range → BAD_ARGS')
    ty, seq, r, ok = link.request(0x63)
    last, mx, avg = struct.unpack('<III', r[1:13]); budget = struct.unpack('<I', r[18:22])[0]
    check(r[0] == 0 and budget == 500000 and 0 < last < budget, f'stats last {last} budget {budget}')
    check(len(r) == 30, f'stats ends with the engine peak and the At() rate ({len(r)} bytes)')
    body = struct.pack('<fB', 440.0, 4) + struct.pack('<4f', 0.1, 0.2, 0.3, 0.4) + bytes([6]) + struct.pack('<6f', 0.05, 0, 0, 0, 0, 0) + struct.pack('<f', 0.02)
    ty, seq, r, ok = link.request(0x6E, body)
    check(r[0] == 0, 'SET_CONTROL accepted')
    time.sleep(0.05)
    t = telemetry(link, 0)
    check(abs(t['f0'] - 440) < 1e-3 and t['stereo'] == 1 and abs(t['spread'] - 0.02) < 1e-6, 'SET_CONTROL reflected: f0, stereo, spread')
    check(all(abs(a - b) < 1e-6 for a, b in zip(t['ctl'], (0.1, 0.2, 0.3, 0.4))), 'control frame reflected')
    check(abs(t['angle'][0] - 0.05) < 1e-6, 'angle reflected')
    check(t['posL'] != t['posR'], 'stereo: posL ≠ posR')
    ty, seq, r, ok = link.request(0x64, bytes([3, 2]))
    check(r[0] == 0, 'ACTION render_div')
    ty, seq, r, ok = link.request(0x63)
    check(r[17] == 2, f'render_div visible in stats ({r[17]})')
    ty, seq, r, ok = link.request(0x64, bytes([77]))
    check(r[0] == 1, 'unknown action → UNSUPPORTED')
    # the world list, the formula, and switching between them
    ty, seq, r, ok = link.request(0x65)
    check(ok and r[0] == 0 and r[1] >= 2, f'GET_WORLDS returns a list ({r[1] if len(r)>1 else "?"})')
    n_worlds, cur, at = r[1], r[2], 5      # ext 4: total, current, start, sent
    names = []
    for _ in range(r[4]):                  # only this page
        kind = r[at]; at += 1
        ln = r[at]; nm = r[at+1:at+1+ln].decode(); at += 1 + ln
        ln2 = r[at]; at += 1 + ln2
        names.append((nm, kind))
    check(at == len(r), f'world list consumed exactly ({at} of {len(r)})')
    check(any(k == 2 for _, k in names), 'at least one analytic world')
    check(any(k == 1 for _, k in names), 'at least one lattice world')
    analytic = next(i for i, (_, k) in enumerate(names) if k == 2)
    lattice = next(i for i, (_, k) in enumerate(names) if k == 1)

    # the analytic world's formula, chunked
    blob, off, total = b'', 0, None
    while total is None or off < total:
        ty, seq, r, ok = link.request(0x66, struct.pack('<BIH', analytic, off, 900))
        check(r[0] == 0, 'GET_BASIS ok')
        total = struct.unpack('<I', r[1:5])[0]
        n = struct.unpack('<H', r[9:11])[0]
        if n == 0: break
        blob += r[11:11+n]; off += n
    bn, bk = blob[0], blob[1]
    extent, floor_ = struct.unpack('<ff', blob[2:10])
    check(len(blob) == total, f'basis fully fetched ({len(blob)} of {total})')
    check(total == 10 + 4*bk*(bn+1), f'basis size matches n={bn} k={bk}')
    check(bn == 4 and bk == 64 and 0.5 < extent < 10, f'basis header n={bn} k={bk} extent={extent:.2f}')
    check(total < 2000, f'the whole world is {total} bytes')

    # a lattice world refuses to hand out a formula, because it has none
    ty, seq, r, ok = link.request(0x66, struct.pack('<BIH', lattice, 0, 900))
    check(r[0] == 1, 'a tabulated world has no basis to give')

    # switch, and confirm the list agrees
    ty, seq, r, ok = link.request(0x64, bytes([4, lattice]))
    check(r[0] == 0, 'ACTION select world')
    time.sleep(0.1)
    ty, seq, r, ok = link.request(0x65)
    check(r[2] == lattice, f'current world is now {lattice} (got {r[2]})')
    ty, seq, r, ok = link.request(0x61)
    check(r[0] == 0, 'the tabulated world reports a lattice header')
    ty, seq, r, ok = link.request(0x64, bytes([4, analytic]))
    check(r[0] == 0, 'switch back to analytic')
    time.sleep(0.1)
    t = telemetry(link, 0)
    check(t['n'] == 4 and t['k'] == 64, 'telemetry still sane after two world switches')
    ty, seq, r, ok = link.request(0x64, bytes([4, 99]))
    check(r[0] == 2, 'an out-of-range world is refused')

    # ── a user world, over the real transport ────────────────────────────
    # Two nodes: a saw and a square, placed at opposite corners of axis 0.
    # Sent chunked, then the module must actually be playing it.
    n, k, count = 4, 64, 2
    nodes = []
    for which in range(count):
        pos = [0.1 if which == 0 else 0.9, 0.5, 0.5, 0.5]
        mags = []
        for h in range(1, k + 1):
            if which == 0: mags.append(1.0 / h)                       # saw
            else:          mags.append((4.0 / (math.pi * h)) if h % 2 else 0.0)  # square
        nodes.append((pos, mags))
    blob = bytearray(32 + count * 4 * (n + k))
    struct.pack_into('<IHBBBB', blob, 0, 0x574B594B, 1, n, k, count, 1)
    struct.pack_into('<f', blob, 10, 0.26)
    blob[16:16 + 4] = b'test'
    at = 32
    for pos, mags in nodes:
        for a in pos: struct.pack_into('<f', blob, at, a); at += 4
        for m in mags: struct.pack_into('<f', blob, at, m); at += 4
    blob = bytes(blob)

    chunk = maxbody - 24
    sent = 0
    while sent < len(blob):
        take = min(chunk, len(blob) - sent)
        req = struct.pack('<II', len(blob), sent) + blob[sent:sent + take]
        ty, seq, r, ok = link.request(0x67, req)
        if r[0] != 0: break
        sent += take
    check(sent == len(blob) and r[0] == 0,
          f'a {len(blob)}-byte user world transfers in {-(-len(blob)//chunk)} chunks')

    t = telemetry(link, 7)
    check(t['n'] == n and t['k'] == k, 'the module reports the new geometry')

    # out-of-order and oversized chunks must be refused, not patched in
    ty, seq, r, ok = link.request(0x67, struct.pack('<II', len(blob), 999) + b'\x00' * 8)
    check(r[0] == 2, 'an out-of-order chunk is refused')
    ty, seq, r, ok = link.request(0x67, struct.pack('<II', 0xFFFFFFFF, 0) + b'\x00' * 8)
    check(r[0] == 2, 'an impossible total is refused')
    ty, seq, r, ok = link.request(0x67, struct.pack('<II', 64, 0) + b'\x00' * 64)
    check(r[0] == 1, 'a well-formed transfer of nonsense is refused at parse')

    # and the module still plays after all that
    t = telemetry(link, 7)
    check(t['n'] >= 2 and t['k'] >= 1, 'the module is still alive and sane afterwards')

    # ── cross-world morph, over the link ─────────────────────────────────
    ty, seq, r, ok = link.request(0x64, bytes([5, analytic]))
    check(r[0] == 0, 'a morph target can be selected')
    t = telemetry(link, 7)
    check(t['n'] == 4, 'the module is fine with a morph target set')
    ty, seq, r, ok = link.request(0x64, bytes([5, 0xFF]))
    check(r[0] == 0, 'and cleared')
    ty, seq, r, ok = link.request(0x64, bytes([5, 99]))
    check(r[0] == 2, 'an out-of-range morph target is refused')

    ty, seq, r, ok = link.request(0x64, bytes([10]))
    check(r[0] == 1, 'aiming with no morph target is refused')
    ty, seq, r, ok = link.request(0x64, bytes([5, analytic]))
    check(r[0] == 0, 'a target can be set again')
    ty, seq, r, ok = link.request(0x64, bytes([10]))
    check(r[0] == 0, 'and then the morph can be aimed')
    t = telemetry(link, 7)
    check(t['n'] == 4 and t['k'] == 64, 'the module is sane after aiming')
    ty, seq, r, ok = link.request(0x64, bytes([5, 0xFF]))

    # ── the world tour, over the link ────────────────────────────────────
    # The stdlib half of the suite covers the wire shape; the sound and the
    # walking of the sequence are in web/selftest.mjs, which has a spectrum to
    # look at. What matters here is that the sequence arrives whole and that a
    # malformed one is refused rather than half-applied.
    ty, seq, r, ok = link.request(0x6D, bytes([0]))
    check(r[0] == 0 and r[1] == 0, 'no tour to begin with')
    ty, seq, r, ok = link.request(0x6D, bytes([1, 2, 3, 13, 14, 15]))
    check(r[0] == 0 and r[1] == 3 and r[2] == 2 and list(r[5:8]) == [13, 14, 15],
          'a three-world tour at a division of two comes back as it went')
    ty, seq, r, ok = link.request(0x6D, bytes([2]))
    check(r[0] == 0, 'a tick is accepted')
    ty, seq, r, ok = link.request(0x6D, bytes([1, 1]))
    check(r[0] == 2, 'a set with no length is refused')
    ty, seq, r, ok = link.request(0x6D, bytes([1, 1, 99]))
    check(r[0] == 2, 'a length past the maximum is refused')
    ty, seq, r, ok = link.request(0x6D, bytes([7]))
    check(r[0] == 2, 'an unknown op is refused')
    ty, seq, r, ok = link.request(0x6D, bytes([0]))
    check(r[1] == 3 and list(r[5:8]) == [13, 14, 15], 'and none of that disturbed the tour')
    ty, seq, r, ok = link.request(0x6D, bytes([1, 1, 0]))
    check(r[0] == 0 and r[1] == 0, 'a length below two turns it off')

    ty, seq, r, ok = link.request(0x63, corrupt=True)
    check(ty == 0xFF and r[0] == 10, f'bad CRC → ERR FRAME_ERROR (type {ty:#x})')
    link.close()


def mute_tests():
    """Muting a plane must stop it and leave its neighbour alone.

    Its own session, with a script that drives rates — SET_CONTROL sets angles,
    not rates, so without one nothing is turning and every check here would
    pass for the wrong reason. m2_orbit drives planes 0 and 4."""
    print('mute')
    link = Stdio(['--gen', '--seed', '1', '--script',
                  os.path.join(ROOT, 'tests/scripts/m2_orbit.txt'), '--loop'])
    link.request(0x01)

    def angles():
        return telemetry(link, 0)['angle']

    link.request(0x64, bytes([9, 0x00, 0x00]))
    a0 = angles(); time.sleep(0.15); a1 = angles()
    turning = [abs(a1[i] - a0[i]) > 1e-6 for i in (0, 4)]
    check(all(turning), f'planes 0 and 4 are turning before anything is muted ({turning})')

    ty, seq, r, ok = link.request(0x64, bytes([9, 0x01, 0x00]))     # plane 0 only
    check(r[0] == 0, 'a plane can be muted')
    t = telemetry(link, 7)
    check(t['mute'] == 0x0001, f"and the module reports which ({t['mute']:#06x})")
    b0 = angles(); time.sleep(0.15); b1 = angles()
    check(abs(b1[0] - b0[0]) < 1e-6, 'the muted plane stops turning')
    check(abs(b1[4] - b0[4]) > 1e-6, 'and the one beside it keeps going')

    link.request(0x64, bytes([9, 0x00, 0x00]))
    c0 = angles(); time.sleep(0.15); c1 = angles()
    check(abs(c1[0] - c0[0]) > 1e-6, 'unmuting restores the rate it had, not zero')

    ty, seq, r, ok = link.request(0x64, bytes([9]))
    check(r[0] == 2, 'a mute with no mask is refused')
    link.close()


def card_tests():
    """A fitted world (.kykm, from ModalBake) is a card world.

    It is a sample library's size — 66 KB for the Wurlitzer, a megabyte for an
    EP with its attack bursts — so it never goes over the wire and never fits
    a slot's store on the module; the card lists it beside the .kykw files,
    card-to-slot keeps the whole file, and slot-live builds a resonate world
    from it. The page then has the same three moves for both kinds. What it
    cannot do with one is what a frame world can: be a morph target, or be
    snapshotted, and both are refused as BAD_STATE rather than half-done."""
    print('card')
    import shutil, tempfile
    d = tempfile.mkdtemp(prefix='kyk-card-')
    src = os.path.join(ROOT, 'tests/data/wurli.kykm')
    shutil.copy(src, os.path.join(d, 'wurli.kykm'))
    with open(src, 'rb') as f: head = f.read(1000)
    with open(os.path.join(d, 'cut.kykm'), 'wb') as f: f.write(head)       # truncated: refused
    with open(os.path.join(d, 'notes.txt'), 'w') as f: f.write('not a world\n')
    link = Stdio(['--gen', '--seed', '1', '--card', d])
    link.request(0x01)
    ty, seq, r, ok = link.request(0x68)
    n = r[1]; names = []; at = 2
    for _ in range(n):
        ln = r[at]; names.append(r[at + 1:at + 1 + ln].decode()); at += 1 + ln
    check('wurli.kykm' in names and 'cut.kykm' in names and 'notes.txt' not in names,
          f'the card lists .kykm files ({names})')
    ty, seq, r, ok = link.request(0x64, bytes([15, names.index('cut.kykm'), 3]))
    check(r[0] == 1, 'a truncated .kykm is refused into a slot')
    ty, seq, r, ok = link.request(0x64, bytes([15, names.index('wurli.kykm'), 3]))
    check(r[0] == 0, 'a whole one goes into a slot')
    ty, seq, r, ok = link.request(0x6A)
    slots = {}; at = 4
    while at < len(r):
        i, ln = r[at], r[at + 1]; slots[i] = r[at + 2:at + 2 + ln].decode(); at += 2 + ln
    check(slots.get(3) == 'wurli', f'and the slot is named by its file ({slots})')
    t = telemetry(link, 7)
    check(t['n'] == 4, f"the generated space is live before ({t['n']}-D)")
    ty, seq, r, ok = link.request(0x64, bytes([11, 3]))
    check(r[0] == 0, 'slot-live builds a resonate world from it')
    t = telemetry(link, 7)
    check(t['n'] == 4 and t['world'] == 0xFF and t['kind'] == 11, f"telemetry: a 4-axis resonate user world is live ({t['n']}-D, world {t['world']:#x}, kind {t['kind']})")
    ty, seq, r, ok = link.request(0x64, bytes([0]))
    check(r[0] == 0, 'reset-phase strikes it')
    ty, seq, r, ok = link.request(0x64, bytes([17, 200]))
    check(r[0] == 0, 'and so does a strike with a velocity')
    ty, seq, r, ok = link.request(0x64, bytes([18, 1, 192]))
    check(r[0] == 0, 'a tune (decay x2) is taken')
    ty, seq, r, ok = link.request(0x64, bytes([18, 3, 128]))
    check(r[0] == 2, 'a tune on an axis there is not is BAD_ARGS')
    ty, seq, r, ok = link.request(0x64, bytes([19, 4]))
    check(r[0] == 0, 'four voices is taken')
    ty, seq, r, ok = link.request(0x64, bytes([19, 3]))
    check(r[0] == 2, 'three is BAD_ARGS')
    ty, seq, r, ok = link.request(0x6F)
    check(r[0] == 0 and r[-2] == 4 and r[-1] == 1, f'the readout ends with the voices and the lock (4, on): {r[-2:].hex()}')
    ty, seq, r, ok = link.request(0x64, bytes([20, 0]))
    check(r[0] == 0, 'the pitch lock comes off')
    ty, seq, r, ok = link.request(0x6F)
    check(r[0] == 0 and r[-1] == 0, 'and the readout says so')
    ty, seq, r, ok = link.request(0x64, bytes([20, 2]))
    check(r[0] == 2, 'a lock of 2 is BAD_ARGS')
    ty, seq, r, ok = link.request(0x64, bytes([12, 3]))
    check(r[0] == 3, 'it cannot be a morph target (BAD_STATE)')
    ty, seq, r, ok = link.request(0x64, bytes([16, 0]))
    check(r[0] == 3, 'nor be snapshotted (BAD_STATE)')
    ty, seq, r, ok = link.request(0x64, bytes([13, 3]))
    check(r[0] == 3, 'nor freed while it is live: the world reads the slot where it lies')
    ty, seq, r, ok = link.request(0x64, bytes([4, 0]))
    check(r[0] == 0, 'a built-in takes over')
    t = telemetry(link, 7)
    check(t['n'] == 4 and t['world'] == 0, 'and telemetry says so')
    ty, seq, r, ok = link.request(0x64, bytes([17, 200]))
    check(r[0] == 3, 'a strike on a frame world is BAD_STATE')
    ty, seq, r, ok = link.request(0x64, bytes([18, 0, 128]))
    check(r[0] == 3, 'and so is a tune')
    ty, seq, r, ok = link.request(0x64, bytes([19, 2]))
    check(r[0] == 3, 'and a polyphony')
    ty, seq, r, ok = link.request(0x64, bytes([20, 1]))
    check(r[0] == 3, 'and the pitch lock')
    ty, seq, r, ok = link.request(0x64, bytes([13, 3]))
    check(r[0] == 0, 'now the slot can be freed')
    ty, seq, r, ok = link.request(0x64, bytes([11, 3]))
    check(r[0] == 2, 'and is empty')
    link.close()
    shutil.rmtree(d, ignore_errors=True)


def ws_client(port, path='/link'):
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(f'GET {path} HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n'.encode())
    resp = b''
    while b'\r\n\r\n' not in resp: resp += s.recv(1024)
    want = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
    check(b' 101 ' in resp.split(b'\r\n')[0] and want.encode() in resp, 'WebSocket upgrade + Sec-WebSocket-Accept')
    return s


def ws_send(s, payload):
    mask = os.urandom(4)
    n = len(payload)
    hdr = bytes([0x82]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack('>H', n))
    s.sendall(hdr + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(payload)))


def ws_recv(s):
    def read(n):
        b = b''
        while len(b) < n:
            c = s.recv(n - len(b))
            if not c: raise EOFError
            b += c
        return b
    h = read(2); n = h[1] & 0x7F
    if n == 126: n = struct.unpack('>H', read(2))[0]
    return read(n)


def bridge_tests():
    print('bridge')
    port = 18765 + os.getpid() % 1000
    br = subprocess.Popen([sys.executable, os.path.join(ROOT, 'tools/bridge/bridge.py'), '--port', str(port), '--', '--gen', '--seed', '1'], stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try: socket.create_connection(('127.0.0.1', port), timeout=0.2).close(); break
            except OSError: time.sleep(0.1)
        c = http.client.HTTPConnection('127.0.0.1', port, timeout=5); c.request('GET', '/'); r = c.getresponse(); page = r.read()
        check(r.status == 200 and b'<canvas' in page, f'HTTP serves index.html ({r.status}, {len(page)} bytes)')
        c.request('GET', '/../Makefile'); r = c.getresponse(); r.read()
        check(r.status in (404, 403, 400), f'path escape refused ({r.status})')
        s = ws_client(port)
        acc = b''
        ws_send(s, frame(0x01, 7))
        while b'\x00' not in acc: acc += ws_recv(s)
        ty, seq, b, ok = parse(acc.split(b'\x00')[0])
        check(ok and ty == 0x81 and seq == 7, 'HELLO over the WebSocket')
        acc = b''
        ws_send(s, frame(0x60, 8, bytes([3])))
        while b'\x00' not in acc: acc += ws_recv(s)
        ty, seq, b, ok = parse(acc.split(b'\x00')[0])
        check(ok and ty == 0xE0 and len(b) == 461, f'telemetry over the WebSocket ({len(b)} bytes)')
        s.close()
    finally:
        br.terminate(); br.wait(timeout=3)


if __name__ == '__main__':
    if not os.path.exists(KYKDESK): sys.exit('build/host/kykdesk missing — make host')
    stdio_tests()
    mute_tests()
    card_tests()
    bridge_tests()
    print(f'link_check: {checks - fails}/{checks} passed' if not fails else f'link_check: {fails} FAILURES of {checks}')
    sys.exit(1 if fails else 0)
