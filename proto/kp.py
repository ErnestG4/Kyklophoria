"""kp.py — the prototype library (build/proto/libkykproto.so) from Python,
and what the fitter and the three-way render share: the notes, the
recordings lined up with the runtime, the band analysis.

Nothing here writes to a world or to out/fit; it reads them."""
import ctypes as C
import math
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal as sg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import specaudit as sa  # noqa: E402

SR = 48000
LIB = os.path.join(ROOT, 'build', 'proto', 'libkykproto.so')


def _lib():
    if not os.path.exists(LIB) or os.path.getmtime(LIB) < max(os.path.getmtime(os.path.join(HERE, f)) for f in ('kyk_exciter.h', 'proto_api.cpp')):
        subprocess.run(['make', '-s', '-C', HERE], check=True)
    L = C.CDLL(LIB)
    fp = C.POINTER(C.c_float)
    L.kp_open.restype = C.c_void_p; L.kp_open.argtypes = [C.c_char_p, C.c_int, C.c_int]
    L.kp_close.argtypes = [C.c_void_p]
    L.kp_npoints.argtypes = [C.c_void_p]
    L.kp_param.restype = C.c_float; L.kp_param.argtypes = [C.c_void_p, C.c_int]
    L.kp_voice_modes.argtypes = [C.c_void_p, C.c_float, C.c_int, fp, fp, fp]
    L.kp_set.argtypes = [C.c_void_p, C.c_int, fp]
    L.kp_get.argtypes = [C.c_void_p, C.c_int, fp]
    L.kp_render.argtypes = [C.c_void_p, C.c_float, C.c_float, C.c_int, C.c_int, C.c_int, fp, fp, fp, fp]
    L.kp_passage.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_int), fp, fp, C.c_int, C.c_int, C.c_int, fp]
    L.kp_contact.argtypes = [fp, C.c_float, fp, C.c_int]
    L.kp_wg.argtypes = [fp, C.c_float, C.c_int, C.POINTER(C.c_int), fp, fp, fp]
    L.kp_band_edge.restype = C.c_float
    return L


L = _lib()
PC = L.kp_pcount()
NB = L.kp_bands()
EDGES = np.array([L.kp_band_edge(k) for k in range(NB + 1)])
CENTRES = np.sqrt(EDGES[:-1] * EDGES[1:])
# the parameter layout (kyk_exciter.h)
P_KIND, P_TC, P_EXP, P_FELT, P_RAMP = 0, 1, 2, 3, 4
P_NL = 5; P_NF = P_NL + 12; P_NS = P_NF + 12; P_NW = P_NS + 12
P_WG = P_NW + 12
P_F0, P_FC, P_FREF, P_KREF, P_M, P_AD, P_S1, P_S3, P_GIN, P_TILT, P_HPK, P_ORDER, P_M2, P_AD2, P_NRISE = range(P_WG + 1, P_WG + 16)
assert P_NRISE + 1 == PC, (P_NRISE, PC)


def fptr(a):
    return a.ctypes.data_as(C.POINTER(C.c_float))


class World:
    """a world (or a family's member) opened in the library"""
    def __init__(self, path, member=0):
        self.path = path
        self.blob = open(path, 'rb').read()
        self.parsed = sa.parse_world(self.blob)
        if self.parsed['kind'] == 2:
            self.mname, self.mblob = self.parsed['members'][member]
            self.parsed = sa.parse_world(self.mblob)
        else:
            self.mname, self.mblob = None, self.blob
        self.h = L.kp_open(self.blob, len(self.blob), member)
        assert self.h, path
        self.P = L.kp_npoints(self.h)
        self.params = [L.kp_param(self.h, i) for i in range(self.P)]

    def near(self, note):
        a = 0
        while a + 1 < self.P and self.params[a + 1] <= note:
            a += 1
        b = a + 1 if a + 1 < self.P else a
        pa, pb = self.params[a], self.params[b]
        t = min(1.0, max(0.0, (note - pa) / (pb - pa))) if pb > pa else 0.0
        return a if t < 0.5 else b

    def set(self, i, p):
        p = np.asarray(p, np.float32)
        assert len(p) == PC
        L.kp_set(self.h, i, fptr(p))

    def get(self, i):
        p = np.zeros(PC, np.float32)
        L.kp_get(self.h, i, fptr(p))
        return p

    def modes(self, note, cap=0):
        hz, z, g = (np.zeros(48, np.float32) for _ in range(3))
        n = L.kp_voice_modes(self.h, note, cap, fptr(hz), fptr(z), fptr(g))
        return hz[:n].astype(float), z[:n].astype(float), g[:n].astype(float)

    def render(self, note, vel, variant, n, cap=0, parts=False):
        out = np.zeros(n, np.float32)
        if parts:
            m, z, w = (np.zeros(n, np.float32) for _ in range(3))
            L.kp_render(self.h, note, vel, variant, cap, n, fptr(out), fptr(m), fptr(z), fptr(w))
            return out.astype(float), m.astype(float), z.astype(float), w.astype(float)
        null = C.POINTER(C.c_float)()
        L.kp_render(self.h, note, vel, variant, cap, n, fptr(out), null, null, null)
        return out.astype(float)

    def passage(self, times, notes, vels, variant, poly, n):
        # the sample each strike is due at, in double as kykdesk reckons it
        at = np.array([int(math.ceil(float(x) * SR - 1e-9)) for x in times], np.int32)
        no = np.asarray(notes, np.float32); v = np.asarray(vels, np.float32)
        out = np.zeros(n, np.float32)
        L.kp_passage(self.h, len(at), at.ctypes.data_as(C.POINTER(C.c_int)), fptr(no), fptr(v), variant, poly, n, fptr(out))
        return out.astype(float)


