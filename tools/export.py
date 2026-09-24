#!/usr/bin/env python3
"""export.py — a fitted world, condensed for the module.

    export.py records out/fit/wurli out/worlds/wurli.kykm
    export.py records out/fit/piano out/worlds-clean/piano.kykm --clean [--level-of=out/card/kyklophoria/piano.kykm]
    export.py shaped out/fit/ep-vel out/worlds/ep-vel.kykm
    export.py corpus out/corpus.mdb wurli out/worlds/wurli-slots.kykm

One file a world, small enough to sit in SDRAM beside the wavetables:

    'KYKM' u16 version=6  u16 N  u16 P  u8 form  u8 kind
    f32 param_lo  f32 param_hi
    kind: what the parameter is. 0 = a note (MIDI), which the runtime takes
    from the pitch it is played at; 1 = an index, a row of bodies, which it
    takes from a position axis. (Version 4 wrote a `body` count here that
    nothing ever read; a 4 reads as kind 0.)
    P points, each:
        f32 param
        f32 h  f32 w  f32 K  f32 fc  f32 Q  f32 swing_soft  f32 swing_hard   (the stage at this point; zeros if none)
        f32 loudest   (the absolute gain the level bytes are relative to: the swing into the field needs it)
        N modes of
        u16 cents from 20 Hz  (12 * 100 * log2(hz / 20): 20 Hz .. 20 kHz in 1 cent)
        u8  decay  (-10 ln zeta, clamped: zeta 1 .. 1e-11, a tenth of a neper)
        u8  level  (dB under the point's loudest, in quarter dB, 0 .. 63.75)
        u8  phase  (0 .. 255 over a turn: the fitted phase at the strike, where the
                    hammer's timing per mode lives — from zero phase every partial
                    rises together and the onset is a spike the recording never had)

        then u8 nbands and per band f32 level, f32 t60: the wash (tools/noise.py),
        white noise through octave band-passes from 62.5 Hz under those envelopes
        then u16 nbursts, and per burst: f32 swing, f32 scale, u16 len, i16 samples[len]
        at 48 kHz — the attack the modes are not (tools/bursts.py): the recording's
        first 40 ms minus the model's, played at strike time after the pickup,
        scaled by swing / this burst's swing; a shaped world crossfades the two
        bursts bracketing the strike's swing

Five bytes a mode and 36 a point, plus ~4 KB a burst; the EP with two
bursts a note is 700 KB, the Wurlitzer 45 KB. The stage
is per point because it is: the fitted voicing walks up the keyboard. The runtime decodes a point
at note-on (exp2, exp, a table), never a sample.

`records` takes a fitted set's records as they are, every mode a note
sorted by frequency, with the phase and the burst: the fit puts a cluster
at a harmonic — a pair, a thump, a double decay — whose phases partly
cancel, and the corpus's one-mode-a-harmonic slot keeps only the loudest of
them, which on the Wurlitzer's C4 was a third harmonic at 2.7 where the
record's cluster sums to a fraction of that. Slot k is then the kth partial
by frequency, which the runtime interpolates between recorded notes; about
right when clusters are absent and approximate when not. `shaped` is the
same for a fitvel set, with the stage and a burst a take. `corpus` takes a
pitched family out of the aligned corpus, slot k harmonic k, which is the
bake's representation and not the sound's. form 0 is no stage, 1 the bell,
2 the gap.
"""
import math
import os
import struct
import sys

import numpy as np


def cents(hz):
    # fifths of a cent from 20 Hz (version 6; a whole cent before, which
    # used 5.5x less of the field than it has and moved a beat pair's rate
    # by more than a quarter on a third of the piano's pairs). 65535 is
    # 13107 cents: 38.8 kHz
    return int(np.clip(round(5 * 1200 * math.log2(max(hz, 20.0) / 20.0)), 0, 65535))


def decay8(zeta):
    return int(np.clip(round(-10.0 * math.log(max(zeta, 1e-11))), 0, 255))


def level8(gain, loudest):
    # 255 is silence — a ghost, a slot with no mode — and not -63.75 dB,
    # which a third of the Wurlitzer's slots rang at
    if gain == 0.0 or loudest <= 0:
        return 255
    db = 20 * math.log10(max(abs(gain), 1e-12) / loudest)
    return int(np.clip(round(-db * 4), 0, 254))


def phase8(ph):
    return int(round((ph % (2 * math.pi)) / (2 * math.pi) * 256)) % 256


def write(path, N, points, form=0, kind=0, body=None):
    """points: [(param, modes, stage[, bursts])] with stage = (h, w, K, fc, Q, swing_soft, swing_hard) or None,
    bursts = [(swing, samples_at_48k)]; body = 8 octave-band gains in dB
    (tools/body.py), the instrument's own radiation envelope, which the
    runtime applies across a transposition"""
    params = [p[0] for p in points]
    ver = 7 if body is not None else 6
    out = b'KYKM' + struct.pack('<HHHBB', ver, N, len(points), form, kind)
    out += struct.pack('<ff', min(params), max(params))
    if body is not None:
        out += struct.pack('<8f', *body)
    for pt in points:
        p, modes, stage = pt[0], pt[1], pt[2]
        bursts = pt[3] if len(pt) > 3 else []
        noise = pt[4] if len(pt) > 4 else []
        out += struct.pack('<f', p)
        loudest = max((abs(m[2]) for m in modes), default=1.0)
        out += struct.pack('<8f', *(stage or (0, 1, 1, 0, 1, 1, 1)), loudest)
        # a record with more modes than the bank keeps its loudest by ring
        # energy, not its lowest: the ones over the budget are the quiet
        # ones, wherever they sit
        ms = [tuple(m) + (0.0,) * (4 - len(m)) for m in modes]
        if len(ms) > N:
            ms = sorted(sorted(ms, key=lambda m: -(m[2] ** 2 / max(m[1] * m[0], 1e-9)))[:N])
        rows = ms + [(20.0, 1.0, 0.0, 0.0)] * max(0, N - len(ms))
        for hz, zeta, g, ph in rows:
            out += struct.pack('<HBBB', cents(hz), decay8(zeta), level8(g, loudest), phase8(ph))
        out += struct.pack('<B', len(noise))
        for level, t60 in noise:
            out += struct.pack('<ff', level, t60)
        out += struct.pack('<H', len(bursts))
        for b in bursts:
            swing, samples = b[0], b[1]
            fade = int(b[2]) if len(b) > 2 else len(samples) // 3      # samples at 48 kHz the modes come in over
            scale = float(np.max(np.abs(samples))) if len(samples) else 1.0
            q = np.clip(np.round(samples / (scale or 1.0) * 32767), -32768, 32767).astype('<i2')
            out += struct.pack('<ffHH', swing, scale, len(q), min(fade, len(q))) + q.tobytes()
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    open(path, 'wb').write(out)
    print('%s: %d points x %d modes, form %d, %d bytes' % (path, len(points), N, form, len(out)))


