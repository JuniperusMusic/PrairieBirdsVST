"""Prairie Bird Voice — measuring bird songs and plugin renders the same way.

Pure whistles are measured with a short-time spectrum: the strongest peak in
the bird's band gives the pitch (refined by parabolic interpolation), the
frame energy gives loudness, and the bins at 2x and 3x give the overtones.

Used by measure.py (print what a recording contains) and match.py (adjust a
preset until a render measures like the recording).
"""

from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass, field

import numpy as np
import soundfile as sf

HOP_S = 0.0025   # 2.5 ms between frames
WIN_S = 0.016    # 16 ms analysis window


def load_mono(path: str, start: float | None = None, end: float | None = None):
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    if start is not None or end is not None:
        a = int((start or 0.0) * sr)
        b = int(end * sr) if end is not None else len(x)
        x = x[a:b]
    return x, sr


def highpass(x: np.ndarray, sr: int, cutoff: float) -> np.ndarray:
    """Removes wind and traffic rumble below the bird's band (FFT brick wall)."""
    if cutoff <= 0:
        return x
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    X[f < cutoff] = 0.0
    return np.fft.irfft(X, len(x))


@dataclass
class Track:
    t: np.ndarray        # frame centre times, s
    f0: np.ndarray       # Hz (nan where silent)
    level: np.ndarray    # dB of the whole frame
    h2: np.ndarray       # 2nd overtone relative to the main tone, dB
    h3: np.ndarray       # 3rd overtone, dB


