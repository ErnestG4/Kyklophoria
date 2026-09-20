#!/usr/bin/env python3
"""modalfit — a recording of a struck thing, fitted as a modal set on the GPU.

    modalfit.py in.wav out.mmr [--resynth out.wav] [--modes 48] [--seconds 2.0]
                [--steps 600] [--onset -30] [--device cuda]

The FEM supplies parametric freedom; a recording supplies the truth. This is
the truth half: take a strike, find its partials, and fit (frequency, decay,
amplitude, phase) for every mode by gradient so that the resynthesis matches
the recording. The result is a corpus record — the same .mmr as modalfem
writes — with a measured decay per mode instead of a Rayleigh model's, which
is where a Rhodes stops being plausible and starts being one.

How it fits:

  1. The onset is the first sample above --onset dBFS. Everything before it is
     discarded; --seconds after it are the target.
  2. Initialisation is classical modal analysis: an STFT, the mean magnitude
     over the ring, its highest peaks as candidate modes; for each, the
     frequency from the phase advance of its bin between hops (the phase
     vocoder's estimate, good to a fraction of a hertz where a bin is twelve)
     and the decay rate from a linear fit of log magnitude against time.
  3. Refinement is gradient descent on a differentiable renderer — a sum of
     decaying sines — against a multi-resolution log-magnitude STFT loss (three
     window sizes), which does not care about phase and does care about
     frequency and decay at every scale. Adam, --steps steps, on the GPU if
     there is one; a fit is a few seconds. Frequencies move at a fortieth of
     the rate of everything else: the loss in frequency is a comb, and a step
     of a bin lands in the wrong tooth. The first version stepped 2% at a time
     and oscillated at 100 cents; the phase-vocoder start plus a slow
     frequency is what recovers a synthetic strike to under a cent.
  4. The record: hz, zeta = rate / omega, and the fitted amplitude as the gain
     at every one of P positions — a recording is one position, and the record
     says so with `fitted 1` in its header. A corpus that mixes fitted and FEM
     models should treat the fitted ones' gain patterns as flat, which is what
     they are.

Checked on a render whose modes are known (tools/modalfit_check.py): the
four modes above -20 dB come back within a cent and within 2% in T60; the two
at -26 dB that ring for 80 ms are dropped rather than misfitted.

Needs torch, numpy, soundfile: the fmexplorer venv on this machine.
"""
import argparse
import math
import sys

import numpy as np
from scipy import signal
import soundfile as sf
import torch


def load(path, seconds, onset_db, normalise=True):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1).astype(np.float64)
    # the onset is found from the strike, not from the start: the envelope's
    # peak, then back to the last 5 ms that sat 20 dB below it, less 10 ms for
    # the ramp. A struck or plucked thing goes from nothing to its peak inside
    # that; a file that opens with a hand on the strings at 22 dB below the
    # note (the Philharmonia guitar's A3, for a second and a half) would
    # trigger any threshold taken from the front, and did — the fit started
    # in the noise and starved. onset_db is the floor a file must reach at all
    hop = max(1, int(0.005 * sr))
    env = np.sqrt(np.array([np.mean(x[i:i + hop] ** 2) for i in range(0, len(x) - hop + 1, hop)]))
    if not len(env) or env.max() < 10 ** (onset_db / 20):
        raise ValueError('no onset above %g dBFS' % onset_db)
    pk = int(env.argmax())
    quiet = np.where(env[:pk] < env[pk] * 10 ** (-20 / 20))[0]
    idx = max(0, (int(quiet[-1]) - 1) * hop) if len(quiet) else 0
    x = x[idx: idx + int(seconds * sr)]
    # nothing under 40 Hz is a mode of anything here, and the Philharmonia
    # guitar carries a quarter of its energy there (one note, 99%) — stand
    # rumble and mic handling that the fit was spending its convergence on
    # and could never explain. Eighth-order Butterworth, run both ways
    sos = signal.butter(8, 40.0, 'highpass', fs=sr, output='sos')
    x = signal.sosfiltfilt(sos, x)
    if normalise:
        x = x / (np.max(np.abs(x)) or 1.0)
    return x, sr


