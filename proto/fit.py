"""fit.py — the exciter's handful of numbers a point, fitted to the
recording's early window. CPU only (numpy/scipy and the prototype library).

Per point:
  contact   the class's (hammer: a Hertzian felt, p = 2.5; pluck: a
            raised-cosine release) and a contact length from the keyboard
            law below. One take does not determine it: at the swing the
            take was fitted at, every mode, band and the waveguide hear the
            reference strike exactly (gain 1) and the modes ramp in over
            the fit's own 3 ms, so the early window is blind to it. It sets
            the velocity law only (see README).
  noise     12 bands x (level, fast decay, slow decay, slow share): one
            joint least squares on log band energy, 5 ms frames every
            2.5 ms over 0-150 ms, of  modes + [waveguide] + noise  against
            the recording. The modes are what the runtime rings (rendered,
            not assumed); the noise only adds where they fall short, and
            the synthesis bands' leakage into the analysis bands is in the
            model (kp.noise_matrix).
  waveguide f0 and B from the bank's own modes (a grid on the energy-weighted
            count of modes within 5 cents of k f0 sqrt(1 + B k^2), then
            least squares); the crossover 5% over the bank's top mode; the
            decay law sigma(f) = s1 + s3 f^2 from the recording's own band
            T60s above the crossover; the dispersion (M first-order
            allpasses and their coefficient, and which partial is tuned
            exactly) by a grid on the loop's own resonances (the library's
            Newton, so what is fitted is what plays) against the stiff
            string's series; the input's level and tilt by the same log
            band least squares, jointly with the noise refitted over it.
"""
import math

import numpy as np
from scipy import optimize as so
from scipy import signal as sg

import kp

SR = kp.SR
FIT_T1 = 0.15           # the early window the noise and the input level are fitted over
FLOOR_DB = -70.0        # band-frames this far under the loudest are floor: they count as the floor
TF_MIN = 0.002          # the noise's fast decay at least: the 5 ms frames cannot tell 1 ms from 2, and
                        # the fit took the bound and made clicks
TS_MAX = 0.1            # the noise's slow decay at most (T60 0.7 s): an attack, not a hiss. At 0.6 s
                        # a bass note's noise stood in for the partials above the bank and sat
                        # 12 dB over the recording's floor at 1-2 s (static, by specaudit's measure)


# ── classes ────────────────────────────────────────────────────────────
def contact_for(cls, note):
    """(kind, T at unit swing, exponent): the class's contact at a note.
    Piano: 4 ms at A0 to 0.8 ms at C8, log-linear (Askenfelt & Jansson's
    measured range, mezzo-forte to forte); Wurlitzer: a small felt on a
    steel reed, 1.5 ms; guitar: a finger's or a pick's release, 1 ms."""
    if cls == 'piano':
        u = min(1.0, max(0.0, (note - 21) / 87.0))
        return 0, 0.004 * (0.2 ** u), 2.5
    if cls == 'wurli':
        return 0, 0.0015, 2.5
    return 1, 0.001, 0.3


def base_params(cls, note):
    p = np.zeros(kp.PC, np.float32)
    kind, tc, ex = contact_for(cls, note)
    p[kp.P_KIND] = kind; p[kp.P_TC] = tc; p[kp.P_EXP] = ex; p[kp.P_FELT] = 1.0; p[kp.P_RAMP] = 0.003
    p[kp.P_HPK] = 0.8; p[kp.P_ORDER] = 2
    return p


# ── the noise, jointly over the bands ──────────────────────────────────
def _env2(theta, t_fine):
    L = np.exp(theta[0:12]); tf = np.exp(theta[12:24]); ts = np.exp(theta[24:36]); w = 1 / (1 + np.exp(-theta[36:48]))
    e = L[:, None] * ((1 - w)[:, None] * np.exp(-t_fine / tf[:, None]) + w[:, None] * np.exp(-t_fine / ts[:, None]))
    return e ** 2


def frame_avg(fine, t_fine, centres):
    """a quantity on the fine grid, averaged under the analysis window at each frame centre"""
    dt = t_fine[1] - t_fine[0]
    half = kp.WIN / SR / 2
    k = int(round(half / dt))
    w = np.hanning(2 * k + 1); w /= w.sum()
    from scipy.ndimage import convolve1d
    sm = convolve1d(fine, w, axis=1, mode='constant')
    idx = np.clip(np.round(np.asarray(centres) / dt).astype(int), 0, fine.shape[1] - 1)
    return sm[:, idx]


