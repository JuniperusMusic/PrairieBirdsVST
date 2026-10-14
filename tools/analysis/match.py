#!/usr/bin/env python3
"""The matching loop: render, measure, compare, adjust — then you listen.

  python tools/analysis/match.py --ref song.wav@29.0-32.3 --ref song.wav@46.3-49.5 \
      --preset presets/white_throated_sparrow.json --out matched.json

For each reference song it finds the whistles (150 ms or longer), writes a
score that plays them on the keys nearest their pitch, renders that score
with pbv_render, measures the render exactly like the recording, and nudges
the chosen settings (Nelder-Mead) until pitch contour and loudness shape
match. Renders are made at real bird pitch (octave 0); the saved preset keeps
its own octave and transposition. Notes are played at full velocity, so the matched values are
what a hard key press produces.

The result is a new preset plus a report. A close measurement isn't proof of
realism: listen to the render (--keep-renders) and in Cubase before keeping it.
"""

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.optimize import minimize

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pbv_analysis as pa  # noqa: E402

DEFAULT_FIT = ["onset", "scoopDepth", "scoopTime", "driftSag", "wobblePitch", "wobbleLevel", "purity"]
MIN_WHISTLE_S = 0.150

# Search ranges (narrower than the full parameter ranges, so the search stays sensible).
RANGES = {
    "onset": (5, 300), "release": (2, 200), "scoopDepth": (0, 150), "scoopTime": (10, 300),
    "driftSag": (-100, 100), "driftAmount": (0, 20), "driftSpeed": (0.1, 5),
    "wobblePitch": (0, 15), "wobbleLevel": (0, 4), "wobbleRate": (4, 30),
    "purity": (0.3, 1), "beakTracking": (0.5, 1), "beakOpen": (0, 1), "velResponse": (0, 1),
}


def find_render():
    env = os.environ.get("PBV_RENDER")
    if env:
        return env
    root = Path(__file__).resolve().parents[2]
    for p in ["build/tools/render/pbv_render", "build/tools/render/Release/pbv_render.exe",
              "build-dsp/tools/render/pbv_render", "build/tools/render/pbv_render.exe"]:
        if (root / p).exists():
            return str(root / p)
    sys.exit("pbv_render not found: build the project or set PBV_RENDER")


def parse_ref(spec):
    """file.wav or file.wav@start-end"""
    if "@" in spec:
        path, rng = spec.rsplit("@", 1)
        a, b = rng.split("-")
        return path, float(a), float(b)
    return spec, None, None


def whistles(song):
    return [n for n in song.notes if n.duration >= MIN_WHISTLE_S]


def key_semis(key, params):
    root = int(params.get("tuneRoot", 0))
    deg = (key - root) % 12
    return key + (params.get(f"tune{deg}", 0.0) + params.get("fineTune", 0.0)) / 100.0


def assign_keys(notes, params):
    """Nearest keys at octave 0, with the fine-tune offset that fits all notes best."""
    target = [69 + 12 * math.log2(n.pitch_hz / 440.0) for n in notes]
    best = None
    for fine in np.arange(-50, 50.5, 1.0):
        p = dict(params, fineTune=float(fine))
        keys, err = [], 0.0
        for s in target:
            k = min(range(int(s) - 2, int(s) + 3), key=lambda k: abs(key_semis(k, p) - s))
            keys.append(k)
            err += (key_semis(k, p) - s) ** 2
        if best is None or err < best[0]:
            best = (err, float(fine), keys)
    return best[2], best[1], math.sqrt(best[0] / len(target)) * 100


def choice_index(value, labels):
    return labels.index(value) if isinstance(value, str) else int(value)