def initialise(x, sr, nmodes, nfft=8192, hop=512, report=None):
    """Peaks of the mean spectrum, verified, with a decay per peak from its
    bin's track.

    Verification is what the first version lacked, and what let a whistle into
    the guitars: a peak of the *mean* spectrum can be a partial, or it can be a
    noise ridge, an mp3 artefact, a room resonance or mains hum, and every one
    of those fits perfectly well as a sinusoid that never decays. Three tests
    from the sinusoidal-modelling literature, each a number a reader can check:

      prominence   the peak stands at least 8 dB above the median of the two
                   octaves' worth of bins around it, in the mean spectrum —
                   a ridge in the noise does not
      coherence    the phase advance of its bin between hops is steady: the
                   deviation from the bin's own frequency has a standard
                   deviation under a third of a radian over the frames where
                   the partial is above its floor — noise wanders, a sinusoid
                   does not
      decay        its log magnitude over time is a line going down: the fit's
                   r^2 is at least 0.5 and the slope negative — or, for a
                   partial that beats and so is no line, its first quarter
                   stands 3 dB above its last — a partial dies, hum and a
                   room mode do not

    A candidate that fails any test is reported, not fitted."""
    win = np.hanning(nfft)
    frames = []
    for s in range(0, len(x) - nfft, hop):
        frames.append(np.fft.rfft(x[s:s + nfft] * win))
    C = np.array(frames)                       # complex, frames x bins
    S = np.abs(C)
    mean = S.mean(axis=0)
    # the fine spectrum for doublets: 65536 points over the first 1.4 s,
    # 0.7 Hz a bin. A mandolin course is two strings 3 Hz apart, a tine and
    # its tone bar the same; on the 5 Hz grid above they are one peak, and
    # one sine can only make a beating envelope by dying fast — which is what
    # the fits did, T60s 2-20x short wherever partials beat
    nfine = 65536
    seg = x[:nfine] * np.hanning(min(len(x), nfine))
    F = np.abs(np.fft.rfft(seg, nfine))
    logmean = np.log(mean + 1e-12)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    cand = []
    for b in range(2, len(mean) - 2):
        if freqs[b] < 40 or freqs[b] > 20000:
            continue
        if mean[b] > mean[b - 1] and mean[b] >= mean[b + 1] and mean[b] > mean[b - 2] and mean[b] >= mean[b + 2]:
            cand.append((mean[b], b))
    cand.sort(reverse=True)
    modes = []
    rejected = {'prominence': 0, 'coherence': 0, 'decay': 0}
    t = np.arange(len(S)) * hop / sr
    for h, b in cand:
        if len(modes) >= nmodes:
            break
        # prominence over the local floor: the median over an octave each side
        lo, hi = max(1, b // 2), min(len(mean) - 1, b * 2)
        floor = np.median(logmean[lo:hi])
        if logmean[b] - floor < 8.0 / 8.686:          # 8 dB, in nepers
            rejected['prominence'] += 1
            continue
        track = np.log(S[:, b] + 1e-9)
        ok = track > track.max() - 6.0
        dphi = np.angle(C[1:, b] * np.conj(C[:-1, b]))
        expect = 2 * math.pi * b * hop / nfft
        dev = np.angle(np.exp(1j * (dphi - expect)))
        good = ok[1:] & ok[:-1]
        if good.sum() >= 2 and np.std(dev[good]) > 0.33:
            rejected['coherence'] += 1
            continue
        f = (expect + (np.median(dev[good]) if good.any() else 0.0)) * sr / (2 * math.pi * hop)
        if ok.sum() >= 3:
            p = np.polyfit(t[ok], track[ok], 1)
            pred = np.polyval(p, t[ok])
            ss = np.sum((track[ok] - track[ok].mean()) ** 2)
            r2 = 1.0 - np.sum((track[ok] - pred) ** 2) / ss if ss > 0 else 0.0
            # the decay itself is read off the falling part — from the
            # track's peak, within 20 dB of it — because a course's
            # fundamental swells for a quarter second first, and a line
            # through the swell says the partial does not decay at all
            pk_i = int(track.argmax())
            fall = np.zeros(len(track), dtype=bool)
            fall[pk_i:] = track[pk_i:] > track[pk_i] - 20 / 8.686
            p_fall = np.polyfit(t[fall], track[fall], 1) if fall.sum() >= 4 else p
            # a partial that beats — a string's two polarisations — is not
            # a line, but it still falls: the first quarter of its track
            # against the last, 3 dB, is a drop that hum and a room never make
            q = max(1, ok.sum() // 4)
            drop = track[ok][:q].mean() - track[ok][-q:].mean()
            if p[0] > 0 or (r2 < 0.5 and ok.sum() > 6 and drop < 3.0 / 8.686):
                rejected['decay'] += 1
                continue
            rate = max(0.5, -p_fall[0])
        else:
            rate = 5.0
        amp = S[0, b] / (nfft / 4)
        parts = split(F, f, sr, nfine, nfft)
        beat = beat_of(track, ok, hop / sr) if ok.sum() >= 3 else None
        if len(parts) == 2 and beat and beat[1]:
            parts = [(parts[0][0], parts[0][1], 0.0), (parts[1][0], parts[1][1], math.pi)]
        elif len(parts) == 1 and beat:
            # a pair the spectrum cannot resolve still beats: periodic nulls in
            # the bin's envelope, at the difference frequency. Read the beat
            # period off the autocorrelation of the detrended log track and
            # start two sines that far apart; if the track opens below its
            # own trend, they open in antiphase — a course's two strings
            # swelling up from the pluck rather than falling from it
            df, anti = beat
            parts = [(f - df / 2, 0.5, 0.0), (f + df / 2, 0.5, math.pi if anti else 0.0)]
            rejected['paired'] = rejected.get('paired', 0) + 1
        knee = knee_of(track, t)
        if knee:
            rejected['double'] = rejected.get('double', 0) + 1
        # the hammer: where the track's first frame stands more than 3 dB
        # above the decay line's own value there, the difference is a fast
        # partner at the same frequency — the thump the fits used to invent
        # for themselves with a free decay, proposed here with a 50 ms one
        thump = None
        if ok.sum() >= 4:
            over = track[0] - np.polyval(p_fall, t[0])
            if over > 3.0 / 8.686:
                thump = amp * (1.0 - np.exp(-over))
                rejected['thump'] = rejected.get('thump', 0) + 1
        for part in parts:
            ff, share = part[0], part[1]
            ph = part[2] if len(part) > 2 else 0.0
            if knee and len(parts) == 1:
                for rr, sh in knee:
                    modes.append((ff, rr, amp * share * sh, ph))
            elif knee:
                # a resolved pair with a knee: the pair is the double decay
                rr = knee[0][0] if part is parts[0] else knee[1][0]
                modes.append((ff, rr, amp * share, ph))
            else:
                modes.append((ff, rate, amp * share, ph))
        if thump is not None:
            modes.append((f, 6.91 / 0.05, thump, 0.0))
    if len(modes) > nmodes:
        # the split can overrun the budget: keep the loudest
        modes = sorted(modes, key=lambda m: -m[2])[:nmodes]
    if report is not None:
        report.update(rejected)
    return modes


def split(F, f, sr, nfine, nfft, rel_db=-20.0, min_sep_hz=1.5):
    """One coarse peak at f becomes the fine peaks within +-1 coarse bin of it
    that stand within rel_db of the strongest and at least min_sep_hz apart:
    [(frequency, amplitude share)], shares summing to one. A single partial
    comes back as itself."""
    half = int(round(sr / nfft * nfine / sr))          # one coarse bin, in fine bins
    c = int(round(f * nfine / sr))
    lo, hi = max(1, c - half), min(len(F) - 1, c + half)
    seg = F[lo:hi]
    if len(seg) < 3:
        return [(f, 1.0)]
    pk, _ = signal.find_peaks(seg, height=seg.max() * 10 ** (rel_db / 20),
                              distance=max(1, int(min_sep_hz * nfine / sr)))
    # two peaks are two partials only if the valley between them is real:
    # 6 dB under the lower of the two. A broad thump's ripple is not a chord
    pk = list(pk)
    keep = []
    for i, p in enumerate(pk):
        if keep:
            q = keep[-1]
            valley = seg[q:p + 1].min()
            if valley > min(seg[q], seg[p]) * 10 ** (-6.0 / 20):
                if seg[p] > seg[q]:
                    keep[-1] = p
                continue
        keep.append(p)
    pk = sorted(keep, key=lambda p: -seg[p])[:3]
    if len(pk) < 2:
        return [(f, 1.0)]
    out = []
    for p in sorted(pk):
        b = lo + p
        # parabolic interpolation on the log magnitude
        if 0 < b < len(F) - 1:
            a0, a1, a2 = np.log(F[b - 1] + 1e-12), np.log(F[b] + 1e-12), np.log(F[b + 1] + 1e-12)
            d = 0.5 * (a0 - a2) / (a0 - 2 * a1 + a2) if (a0 - 2 * a1 + a2) != 0 else 0.0
        else:
            d = 0.0
        out.append(((b + d) * sr / nfine, F[b]))
    tot = sum(a for _, a in out)
    return [(ff, a / tot) for ff, a in out]


def knee_of(track, t, min_frames=10):
    """A partial with two decays — a string pair's in-phase and antiphase
    modes damp differently, and the track falls fast, then slow — comes back
    as [(rate_fast, 1.0), (rate_slow, share)], the share being the slow
    component's level extrapolated to t = 0 relative to the fast one. A
    single exponential comes back as None. Measured on the falling part of
    the log track within 25 dB of its peak: a line through its first 40% and
    a line through its last 60%, and a knee is a late rate under 0.6 of the
    early one."""
    pk = int(track.argmax())
    fall = np.zeros(len(track), dtype=bool)
    fall[pk:] = track[pk:] > track[pk] - 25 / 8.686
    idx = np.where(fall)[0]
    if len(idx) < min_frames:
        return None
    idx = idx[:int(np.argmax(np.diff(idx) > 1)) + 1] if (np.diff(idx) > 1).any() else idx  # the contiguous run
    if len(idx) < min_frames:
        return None
    cut = idx[0] + int(0.4 * len(idx))
    e = idx[idx < cut]; l = idx[idx >= cut]
    if len(e) < 4 or len(l) < 4:
        return None
    pe = np.polyfit(t[e], track[e], 1); pl = np.polyfit(t[l], track[l], 1)
    re_, rl = -pe[0], -pl[0]
    if not (re_ > 0.5 and rl > 0.2 and rl < 0.6 * re_):
        return None
    share = float(np.exp(min(0.0, pl[1] - pe[1])))
    return [(re_, 1.0), (rl, share)]


def beat_of(track, ok, dt, min_depth=0.35):
    """(difference frequency, antiphase?) for a bin whose envelope beats, or
    None. The log track over its above-floor frames is detrended by its own
    line; a beating pair leaves a periodic residual deeper than min_depth
    nepers (3 dB), whose autocorrelation peaks at the beat period."""
    idx = np.where(ok)[0]
    if len(idx) < 8:
        return None
    seg = track[idx[0]:idx[-1] + 1]
    tt = np.arange(len(seg)) * dt
    d = seg - np.polyval(np.polyfit(tt, seg, 1), tt)
    if d.std() < min_depth:
        return None
    d = d - d.mean()
    ac = np.correlate(d, d, 'full')[len(d) - 1:]
    ac /= ac[0] if ac[0] > 0 else 1.0
    # first peak after the first zero crossing
    z = np.argmax(ac < 0) if (ac < 0).any() else 0
    if z == 0 or z >= len(ac) - 2:
        return None
    pk = z + int(np.argmax(ac[z:]))
    # a course beats at a few hertz: a period under ten frames (~115 ms,
    # 8.6 Hz) is not a pair, it is the attack or noise in the autocorrelation
    if ac[pk] < 0.2 or pk < 10:
        return None
    period = pk * dt
    if period > 0.6 * len(seg) * dt:
        return None                                # fewer than two beats seen
    anti = d[0] < -0.5                             # opens 4 dB under its trend
    return 1.0 / period, bool(anti)


def decay_ratio(f, r, amp, x, sr, nfft=4096, hop=256, top=10):
    """Fitted T60 against the recording's own, for the `top` modes by ring
    energy: the energy-weighted geometric mean of fit/track. One is right; a half is a model
    twice as dead as the instrument. The track's T60 is the slope of the log
    magnitude at the mode's bin over the frames within 10 dB of its peak —
    a beating pair shares a bin, and the line through the beats is the pair's
    decay."""
    if not len(f):
        return 1.0
    win = np.hanning(nfft)
    S = np.log(np.array([np.abs(np.fft.rfft(x[s:s + nfft] * win)) for s in range(0, len(x) - nfft, hop)]) + 1e-9)
    t = np.arange(len(S)) * hop / sr
    energy = amp ** 2 / (2 * np.maximum(r, 1e-3))
    # modes sharing a bin — a thump under a partial, a pair, a double decay —
    # are one track to the ear: the bin's decay is its slowest component's,
    # and the bin's energy is the sum
    groups = []                                    # [energy, r_slow, f]
    for i in np.argsort(f):
        if groups and (f[i] - groups[-1][2]) < max(0.015 * f[i], 1.5 * sr / nfft):
            groups[-1][0] += energy[i]
            groups[-1][1] = min(groups[-1][1], r[i])
        else:
            groups.append([energy[i], r[i], f[i]])
    ratios = []
    for e, r_slow, fg in sorted(groups, key=lambda g: -g[0])[:top]:
        b = int(round(fg * nfft / sr))
        if b < 1 or b >= S.shape[1] - 1:
            continue
        tr = S[:, b - 1:b + 2].max(axis=1)
        # from the track's peak on — a course's fundamental can swell for a
        # quarter second before it falls — and within 20 dB of it, so that
        # the line runs through the beats rather than along their tops
        pk = int(tr.argmax())
        ok = np.zeros(len(tr), dtype=bool)
        ok[pk:] = tr[pk:] > tr[pk] - 20 / 8.686
        if ok.sum() < 6:
            continue
        slope = np.polyfit(t[ok], tr[ok], 1)[0]
        if slope >= -0.05 or (tr[pk] - tr[ok][-1]) < 10 / 8.686:
            continue                                   # a decay the window did not show 10 dB of was not measured
        ratios.append((np.log((6.91 / r_slow) / (6.91 / -slope)), e))
    if not ratios:
        return 1.0
    # weighted by ring energy: a thump at a three-hundredth of the loudest
    # partial's energy, judged against a track that is the room's, does not
    # get a vote equal to the fundamental's
    w = np.array([e for _, e in ratios])
    return float(np.exp(np.sum([v * e for v, e in ratios]) / w.sum()))


def excess_db(y, x, sr, n=2048, hop=512):
    """The whistle detector: how much energy the resynthesis has where the
    recording has none. Over the time-frequency cells where the target sits at
    or below its own floor (20th percentile of log magnitude), the mean of the
    model's excess above that floor, in dB. A clean fit is under 1; a whistle
    is several."""
    tx = torch.tensor(x, dtype=torch.float32)
    ty = torch.tensor(y, dtype=torch.float32)
    X = torch.log(stft_mag(tx, n, hop) + 1e-4)
    Y = torch.log(stft_mag(ty, n, hop) + 1e-4)
    floor = torch.quantile(X.flatten(), 0.2)
    quiet = X <= floor
    if not quiet.any():
        return 0.0
    return float(8.686 * torch.relu(Y[quiet] - floor).mean())


def validate(f, r, amp, x, sr, nfft=4096, hop=256, margin_db=6.0):
    """Drop a fitted mode the recording does not show: over the mode's own
    first half-second (or its T60, if shorter), the target's magnitude at its
    frequency has to sit `margin_db` above the target's floor at that time. A
    mode that passed the fit but not this is one the optimiser invented to fill
    a hole in the loss, and it is the kind that whistles."""
    win = np.hanning(nfft)
    frames = [np.abs(np.fft.rfft(x[s:s + nfft] * win)) for s in range(0, len(x) - nfft, hop)]
    S = np.log(np.array(frames) + 1e-9)
    floor = np.quantile(S, 0.2)
    keep = np.ones(len(f), dtype=bool)
    for i in range(len(f)):
        b = int(round(f[i] * nfft / sr))
        if b < 1 or b >= S.shape[1]:
            keep[i] = False
            continue
        n = max(1, min(len(S), int(min(0.5, 6.91 / r[i]) * sr / hop)))
        seen = S[:n, max(0, b - 1):b + 2].max()
        if seen - floor < margin_db / 8.686:
            keep[i] = False
    return keep


def stft_mag(y, n, hop):
    win = torch.hann_window(n, device=y.device)
    return torch.stft(y, n, hop, window=win, return_complex=True).abs()


ATTACK_MS = 3.0
MIN_T60 = 0.005      # s; faster is exciter, not mode
import os as _os
DECAY_PRIOR = float(_os.environ.get('MB_DECAY_PRIOR', 2.0))   # weight holding log decay to the track's measurement
DECAY_BAND = float(_os.environ.get('MB_DECAY_BAND', 0.405))   # ln 1.5: the free band around it


def audible(amp, r, floor_db=-60.0, at=0.01):
    """Which modes stand within floor_db of the loudest, judged 10 ms in rather
    than at t=0, where a fast mode's amplitude says nothing about what is heard."""
    a = amp * np.exp(-r * at)
    return a > a.max() * 10 ** (floor_db / 20)


def attack_ramp(n, sr, ms=ATTACK_MS):
    """A raised cosine over the first `ms`, then one."""
    k = int(ms * sr / 1000)
    ramp = np.ones(n)
    ramp[:k] = 0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k)
    return ramp


def resynth(f, r, amp, n, sr, phase=None):
    """The record as sound: decaying sines at their fitted phases, under the
    model's own attack. From zero phase every partial rises together and
    the sum is a spike the recording never had — ten to twenty-five times
    the target's energy in the first 40 ms, measured on four instruments;
    the phases are where the hammer's timing per mode lives."""
    t = np.arange(n) / sr
    y = np.zeros(n)
    for i in range(len(f)):
        y += amp[i] * np.exp(-r[i] * t) * np.sin(2 * math.pi * f[i] * t + (phase[i] if phase is not None else 0.0))
    return y * attack_ramp(n, sr)


def fit(x, sr, init, steps, device, verbose=True):
    t = torch.arange(len(x), device=device, dtype=torch.float32) / sr
    target = torch.tensor(x, device=device, dtype=torch.float32)
    f0 = torch.tensor([m[0] for m in init], device=device)
    r0 = torch.tensor([m[1] for m in init], device=device)
    a0 = torch.tensor([m[2] for m in init], device=device)
    logf = torch.log(f0).clone().requires_grad_(True)
    logr = torch.log(r0).clone().requires_grad_(True)
    loga = torch.log(a0 + 1e-6).clone().requires_grad_(True)
    phase = torch.tensor([m[3] if len(m) > 3 else 0.0 for m in init], device=device, dtype=torch.float32).requires_grad_(True)
    # the decay is measured, not fitted: the track's own slope is a better
    # damping than the STFT loss can find through a beating pair or a swell
    # (the fits had come back 2-20x too dead on the mandolin), so the log
    # decay is held to it — a factor of two costs about as much as the rest
    # of the loss — and amplitude, phase and frequency stay free
    logr0 = logr.detach().clone()
    opt = torch.optim.Adam([{'params': [logr, loga, phase], 'lr': 0.02},
                            {'params': [logf], 'lr': 0.0005}])
    scales = [(4096, 1024), (1024, 256), (256, 64)]
    tmag = [stft_mag(target, n, h) for n, h in scales]
    tlog = [torch.log(m + 1e-4) for m in tmag]
    tnorm = [m.norm() for m in tmag]
    # the target's own floor, per scale: the 20th percentile of its log
    # magnitude. Below it there is nothing to match, only noise to imitate.
    tfloor = [torch.quantile(tl.flatten(), 0.2) for tl in tlog]

    # a sine that starts as a step is a click: after the partials were right,
    # nine tenths of what the whistle metric still measured on the Wurlitzer
    # sat above 12 kHz in the first frame. A hammer takes about 2 ms to arrive
    # (the recording's own rise to half its peak), and the model does too
    ramp = torch.tensor(attack_ramp(len(x), sr), device=device, dtype=torch.float32)

    def render():
        f, r, a = torch.exp(logf), torch.exp(logr), torch.exp(loga)
        env = torch.exp(-r[:, None] * t[None, :]) * ramp[None, :]
        return (a[:, None] * env * torch.sin(2 * math.pi * f[:, None] * t[None, :] + phase[:, None])).sum(0)

    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=steps, eta_min=0.0)
    for step in range(steps):
        opt.zero_grad()
        y = render()
        # three terms a scale. Spectral convergence, relative and so dominated
        # by the loud partials. Log-magnitude L1 at a tenth of the weight —
        # Diaz et al. (ICASSP 2023) use 1.0 linear against 0.1 log for the same
        # fit, and equal weights here let the floor's two thousand bins outvote
        # the partials: the loudest modes rang four times too long. And an
        # asymmetric term, the model's log magnitude above the target's where
        # the target is at its own floor — energy the instrument never made,
        # which is exactly what a whistle is. The log terms sit on a floor at
        # the target's 20th percentile, so imitating noise buys nothing.
        loss = 0.0
        for (n, h), tm, tl, tn, tf in zip(scales, tmag, tlog, tnorm, tfloor):
            ym = stft_mag(y, n, h)
            yl = torch.log(ym + 1e-4)
            sc = (ym - tm).norm() / tn
            lg = (torch.clamp(yl, min=tf) - torch.clamp(tl, min=tf)).abs().mean()
            excess = torch.relu(yl - torch.clamp(tl, min=tf))
            ex = excess[tl <= tf].mean() if (tl <= tf).any() else excess.mean()
            loss = loss + sc + 0.1 * lg + 1.0 * ex
        # a band, not a point: free within DECAY_BAND of the measurement so a
        # pair can split into the fast and slow decays of a real course
        loss = loss + DECAY_PRIOR * (torch.relu((logr - logr0).abs() - DECAY_BAND) ** 2).mean()
        loss.backward()
        opt.step()
        sched.step()
        # nothing that dies inside 5 ms is a mode: it is the exciter, and left
        # free the optimiser builds the hammer's click out of one overdamped
        # sine at a million times the loudest partial (the EP's e5 did: one
        # mode at 14.5 kHz, zeta 1.9, and every real partial gone under it)
        with torch.no_grad():
            logr.clamp_(max=math.log(6.91 / MIN_T60))
        if verbose and (step % 100 == 0 or step == steps - 1):
            print('  step %4d  loss %.4f' % (step, loss.item()))
    with torch.no_grad():
        y = render()
        f, r, a = torch.exp(logf), torch.exp(logr), torch.exp(loga)
    ph = torch.remainder(phase.detach(), 2 * math.pi)
    return f.cpu().numpy(), r.cpu().numpy(), a.cpu().numpy(), y.cpu().numpy(), loss.item(), ph.cpu().numpy()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('out')
    ap.add_argument('--resynth', help='write the resynthesis here')
    ap.add_argument('--target', help='write the analysed excerpt here, at the same level, for an A/B')
    ap.add_argument('--max-t60', type=float, default=0.0,
                    help='cap a fitted T60 at this many seconds (0: three times the analysed length). A decay longer '
                         'than the window was not measured; on recordings it is room rumble and mp3 floor')
    ap.add_argument('--modes', type=int, default=48)
    ap.add_argument('--seconds', type=float, default=2.0)
    ap.add_argument('--steps', type=int, default=600)
    ap.add_argument('--onset', type=float, default=-30.0)
    ap.add_argument('--positions', type=int, default=12)
    ap.add_argument('--floor', type=float, default=-60.0, help='drop fitted modes this far below the loudest, dB')
    ap.add_argument('--device', default='cuda' if torch.cuda.is_available() else 'cpu')
    a = ap.parse_args()
    x, sr = load(a.wav, a.seconds, a.onset)
    rep = {}
    init = initialise(x, sr, a.modes, report=rep)
    print('  %d candidate modes from the spectrum, %.1f .. %.1f Hz; rejected %s' % (
        len(init), min(m[0] for m in init), max(m[0] for m in init), rep))
    f, r, amp, y, loss, ph = fit(x, sr, init, a.steps, a.device)
    # a mode the fit has turned down to nothing is a mode it could not place,
    # not a quiet one: keep it out of the record rather than in with a decay
    # nobody measured
    cap = a.max_t60 if a.max_t60 > 0 else 3.0 * a.seconds
    r = np.maximum(r, 6.91 / cap)
    keep = audible(amp, r, a.floor) & validate(f, r, amp, x, sr)
    dropped = int((~keep).sum())
    f, r, amp, ph = f[keep], r[keep], amp[keep], ph[keep]
    order = np.argsort(f)
    # resynthesise what survived, for the file and for the whistle detector
    y = resynth(f, r, amp, len(x), sr, ph)
    ex = excess_db(y, x, sr)
    with open(a.out, 'w') as o:
        o.write('# modalfit record: a strike fitted as decaying sines on %s. zeta is measured,\n' % a.device)
        o.write('# gains are the fitted amplitude at the one position the recording is, repeated.\n')
        o.write('source %s\nfitted 1\nloss %.5f\nexcess_db %.3f\n' % (a.wav, loss, ex))
        o.write('positions %d\n' % a.positions)
        o.write('modes %d\n' % len(order))
        for k, i in enumerate(order):
            w = 2 * math.pi * f[i]
            o.write('mode %d hz %.6f zeta %.9g phase %.5f gains %s\n' % (k, f[i], r[i] / w, ph[i], ' '.join('%.9g' % amp[i] for _ in range(a.positions))))
    if a.resynth:
        sf.write(a.resynth, np.clip(y / (np.max(np.abs(y)) or 1.0) * 0.5, -1, 1), sr)
    if a.target:
        sf.write(a.target, np.clip(x * 0.5, -1, 1), sr)
    print('  fitted %d modes (%d dropped), final loss %.4f, excess %.2f dB -> %s' % (len(order), dropped, loss, ex, a.out))
    for i in order[:8]:
        print('    %8.1f Hz  T60 %.2fs  amp %.3g' % (f[i], 6.91 / r[i], amp[i]))