def noise_of(recdir, rid):
    """[(level, t60)] x 8 from a record's noise line, or []"""
    for w in (l.split() for l in open(os.path.join(recdir, rid + '.mmr'))):
        if w and w[0] == 'noise':
            v = [float(x) for x in w[1:]]
            return [(v[2 * k], v[2 * k + 1]) for k in range(len(v) // 2)]
    return []


def bursts_of(recdir, rid, swings=None):
    """[(swing, samples at 48 kHz, fade samples at 48 kHz)] from a record's
    burst lines — `burst <file> <fade>` on a pitched record, one at swing 1;
    `burst <take> <file> <fade>` on a shaped record, one a take at the
    take's swing. The fade is how many samples the modes come in over."""
    import soundfile as sf
    from scipy.signal import resample_poly
    out = []
    for w in (l.split() for l in open(os.path.join(recdir, rid + '.mmr'))):
        if not w or w[0] != 'burst':
            continue
        toks = w[1:]
        fade = None
        if toks and toks[-1].isdigit():
            fade = int(toks[-1]); toks = toks[:-1]
        take, fname = (toks[0], toks[1]) if len(toks) > 1 else (None, toks[0])
        x, sr = sf.read(os.path.join(recdir, fname))
        if fade is None:
            fade = len(x) // 3
        if sr != 48000:
            x = resample_poly(x, 48000, sr); fade = int(fade * 48000 / sr)
        out.append(((swings or {}).get(take, 1.0), x.astype(np.float32), fade))
    return out


def source_rms(recdir, rid):
    """the recording's own level, before the fit normalised it: the RMS of
    its loudest half second, from the file the record names. None if the
    file is not there (the samples are not in the repo)"""
    import soundfile as sf
    src = None
    for w in (l.split() for l in open(os.path.join(recdir, rid + '.mmr'))):
        if w and w[0] == 'source':
            src = ' '.join(w[1:]); break
    if not src or not os.path.exists(src):
        return None
    x, sr = sf.read(src, always_2d=True); x = x.mean(axis=1)
    n = int(0.5 * sr)
    if len(x) <= n:
        return float(np.sqrt(np.mean(x ** 2)) + 1e-9)
    # a running mean by cumulative sum: np.convolve with a 22050-point
    # kernel over a 3.5 s file was 3e9 multiplies a file, and the banjo's
    # export ran for an hour at five cores
    c = np.concatenate([[0.0], np.cumsum(x.astype(np.float64) ** 2)])
    e = (c[n:] - c[:-n]) / n
    return float(np.sqrt(e.max()) + 1e-9)


def layer(recdir, pts):
    """Records at the same parameter are one point with velocity layers,
    not several points. The Philharmonia sets have two or three dynamics a
    note, and exported as separate points the runtime played whichever came
    last — a keyboard walking piano and forte at random, which Combust
    heard as the violin and viola sounding wrong. The fit normalised every
    recording, so the dynamics' levels are read back from the source files:
    the loudest take is the point (its modes, its wash), each take's swing
    is its level over the loudest's, and each take's burst — its own attack,
    which is where a pizzicato's dynamics differ most — is placed at that
    swing, scaled into the loudest take's units, so a strike between two
    dynamics crossfades their attacks over the shared modes at the level
    between them. Without the source files the loudest-fitting record
    stands alone and the others are dropped, and it says so."""
    groups = {}
    for pt in pts:
        groups.setdefault(pt[0], []).append(pt)
    out = []
    for param, g in groups.items():
        if len(g) == 1:
            out.append(g[0][:5]); continue
        lv = [(source_rms(recdir, pt[5]), pt) for pt in g]
        if any(l is None for l, _ in lv):
            print('  %s: %d records at param %g and no source levels: keeping %s, dropping the rest' % (recdir, len(g), param, g[-1][5]))
            out.append(g[-1][:5]); continue
        lv.sort(key=lambda t: -t[0])
        top, carrier = lv[0]
        bursts = []
        for l, pt in lv:
            sw = l / top
            for b in pt[3]:
                bursts.append((sw, b[1] * sw, b[2] if len(b) > 2 else len(b[1]) // 3))
        bursts.sort(key=lambda b: b[0])
        stage = tuple(carrier[2][:5]) + (bursts[0][0] if bursts else 1.0, 1.0)
        print('  %s: param %g, %d takes as layers: %s' % (recdir, param, len(g), ' '.join('%s@%.2f' % (pt[5], l / top) for l, pt in lv)))
        out.append((param, carrier[1], stage, bursts, carrier[4]))
    return out


TAKE_RANK = {'MIN': 0, 'SOFT': 0, 'MED': 1, 'MID': 1, 'MAX': 2, 'HARD': 2}


def monotonic(takes, rid):
    """A harder hit is a bigger swing. The fit did not always say so: on 39
    of the EP's 84 notes, every one from midi 81 up, the MAX take's fitted
    swing came out below MED's — at the top of the keyboard the tine barely
    moves, the field is nearly linear there and the swing is whatever the
    optimiser landed on — and the runtime, which brackets the burst by
    swing and scales the bank by it, played MED's attack at full velocity.
    So the swings are assigned to the takes in the takes' own order (MIN,
    MED, MAX; v015 .. v100), sorted ascending: the fit's numbers, the
    library's order. The fitter's own fix is a monotonic prior on the
    swings at the next refit (docs/holistic-math.md, item 7)."""
    if len(takes) < 2:
        return takes
    def rank(name):
        if name.upper() in TAKE_RANK:
            return TAKE_RANK[name.upper()]
        digits = ''.join(ch for ch in name if ch.isdigit())
        return int(digits) if digits else 0
    names = sorted(takes, key=rank)
    sw = sorted(takes[n] for n in names)
    fixed = dict(zip(names, sw))
    if fixed != takes:
        print('  %s: takes %s reordered to a rising swing' % (rid, ' '.join('%s@%.2f' % (n, takes[n]) for n in names)))
    return fixed


def headroom(pts, form, target=3.0):
    """A world's loudest note at full velocity peaks at 3 through the
    runtime's own model (4 was tried and two notes overlapping railed) — modes with their phases through the pickup where
    there is one, plus the burst — which is where a wavetable cell's peaks
    sit (unit RMS, a crest of up to 4.3), so that behind the engine's 0.23
    of output gain a resonator at full level peaks where the wavetable
    does, just under full scale. There was no convention: the EP came out
    16 dB above the Wurlitzer through the same engine and railed 13,000
    samples of a sweep on the desktop (the holistic pass: -10 to -18 dB re
    a wavetable cell, 20 dB note to note). Every point's `loudest` and
    every burst are scaled by one number, so the world keeps its own
    balance note to note and sits where the other worlds sit."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import playvel
    peak = 0.0
    for param, modes, stage, bursts, *rest in pts:
        sh = ({0: 'none', 1: 'bell', 2: 'gap'}[form],) + tuple(stage[:5])
        swing = stage[6] if len(stage) > 6 else 1.0
        lit = [m for m in modes if m[2] != 0.0]
        if lit:
            y = playvel.note(lit, sh, swing, 0.15, 48000)
            b = bursts[-1][1] if bursts else np.zeros(1)
            n = min(len(y), len(b))
            y = y.copy(); y[:n] += b[:n]
            peak = max(peak, float(np.max(np.abs(y))))
    if peak <= 0:
        return pts
    k = target / peak
    out = scale_points(pts, form, k)
    print('  headroom: the loudest note at full velocity peaked at %.2f through the model; every point scaled by %.3f (%+.1f dB)' % (peak, k, 20 * math.log10(k)))
    return out


def scale_points(pts, form, k):
    """Every point k times louder: the modes on a plain world; on a pickup
    world the coil's gain K — the modes are the tine's displacement into
    the field, and the field's nonlinearity, the bark that is the
    instrument, is set by that displacement over the pole's width, so
    scaling them would scale the physics (the EP's C3 lost 11 dB of its
    h2 that way). The bursts always."""
    out = []
    for param, modes, stage, bursts, *rest in pts:
        if form == 0:
            modes = [(m[0], m[1], m[2] * k, m[3]) for m in modes]
        else:
            stage = tuple(stage[:2]) + (stage[2] * k,) + tuple(stage[3:])
        out.append((param, modes, stage, [(b[0], b[1] * k) + tuple(b[2:]) for b in bursts], *rest))
    return out


def voice(pts, form, kind, strength=1.0, cap_db=9.0, reach=3, tol_db=3.0):
    """Voicing, as a piano technician does it: each note's loudness evened
    against its neighbours'. Loudness here is the note as it plays at full
    velocity — modes (through the pickup where there is one) plus the
    hardest take's burst — as RMS over its first 300 ms, in dB. Its target
    is the median of the `reach` notes either side of it, itself left out.
    Within tol_db of it a note is left alone — that much is the instrument's
    character, and every note is its own (Combust: "some of the difference
    ... is character ... but some of the largest jumps are indicative of
    some kind of error in our process or in the recordings themselves"); past
    it, the excess is taken back, times `strength`, at most cap_db: the whole
    point, attack and ring by one number (scale_points), so the seam stays
    where it was and each note keeps its own timbre and decay.

    The Iowa grand's recordings are uneven where the keyboard seesawed —
    G5 10 dB under its neighbours 150 ms in at ff and at mf alike, A#5 6 dB
    — and a fit can only be as even as its recording (Combust: "huge jumps
    in the output spectra", "smooth across each note"). Not on a row of
    bodies (kind 1), whose points are different objects on purpose."""
    if kind == 1 or len(pts) < 3 or strength <= 0:
        return pts
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import playvel
    loud = []
    for param, modes, stage, bursts, *rest in pts:
        sh = ({0: 'none', 1: 'bell', 2: 'gap'}[form],) + tuple(stage[:5])
        swing = stage[6] if len(stage) > 6 else 1.0
        lit = [m for m in modes if m[2] != 0.0]
        y = playvel.note(lit, sh, swing, 0.3, 48000) if lit else np.zeros(14400)
        b = bursts[-1][1] if bursts else np.zeros(1)
        n = min(len(y), len(b))
        y = y.copy(); y[:n] += b[:n]
        loud.append(10 * math.log10(float(np.mean(y[:14400] ** 2)) + 1e-20))
    out, moved = [], []
    for i, pt in enumerate(pts):
        near = [loud[j] for j in range(max(0, i - reach), min(len(pts), i + reach + 1)) if j != i]
        dv = float(np.median(near)) - loud[i]
        c = strength * math.copysign(max(0.0, abs(dv) - tol_db), dv)
        c = max(-cap_db, min(cap_db, c))
        if abs(c) >= 1.0:
            moved.append('%s %+.1f' % (note_name(pt[0]) if kind == 0 else '%g' % pt[0], c))
        out.extend(scale_points([pt], form, 10 ** (c / 20)))
    print('  voice: %d of %d notes moved toward their neighbours by what they stray past %.0f dB (%.0f%% of it, at most %.0f dB)%s'
          % (len(moved), len(pts), tol_db, 100 * strength, cap_db, (': ' + ', '.join(moved)) if moved else ''))
    return out


def body_curve(fitdir, kind):
    """The set's own radiation envelope (tools/body.py), eight octave-band
    gains in dB, 0 at its loudest band, with any band the set has too few
    modes in filled from its nearest measured neighbour. A row of bodies
    (kind 1) has no keyboard to measure one over and no transposition to
    apply it across, so it gets none."""
    if kind == 1:
        return None
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        import body as bodymod
    except Exception:
        return None
    b, n = bodymod.measure(fitdir)
    if b is None:
        print('  body: too little to measure a curve (%d modes)' % n); return None
    seen = [k for k in range(8) if not math.isnan(b[k])]
    v = [float(b[k]) if not math.isnan(b[k]) else float(b[min(seen, key=lambda j: abs(j - k))]) for k in range(8)]
    print('  body: %s (dB, %d modes)' % (' '.join('%.0fHz %+.1f' % (math.sqrt(bodymod.EDGES[k] * bodymod.EDGES[k + 1]), v[k]) for k in range(8)), n))
    return v


def runtime_headroom(path, pts, N, form, kind, target=3.0, body=None):
    """The convention checked against the runtime itself: the written
    world's keyboard at full velocity through build/modaltest (which is
    Kyklophoria's kyk_resonate.h), its peak read back, and the world
    rescaled and rewritten when that peak is not the target. The model in
    headroom() is a Python picture of the runtime and on a badly fitted
    pickup note it was 3x under — the EP's G2 railed the note sweep."""
    import subprocess, tempfile
    mt = os.path.join(os.path.dirname(__file__), '..', 'build', 'modaltest')
    if not os.path.exists(mt):
        print('  headroom: no build/modaltest, the runtime peak not checked'); return pts
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run([mt, path, os.path.join(d, 'k.wav'), '--velocity', '1.0'], capture_output=True, text=True)
    peak = None
    for l in r.stdout.splitlines():
        if 'peak through the runtime' in l:
            peak = float(l.split()[-1])
    if not peak or peak <= 0:
        print('  headroom: the runtime peak could not be read (%s)' % r.stdout.strip().splitlines()[-1:]); return pts
    k = target / peak
    if abs(k - 1.0) < 0.05:
        print('  headroom: the runtime agrees, peak %.2f at full velocity' % peak); return pts
    out = scale_points(pts, form, k)
    write(path, N, out, form, kind, body)
    print('  headroom: the runtime peaked at %.2f at full velocity where the model said %.1f; every point scaled by %.3f (%+.1f dB) and the world rewritten' % (peak, target, k, 20 * math.log10(k)))
    return out


def settled_cents(path, midi):
    """Where the recording itself settles, in cents re the note: 50-300 ms
    past its loudest sample, the f0 whose first six harmonics carry the most
    magnitude, searched +-150 c around the label in 2 c steps (each harmonic
    the largest bin within 0.3%%, and a third of the magnitude in the odd
    ones so a subharmonic cannot win), then +-4 c in 0.25 c steps within
    0.1%%. None when the file is gone or the search ends on its edge (a
    detector failure, not a tuning). The note-start audit's own measure,
    tightened for pulling rather than flagging."""
    import soundfile as sf
    if not os.path.exists(path):
        return None
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    pk = int(np.abs(x).argmax())
    seg = x[pk + int(0.05 * sr):pk + int(0.30 * sr)]
    if len(seg) < int(0.2 * sr):
        return None
    # a window that is mostly silence reads noise as a pitch: the plucked
    # viola's E3 is 90 ms of sound and then -73 dB, and read +132 c
    head = x[pk:pk + int(0.05 * sr)]
    if np.sqrt(np.mean(seg ** 2)) < 0.03 * np.sqrt(np.mean(head ** 2)):
        return None
    n = 1 << 18
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    fr = sr / n
    f_lab = 440.0 * 2 ** ((midi - 69) / 12)

    def score(c, w):
        f0 = f_lab * 2 ** (c / 1200)
        tot = odd = 0.0
        for k in range(1, 7):
            f = k * f0
            if f > 0.45 * sr:
                break
            v = X[int(f * (1 - w) / fr):int(f * (1 + w) / fr) + 2].max()
            tot += v
            odd += v if k % 2 else 0.0
        return tot if odd >= 0.33 * tot else 0.0
    grid = np.arange(-150, 151, 2.0)
    best = grid[int(np.argmax([score(c, 0.003) for c in grid]))]
    if abs(best) >= 150:
        return None
    fine = np.arange(best - 4, best + 4.01, 0.25)
    return float(fine[int(np.argmax([score(c, 0.001) for c in fine]))])


def intune(pts, kind, max_cents=120.0, sources=None):
    """Each point pulled to its nominal note. A point played at the pitch
    the recording had — the Philharmonia's within a few cents, a sampler's
    top octave 40 cents out and two files 67 cents apart — so a world was in
    tune with itself and not with A440 along the keyboard, and a v/oct that
    asked for a C4 got the C4 file's idea of one. The fundamental is the
    fitted mode nearest ratio 1 with a fifth of the loudest gain (a
    Wurlitzer's bass fundamental is weak, so a strong second harmonic at
    ratio 2 is checked as well); every mode is scaled by nominal over
    measured, so the inharmonicity stays and the note lands on the note.
    Corrections past 120 cents are an octave error in a manifest, not a
    tuning, and are refused and reported. Index worlds have no note."""
    if kind != 0:
        return pts
    out = []
    corr = []
    for param, modes, *rest in pts:
        f = 440.0 * 2 ** ((param - 69) / 12)

        # each mode by the energy it has 50-300 ms into the note, which is
        # what the ear takes the pitch from: by gain alone a loud mode that
        # dies in 70 ms set the pull, and moved the ring that holds the note
        # off it (the plucked cello's F#3 to +126 c, the viola's G#3 to -102)
        def heard(m):
            a = 4 * math.pi * m[1] * m[0]              # 2 zeta w
            return m[2] ** 2 * ((math.exp(-a * 0.05) - math.exp(-a * 0.30)) / a if a > 1e-9 else 0.25)
        loud = max((heard(m) for m in modes), default=0.0)
        cands = [m for m in modes if heard(m) > 0.04 * loud and 0.9 < m[0] / f < 1.1]
        ratio = 1.0
        if not cands:
            cands = [m for m in modes if heard(m) > 0.04 * loud and 1.8 < m[0] / f < 2.2]
            ratio = 2.0
        if not cands:
            corr.append((param, None)); out.append((param, modes, *rest)); continue
        # the energy-weighted centre of the modes within 6% of the most heard
        # candidate: a pizzicato's pitch settles after the pluck and the fit
        # spends two or three modes on it
        top = max(cands, key=heard)[0]
        clus = [m for m in cands if abs(m[0] / top - 1) < 0.06]
        fm = sum(m[0] * heard(m) for m in clus) / sum(heard(m) for m in clus) / ratio
        cents = 1200 * math.log2(f / fm)
        if abs(cents) > max_cents:
            corr.append((param, cents)); out.append((param, modes, *rest)); continue
        # the burst IS the recording, so it is pulled by the recording's own
        # settled pitch where that can be read and is plausibly in tune
        # (within 50 c; past that it is as likely the measure as the player),
        # and otherwise by the modes' k, as before
        rec = settled_cents(sources[param], int(round(param))) if sources and param in sources else None
        k = f / fm
        rest = list(rest)                 # stage, bursts, noise[, id]
        if len(rest) > 1 and rest[1]:
            # the burst pulled onto the note too, or it plays at the recorded
            # pitch under modes at the note (up to 110 cents apart on the
            # piano): a linear read at step kb, as the runtime's own
            kb = 2 ** (-rec / 1200) if rec is not None and abs(rec) <= 50.0 else k
            if abs(1200 * math.log2(kb / k)) > 30:
                print('  intune: midi %g, the burst pulled %+.0f c by the recording and the modes %+.0f c by their ring'
                      % (param, 1200 * math.log2(kb), 1200 * math.log2(k)))
            nb = []
            for b in rest[1]:
                x = b[1]; m = int(len(x) / kb)
                nb.append((b[0], np.interp(np.arange(m) * kb, np.arange(len(x)), x).astype(np.float32), int(b[2] / kb) if len(b) > 2 else m // 3))
            rest[1] = nb
        out.append((param, [(m[0] * k, m[1], m[2], m[3]) for m in modes], *rest))
        corr.append((param, cents))
    fixed = [c for _, c in corr if c is not None and abs(c) <= max_cents]
    refused = [(p, c) for p, c in corr if c is not None and abs(c) > max_cents]
    print('  in tune: %d of %d points pulled to their note, |cents| median %.1f max %.1f%s' % (
        len(fixed), len(pts), np.median(np.abs(fixed)) if fixed else 0, max(np.abs(fixed)) if fixed else 0,
        ''.join('; %g refused at %+.0f cents (an octave error?)' % (p, c) for p, c in refused)))
    return out


def align(pts, kind, cap=48):
    """The points padded to one width, with ghosts that mean something.

    A note world (kind 0) is no longer interpolated slot by slot: the
    runtime plays the nearest point transposed to the note (docs/
    holistic-math.md, its item 2 — the interpolated bank at a midpoint
    was up to 12-19 dB louder than either point, the fitter's antiphase
    pairs un-cancelling), so its slots need no alignment, only a common
    width: each point's modes in frequency order, and silence — the note
    at zero gain, level byte 255 — where it has fewer than the widest.
    The chain alignment by ratio that was here is in the history (d33fb45)
    should slot interpolation of a note world ever come back.

    An index world (kind 1), a row of bodies, is still interpolated along
    the row — the glide from gong to woodblock is the point of it — and a
    body with fewer modes than its neighbour used to be padded with 20 Hz,
    which a real mode was interpolated against on the way: a 12.6 kHz mode
    through 502 Hz at -1 dB between two bodies. A body's missing slot now
    holds its nearest neighbour's frequency in that rank at zero gain, so
    the neighbour's mode fades out on the way rather than diving."""
    N = min(cap, max(len(pt[1]) for pt in pts))
    out = []
    for i, (param, modes, *rest) in enumerate(pts):
        ms = sorted(modes)
        if len(ms) > N:
            ms = sorted(sorted(ms, key=lambda m: -(m[2] ** 2 / max(m[1] * m[0], 1e-9)))[:N])
        while len(ms) < N:
            s_ = len(ms)
            if kind == 1:
                near = min((pts[j] for j in range(len(pts)) if j != i and len(pts[j][1]) > s_),
                           key=lambda pt: abs(pt[0] - param), default=None)
                hz = sorted(near[1])[s_][0] if near else (ms[-1][0] if ms else 440.0)
                ms.append((hz, ms[-1][1] if ms else 0.01, 0.0, 0.0))
            else:
                ms.append((440.0 * 2 ** ((param - 69) / 12), 0.01, 0.0, 0.0))
        out.append((param, ms, *rest))
    return out


def thin_points(path, floor=8):
    """Read back what was written and say which points came out too thin to
    be an instrument. A point whose modes all encode as silence but one is a
    sine, and a sine does not sound like a violin: violin-suld's B4 and D5
    came out with the fundamental alone, because the FIT found twelve modes
    at 1.00x, 6.01x, 7.02x, 8.70x... and skipped harmonics two through five.
    The export was faithful to a bad fit, which is why this warns rather than
    repairs — the fix is a refit, and a silent world on the card is worse
    than a noisy build. Mode count does fall honestly with register (a
    xylophone bar at 2 kHz has few partials under Nyquist at all), so this
    is a shortlist to look at and not a verdict."""
    b = open(path, 'rb').read()
    ver, N, P = struct.unpack_from('<HHH', b, 4)
    o = 20 + (32 if ver >= 7 else 0)
    thin = []
    for p in range(P):
        param = struct.unpack_from('<f', b, o)[0]; o += 4 + 32
        n = 0
        for m in range(N):
            if b[o + 3] != 255:            # fifth-cents u16, decay u8, LEVEL u8, phase u8
                n += 1
            o += 5
        nb = b[o]; o += 1 + nb * 8
        nbur = struct.unpack_from('<H', b, o)[0]; o += 2
        for k in range(nbur):
            ln = struct.unpack_from('<H', b, o + 8)[0]; o += 12 + 2 * ln
        if n < floor:
            thin.append((param, n))
    if thin:
        print('  WARNING %s: %d of %d points under %d live modes — %s'
              % (path, len(thin), P, floor,
                 ', '.join('%g=%d' % (pm, n) for pm, n in thin)))
    return thin


# ── --clean: what the world plays and its recording does not ─────────────
#
# tools/specaudit.py plays every point through the runtime and holds it
# against the note's own recording: a render peak more than 10 dB over the
# recording within 1.5% is a stray, and the late windows are where one sings
# alone — the piano's top octave rang a mode at 0.04 x f0 22 dB over its
# recording from 0.3 s on, the whole of what was left of the note there, and
# the plucked violin's D6 a mode 5% under f0 at +35 dB. `records --clean`
# takes those out here, from the same comparison, on each record before it
# becomes a point: the record's modes are played as the runtime plays them
# at their own note (decaying sines at their phases from the strike, silent
# for the burst's lead and in under the burst's fade, the burst itself on
# top) and every mode's level at its frequency is set beside the recording's
# over the windows the audit uses. A mode is changed only where that model
# stands more than CLEAN_DB over the recording in a window where the mode is
# heard (its own level within 12 dB of the model's peak there, and that peak
# within 60 dB of the window's loudest); everything else is written exactly
# as it would have been. A flagged mode (a cluster of them within 1%, as
# one) that rings longer than the recording does at its frequency
# (ringers.py's track, generalised to every mode the comparison flags) has
# its T60 cut to the recording's, or to the rate that closes the growth of
# its excess through the note; one still over after that is brought down to
# the recording's level in its worst window. Neither may take a window where
# it matches the recording out of the match (CLEAN_MATCH_DB), and one that
# would have to come down 30 dB or more has nothing in the recording to be
# and is dropped. A wash band whose floor between the partials stands over
# the recording's by more than CLEAN_DB is turned down by the excess. The
# decisions are the recording's, never a taste: a mode the recording holds
# at its level is never touched however odd it looks.

# CLEAN_DB, measured (tools/specaudit.py --selftest and the note-by-note
# numbers behind it): the same comparison made between a recording and
# itself 100 ms later stands a peak more than 10 dB over itself at 0.01% of
# the recording's partials and 0.04% of its other peaks (250 ms: 0.05% and
# 0.2%), where 6 dB is 0.1% and 0.2% (250 ms: 0.3% and 0.7%). The fit's own
# spread at the strongest partials in the first window is +5 dB at the 95th
# percentile. So 10 dB is a stray and not the comparison's noise or the
# fit's ordinary error, and it is the audit's own line for one
CLEAN_DB = 10.0
# and what a match is: the same comparison of a recording with itself 100 ms
# on stands within 2 dB of itself at 99% of its partials, 4.4 dB at its
# loudest. A correction may take no heard window more than this under the
# recording (or under where it already was)
CLEAN_MATCH_DB = 3.0
CLEAN_WINDOWS = ((0.05, 0.3), (0.3, 1.0), (1.0, 2.0), (2.0, 4.0), (4.0, 6.0))
CLEAN_DROP_DB = 30.0


def _clean_spec(seg):
    from scipy.signal import windows as sw
    n = len(seg)
    w = sw.blackmanharris(n)
    nfft = 1 << int(math.ceil(math.log2(4 * n)))
    return 20 * np.log10(np.abs(np.fft.rfft(seg * w, nfft)) + 1e-20), w, nfft


def _track_t60s(x, sr, hzs, start=0.05):
    """ringers.track_t60 for many frequencies over one STFT: the recording's
    own T60 at each, None where it cannot be read, 60 where the track is flat"""
    n, hop = 2048, 512
    w = np.hanning(n)
    starts = range(int(start * sr), len(x) - n, hop)
    if len(starts) < 4:
        return [None] * len(hzs)
    S = np.abs(np.array([np.fft.rfft(x[s:s + n] * w) for s in starts]))
    t = np.arange(len(starts)) * hop / sr
    out = []
    for hz in hzs:
        k = int(round(hz * n / sr))
        if k < 1 or k >= n // 2:
            out.append(None); continue
        e = S[:, max(0, k - 1):k + 2].max(axis=1) ** 2 + 1e-20
        le = np.log(e)
        floor = np.log(e[-3:].mean() * 2)
        ok = (le > floor) & (le > le.max() - math.log(1e4))
        if ok.sum() < 3:
            out.append(None); continue
        p = np.polyfit(t[ok], le[ok], 1)
        out.append(60.0 if p[0] >= 0 else 6.91 / (-p[0] / 2))
    return out


def _burst_line(recdir, rid):
    """(samples, fade) of a plain record's burst at its own rate, or (None, 0)"""
    import soundfile as sf
    for w in (l.split() for l in open(os.path.join(recdir, rid + '.mmr'))):
        if w and w[0] == 'burst' and len(w) in (2, 3):
            b, _ = sf.read(os.path.join(recdir, w[1]))
            if b.ndim > 1:
                b = b.mean(axis=1)
            return b, int(w[2]) if len(w) == 3 else len(b) // 3
    return None, 0


def clean_record(recdir, rid, modes, noise=None, over=CLEAN_DB, report=None):
    """A plain record's modes (and wash) with what its recording does not
    have taken out: (modes, noise, notes). Modes the recording holds are
    returned as they came, the same tuples."""
    import soundfile as sf
    tp = os.path.join(recdir, rid + '-target.wav')
    if not os.path.exists(tp) or not modes:
        return modes, noise, []
    x, sr = sf.read(tp, always_2d=True)
    x = x.mean(axis=1) / 0.5                        # the target is written at half scale
    n = len(x)
    t = np.arange(n) / sr
    b, fade = _burst_line(recdir, rid)
    # the modes as the runtime brings them in: nothing for the burst's
    # lead, then 1 - its fade; 3 ms up from the strike with no burst
    if b is not None and len(b) > fade > 0:
        lead = len(b) - fade
        u = np.clip((np.arange(n) - lead) / fade, 0.0, 1.0)
    else:
        u = np.clip(np.arange(n) / (0.003 * sr), 0.0, 1.0)
    ramp = 0.5 - 0.5 * np.cos(np.pi * u)
    base = np.zeros(n)
    if b is not None:
        base[:min(n, len(b))] = b[:n]
    f = np.array([m[0] for m in modes]); z = np.array([m[1] for m in modes])
    g = np.array([m[2] for m in modes]); ph = np.array([m[3] if len(m) > 3 else 0.0 for m in modes])

    def floor_g():
        # the format's quietest level: 63.5 dB under the point's loudest
        # (level8 clips there), so a mode written quieter plays at that —
        # a pizzicato's 1.7 s ringer at -88 dB came back at -63.5 and was
        # the whole of the note from 0.3 s on
        return float(np.abs(g).max()) * 10 ** (-254 / 80.0)

    def comp(i):
        # at the decay and the level the world will play: the decay byte is
        # a tenth of a neper of zeta, a T60 in steps of 10%, which is 2 dB
        # at a second and a half; the level byte stops 63.5 dB down
        if g[i] == 0.0:
            return np.zeros(n)
        zq = math.exp(-0.1 * decay8(z[i]))
        gi = math.copysign(max(abs(g[i]), floor_g()), g[i])
        return gi * np.exp(-zq * 2 * math.pi * f[i] * t) * np.sin(2 * math.pi * f[i] * t + ph[i]) * ramp

    comps = np.array([comp(i) for i in range(len(f))])
    wins = []
    for a, e in CLEAN_WINDOWS:
        a_, e_ = int(a * sr), min(int(e * sr), n - int(0.01 * sr))
        if e_ - a_ < max(0.04 * sr, 0.4 * (e - a) * sr):      # the audit's rule: 40% of the window, 40 ms at least
            continue
        X, w, nfft = _clean_spec(x[a_:e_])
        fr = np.fft.rfftfreq(nfft, 1.0 / sr)
        df = sr / (e_ - a_)
        wins.append(dict(a=a_, e=e_, X=X, w=w, nfft=nfft, fr=fr, df=df, hw=int(math.ceil(2 * df / (fr[1] - fr[0])))))
    if not wins:
        return modes, noise, []
    t60_rec = None
    z0, g0 = z.copy(), g.copy()
    # clusters: modes within 1% (the fitter's own cluster, MB_CLUSTER) and
    # 8 Hz (two bins of the shortest window, 250 ms: closer than that the
    # comparison cannot tell them apart anyway), chained
    order = np.argsort(f)
    clus = np.zeros(len(f), int)
    c = 0
    for a_, b_ in zip(order[:-1], order[1:]):
        clus[b_] = c = c + (0 if f[b_] - f[a_] <= min(0.01 * f[a_], 8.0) else 1)
    clus[order[0]] = 0
    members = [[i for i in range(len(f)) if clus[i] == k] for k in range(c + 1)]
    capped = np.zeros(c + 1, bool)
    decayed = np.zeros(c + 1, bool)
    notes = {}
    active = np.zeros(len(f), bool)       # shown to stand over the recording: corrected until it does not
    stuck = np.zeros(len(f), bool)        # as close as it can come without going under the recording elsewhere
    fl = floor_g()
    for _pass in range(8):
        if abs(floor_g() / fl - 1) > 0.01:
            # the loudest came down: the quiet modes' floor with it
            lo_ = min(fl, floor_g()); hi_ = max(fl, floor_g()); fl = floor_g()
            for i in range(len(f)):
                if 0 < abs(g[i]) < hi_ * 1.01:
                    comps[i] = comp(i)
        y = base + comps.sum(axis=0)
        exc = np.full((len(f), len(wins)), np.nan)
        rel = np.full((len(f), len(wins)), np.nan)
        for j, W in enumerate(wins):
            Y, w, nfft = _clean_spec(y[W['a']:W['e']])
            fr = W['fr']
            top = Y[fr > 25].max()
            tt = t[W['a']:W['e']]
            # each mode's own peak in this window: A/2 sum(w env)
            zq = np.exp(-0.1 * np.array([decay8(v) for v in z]))
            env = np.exp(-np.outer(zq * 2 * math.pi * f, tt)) * ramp[W['a']:W['e']]
            own = 20 * np.log10(np.maximum(np.abs(g), floor_g()) / 2 * (env * w).sum(axis=1) + 1e-20)
            del env
            for i in range(len(f)):
                if g[i] == 0.0 or f[i] <= 25 or f[i] >= 0.45 * sr:
                    continue
                k = int(round(f[i] / (fr[1] - fr[0])))
                lm = Y[max(0, k - W['hw']):k + W['hw'] + 1].max()
                if own[i] < lm - 12.0 or lm < top - 60.0:
                    continue
                tol = max(0.015 * f[i], 2 * W['df'])
                lo_, hi_ = np.searchsorted(fr, f[i] - tol), np.searchsorted(fr, f[i] + tol) + 1
                exc[i, j] = lm - W['X'][lo_:hi_].max()
                rel[i, j] = lm - top
        worst = np.nanmax(np.where(np.isnan(exc), -np.inf, exc), axis=1)
        if _pass == 0:
            exc0 = exc.copy()                     # where each mode stood before anything was changed
        if report is not None and _pass == 0:
            report.extend([(float(f[i]), [None if np.isnan(v) else float(v) for v in exc[i]], [None if np.isnan(v) else float(v) for v in rel[i]]) for i in range(len(f))])
        # a mode is taken up when it stands more than `over` above the
        # recording — past what the comparison does to the recording held
        # against itself — and once taken up it is brought all the way
        # down to the recording, not to just under the threshold
        active |= worst > over
        # a cluster is corrected as one: the fit's pairs and triplets (a
        # doublet's beat, a double decay) cancel in part by their balance,
        # and turning one member down or its decay up alone breaks that —
        # the VCSL grand's D5 came out 2 dB louder at its attack with its
        # 552 Hz triplet's members capped one by one. Every member takes
        # the same cut and the same extra decay, so the sum is scaled and
        # damped and its inside is left as fitted
        todo = sorted(set(clus[i] for i in range(len(f)) if active[i] and not stuck[i] and worst[i] > 0.5))
        if not todo:
            break
        if t60_rec is None:
            t60_rec = _track_t60s(x, sr, f)
        tc = [(W['a'] + W['e']) / 2 / sr for W in wins]
        for c in todo:
            mem = [i for i in members[c] if g[i] != 0.0]
            if not mem:
                continue
            ex_c = np.nanmax(np.where(np.isnan(exc[mem]), -np.inf, exc[mem]), axis=0)
            js = [j for j in range(len(wins)) if np.isfinite(ex_c[j])]
            if not js:
                continue
            jw = max(js, key=lambda j: ex_c[j]); jb = min(js, key=lambda j: ex_c[j])
            wc = ex_c[jw]
            # how far each window may still come down: to CLEAN_MATCH_DB
            # under the recording, or under where it stood before any
            # correction if that was lower — counted from the first
            # measurement, so repeated passes cannot ratchet a match away
            e0 = np.nanmax(np.where(np.isnan(exc0[mem]), -np.inf, exc0[mem]), axis=0)
            room = {j: ex_c[j] - (min(e0[j] if np.isfinite(e0[j]) else ex_c[j], 0.0) - CLEAN_MATCH_DB) for j in js}
            # the member that carries the cluster: the most ring energy
            r = max(mem, key=lambda i: g[i] ** 2 / max(z[i] * f[i], 1e-12))
            t60 = 6.91 / (z[r] * 2 * math.pi * f[r])
            tr = t60_rec[r]
            label = '+'.join('%.1f' % f[i] for i in mem) + 'Hz'
            tl = max(6.91 / (z[i] * 2 * math.pi * f[i]) for i in mem)     # the longest ring, for the note
            ds = 0.0
            if not capped[c] and tr is not None and tr < 59.9 and t60 > tr:
                # it rings longer than the recording does here: the
                # recording's decay, and the level is looked at again
                ds = 6.91 / tr - 6.91 / t60                  # nepers a second more
                capped[c] = True
                notes.setdefault(label, []).append('T60 %.2fs->%.2fs' % (tl, 6.91 / (6.91 / tl + ds)))
            elif not decayed[c] and jw > jb and ex_c[jw] - ex_c[jb] > 6.0:
                # the excess still grows through the note, by more than
                # the comparison's own noise (6 dB): the recording falls
                # faster here than its track could say — a partial that
                # sinks into the floor in a few frames. The rate that
                # closes the growth between those windows, from their
                # centres, and the level is looked at again
                capped[c] = decayed[c] = True
                ds = (ex_c[jw] - ex_c[jb]) / (tc[jw] - tc[jb]) / 8.686
                notes.setdefault(label, []).append('T60 %.2fs->%.2fs (the growth of its excess)' % (tl, 6.91 / (6.91 / tl + ds)))
            if ds > 0:
                # and never so much that a window where the cluster matches
                # the recording stops matching it: no heard window is taken
                # more than CLEAN_MATCH_DB under the recording, or further
                # under than it already was. A recording that falls fast and
                # then rings on (a string's prompt sound and its aftersound)
                # is one decay in the fit, and taking the fall's rate for it
                # left a guitar's A#4 20-40 dB under its recording from
                # 0.3 s on. The extra decay takes a window centred at t down
                # by 8.7 ds t dB
                lim = min(room[j] / (8.686 * tc[j]) for j in js)
                if lim < ds:
                    notes[label][-1] += ' (held to %.2fs: the recording has it at %s)' % (
                        6.91 / (6.91 / tl + max(lim, 0.0)), ', '.join('%+.0f' % ex_c[j] for j in js))
                    ds = max(lim, 0.0)
            if ds > 0:
                for i in mem:
                    z[i] += ds / (2 * math.pi * f[i])
                    comps[i] = comp(i)
                continue
            capped[c] = decayed[c] = True
            # down to the recording in its worst window, but a window where
            # it matches the recording is left matching (no more than
            # CLEAN_MATCH_DB under it): a mode the recording holds in one
            # window is not taken out for another
            cut = min(wc, min(room.values()))
            if cut >= CLEAN_DROP_DB:
                for i in mem:
                    g[i] = 0.0; comps[i] = 0.0
                notes.setdefault(label, []).append('dropped (+%.0f dB)' % wc)
                stuck[mem] = True
            elif cut > 0.25:
                gone = 0
                for i in mem:
                    g[i] *= 10 ** (-cut / 20)
                    if abs(g[i]) < floor_g():
                        g[i] = 0.0                       # under the format's floor it would play at the floor
                        gone += 1
                    comps[i] = comp(i)
                notes.setdefault(label, []).append('-%.1f dB (+%.0f)%s' % (cut, wc, ', under the floor: dropped' if gone else ''))
            else:
                stuck[mem] = True
    out = []
    for i, m in enumerate(modes):
        if g[i] == 0.0:
            continue
        if z[i] == z0[i] and g[i] == g0[i]:
            out.append(m)                                    # as it came, the same tuple
        else:
            out.append((float(f[i]), float(z[i]), float(g[i])) + tuple(m[3:]))
    lines = ['%s %s' % (k, ', '.join(v)) for k, v in sorted(notes.items(), key=lambda kv: float(kv[0].split('+')[0].rstrip('Hz')))]
    if noise and any(l > 0 for l, _ in noise):
        noise, wl = _clean_wash(x, sr, base + comps.sum(axis=0), noise, wins, over)
        lines += wl
    return out, noise, lines


def _clean_wash(x, sr, y, noise, wins, over):
    """the wash's bands turned down where the model's floor between the
    partials stands over the recording's: the wash simulated as the runtime
    plays it (washcheck.py's), the floor the 20th percentile of the band's
    spectrum a main lobe away from any peak of either, as the audit reads
    it, and a band's excess the median over the windows — one band's floor
    wanders by 10 dB between two stretches of the same recording (1% of
    band-windows), and a wash too loud is too loud in every window"""
    from scipy.signal import lfilter, find_peaks
    edges = [62.5 * 2 ** k for k in range(9)]
    n = len(x)
    rng = np.random.default_rng(0x9E3779B9)
    wn = rng.uniform(-math.sqrt(3), math.sqrt(3), n)
    yy = y.copy()
    for k in range(8):
        lvl, t60 = noise[k] if k < len(noise) else (0.0, 0.0)
        if lvl <= 0 or t60 <= 0:
            continue
        fc = math.sqrt(edges[k] * edges[k + 1])
        w0 = 2 * math.pi * fc / sr; al = math.sin(w0) / (2 * 1.41421); a0 = 1 + al
        bb = [al / a0, 0, -al / a0]; aa = [1, -2 * math.cos(w0) / a0, (1 - al) / a0]
        gn = math.sqrt((lfilter(bb, aa, np.eye(1, 2048)[0]) ** 2).sum())
        yy += (lvl / gn) * np.exp(-6.91 * np.arange(n) / (t60 * sr)) * lfilter(bb, aa, wn)
    diffs = [[] for _ in range(8)]
    for W in wins:
        Y, _, _ = _clean_spec(yy[W['a']:W['e']])
        fr = W['fr']; hw = 2 * W['hw']
        near = np.zeros(len(fr), bool)
        for D in (Y, W['X']):
            p, _ = find_peaks(D, prominence=6.0)
            for q in p:
                near[max(0, q - hw):q + hw + 1] = True
        for k in range(8):
            if k >= len(noise) or noise[k][0] <= 0:
                continue
            sel = (fr >= edges[k]) & (fr < edges[k + 1]) & ~near
            if sel.sum() < 16:
                continue
            diffs[k].append(np.percentile(Y[sel], 20) - np.percentile(W['X'][sel], 20))
    out, lines = list(noise), []
    for k in range(8):
        if not diffs[k]:
            continue
        d = float(np.median(diffs[k]))
        if d > over:
            out[k] = (noise[k][0] * 10 ** (-d / 20), noise[k][1])
            lines.append('wash %.0f Hz -%.1f dB' % (math.sqrt(edges[k] * edges[k + 1]), d))
    return out, lines


def _quiet(fn, *a, **kw):
    """fn(*a, **kw) with its printing kept to itself"""
    import contextlib, io
    with contextlib.redirect_stdout(io.StringIO()):
        return fn(*a, **kw)


def _headroom_k(pts, form, target=3.0):
    """headroom()'s number without applying it: (k, peak)"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import playvel
    peak = 0.0
    for param, modes, stage, bursts, *rest in pts:
        sh = ({0: 'none', 1: 'bell', 2: 'gap'}[form],) + tuple(stage[:5])
        swing = stage[6] if len(stage) > 6 else 1.0
        lit = [m for m in modes if m[2] != 0.0]
        if lit:
            y = playvel.note(lit, sh, swing, 0.15, 48000)
            b = bursts[-1][1] if bursts else np.zeros(1)
            n = min(len(y), len(b))
            y = y.copy(); y[:n] += b[:n]
            peak = max(peak, float(np.max(np.abs(y))))
    return (target / peak if peak > 0 else 1.0), peak


def _runtime_peak(path):
    """the written world's keyboard at full velocity through build/modaltest: its peak, or None"""
    import subprocess, tempfile
    mt = os.path.join(os.path.dirname(__file__), '..', 'build', 'modaltest')
    if not os.path.exists(mt):
        return None
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run([mt, path, os.path.join(d, 'k.wav'), '--velocity', '1.0'], capture_output=True, text=True)
    for l in r.stdout.splitlines():
        if 'peak through the runtime' in l:
            return float(l.split()[-1])
    return None


def _burst_levels(path):
    """{param: the loudest burst's peak} of a written world, for --level-of"""
    b = open(path, 'rb').read()
    ver, N, P, form, kind = struct.unpack_from('<HHHBB', b, 4)
    o = 20 + (32 if ver >= 7 else 0)
    head = 12 if ver >= 6 else 10
    out = {}
    for p in range(P):
        param = struct.unpack_from('<f', b, o)[0]; o += 4 + 32 + 5 * N
        nb = b[o]; o += 1 + 8 * nb
        nbur = struct.unpack_from('<H', b, o)[0]; o += 2
        top = 0.0
        for k in range(nbur):
            sc = struct.unpack_from('<f', b, o + 4)[0]; ln = struct.unpack_from('<H', b, o + 8)[0]
            top = max(top, sc); o += head + 2 * ln
        if top > 0:
            out[round(param, 3)] = top
    return out


def level_of(pts, ref):
    """the one factor that puts a world's attacks where a reference world's
    are: the median over the points both have of the loudest burst's peak,
    ref over this. The bursts are the recordings and --clean never touches
    them, so this is the reference's level and nothing of its cleaning"""
    rl = _burst_levels(ref)
    r = []
    for param, modes, stage, bursts, *rest in pts:
        top = max((float(np.max(np.abs(bb[1]))) for bb in bursts if len(bb[1])), default=0.0)
        if top > 0 and round(param, 3) in rl:
            r.append(rl[round(param, 3)] / top)
    return float(np.median(r)) if r else None


NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def note_name(midi):
    m = int(round(midi))
    return NOTE_NAMES[m % 12] + str(m // 12 - 1)


def write_family(path, members, by_pitch=False):
    """A family: one world that is a row of instruments that work the same
    way, position 0 choosing among them and v/oct the note within each —
    strings, pianos, the percussion row as it already is. Combust, after
    the bench: "when you tried to model a bunch together they were mush
    because we were trying to bridge paradigms. Keeping families together
    by the way they work is key." A container, not a merge: each member is
    a complete .kykm (its own form, bursts, wash, points) and the runtime
    plays the chosen one; a morph between two instruments is not a lerp of
    their slots, which the holistic pass measured as mush.

        'KYKM' u16 version=6  u16 N (the widest member)  u16 P=0  u8 form=0  u8 kind=2
        f32 lo=0  f32 hi=M-1
        u8 M, then M x (u32 offset from the file's start, u32 size, char[16] name)
        the members' bytes, each a whole .kykm, 4-aligned
    """
    blobs = []
    for name, f in members:
        b = open(f, 'rb').read()
        assert b[:4] == b'KYKM', f
        blobs.append((name[:15], b))
    # An instrument's strings are not an editorial order, they are a ladder:
    # the position axis IS the neck, and sweeping it should walk from the
    # lowest string to the highest rather than jump about. A note world's lo
    # is the lowest note fitted on that string, which for an open-string
    # recording is the open string itself — measured, not assumed: violin
    # g/d/a/e came out 55/62/69/76, which is G3 D4 A4 E5 exactly. So sorting
    # by lo gives the physical order and names each member at the same time,
    # which also settles the guitar's two E strings ('e' and '_e' from the
    # filenames, alphabetically four apart, with the low E landing between B
    # and G). Opt-in, because a family of different INSTRUMENTS — the piano
    # row, the electrics — is an order somebody chose and not a ladder.
    if by_pitch:
        blobs.sort(key=lambda nb: struct.unpack_from('<f', nb[1], 12)[0])
        blobs = [(note_name(struct.unpack_from('<f', b, 12)[0]), b) for _, b in blobs]
    N = max(struct.unpack_from('<H', b, 6)[0] for _, b in blobs)
    head = b'KYKM' + struct.pack('<HHHBB', 6, N, 0, 0, 2) + struct.pack('<ff', 0.0, float(len(blobs) - 1))
    table_at = len(head) + 1
    off = table_at + 24 * len(blobs)
    off = (off + 3) & ~3
    table = b''
    body = b''
    for name, b in blobs:
        table += struct.pack('<II16s', off + len(body), len(b), name.encode())
        body += b + b'\0' * ((-len(b)) & 3)
    out = head + struct.pack('<B', len(blobs)) + table
    out += b'\0' * (off - len(out)) + body
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    open(path, 'wb').write(out)
    print('%s: a family of %d — %s — %d bytes' % (path, len(blobs), ', '.join(n for n, _ in blobs), len(out)))


def read_corpus(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'MODB'
    ver, n, N, P, nfam = struct.unpack_from('<IIIII', b, 4)
    off = 24
    fams = [struct.unpack_from('<16s', b, off + 16 * i)[0].rstrip(b'\0').decode() for i in range(nfam)]
    off += 16 * nfam
    rows = []
    for _ in range(n):
        name, fam, fitted, value, nreal = struct.unpack_from('<16sBB2xfI', b, off); off += 28
        hz = struct.unpack_from('<%df' % N, b, off); off += 4 * N
        zeta = struct.unpack_from('<%df' % N, b, off); off += 4 * N
        g = struct.unpack_from('<%df' % (N * P), b, off); off += 4 * N * P
        rows.append((name.rstrip(b'\0').decode(), fams[fam], fitted, value, nreal, hz, zeta, [g[i * P] for i in range(N)]))
    return N, rows



def export_clean(d, path, raw, pts, sources, form, kind, over, ref=None, target=3.0, cleaned=True, voicing=0.0):
    """records --clean: the cleaned points through the same chain as the
    raw ones, at the level the raw world would have had — the model's
    headroom and the runtime's check taken on the raw points — so that
    what an A/B hears is the cleaning and not a rescale (taking a loud
    stray out of the loudest note would otherwise lift the whole world).
    Only if the cleaned world then peaks over the target by more than the
    runtime check's 5% is it brought down to it. --level-of moves it to a
    reference world's level instead (the median of the attacks' peaks, which
    the cleaning never touches), under the same ceiling, or under the
    reference's own peak through the runtime where that is higher: never
    louder than the world it is held against."""
    import tempfile
    raw = _quiet(layer, d, raw)
    raw.sort(key=lambda p: p[0])
    raw = _quiet(intune, raw, kind, sources=sources)
    raw = align(raw, kind)
    pts = layer(d, pts)
    pts.sort(key=lambda p: p[0])
    pts = intune(pts, kind, sources=sources)
    pts = align(pts, kind)
    if voicing:
        raw = _quiet(voice, raw, form, kind, voicing)
        pts = voice(pts, form, kind, voicing)
    body = body_curve(d, kind)
    k, peak = _headroom_k(raw, form, target)
    raw = scale_points(raw, form, k)
    with tempfile.TemporaryDirectory() as td:
        rp = os.path.join(td, 'raw.kykm')
        _quiet(write, rp, max(len(pt[1]) for pt in raw), raw, form, kind, body)
        rpk = _runtime_peak(rp)
    if rpk and abs(target / rpk - 1.0) >= 0.05:
        k *= target / rpk
    what = 'cleaned world' if cleaned else 'world'
    print('  headroom: the world without --clean is scaled by %.3f (%+.1f dB: the model peaked at %.2f, the runtime at %s)%s'
          % (k, 20 * math.log10(k), peak, '%.2f' % rpk if rpk else '?', '; the cleaned one takes the same' if cleaned else ''))
    pts = scale_points(pts, form, k)
    if ref:
        kr = level_of(pts, ref)
        if kr:
            pts = scale_points(pts, form, kr)
            print('  level: %+.2f dB to %s\'s attacks (the median over the points both have)' % (20 * math.log10(kr), ref))
        else:
            print('  level: no point shared with %s, left as it is' % ref)
    N = max(len(pt[1]) for pt in pts)
    write(path, N, pts, form, kind, body)
    pk = _runtime_peak(path)
    # the ceiling: the runtime check's own 5% over the target, or, beside a
    # reference that already peaks higher, the reference's peak — the A/B
    # is then at the level the reference is played at, and never louder
    ceil, dest = target * 1.05, target
    rpk = _runtime_peak(ref) if ref else None
    if rpk and rpk > ceil:
        ceil = dest = rpk
        print('  headroom: %s itself peaks at %.2f through the runtime at full velocity; that is the ceiling' % (ref, rpk))
    if pk and pk > ceil:
        pts = scale_points(pts, form, dest / pk)
        write(path, N, pts, form, kind, body)
        print('  headroom: the %s peaked at %.2f through the runtime at full velocity; every point scaled by %.3f (%+.1f dB) and the world rewritten'
              % (what, pk, dest / pk, 20 * math.log10(dest / pk)))
    else:
        print('  headroom: the %s peaks at %s through the runtime at full velocity (target %.1f, held)' % (what, '%.2f' % pk if pk else '?', target))
    thin_points(path)
    return 0


def main():
    kind = sys.argv[1]
    if kind == 'corpus':
        N, rows = read_corpus(sys.argv[2])
        fam = sys.argv[3]
        # the corpus carries no phase; the family's records do, matched by frequency
        phases = {}
        recdir = os.path.join('out', 'fit', fam)
        if os.path.isdir(recdir):
            for r in rows:
                if r[1] != fam:
                    continue
                rp = os.path.join(recdir, r[0] + '.mmr')
                if os.path.exists(rp):
                    phases[r[0]] = [(float(w[3]), float(w[w.index('phase') + 1])) for w in (l.split() for l in open(rp)) if w and w[0] == 'mode' and 'phase' in w]

        def phase_of(rid, hz):
            # the corpus holds float32 frequencies: the nearest record mode within 0.01%
            best = min(((abs(f / hz - 1), ph) for f, ph in phases.get(rid, [])), default=(1.0, 0.0))
            return best[1] if best[0] < 1e-4 else 0.0

        pts = sorted([(r[3], [(h, z, g, phase_of(r[0], h)) for h, z, g in zip(r[5], r[6], r[7])], None,
                       bursts_of(recdir, r[0]) if os.path.exists(os.path.join(recdir, r[0] + '.mmr')) else [],
                       noise_of(recdir, r[0]) if os.path.exists(os.path.join(recdir, r[0] + '.mmr')) else [])
                      for r in rows if r[1] == fam], key=lambda p: p[0])
        if not pts:
            print('no family', fam); return 1
        write(sys.argv[4], N, pts)
    elif kind == 'family':
        # export.py family out/worlds/strings.kykm violin=out/worlds/violin.kykm viola=...
        # export.py family --by-pitch out/worlds/guitar-strings.kykm out/worlds/guitar-sul*.kykm
        args = sys.argv[2:]
        by_pitch = args and args[0] == '--by-pitch'
        if by_pitch:
            args = args[1:]
        write_family(args[0], [(a.split('=')[0] if '=' in a else os.path.basename(a)[:-5], a.split('=')[-1]) for a in args[1:]], by_pitch)
    elif kind in ('shaped', 'records'):
        # --gate leaves out the points tools/gate.py fails (a loss past 1.5,
        # or ringing 3x longer than the recording): the note is then played
        # by its neighbour transposed, which is better than a broken fit —
        # the double bass's E string had an A2 at a loss of 3.15 with three
        # modes, from a recording the splitter cut 2 of 12 notes out of
        # --clean[=DB] takes out what the recordings do not have (see
        # clean_record); --level-of=WORLD.kykm puts the world's level where
        # that world's is, for an A/B of the cleaning alone. Neither changes
        # a byte of a world exported without them
        flags = [a for a in sys.argv[2:] if a.startswith('--clean') or a.startswith('--level-of=') or a.startswith('--voice')]
        # --voice[=S]: a note straying more than 3 dB from its neighbours'
        # loudness brought back by S of the excess (all of it by default);
        # see voice()
        vflag = next((a for a in flags if a.startswith('--voice')), None)
        voicing = (float(vflag.split('=', 1)[1]) if '=' in vflag else 1.0) if vflag else 0.0
        argv = [a for a in sys.argv if a != '--gate' and a not in flags]
        gated = '--gate' in sys.argv
        clean = next((a for a in flags if a.startswith('--clean')), None)
        over = float(clean.split('=', 1)[1]) if clean and '=' in clean else CLEAN_DB
        ref = next((a.split('=', 1)[1] for a in flags if a.startswith('--level-of=')), None)
        d = argv[2]
        pts, form, kind = [], 0, 0
        cpts = []
        lines = open(os.path.join(d, 'fits.tsv')).read().splitlines()
        col = {k: i for i, k in enumerate(lines[0].split('\t'))}
        for line in lines[1:]:
            c = line.split('\t')
            if gated and 'decay_ratio' in col and (float(c[col['loss']]) > 1.5 or float(c[col['decay_ratio']]) > 3.0):
                print('  %s: %s at midi %s left out by the gate (loss %s, rings %sx)' % (d, c[0], c[3], c[col['loss']], c[col['decay_ratio']]))
                continue
            kind = 1 if c[2] == 'index' else 0
            modes, shaper, swings = [], None, [1e9, 0]
            for l in open(os.path.join(d, c[0] + '.mmr')):
                w = l.split()
                if not w:
                    continue
                if w[0] == 'mode':
                    modes.append((float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0))
                elif w[0] == 'shaper':
                    form = {'bell': 1, 'gap': 2}[w[1]]
                    shaper = tuple(float(v) for v in w[2:7])
                elif w[0] == 'take':
                    swings[0] = min(swings[0], float(w[3])); swings[1] = max(swings[1], float(w[3]))
            takes = {}
            for l in open(os.path.join(d, c[0] + '.mmr')):
                w = l.split()
                if w and w[0] == 'take':
                    takes[w[1]] = float(w[3])
            takes = monotonic(takes, c[0])
            if swings[1] <= 0:
                swings = [1.0, 1.0]
            if not modes:
                # a record the manifest calls fitted and that holds no modes:
                # piano-iowa's A#1 lost all 44 of its lines to an interrupted
                # rewrite and exported as a burst and then silence. Never
                # silently: the neighbour plays the note, and this says so
                print('  WARNING %s: %s at midi %s has NO modes (a damaged record) — left out; refit it' % (d, c[0], c[3]))
                continue
            pts.append((float(c[3]), sorted(modes), (shaper or (0, 1, 1, 0, 1)) + tuple(swings), bursts_of(d, c[0], takes), noise_of(d, c[0]), c[0]))
            if clean:
                if shaper is None:
                    cm, cn, notes = clean_record(d, c[0], sorted(modes), pts[-1][4], over)
                    if notes:
                        print('  clean %s (midi %s): %s' % (c[0], c[3], '; '.join(notes)))
                else:
                    cm, cn = sorted(modes), pts[-1][4]    # the pickup's output is not the modes': nothing to hold a mode against
                cpts.append(pts[-1][:1] + (sorted(cm),) + pts[-1][2:4] + (cn,) + pts[-1][5:])
        sources = {}
        for line in lines[1:]:
            c = line.split('\t')
            for l in open(os.path.join(d, c[0] + '.mmr')):
                if l.startswith('source '):
                    sources.setdefault(float(c[3]), l.split(' ', 1)[1].strip()); break
        if clean or ref:
            # --level-of alone is the same chain with nothing cleaned: the
            # world as it would be exported, at the reference's level
            return export_clean(d, argv[3], pts, cpts if clean else list(pts), sources, form, kind, over, ref, cleaned=bool(clean), voicing=voicing)
        pts = layer(d, pts)
        pts.sort(key=lambda p: p[0])
        pts = intune(pts, kind, sources=sources)
        pts = align(pts, kind)
        if voicing:
            pts = voice(pts, form, kind, voicing)
        pts = headroom(pts, form)
        N = max(len(pt[1]) for pt in pts)
        body = body_curve(d, kind)
        write(argv[3], N, pts, form, kind, body)
        runtime_headroom(argv[3], pts, N, form, kind, body=body)
        thin_points(argv[3])
    return 0


if __name__ == '__main__':
    sys.exit(main())