def load_preset(path):
    with open(path) as f:
        preset = json.load(f)
    params = dict(preset.get("params", {}))
    if isinstance(params.get("tuneRoot"), str):
        params["tuneRoot"] = choice_index(params["tuneRoot"], ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"])
    return preset, params


class Matcher:
    def __init__(self, refs, preset_path, fit, render, workdir, seed, highpass):
        self.preset_path = preset_path
        self.preset, self.params = load_preset(preset_path)
        self.fit = fit
        self.render = render
        self.workdir = Path(workdir)
        self.seed = seed
        self.highpass = highpass
        self.songs = []
        for i, (path, a, b) in enumerate(refs):
            x, sr = pa.load_mono(path, a, b)
            song = pa.analyse(x, sr, highpass_hz=highpass)
            notes = whistles(song)
            if not notes:
                sys.exit(f"no whistles found in {path} {a}-{b}")
            keys, fine, fit_err = assign_keys(notes, self.params)
            score = self.workdir / f"song{i}.txt"
            t0 = notes[0].start - 0.1
            with open(score, "w") as f:
                f.write(f"# from {path} {a}-{b}\n")
                for n, k in zip(notes, keys):
                    f.write(f"{n.start - t0:.4f} on {k} 127\n{n.end - t0:.4f} off {k}\n")
            self.songs.append({"ref": song, "notes": notes, "score": score, "fine": fine,
                               "key_fit_cents": fit_err, "label": f"{Path(path).name} {a}-{b}"})
        self.evals = 0

    def overrides(self, values):
        sets = {"octave": 0, "transposeSemitones": 0, "transposeCents": 0, "chirpThreshold": 128}
        sets.update({k: v for k, v in zip(self.fit, values)})
        return sets

    def render_song(self, song, sets, out):
        cmd = [self.render, "--preset", str(self.preset_path), "--score", str(song["score"]),
               "--out", str(out), "--mono", "--tail", "0.6", "--seed", str(self.seed),
               "--set", f"fineTune={song['fine']}"]
        for k, v in sets.items():
            cmd += ["--set", f"{k}={v}"]
        subprocess.run(cmd, check=True)

    def errors(self, values, keep=None):
        sets = self.overrides(values)
        results = []
        for i, song in enumerate(self.songs):
            out = (keep / f"render{i}.wav") if keep else self.workdir / "tmp.wav"
            self.render_song(song, sets, out)
            x, sr = pa.load_mono(str(out))
            test = pa.analyse(x, sr, highpass_hz=self.highpass)
            ref = pa.Song(song["notes"], [], [], 0, 0, 0)
            got = pa.Song(whistles(test), [], [], 0, 0, 0)
            got.intervals_cents = [1200 * math.log2(b.pitch_hz / a.pitch_hz) for a, b in zip(got.notes, got.notes[1:])]
            ref.intervals_cents = [1200 * math.log2(b.pitch_hz / a.pitch_hz) for a, b in zip(ref.notes, ref.notes[1:])]
            results.append(pa.compare(ref, got))
        return results

    def cost(self, unit):
        values = self.from_unit(unit)
        self.evals += 1
        res = self.errors(values)
        p = np.mean([r["pitch_rms_cents"] for r in res])
        l = np.mean([r["loudness_rms_db"] for r in res])
        return p / 6.0 + l / 3.0

    def to_unit(self, values):
        return np.array([(v - RANGES[k][0]) / (RANGES[k][1] - RANGES[k][0]) for k, v in zip(self.fit, values)])

    def from_unit(self, unit):
        return [float(np.clip(RANGES[k][0] + u * (RANGES[k][1] - RANGES[k][0]), *RANGES[k])) for k, u in zip(self.fit, unit)]


def summarise(res):
    return {
        "pitch_rms_cents": round(float(np.mean([r["pitch_rms_cents"] for r in res])), 2),
        "loudness_rms_db": round(float(np.mean([r["loudness_rms_db"] for r in res])), 2),
        "per_song": res,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", action="append", required=True, help="recording.wav or recording.wav@start-end (repeat per song)")
    ap.add_argument("--preset", required=True, help="starting preset JSON")
    ap.add_argument("--out", required=True, help="matched preset JSON to write")
    ap.add_argument("--fit", default=",".join(DEFAULT_FIT), help="comma-separated settings to adjust")
    ap.add_argument("--iters", type=int, default=150, help="maximum renders per song (default 150)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--highpass", type=float, default=1000.0, help="Hz; removes rumble from recordings")
    ap.add_argument("--keep-renders", help="folder for the final renders and scores, to listen to")
    args = ap.parse_args()

    fit = [f.strip() for f in args.fit.split(",") if f.strip()]
    for f in fit:
        if f not in RANGES:
            sys.exit(f"cannot fit '{f}'; choose from {', '.join(RANGES)}")

    render = find_render()
    with tempfile.TemporaryDirectory() as tmp:
        m = Matcher([parse_ref(r) for r in args.ref], args.preset, fit, render, tmp, args.seed, args.highpass)
        for s in m.songs:
            print(f"{s['label']}: {len(s['notes'])} whistles, keys fit within {s['key_fit_cents']:.1f} cents", file=sys.stderr)

        start = [float(m.params.get(k, 0.0)) for k in fit]
        before = summarise(m.errors(start))
        print(f"before: pitch {before['pitch_rms_cents']} cents, loudness {before['loudness_rms_db']} dB", file=sys.stderr)

        res = minimize(m.cost, m.to_unit(start), method="Nelder-Mead",
                       options={"maxfev": args.iters, "xatol": 1e-3, "fatol": 1e-3, "initial_simplex": None})
        best = m.from_unit(res.x)
        keep = Path(args.keep_renders) if args.keep_renders else None
        if keep:
            keep.mkdir(parents=True, exist_ok=True)
            for i, s in enumerate(m.songs):
                (keep / f"score{i}.txt").write_text(Path(s["score"]).read_text())
        after = summarise(m.errors(best, keep))
        print(f"after {m.evals} rounds: pitch {after['pitch_rms_cents']} cents, loudness {after['loudness_rms_db']} dB", file=sys.stderr)

    preset = m.preset
    preset.setdefault("params", {})
    for k, v in zip(fit, best):
        preset["params"][k] = round(v, 3)
    preset["matching"] = {
        "references": args.ref,
        "fitted": fit,
        "before": {k: before[k] for k in ("pitch_rms_cents", "loudness_rms_db")},
        "after": {k: after[k] for k in ("pitch_rms_cents", "loudness_rms_db")},
    }
    with open(args.out, "w") as f:
        json.dump(preset, f, indent=2)
        f.write("\n")
    print(pa.dumps({"fitted": dict(zip(fit, best)), "before": before, "after": after}))


if __name__ == "__main__":
    main()