def track(x: np.ndarray, sr: int, fmin: float = 300.0, fmax: float = 9000.0) -> Track:
    hop = max(1, int(HOP_S * sr))
    win = int(WIN_S * sr)
    nfft = 1 << int(math.ceil(math.log2(win * 4)))
    w = np.hanning(win)
    n_frames = max(0, 1 + (len(x) - win) // hop)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    band = (freqs >= fmin) & (freqs <= fmax)
    band_idx = np.flatnonzero(band)

    t = np.empty(n_frames)
    f0 = np.full(n_frames, np.nan)
    level = np.empty(n_frames)
    h2 = np.full(n_frames, np.nan)
    h3 = np.full(n_frames, np.nan)

    frames = np.lib.stride_tricks.sliding_window_view(x, win)[::hop][:n_frames]
    spec = np.abs(np.fft.rfft(frames * w, nfft, axis=1)) + 1e-12
    rms = np.sqrt(np.mean(frames ** 2, axis=1)) + 1e-12
    level[:] = 20 * np.log10(rms)
    t[:] = (np.arange(n_frames) * hop + win / 2) / sr

    sub = spec[:, band_idx]
    k = band_idx[np.argmax(sub, axis=1)]
    for i in range(n_frames):
        ki = k[i]
        if ki <= 0 or ki >= len(freqs) - 1:
            continue
        a, b, c = np.log(spec[i, ki - 1:ki + 2])
        denom = a - 2 * b + c
        p = float(np.clip(0.5 * (a - c) / denom, -0.5, 0.5)) if denom != 0 else 0.0  # flat spectra (silence) can't run away
        f = (ki + p) * sr / nfft
        f0[i] = f
        peak = b
        for mult, out in ((2, h2), (3, h3)):
            kk = int(round(f * mult * nfft / sr))
            if 2 <= kk and kk + 2 < len(freqs):
                out[i] = 20 / math.log(10) * (np.log(spec[i, kk - 2:kk + 3].max()) - peak)
    return Track(t, f0, level, h2, h3)


@dataclass
class Note:
    start: float
    end: float
    pitch_hz: float                 # median over the steady middle
    contour_cents: list = field(repr=False, default_factory=list)  # vs pitch_hz, one value per frame
    envelope_db: list = field(repr=False, default_factory=list)    # vs song peak, one value per frame
    rise_10db_ms: float = 0.0       # onset to -10 dB below the note's peak
    rise_full_ms: float = 0.0       # onset to -1 dB below the peak
    scoop_cents: float = 0.0        # pitch offset in the first 10 ms (+ = from above)
    scoop_settle_ms: float = 0.0    # time until pitch stays within 8 cents
    drift_cents_per_s: float = 0.0  # slope over the steady part
    wobble_cents: float = 0.0       # spread around that slope
    wobble_db: float = 0.0          # loudness spread on the steady part
    overtone2_db: float = float("nan")
    overtone3_db: float = float("nan")

    @property
    def duration(self):
        return self.end - self.start


@dataclass
class Song:
    notes: list
    intervals_cents: list
    gaps_ms: list
    echo_db: float      # tail level 30–80 ms after the last note, vs the note's peak
    echo_t60_s: float   # tail decay time estimate
    peak_db: float


def _runs(mask):
    spans, i, n = [], 0, len(mask)
    while i < n:
        if mask[i]:
            j = i
            while j < n and mask[j]:
                j += 1
            spans.append([i, j])
            i = j
        else:
            i += 1
    return spans


def segment(tr: Track, floor_db: float = 30.0, min_ms: float = 25.0, jump_cents: float = 70.0,
            note_floor_db: float = 14.0, merge_ms: float = 70.0):
    """Finds the notes in a track.

    1. Sound: frames louder than (song peak - floor_db).
    2. Split wherever the pitch jumps by more than jump_cents between frames
       (the forest echo often fills the gap between two whistles).
    3. Trim each piece to the frames within note_floor_db of its own peak,
       which drops the echo tail that continues at the same pitch.
    """
    peak = np.nanmax(tr.level)
    smooth = np.copy(tr.f0)
    for i in range(1, len(smooth) - 1):  # 3-point median against single-frame glitches
        smooth[i] = np.nanmedian(tr.f0[i - 1:i + 2])

    pieces = []
    for a, b in _runs(tr.level > peak - floor_db):
        cuts = [a]
        for i in range(a + 1, b):
            if np.isfinite(smooth[i]) and np.isfinite(smooth[i - 1]) and \
                    abs(1200 * math.log2(smooth[i] / smooth[i - 1])) > jump_cents:
                cuts.append(i)
        cuts.append(b)
        for s, e in zip(cuts[:-1], cuts[1:]):
            if e - s < 2:
                continue
            top = tr.level[s:e].max()
            keep = _runs(tr.level[s:e] > top - note_floor_db)
            if keep:
                # the longest loud run is the note itself
                ks, ke = max(keep, key=lambda r: r[1] - r[0])
                pieces.append((s + ks, s + ke))

    # 4. Rejoin pieces of one whistle that a loudness dip split apart.
    merged = []
    for a, b in pieces:
        if merged:
            pa_, pb_ = merged[-1]
            gap_ms = (a - pb_) * HOP_S * 1000
            f1, f2 = np.nanmedian(tr.f0[pa_:pb_]), np.nanmedian(tr.f0[a:b])
            if gap_ms < merge_ms and abs(1200 * math.log2(f2 / f1)) < 40:
                merged[-1] = (pa_, b)
                continue
        merged.append((a, b))
    return [(a, b) for a, b in merged if (b - a) * HOP_S * 1000 >= min_ms], peak


def analyse(x: np.ndarray, sr: int, floor_db: float = 30.0, highpass_hz: float = 0.0) -> Song:
    x = highpass(x, sr, highpass_hz)
    tr = track(x, sr)
    spans, peak = segment(tr, floor_db)
    notes = []
    for a, b in spans:
        f = tr.f0[a:b]
        lv = tr.level[a:b]
        n = b - a
        mid = slice(n // 4, max(n // 4 + 1, 3 * n // 4))
        pitch = float(np.nanmedian(f[mid]))
        cents = 1200 * np.log2(f / pitch)
        note = Note(start=float(tr.t[a]), end=float(tr.t[b - 1]), pitch_hz=pitch)
        note.contour_cents = [float(c) for c in cents]
        note.envelope_db = [float(v - peak) for v in lv]

        top = lv.max()
        note.rise_10db_ms = float(np.argmax(lv >= top - 10) * HOP_S * 1000)
        note.rise_full_ms = float(np.argmax(lv >= top - 1) * HOP_S * 1000)

        first = cents[: max(1, int(0.010 / HOP_S))]
        note.scoop_cents = float(np.nanmean(first))
        outside = np.flatnonzero(np.abs(cents[: n // 2]) > 8)
        note.scoop_settle_ms = float((outside[-1] + 1) * HOP_S * 1000) if len(outside) else 0.0

        steady = slice(max(int(0.1 / HOP_S), n // 5), max(int(0.1 / HOP_S) + 2, n - n // 8))
        cs, ls = cents[steady], lv[steady]
        ts = np.arange(len(cs)) * HOP_S
        ok = np.isfinite(cs)
        if ok.sum() > 4:
            slope, icpt = np.polyfit(ts[ok], cs[ok], 1)
            note.drift_cents_per_s = float(slope)
            note.wobble_cents = float(np.std(cs[ok] - (slope * ts[ok] + icpt)))
            lslope, licpt = np.polyfit(ts, ls, 1)
            note.wobble_db = float(np.std(ls - (lslope * ts + licpt)))
        note.overtone2_db = float(np.nanmedian(tr.h2[a:b][mid]))
        note.overtone3_db = float(np.nanmedian(tr.h3[a:b][mid]))
        notes.append(note)

    intervals = [1200 * math.log2(notes[i + 1].pitch_hz / notes[i].pitch_hz) for i in range(len(notes) - 1)]
    gaps = [(notes[i + 1].start - notes[i].end) * 1000 for i in range(len(notes) - 1)]

    echo_db, t60 = float("nan"), float("nan")
    if notes:
        last = notes[-1]
        i0 = int(round((last.end + 0.030) / HOP_S))
        i1 = int(round((last.end + 0.080) / HOP_S))
        note_peak = max(last.envelope_db) + peak
        if i1 < len(tr.level):
            echo_db = float(np.mean(tr.level[i0:i1]) - note_peak)
            tail = tr.level[i0:min(len(tr.level), i0 + int(0.5 / HOP_S))]
            if len(tail) > 10:
                slope = np.polyfit(np.arange(len(tail)) * HOP_S, tail, 1)[0]
                t60 = float(-60.0 / slope) if slope < 0 else float("inf")

    return Song(notes, intervals, gaps, echo_db, t60, float(peak))


def summary(song: Song) -> dict:
    out = {
        "notes": [{k: v for k, v in asdict(n).items() if k not in ("contour_cents", "envelope_db")} for n in song.notes],
        "intervals_cents": song.intervals_cents,
        "gaps_ms": song.gaps_ms,
        "echo_db": song.echo_db,
        "echo_t60_s": song.echo_t60_s,
    }
    for n, d in zip(song.notes, out["notes"]):
        d["duration_s"] = n.duration
    return out


def _median5(v: np.ndarray) -> np.ndarray:
    out = np.copy(v)
    for i in range(len(v)):
        w = v[max(0, i - 2):i + 3]
        w = w[np.isfinite(w)]
        out[i] = np.median(w) if len(w) else np.nan
    return out


def compare(ref: Song, test: Song, quiet_db: float = 15.0, cap_cents: float = 40.0, floor_db: float = 20.0) -> dict:
    """Pitch and loudness-shape error between two songs with the same notes.

    Pitch: each note's contour in cents (relative to the song's first note, so
    intervals count) after removing the one overall offset between the two
    songs. Contours are median-smoothed over 5 frames, frames more than
    quiet_db below the note's peak (noise and other birds dominate there) are
    skipped, and a single frame's error is capped at cap_cents so one glitch
    cannot outweigh a whole note. Loudness: each note's envelope in dB from its
    own peak, floored at -floor_db, frame by frame from the onset.
    """
    if len(ref.notes) != len(test.notes):
        return {"ok": False, "reason": f"note count differs: {len(ref.notes)} vs {len(test.notes)}",
                "pitch_rms_cents": 1e3, "loudness_rms_db": 1e2}
    p_err, l_err = [], []
    r0, t0 = ref.notes[0].pitch_hz, test.notes[0].pitch_hz
    offset = np.median([1200 * math.log2((tn.pitch_hz / t0) / (rn.pitch_hz / r0)) for rn, tn in zip(ref.notes, test.notes)])
    for rn, tn in zip(ref.notes, test.notes):
        n = min(len(rn.contour_cents), len(tn.contour_cents))
        re = np.array(rn.envelope_db[:n]); re -= re.max()
        te = np.array(tn.envelope_db[:n]); te -= te.max()
        rc = _median5(np.array(rn.contour_cents[:n])) + 1200 * math.log2(rn.pitch_hz / r0)
        tc = _median5(np.array(tn.contour_cents[:n])) + 1200 * math.log2(tn.pitch_hz / t0) - offset
        ok = np.isfinite(rc) & np.isfinite(tc) & (re > -quiet_db) & (te > -quiet_db)
        p_err.extend(np.clip(rc - tc, -cap_cents, cap_cents)[ok])
        l_err.extend(np.maximum(re, -floor_db) - np.maximum(te, -floor_db))
    return {
        "ok": True,
        "pitch_rms_cents": float(np.sqrt(np.mean(np.square(p_err)))),
        "loudness_rms_db": float(np.sqrt(np.mean(np.square(l_err)))),
        "interval_error_cents": [t - r for r, t in zip(ref.intervals_cents, test.intervals_cents)],
        "length_error_ms": [(tn.duration - rn.duration) * 1000 for rn, tn in zip(ref.notes, test.notes)],
    }


def dumps(obj) -> str:
    def clean(o):
        if isinstance(o, float):
            return None if not math.isfinite(o) else round(o, 3)
        if isinstance(o, dict):
            return {k: clean(v) for k, v in o.items()}
        if isinstance(o, (list, tuple)):
            return [clean(v) for v in o]
        return o
    return json.dumps(clean(obj), indent=2)