def contact(p, swing=1.0, max_len=2048):
    p = np.asarray(p, np.float32)
    out = np.zeros(max_len, np.float32)
    n = L.kp_contact(fptr(p), swing, fptr(out), max_len)
    return out[:min(n, max_len)].astype(float)


def wg_partials(p, ks, guess, ratio=1.0):
    p = np.asarray(p, np.float32)
    ks = np.asarray(ks, np.int32); g = np.asarray(guess, np.float32)
    f = np.zeros(len(ks), np.float32); m = np.zeros(len(ks), np.float32)
    D = L.kp_wg(fptr(p), ratio, len(ks), ks.ctypes.data_as(C.POINTER(C.c_int)), fptr(g), fptr(f), fptr(m))
    return D, f.astype(float), m.astype(float)


# ── the recording, lined up with the runtime ────────────────────────────
def note_record(world, note, fitdirs):
    """(point index, record, k): the record a point was exported from and
    the pull the export applied, by specaudit's own match"""
    i = world.near(note)
    pt = world.parsed['points'][i]
    recs = [r for d in fitdirs for r in sa.load_set(d)]
    rec, k, m, nw = sa.match_point(pt, world.parsed['ver'], recs)
    return i, rec, k


def recording_aligned(world, i, rec, k, vel=1.0, dur=None):
    """the recording at 48 kHz, pitch pulled as the export pulled the modes,
    at the world's scale (what the runtime's burst plays at this swing),
    sample 0 on the runtime's strike. Returns (x, scale, lag)."""
    x = sa.recording(rec['target'], k)
    n = len(x) if dur is None else min(len(x), int(dur * SR))
    x = x[:n]
    note = world.params[i]
    lead = int(0.05 * SR)
    a = world.render(note, vel, 0, min(len(x) + lead, int(0.6 * SR)))
    a = np.concatenate([np.zeros(lead), a])
    s = sa.align(a, x, lead)
    lag = s - lead
    # the burst is the recording times a scale: the lead's first 20 ms, least squares
    m = int(0.02 * SR)
    seg = a[s:s + m]
    g = float(np.dot(seg, x[:m]) / (np.dot(x[:m], x[:m]) + 1e-20))
    if lag != 0:
        # the runtime's strike is at 0; the recording's sample 0 is at lag
        x = np.concatenate([np.zeros(lag), x])[:n] if lag > 0 else x[-lag:]
    return g * x, g, lag


# ── bands ───────────────────────────────────────────────────────────────
HOP = int(0.0025 * SR)
WIN = int(0.005 * SR)
_SOS = None


def band_sos():
    global _SOS
    if _SOS is None:
        _SOS = [sg.butter(4, [EDGES[k], min(EDGES[k + 1], 0.45 * SR)], 'bandpass', fs=SR, output='sos') for k in range(NB)]
    return _SOS