def fit_noise(X, Em, centres, rise, extra=None, theta0=None, lock_noise=False, bound=None):
    """X, Em: bands x frames (recording, what plays besides the noise).
    extra: (E_unit, lo, hi) a component whose level is fitted with the
    noise (the waveguide's, rendered at unit input). Returns (theta, g,
    cost in dB rms)."""
    M = kp.noise_matrix()
    t_fine = np.arange(0, centres[-1] + 0.01, 0.00025)
    rise = np.interp(t_fine, rise[0], rise[1], left=0.0, right=1.0)
    eps = X.max() * 10 ** (FLOOR_DB / 10)
    lX = np.log(X + eps)
    sq = np.sqrt(X.max())
    if theta0 is None:
        early = X[:, :4].mean(axis=1) - Em[:, :4].mean(axis=1)
        L0 = np.sqrt(np.maximum(early, 1e-6 * X.max()) / np.maximum(np.diag(M), 1e-3))
        theta0 = np.concatenate([np.log(L0), np.log(np.full(12, 0.008)), np.log(np.full(12, 0.08)), np.full(12, -1.0)])
    lo = np.concatenate([np.full(12, math.log(sq * 1e-6)), np.full(12, math.log(TF_MIN)), np.full(12, math.log(0.015)), np.full(12, -8.0)])
    hi = np.concatenate([np.full(12, math.log(sq * 10)), np.full(12, math.log(0.06)), np.full(12, math.log(TS_MAX)), np.full(12, 8.0)])
    theta0 = np.clip(theta0, lo + 1e-6, hi - 1e-6)
    has_extra = extra is not None
    if has_extra:
        Ew = extra
        g0 = math.log(1.0)
        theta0 = np.concatenate([theta0, [g0]]); lo = np.concatenate([lo, [-30.0]]); hi = np.concatenate([hi, [30.0]])

    def model(th):
        e2 = _env2(th[:48], t_fine) * rise[None, :] ** 2
        N = M @ frame_avg(e2, t_fine, centres)
        P = Em + N
        if has_extra:
            P = P + math.exp(2 * th[48]) * Ew
        return P

    # the bound: in each band and segment (kp.SEGS) the noise's mean energy
    # may not pass the recording's floor between its partials by more than
    # 3 dB. Without it the noise stood in for whatever the modes fell short
    # by, pitched or not — a guitar G3's fundamental, louder early than its
    # fitted exponential, became a +15 dB burst of noise in its band
    segsel = [(centres >= a) & (centres < b) for a, b in kp.SEGS]
    def noise_only(th):
        e2 = _env2(th[:48], t_fine) * rise[None, :] ** 2
        return M @ frame_avg(e2, t_fine, centres)

    def resid(th):
        r = (np.log(model(th) + eps) - lX).ravel()
        if bound is None:
            return r
        N = noise_only(th)
        extra_r = []
        for j, sel in enumerate(segsel):
            w = math.sqrt(max(1, sel.sum()))
            over = np.log(N[:, sel].mean(axis=1) + 1e-30) - np.log(2.0 * bound[:, j] + 1e-30)
            extra_r.append(3.0 * w * np.maximum(0.0, over))
        return np.concatenate([r] + extra_r)

    if lock_noise:
        # only the extra's level moves
        def r2(g):
            th = theta0.copy(); th[48] = g[0]
            return resid(th)
        s = so.least_squares(r2, [theta0[48]], bounds=([-30.0], [30.0]))
        th = theta0.copy(); th[48] = s.x[0]
    else:
        s = so.least_squares(resid, theta0, bounds=(lo, hi), x_scale='jac', max_nfev=400)
        th = s.x
    r = (np.log(model(th) + eps) - lX)
    cost = float(np.sqrt(np.mean(r ** 2)) * 10 / math.log(10))
    return th, (math.exp(th[48]) if has_extra else None), cost


RAMPS = (0.003, 0.005, 0.008, 0.012, 0.018, 0.026)


