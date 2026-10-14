#!/usr/bin/env python3
"""Measure a bird song (or a plugin render) and print what it contains.

  python tools/analysis/measure.py song.wav --start 12.3 --end 15.1
  python tools/analysis/measure.py song.wav --propose-temperament

Prints per-note pitch, length, onset, scoop, drift, wobble and overtones, the
intervals and gaps between notes, and the forest echo after the last note.
With --propose-temperament it also suggests the 12 tuning offsets (cents from
equal temperament, counted from the lowest note) that would let the keyboard
play those intervals.
"""

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pbv_analysis as pa  # noqa: E402

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def propose_temperament(notes):
    """Lowest note becomes the root; every other note lands on its nearest key."""
    root_hz = min(n.pitch_hz for n in notes)
    root_semis = 69 + 12 * math.log2(root_hz / 440.0)
    root_key = int(round(root_semis))
    tune = [0.0] * 12
    used = {}
    for n in notes:
        above = 1200 * math.log2(n.pitch_hz / root_hz)
        degree = int(round(above / 100.0))
        offset = above - 100.0 * degree
        used.setdefault(degree % 12, []).append(offset)
    for d, offs in used.items():
        tune[d] = sum(offs) / len(offs)
    return {
        "tuneRoot": NAMES[root_key % 12],
        "root_offset_cents": round(100 * (root_semis - root_key), 1),
        **{f"tune{d}": round(v, 1) for d, v in enumerate(tune)},
        "note": "keys are counted at real bird pitch (octave 0); root_offset_cents can go into fineTune",
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("--start", type=float)
    ap.add_argument("--end", type=float)
    ap.add_argument("--floor-db", type=float, default=30.0, help="notes are louder than peak minus this (default 30)")
    ap.add_argument("--highpass", type=float, default=1000.0, help="remove rumble below this many Hz (default 1000; 0 = off)")
    ap.add_argument("--propose-temperament", action="store_true")
    args = ap.parse_args()

    x, sr = pa.load_mono(args.wav, args.start, args.end)
    song = pa.analyse(x, sr, args.floor_db, args.highpass)
    out = pa.summary(song)
    if args.propose_temperament and song.notes:
        out["proposed_temperament"] = propose_temperament(song.notes)
    print(pa.dumps(out))


if __name__ == "__main__":
    main()