def band_energy(x, t1=0.15, t0=0.0):
    """mean square per band per frame (5 ms Hann, 2.5 ms hop), frames centred
    from t0 to t1: (bands x frames), frame centres in s"""
    n0 = max(0, int(t0 * SR) - WIN)
    n1 = min(len(x), int(t1 * SR) + WIN)
    seg = x[n0:n1]
    w = np.hanning(WIN); w /= w.sum()
    cs = np.arange(int(t0 * SR), int(t1 * SR), HOP)
    out = np.zeros((NB, len(cs)))
    for k, sos in enumerate(band_sos()):
        y = sg.sosfiltfilt(sos, seg) ** 2
        c = np.convolve(y, w, mode='same')
        idx = np.clip(cs - n0, 0, len(c) - 1)
        out[k] = c[idx]
    return out, cs / SR


_M = None


def noise_matrix():
    """M[a, b]: the share of noise band b's power (unit RMS) that analysis
    band a sees — the synthesis band-passes are not brick walls"""
    global _M
    if _M is None:
        n = 1 << 16
        f = np.fft.rfftfreq(n, 1 / SR)
        A = np.zeros((NB, NB))
        ana = [np.abs(sg.sosfreqz(s, worN=f, fs=SR)[1]) ** 2 for s in band_sos()]
        # sosfiltfilt: the magnitude squared twice
        ana = [a ** 2 for a in ana]
        for b in range(NB):
            lo, hi = EDGES[b], EDGES[b + 1]; fc = math.sqrt(lo * hi); q = fc / (hi - lo)
            w0 = 2 * math.pi * fc / SR; al = math.sin(w0) / (2 * q); a0 = 1 + al
            bb = [al / a0, 0, -al / a0]; aa = [1, -2 * math.cos(w0) / a0, (1 - al) / a0]
            H = np.abs(sg.freqz(bb, aa, worN=f, fs=SR)[1]) ** 2
            H /= H.sum()
            # the 16 kHz low-pass on the sum (kyk_exciter.h NoiseBands::lp)
            w0 = 2 * math.pi * 16000.0 / SR; c = math.cos(w0); al = math.sin(w0) / (2 * 0.70710678); a0 = 1 + al
            H = H * np.abs(sg.freqz([(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0], [1, -2 * c / a0, (1 - al) / a0], worN=f, fs=SR)[1]) ** 2
            for a in range(NB):
                A[a, b] = float((H * ana[a]).sum())
        _M = A
    return _M


# ── the floor between the partials: what of the recording is unpitched ──
SEGS = ((0.0, 0.05), (0.05, 0.15))
_FCAL = None


def _floor_raw(x, segs, hz_mask=()):
    """per band per segment: the median power of the bins a main lobe away
    from every peak of the segment (6 dB prominence) and every bank mode,
    times the band's bin count (Blackman-Harris over the whole segment,
    zero-padded four times: specaudit's reading of static). A band with
    fewer than 3 such bins takes its 20th percentile"""
    out = np.zeros((NB, len(segs)))
    for j, (a, b) in enumerate(segs):
        seg = x[int(a * SR):int(b * SR)]
        n = len(seg)
        nfft = 1 << int(math.ceil(math.log2(4 * n)))
        P = np.abs(np.fft.rfft(seg * sg.windows.blackmanharris(n), nfft)) ** 2 + 1e-30
        f = np.fft.rfftfreq(nfft, 1 / SR)
        hw = int(math.ceil(4 * nfft / n))                 # the main lobe, half
        m = np.zeros(len(f), bool)
        pk, _ = sg.find_peaks(10 * np.log10(P), prominence=6.0)
        for q in pk:
            m[max(0, q - hw):q + hw + 1] = True
        for h in hz_mask:
            q = int(round(h * nfft / SR))
            m[max(0, q - hw):q + hw + 1] = True
        for k in range(NB):
            band = (f >= EDGES[k]) & (f < min(EDGES[k + 1], 0.45 * SR))
            sel = band & ~m
            v = np.median(P[sel]) if sel.sum() >= 3 else (np.percentile(P[band], 20) if band.any() else 0.0)
            out[k, j] = v * band.sum() / n
    return out


def floor_energy(x, hz_mask=(), segs=SEGS):
    """the recording's unpitched part: per band per segment, in
    band_energy's units (calibrated once on white noise)"""
    global _FCAL
    if _FCAL is None:
        rng = np.random.default_rng(1)
        cal = []
        for _ in range(8):
            wn = rng.standard_normal(int(0.2 * SR))
            E, cs = band_energy(wn, 0.15)
            F = _floor_raw(wn, segs)
            cal.append(np.array([E[:, (cs >= a) & (cs < b)].mean(axis=1) for a, b in segs]).T / F)
        _FCAL = np.median(np.array(cal), axis=0)
    return _floor_raw(x, segs, hz_mask) * _FCAL