if __name__ == '__main__':
    main()


# ---------------------------------------------------------------------------
# The pickup as a stage: a linear bank into a nonlinear field, differentiated.
#
# A Rhodes tine moves as a pure sine after its first ten milliseconds
# (Muenster & Pfeifle, ISMA 2014, tracked at 38 kfps); every harmonic at the
# jack is made by the pickup, because the tine samples a strongly non-uniform
# field and the coil reads the RATE of flux change. So a recording at one
# velocity cannot separate the metal from the transducer — the harmonics look
# like modes — but several velocities of the same note can: the metal's
# partials are shared, only the strike amplitude differs, and the harmonics
# that grow with it are the field's. That is what this fits, jointly:
#
#   u_k(t) = g_k · sum_i a_i e^{-r_i t} sin(2 pi f_i t + phi_i)     the metal
#   Phi(u) = 1 / (1 + ((u - h) / w)^2)                               the field
#   y_k(t) = K · d/dt Phi(u_k(t))                                    the coil
#
# h is the voicing (the tine's rest offset from the pole's centreline, the
# reason there is a fundamental at all), w the field's width against the
# swing, g_k one gain per take. The same STFT loss as fit(), summed over the
# takes.
# ---------------------------------------------------------------------------

def fit_shaped(xs, sr, init, steps, device, verbose=True, normalised=False, form='bell'):
    """xs: list of takes (numpy, same length, one note at several velocities,
    softest first). Returns (f, r, a, phase, gains, (h, w, K), ys, loss)."""
    n = min(len(x) for x in xs)
    xs = [x[:n] for x in xs]
    t = torch.arange(n, device=device, dtype=torch.float32) / sr
    targets = [torch.tensor(x, device=device, dtype=torch.float32) for x in xs]
    f0 = torch.tensor([m[0] for m in init], device=device)
    r0 = torch.tensor([m[1] for m in init], device=device)
    a0 = torch.tensor([m[2] for m in init], device=device)
    logf = torch.log(f0).clone().requires_grad_(True)
    logr = torch.log(r0).clone().requires_grad_(True)
    loga = torch.log(a0 + 1e-6).clone().requires_grad_(True)
    phase = torch.tensor([m[3] if len(m) > 3 else 0.0 for m in init], device=device, dtype=torch.float32).requires_grad_(True)
    logr0 = logr.detach().clone()
    # take gains: the softest take at unit gain, the others by their RMS ratio
    rms = [float(np.sqrt(np.mean(x ** 2))) for x in xs]
    logg = torch.log(torch.tensor([m / rms[0] for m in rms], device=device)).requires_grad_(True)
    # the field opens wide: the softest take's swing a tenth of the width,
    # so the stage starts near-linear and the loudest take, at whatever gain
    # its RMS says, is still inside the bell; the voicing a third of a width
    # off centre; the coil gain set so the softest take comes out at the
    # target's level. Started deep in the field the model is noise, and the
    # loss's answer to noise is silence
    with torch.no_grad():
        swing = float(torch.exp(loga).sum())
    # form 'bell': a magnetic pole, phi = 1 / (1 + ((u - h) / w)^2), h the
    # voicing. form 'gap': an electrostatic plate, C = C0 / (1 - u / g) (Epi's
    # Wurlitzer law, i = V dC/dt), asymmetric by nature, g the rest gap in
    # swing units; h is unused and w is the gap
    h = torch.tensor(0.3 * 10.0 * swing if form == 'bell' else 0.0, device=device, requires_grad=(form == 'bell'))
    logw = torch.tensor(math.log(10.0 * swing), device=device, requires_grad=True)
    logK = torch.tensor(0.0, device=device, requires_grad=True)
    # the coil: inductance against its own capacitance and the cable is a
    # resonant second-order low-pass after Faraday (Epi carries it as coil
    # frequency and Q); without it the derivative tilts everything
    # +6 dB/octave and nothing matches. Applied in the frequency domain
    logfc = torch.tensor(math.log(4000.0), device=device, requires_grad=True)
    logQ = torch.tensor(0.0, device=device, requires_grad=True)
    freqs = torch.fft.rfftfreq(n, 1.0 / sr).to(device)
    # takes a sample library normalised one by one carry no level: give each
    # its own output gain after the coil, so that the swing into the field
    # is decided by the harmonics alone
    logout = torch.zeros(len(xs), device=device, requires_grad=normalised)
    opt = torch.optim.Adam([{'params': [logr, loga, phase, logg, h, logw, logK, logfc, logQ] + ([logout] if normalised else []), 'lr': 0.02},
                            {'params': [logf], 'lr': 0.0005}])
    scales = [(4096, 1024), (1024, 256), (256, 64)]
    T = []
    for target in targets:
        tmag = [stft_mag(target, nn, hh) for nn, hh in scales]
        tlog = [torch.log(m + 1e-4) for m in tmag]
        T.append((tmag, tlog, [m.norm() for m in tmag], [torch.quantile(tl.flatten(), 0.2) for tl in tlog]))
    ramp = torch.tensor(attack_ramp(n, sr), device=device, dtype=torch.float32)

    def metal():
        f, r, a = torch.exp(logf), torch.exp(logr), torch.exp(loga)
        env = torch.exp(-r[:, None] * t[None, :]) * ramp[None, :]
        return (a[:, None] * env * torch.sin(2 * math.pi * f[:, None] * t[None, :] + phase[:, None])).sum(0)

    def coil(u):
        w = torch.exp(logw)
        if form == 'gap':
            # bounded short of the plate: the reed does not pass through it
            sgap = 0.9 * torch.tanh(u / (0.9 * w))
            phi = 1.0 / (1.0 - sgap)
        else:
            phi = 1.0 / (1.0 + ((u - h) / w) ** 2)
        d = torch.diff(phi, prepend=phi[:1])   # Faraday, per sample
        fc, Q = torch.exp(logfc), torch.exp(logQ)
        s_ = freqs / fc
        H = 1.0 / (1.0 - s_ ** 2 + 1j * s_ / Q)
        return torch.exp(logK) * torch.fft.irfft(torch.fft.rfft(d) * H, n=n)

    def render():
        u = metal()
        return [torch.exp(logout[k]) * coil(torch.exp(logg[k]) * u) for k in range(len(xs))]

    with torch.no_grad():
        y0 = render()[0]
        logK.fill_(math.log(rms[0] / (float(y0.pow(2).mean().sqrt()) + 1e-12)))

    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=steps, eta_min=0.0)
    for step in range(steps):
        opt.zero_grad()
        ys = render()
        loss = 0.0
        for y, (tmag, tlog, tnorm, tfloor) in zip(ys, T):
            for (nn, hh), tm, tl, tn, tf in zip(scales, tmag, tlog, tnorm, tfloor):
                ym = stft_mag(y, nn, hh)
                yl = torch.log(ym + 1e-4)
                sc = (ym - tm).norm() / tn
                lg = (torch.clamp(yl, min=tf) - torch.clamp(tl, min=tf)).abs().mean()
                excess = torch.relu(yl - torch.clamp(tl, min=tf))
                ex = excess[tl <= tf].mean() if (tl <= tf).any() else excess.mean()
                loss = loss + sc + 0.1 * lg + 1.0 * ex
        loss = loss / len(xs) + DECAY_PRIOR * (torch.relu((logr - logr0).abs() - DECAY_BAND) ** 2).mean()
        loss.backward()
        opt.step()
        sched.step()
        with torch.no_grad():
            logr.clamp_(max=math.log(6.91 / MIN_T60))
        if verbose and (step % 100 == 0 or step == steps - 1):
            print('  step %4d  loss %.4f  h/w %.3f  w %.3g  K %.3g  coil %.0f Hz Q %.2f  gains %s' % (
                step, loss.item(), h.item() / math.exp(logw.item()), math.exp(logw.item()), math.exp(logK.item()),
                math.exp(logfc.item()), math.exp(logQ.item()),
                ' '.join('%.2f' % math.exp(v) for v in logg.tolist())))
    with torch.no_grad():
        ys = [y.cpu().numpy() for y in render()]
        f, r, a = torch.exp(logf), torch.exp(logr), torch.exp(loga)
    return (f.cpu().numpy(), r.cpu().numpy(), a.cpu().numpy(), phase.detach().cpu().numpy(),
            torch.exp(logg).detach().cpu().numpy(),
            (h.item(), math.exp(logw.item()), math.exp(logK.item()), math.exp(logfc.item()), math.exp(logQ.item())), ys, loss.item())