def early_cost(th, X, Em, centres, rise, t1=0.05):
    """the noise model's dB rms over the first t1 only (fit_noise's model)"""
    M = kp.noise_matrix()
    t_fine = np.arange(0, centres[-1] + 0.01, 0.00025)
    r = np.interp(t_fine, rise[0], rise[1], left=0.0, right=1.0)
    e2 = _env2(th[:48], t_fine) * r[None, :] ** 2
    P = Em + M @ frame_avg(e2, t_fine, centres)
    eps = X.max() * 10 ** (FLOOR_DB / 10)
    sel = centres < t1
    d = (np.log(P[:, sel] + eps) - np.log(X[:, sel] + eps)) * 10 / math.log(10)
    return float(np.sqrt(np.mean(d * d)))


def theta_to_params(p, th):
    p = p.copy()
    p[kp.P_NL:kp.P_NL + 12] = np.exp(th[0:12])
    p[kp.P_NF:kp.P_NF + 12] = np.exp(th[12:24])
    p[kp.P_NS:kp.P_NS + 12] = np.exp(th[24:36])
    p[kp.P_NW:kp.P_NW + 12] = 1 / (1 + np.exp(-th[36:48]))
    return p


# ── f0 and B from the bank's modes ─────────────────────────────────────
def fit_B(hz, zeta, gain, f0_nom):
    """the stiff string's (f0, B) that puts the most of the modes' energy
    within 5 cents of k f0 sqrt(1 + B k^2); then least squares on the
    assigned. Returns (f0, B, share of energy assigned, median |cents|)"""
    e = gain ** 2 / np.maximum(zeta * 2 * np.pi * hz, 1e-9)
    w = np.sqrt(e / e.max())
    sel = hz > f0_nom * 0.8
    hz, w = hz[sel], w[sel]
    best = (-1, f0_nom, 0.0)
    Bs = np.concatenate([[0.0], np.logspace(-6.5, -1.5, 101)])
    f0s = f0_nom * 2 ** (np.arange(-40, 40.5, 1.0) / 1200)
    for B in Bs:
        sc = _series_score(hz, w, f0s, B, tol_cents=5.0)
        j = int(np.argmax(sc))
        if sc[j] > best[0]:
            best = (float(sc[j]), float(f0s[j]), float(B))
    _, f0, B = best
    k = np.maximum(1, np.round(hz / f0 / np.sqrt(1 + B * (hz / f0) ** 2)))
    fk = k * f0 * np.sqrt(1 + B * k * k)
    d = 1200 * np.log2(hz / fk)
    ok = np.abs(d) < 15
    if ok.sum() >= 3:
        # (f/k)^2 = f0^2 + f0^2 B k^2
        y = (hz[ok] / k[ok]) ** 2; A = np.vstack([np.ones(ok.sum()), k[ok] ** 2]).T
        c, *_ = np.linalg.lstsq(A * w[ok, None], y * w[ok], rcond=None)
        if c[0] > 0 and c[1] >= 0:
            f0, B = math.sqrt(c[0]), c[1] / c[0]
        k = np.maximum(1, np.round(hz / f0 / np.sqrt(1 + B * (hz / f0) ** 2)))
        fk = k * f0 * np.sqrt(1 + B * k * k)
        d = 1200 * np.log2(hz / fk)
        ok = np.abs(d) < 15
    share = float((w[ok] ** 2).sum() / (w ** 2).sum())
    return f0, B, share, float(np.median(np.abs(d[ok]))) if ok.any() else float('nan')


def series(f0, B, lo, hi):
    k = np.arange(1, 2000)
    f = k * f0 * np.sqrt(1 + B * k * k)
    sel = (f > lo) & (f < hi)
    return k[sel], f[sel]