def bar_metal(init, f0, tol=0.03):
    """The metal of a tine or reed piano is a clamped bar: its own partials
    sit at 1 : 6.27 : 17.5, and anything within tol of an integer multiple of
    the fundamental (from the second up) is the pickup's, not the metal's.
    Keep the fundamental and the inharmonic partials; the field has to make
    the rest — which is the one constraint that lets two velocities separate
    metal from transducer when the takes were normalised and their levels
    say nothing."""
    out = []
    for m in init:
        k = m[0] / f0
        if abs(k - round(k)) * f0 <= tol * f0 * max(1.0, round(k)) and round(k) >= 2:
            continue
        out.append(m)
    return out


# ---------------------------------------------------------------------------
# The body: what the partials did not explain, as broad modes.
#
# x - y after the modal fit is the pluck, the body's dense low-Q resonances
# and the sympathetic strings. The body is still modes, only broad ones, so
# it is fitted the same way: candidates at the peaks of the residual's
# spectrum smoothed over a sixth of an octave — a hump, not a line — with a
# Q of ten to start, and the same fit() against the residual as target. At
# runtime they are resonators like the rest; in the world they take their
# own slots, aligned by frequency, because a body is the same body under
# every note.
# ---------------------------------------------------------------------------

def body_init(res, sr, nbody, partials=(), nfft=8192, f_lo=80.0, f_hi=6000.0, q=10.0, clear=0.03):
    win = np.hanning(nfft)
    frames = [np.abs(np.fft.rfft(res[s:s + nfft] * win)) for s in range(0, max(1, len(res) - nfft), nfft // 4)]
    mean = np.mean(frames, axis=0)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    logm = np.log(mean + 1e-12)
    # smooth over a sixth of an octave: a running mean whose width grows with
    # frequency, done on a log-frequency resample
    grid = np.exp(np.linspace(np.log(f_lo), np.log(f_hi), 600))
    lg = np.interp(grid, freqs, logm)
    k = 600 // (int(np.log2(f_hi / f_lo) * 6) or 1)
    k = max(3, k | 1)
    sm = np.convolve(lg, np.ones(k) / k, 'same')
    pk, _ = signal.find_peaks(sm, distance=k)
    # not at a partial: what the partial fit left there is its own error —
    # a pair's beating, a phase — and a body mode on top of it would be that
    # error dressed up, at an amplitude the body never had
    pk = [i for i in pk if not any(abs(grid[i] / f - 1) < clear for f in partials)]
    pk = sorted(pk, key=lambda i: -sm[i])[:nbody]
    out = []
    for i in pk:
        f = float(grid[i])
        rate = 2 * math.pi * f / (2 * q)              # zeta = 1/(2Q), r = zeta w
        amp = float(np.exp(sm[i])) / (nfft / 4)
        out.append((f, rate, amp, 0.0))
    return out


BODY_GRID = np.exp(np.linspace(np.log(80.0), np.log(6000.0), 600))


def residual_spectrum(res, sr, nfft=8192):
    """The residual's mean log magnitude on the body grid (80 Hz to 6 kHz,
    log-spaced), from the first 0.5 s — where the body speaks."""
    win = np.hanning(nfft)
    n = min(len(res), int(0.5 * sr))
    frames = [np.abs(np.fft.rfft(res[s:s + nfft] * win)) for s in range(0, max(1, n - nfft), nfft // 4)] or [np.abs(np.fft.rfft(np.pad(res[:n], (0, nfft - n)) * win))]
    mean = np.mean(frames, axis=0) / (nfft / 4)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    return np.interp(BODY_GRID, freqs, np.log(mean + 1e-12))


def set_body(spectra, nbody, q_default=12.0):
    """[(hz, Q)] of the body: peaks of the mean residual spectrum over the
    set, smoothed over a sixth of an octave, the Q from each peak's half-power
    width where it can be read and q_default where it cannot."""
    mean = np.mean(spectra, axis=0)
    k = 600 // int(np.log2(6000.0 / 80.0) * 6)
    k = max(3, k | 1)
    sm = np.convolve(mean, np.ones(k) / k, 'same')
    pk, props = signal.find_peaks(sm, distance=k, prominence=0.2)
    pk = sorted(pk, key=lambda i: -sm[i])[:nbody]
    out = []
    for i in sorted(pk):
        f = float(BODY_GRID[i])
        half = sm[i] - np.log(np.sqrt(2.0))
        lo = i
        while lo > 0 and sm[lo] > half:
            lo -= 1
        hi = i
        while hi < len(sm) - 1 and sm[hi] > half:
            hi += 1
        bw = BODY_GRID[hi] - BODY_GRID[lo]
        q = f / bw if bw > 0 and 2.0 < f / bw < 60.0 else q_default
        out.append((f, float(q)))
    return out


def append_body(path, body, spectrum, sr, clear=0.03):
    """Write the body's modes into a record at this note's residual level,
    silent where they sit on one of the note's partials."""
    lines = open(path).read().splitlines()
    partials = [float(l.split()[3]) for l in lines if l.startswith('mode ')]
    k = len(partials)
    out = [l for l in lines if not l.startswith('body ')]
    out = [l if not l.startswith('modes ') else 'modes %d' % (k + len(body)) for l in out]
    out.append('body %d' % len(body))
    for f, q in body:
        lvl = float(np.exp(np.interp(f, BODY_GRID, spectrum)))
        if any(abs(f / p - 1) < clear for p in partials):
            lvl = 1e-6
        out.append('mode %d hz %.6f zeta %.9g gains %s' % (k, f, 1.0 / (2 * q), ' '.join('%.9g' % lvl for _ in range(12))))
        k += 1
    open(path, 'w').write('\n'.join(out) + '\n')