# ── the decay law above the crossover, from the recording ──────────────
def decay_law(x, fc, hz, zeta):
    """sigma(f) = s1 (f / 1 kHz)^s3 (amplitude, 1/s): the recording's own band
    T60s above fc (half-octave bands, 20 ms frames from 60 ms, the part of
    each band's envelope 10 dB over its floor), else the bank's top modes"""
    top = min(15000.0, 0.42 * SR)
    fs, sig, wts = [], [], []
    f = fc * 1.05
    while f * 2 ** 0.5 < top:
        lo, hi = f, f * 2 ** 0.5
        b = sg.sosfiltfilt(sg.butter(4, [lo, hi], 'bandpass', fs=SR, output='sos'), x)
        hop = int(0.02 * SR)
        e = np.array([np.mean(b[i:i + hop] ** 2) for i in range(int(0.06 * SR), len(b) - hop, hop)])
        if len(e) > 8:
            le = 10 * np.log10(e + 1e-30)
            floor = float(np.median(le[-max(3, len(le) // 10):]))
            ok = le > floor + 10
            # the first run over the floor
            idx = np.where(ok)[0]
            if len(idx) >= 5:
                run = idx[:np.argmax(np.diff(np.concatenate([idx, [idx[-1] + 2]])) > 1) + 1]
                if len(run) >= 5:
                    t = run * hop / SR
                    p = np.polyfit(t, le[run], 1)
                    if p[0] < -0.5:
                        T60 = 60.0 / -p[0]
                        fs.append(math.sqrt(lo * hi)); sig.append(6.91 / T60); wts.append(len(run))
        f *= 2 ** 0.5
    src = 'recording'
    if len(fs) < 2:
        # the bank's top third, by frequency
        o = np.argsort(hz)[-max(3, len(hz) // 3):]
        fs = list(hz[o]); sig = list(zeta[o] * 2 * np.pi * hz[o]); wts = [1.0] * len(o); src = 'modes'
    fs, sig, wts = np.array(fs), np.array(sig), np.array(wts, float)
    # a power law: log sigma against log f, weighted by how long each band
    # was read over (s1 + s3 f^2 with s1 >= 0 could not pass through two
    # bands that fall faster than f^2 between them)
    lf, ls = np.log(fs / 1000.0), np.log(sig)
    if len(fs) >= 2 and np.ptp(lf) > 0.1:
        A = np.vstack([np.ones_like(lf), lf]).T
        c, *_ = np.linalg.lstsq(A * wts[:, None], ls * wts, rcond=None)
        s3 = float(np.clip(c[1], 0.0, 4.0))
        s1 = float(np.exp(np.average(ls - s3 * lf, weights=wts)))
    else:
        s3 = 2.0
        s1 = float(np.exp(np.average(ls - s3 * lf, weights=wts)))
    return s1, s3, src, list(zip(fs.tolist(), (6.91 / sig).tolist()))


# ── the dispersion ─────────────────────────────────────────────────────
def lag_ap(w, a):
    """a first-order allpass (a + z^-1) / (1 + a z^-1): its phase lag, rad"""
    return w - 2 * np.arctan2(a * np.sin(w), 1 + a * np.cos(w))


def hp_lag(w, fc, hk=0.8):
    """the in-loop fourth-order high-pass's lag (a lead: negative)"""
    out = np.zeros_like(w)
    for q in (0.5411961, 1.3065630):
        f = min(fc * hk, 0.45 * SR)
        ww = 2 * math.pi * f / SR; c = math.cos(ww); al = math.sin(ww) / (2 * q); a0 = 1 + al
        bq = [(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0]; aq = [1, -2 * c / a0, (1 - al) / a0]
        _, h = sg.freqz(bq, aq, worN=w)
        out -= np.angle(h)
    return out


MGRID = (0, 1, 2, 3, 4, 6, 8, 12, 16)
AGRID = np.linspace(-0.97, -0.05, 24)


def high_partials(x, fc, f0, t0=0.15, t1=1.5, within=40.0):
    """the recording's own partials above fc: (hz, amplitude) of the
    spectrum's peaks over t0-t1 within `within` dB of the loudest of them"""
    a, b = int(t0 * SR), min(len(x), int(t1 * SR))
    if b - a < int(0.2 * SR):
        return np.array([]), np.array([])
    seg = x[a:b] * np.hanning(b - a)
    n = 1 << int(math.ceil(math.log2(4 * (b - a))))
    S = 20 * np.log10(np.abs(np.fft.rfft(seg, n)) + 1e-20); f = np.fft.rfftfreq(n, 1 / SR)
    pk, _ = sg.find_peaks(S, prominence=10, distance=max(1, int(0.5 * f0 * n / SR)))
    pk = pk[(f[pk] > fc) & (f[pk] < min(16000.0, 0.45 * SR))]
    if not len(pk):
        return np.array([]), np.array([])
    pk = pk[S[pk] > S[pk].max() - within]
    return f[pk], 10 ** ((S[pk] - S[pk].max()) / 20)


def _series_score(hz, w, f0s, B, tol_cents=None, tol_spacing=None):
    """for each candidate f0 (vector), the weighted count of hz near the
    stiff-string series (f0, B): within tol_cents, or a fraction of the
    local spacing"""
    F = f0s[:, None]
    k = np.maximum(1, np.round(hz[None, :] / F / np.sqrt(1 + B * (hz[None, :] / F) ** 2)))
    for _ in range(2):
        fk = k * F * np.sqrt(1 + B * k * k)
        k = np.maximum(1, k + np.round((hz[None, :] - fk) / (F * (1 + 2 * B * k * k) / np.sqrt(1 + B * k * k))))
    fk = k * F * np.sqrt(1 + B * k * k)
    if tol_cents:
        d = 1200 * np.log2(hz[None, :] / fk) / tol_cents
    else:
        d = (hz[None, :] - fk) / (F * (1 + 2 * B * k * k) / np.sqrt(1 + B * k * k)) / tol_spacing
    return (w[None, :] * np.exp(-0.5 * d * d)).sum(axis=1)


def fit_B_peaks(fp, ap, f0, B):
    """(f0, B, share) that puts the most of the recording's high partials
    (amplitude-weighted) within a tenth of a spacing of the series"""
    f0s = f0 * 2 ** (np.arange(-30, 30.5, 0.5) / 1200)
    best = (-1.0, f0, B)
    for Bt in np.concatenate([[B], np.logspace(-6.5, -1.5, 121)]):
        sc = _series_score(fp, ap, f0s, Bt, tol_spacing=0.1)
        j = int(np.argmax(sc))
        if sc[j] > best[0]:
            best = (float(sc[j]), float(f0s[j]), float(Bt))
    return best[1], best[2], best[0] / float(ap.sum())


def design_dispersion(p, f0, B, fc, cap=16, hi=12000.0, env=None):
    """(M1, a1, M2, a2, kref, j, fref, err): two groups of first-order
    allpasses and the loop's resonance offset j against the stiff string's
    series over the waveguide's band. The loop's phase must be 2 pi (k + j)
    at partial k: j a whole number the fit chooses, since the loop need not
    count partials from the string's first — a stretched series is, over a
    band, close to a comb with an offset, and the offset is cheap where the
    stretch is not. Searched in the phase domain (the delay and j by least
    squares for every pair of groups), the cheapest within a little of the
    best (at most cap sections) taken, and checked by the library's Newton
    on the loop as it plays. err: rms over the band, weighted 1/k, in units
    of the local partial spacing."""
    hi = min(hi, 0.4 * SR)
    ks, fk = series(f0, B, fc, hi)
    if len(ks) < 2:
        return dict(M1=0, a1=0.0, M2=0, a2=0.0, kref=int(ks[0]) if len(ks) else 1, j=0, fref=float(fk[0]) if len(fk) else fc, err=float('nan'), err_cpp=float('nan'))
    if len(ks) > 200:
        sel = np.unique(np.round(np.linspace(0, len(ks) - 1, 200)).astype(int)); ks, fk = ks[sel], fk[sel]
    w = 2 * math.pi * fk / SR
    # weighted by the recording's own level there (its peaks' envelope), else 1/k
    wt = np.interp(fk, env[0], env[1]) if env is not None and len(env[0]) >= 2 else 1.0 / ks
    wt = np.maximum(wt, 1e-3 * wt.max()); wt = wt / wt.sum()
    base = 2 * math.pi * ks - hp_lag(w, fc)
    LA = np.array([lag_ap(w, a) for a in AGRID])            # (na, K)
    res = []
    for M1 in MGRID:
        for M2 in MGRID:
            if M2 > M1 or M1 + M2 > cap or (M1 == 0 and M2 > 0):
                continue
            A = M1 * LA[:, None, :] + M2 * LA[None, :, :] if M2 else (M1 * LA)[:, None, :]
            psi = base[None, None, :] - A                     # D w - 2 pi j = psi
            # weighted least squares for (D, j), then j rounded and D again
            Sww = (wt * w * w).sum(); Sw = (wt * w).sum(); S1 = wt.sum()
            Swp = (psi * (wt * w)).sum(-1); Sp = (psi * wt).sum(-1)
            det = Sww * S1 - Sw * Sw
            D = (Swp * S1 - Sw * Sp) / det
            J = -(Sww * Sp - Sw * Swp) / det / (2 * math.pi)
            J = np.round(J)
            D = (Swp + 2 * math.pi * J * Sw) / Sww
            e = (D[..., None] * w - 2 * math.pi * J[..., None] - psi) / (2 * math.pi)
            err = np.sqrt((e * e * wt).sum(-1))
            err = np.where((D > 3) & (D < 4000) & (ks[0] + J >= 1), err, np.inf)
            i1, i2 = np.unravel_index(np.argmin(err), err.shape)
            res.append((float(err[i1, i2]), M1, float(AGRID[i1]) if M1 else 0.0, M2, float(AGRID[i2]) if M2 else 0.0, int(J[i1, i2])))
    res.sort()
    best = res[0][0]
    ok = sorted([r for r in res if r[0] <= max(0.1, best * 1.2)], key=lambda r: (r[1] + r[3], r[0]))
    err, M1, a1, M2, a2, j = ok[0]
    kr = int(ks[len(ks) // 4]); fr = float(fk[len(ks) // 4])
    q = p.copy(); q[kp.P_WG] = 1; q[kp.P_F0] = f0; q[kp.P_FC] = fc
    q[kp.P_M] = M1; q[kp.P_AD] = a1; q[kp.P_M2] = M2; q[kp.P_AD2] = a2; q[kp.P_KREF] = kr + j; q[kp.P_FREF] = fr
    D, f, _ = kp.wg_partials(q, ks + j, fk)
    sp = np.gradient(fk) / np.maximum(np.gradient(ks.astype(float)), 1)
    ec = float(np.sqrt(np.average(((f - fk) / sp) ** 2, weights=wt))) if D > 0 else float('nan')
    return dict(M1=M1, a1=a1, M2=M2, a2=a2, kref=kr + j, j=j, fref=fr, err=err, err_cpp=ec, D=D, npart=len(ks))


# ── helpers for the fit loop ───────────────────────────────────────────
def energies(sig, t1=FIT_T1):
    return kp.band_energy(sig, t1)


def long_energy(sig, t0=0.15, hop=0.02):
    """mean square per band in 20 ms frames from t0 to the end"""
    h = int(hop * SR)
    a = int(t0 * SR)
    out = []
    for sos in kp.band_sos():
        y = sg.sosfiltfilt(sos, sig) ** 2
        n = (len(y) - a) // h
        out.append(y[a:a + n * h].reshape(n, h).mean(axis=1))
    return np.array(out)


def fit_level(X, Mo, Wu, mask=None):
    """g: log-energy least squares of Mo + g^2 Wu against X over the given
    bands and frames (those over the recording's floor); (g, cost dB)"""
    eps = X.max() * 1e-5
    lX = np.log(X + eps)
    sel = np.ones_like(X, bool) if mask is None else mask
    if sel.sum() < 3:
        sel = np.ones_like(X, bool)
    def r(lg):
        return (np.log(Mo + math.exp(2 * lg[0]) * Wu + eps) - lX)[sel]
    best = None
    for g0 in (-12.0, -6.0, 0.0, 6.0):
        s = so.least_squares(r, [g0], bounds=([-30.0], [30.0]))
        if best is None or s.cost < best.cost:
            best = s
    cost = float(np.sqrt(np.mean(r(best.x) ** 2)) * 10 / math.log(10))
    return math.exp(best.x[0]), cost


def rise_times(p, swing=1.0):
    """(t, arrival): the contact's impulse so far, which the noise rises under"""
    f = kp.contact(p, swing)
    if len(f) < 2:
        return np.array([0.0, 1e-4]), np.array([0.0, 1.0])
    return np.arange(len(f)) / SR, np.cumsum(f) / f.sum()


def fit_point(W, i, x, cls, log=print, want_wg=True):
    """the point's parameters for (b) and (c): (pb, pc, info)"""
    note = W.params[i]
    p = base_params(cls, note)
    W.set(i, p)
    n = int((FIT_T1 + 0.02) * SR)
    X, cs = energies(x[:n])
    rt = rise_times(p)
    # (b): the modes as they play, the noise over them. The modes' ramp is
    # the one thing of the contact the early window can see: how long the
    # partials take to build (a Wurlitzer's reed some 10 ms, where the fit's
    # model rose over 3 and played the note 28 dB early at 5 ms). A grid,
    # each ramp scored on its own noise fit over 0-50 ms and the whole window
    # the noise is fitted to what of the recording is unpitched: the floor
    # between its partials (and the bank's modes), not its band energy —
    # fitted to band energy, the noise stood in for whatever the modes fell
    # short by, pitched or not (a guitar G3's fundamental, louder early
    # than its fitted exponential, became a +15 dB noise burst in its band
    # and doubled the passage's peaks)
    hz_all, _, _ = W.modes(note)
    bound = kp.floor_energy(x[:n], hz_all)
    best = None
    for ramp in RAMPS:
        p2 = p.copy(); p2[kp.P_RAMP] = ramp
        W.set(i, p2)
        _, m, _, _ = W.render(note, 1.0, 1, n, parts=True)
        Em, _ = energies(m)
        # the noise arrives with the contact, or builds with the modes;
        # the pair is chosen on the whole: modes and noise against the
        # recording's band energy, over 0-50 ms and 0-150 ms
        for nrise in (0.0, ramp):
            r = rt if nrise == 0.0 else (np.arange(int(nrise * SR) + 1) / SR, 0.5 - 0.5 * np.cos(np.pi * np.arange(int(nrise * SR) + 1) / int(nrise * SR)))
            th, _, cost = fit_noise(X, Em, cs, r, bound=bound)
            early = early_cost(th, X, Em, cs, r)
            score = cost + early
            if best is None or score < best[0]:
                best = (score, ramp, nrise, r, th, cost, early, Em)
    _, ramp, nrise, rt, th_b, cost_b, early_b, Em = best
    p[kp.P_RAMP] = ramp; p[kp.P_NRISE] = nrise
    W.set(i, p)
    pb = theta_to_params(p, th_b)
    info = dict(note=note, cls=cls, tc=float(p[kp.P_TC]), ramp=float(ramp), nrise=float(nrise), cost_b=cost_b, early_b=early_b,
                floor_db=(10 * np.log10(bound + 1e-30)).round(1).tolist())
    if not want_wg:
        return pb, None, info
    # (c): the waveguide above the bank's top mode
    hz, zeta, gain = W.modes(note)
    live = gain > gain.max() * 10 ** (-60 / 20)
    ftop = float(hz[live].max())
    fc = 1.05 * ftop
    info.update(ftop=ftop, fc=fc)
    if fc > 0.40 * SR:
        info['wg'] = 'off: the bank reaches %.0f Hz' % ftop
        return pb, pb.copy(), info
    f0_nom = 440.0 * 2 ** ((note - 69) / 12)
    f0, B, share, med = fit_B(hz[live], zeta[live], gain[live], f0_nom)
    s1, s3, src, t60s = decay_law(x, fc, hz[live], zeta[live])
    # the waveguide is for partials above the bank that RING: the
    # recording's own bands up there must hold a decay (T60 >= 0.25 s). A
    # Wurlitzer's C4 has a click up there and then its floor by 0.3 s; a
    # level fitted after the click to a loop that dies in 0.1 s put the
    # waveguide's first pass 49 dB over the recording above 16 kHz
    if src != 'recording' or max(t for _, t in t60s) < 0.25:
        info.update(s1=s1, s3=s3, decay_src=src, t60s=t60s, wg='off: nothing above the bank rings in the recording (%s)' % (
            'no band held a decay' if src != 'recording' else 'longest T60 %.2f s' % max(t for _, t in t60s)))
        return pb, pb.copy(), info
    q = pb.copy()
    q[kp.P_WG] = 1; q[kp.P_F0] = f0; q[kp.P_FC] = fc; q[kp.P_S1] = s1; q[kp.P_S3] = s3
    # the target series: the recording's own partials above the bank where
    # it has enough of them (the band's top from them too), else the bank's
    fp, ap = high_partials(x, fc, f0)
    hi = 12000.0
    env = None
    info.update(B_bank=B, f0_bank=f0, npeaks=len(fp))
    if len(fp) >= 8:
        f0, B, sh2 = fit_B_peaks(fp, ap, f0, B)
        hi = float(max(1.3 * fc, fp.max() * 1.02))
        o = np.argsort(fp)
        env = (fp[o], sg.medfilt(ap[o], 5) if len(fp) >= 5 else ap[o])
        info.update(B_peaks_share=sh2)
    q[kp.P_F0] = f0
    info.update(f0=f0, B=B, wg_top=hi)
    dd = design_dispersion(q, f0, B, fc, hi=hi, env=env)
    q[kp.P_M] = dd['M1']; q[kp.P_AD] = dd['a1']; q[kp.P_M2] = dd['M2']; q[kp.P_AD2] = dd['a2']
    q[kp.P_KREF] = dd['kref']; q[kp.P_FREF] = dd['fref']
    info.update(f0=f0, B=B, B_share=share, B_med_cents=med, s1=s1, s3=s3, decay_src=src, t60s=t60s, disp=dd)
    # the input: level and tilt where only the waveguide can explain the
    # energy — the bands above the crossover, 0.15-2 s, after the attack's
    # noise has gone (over 0-150 ms alone the noise bands explain the high
    # band as well as the waveguide does and the joint fit left it out) —
    # then the noise refitted over 0-150 ms with the waveguide playing
    nl = min(len(x), int(2.0 * SR))
    T0 = 0.0          # from the strike: the loop's first pass counts
    hib = [k for k in range(kp.NB) if kp.EDGES[k] >= 0.9 * fc and kp.EDGES[k] < 0.45 * SR]
    info['wg_bands'] = hib
    if not hib:
        info['wg'] = 'off: no band above the crossover'
        return pb, pb.copy(), info
    Xl = long_energy(x[:nl], T0)
    # the recording's own floor a band (its last 15%, over the whole take):
    # a band that never stands 12 dB over it early has no partials above the
    # bank to supply — the Wurlitzer's C5 is its noise floor up there, and a
    # level fitted to a floor put the waveguide 30 dB over the recording
    Xall = long_energy(x, T0)
    fl = np.array([np.median(Xall[k, -max(3, Xall.shape[1] * 15 // 100):]) for k in range(kp.NB)])
    early_hi = Xl[:, :min(Xl.shape[1], 12)].mean(axis=1)
    live = [k for k in hib if early_hi[k] > fl[k] * 10 ** 1.2]
    info['wg_live_bands'] = live
    if not live:
        info['wg'] = 'off: nothing above the bank stands 12 dB over the recording\'s floor'
        return pb, pb.copy(), info
    hib = live
    mask = Xl > fl[:, None] * 10 ** 0.6
    _, ml, _, _ = W.render(note, 1.0, 1, nl, parts=True)
    Ml = long_energy(ml, T0)
    best = None
    for tilt in (0.0, 8 * fc, 4 * fc, 2.8 * fc, 2 * fc, 1.4 * fc, 1.0 * fc, 0.7 * fc, 0.5 * fc):
        if tilt > 0.45 * SR:
            continue
        q2 = q.copy(); q2[kp.P_GIN] = 1.0; q2[kp.P_TILT] = tilt
        q2[kp.P_NL:kp.P_NL + 12] = 0.0
        W.set(i, q2)
        _, _, _, w1 = W.render(note, 1.0, 2, nl, parts=True)
        Wl = long_energy(w1, T0)
        g, cost = fit_level(Xl[hib], Ml[hib], Wl[hib], mask[hib])
        if best is None or cost < best[0]:
            best = (cost, tilt, g)
    cost_l, tilt, g = best
    # and never over the recording in the attack: the waveguide's first 10 ms
    # above the crossover at most 3 dB over the recording's (a level fitted
    # over 2 s let an outlier's first pass stand 45 dB over the ringing
    # before it, measured in the passage)
    qa = q.copy(); qa[kp.P_GIN] = g; qa[kp.P_TILT] = tilt; qa[kp.P_NL:kp.P_NL + 12] = 0.0
    W.set(i, qa)
    _, _, _, wa = W.render(note, 1.0, 2, int(0.03 * SR), parts=True)
    hp = sg.butter(4, min(fc, 0.45 * SR), 'highpass', fs=SR, output='sos')
    ew = float(np.mean(sg.sosfiltfilt(hp, wa)[:int(0.01 * SR)] ** 2))
    ex = float(np.mean(sg.sosfiltfilt(hp, x[:int(0.03 * SR)])[:int(0.01 * SR)] ** 2))
    capped = False
    if ew > 2.0 * ex > 0:
        g *= math.sqrt(2.0 * ex / ew); capped = True
    info['wg_attack_capped'] = capped
    # the noise is (b)'s: the unpitched part does not change with what
    # plays the pitched part
    pc = q.copy(); pc[kp.P_GIN] = g; pc[kp.P_TILT] = tilt
    info.update(cost_wg_long=cost_l, tilt=tilt, gin=g)
    W.set(i, pb)
    return pb, pc, info
