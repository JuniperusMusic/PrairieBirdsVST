"""Engine and analysis tests: render with pbv_render, measure with the analysis tools.

Run with:  ctest --test-dir build   (or)   PBV_RENDER=path/to/pbv_render python -m unittest discover tests
"""

import math
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "analysis"))
import pbv_analysis as pa  # noqa: E402
from match import find_render  # noqa: E402

SPARROW = ROOT / "presets" / "white_throated_sparrow.json"
# Settings that make the pitch steady, so pitch tests measure tuning only.
STEADY = ["wobblePitch=0", "wobbleLevel=0", "driftAmount=0", "driftSag=0", "scoopDepth=0", "chirpThreshold=128"]


def cents(a, b):
    return 1200 * math.log2(a / b)


class EngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.render_bin = find_render()
        cls.tmp = tempfile.TemporaryDirectory()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def render(self, score, preset=None, sets=(), tail=0.5):
        d = Path(self.tmp.name)
        (d / "score.txt").write_text(score)
        out = d / "out.wav"
        cmd = [self.render_bin, "--score", str(d / "score.txt"), "--out", str(out), "--mono", "--tail", str(tail)]
        if preset:
            cmd += ["--preset", str(preset)]
        for s in sets:
            cmd += ["--set", s]
        subprocess.run(cmd, check=True)
        x, sr = sf.read(out)
        return x, sr

    def steady_pitch(self, x, sr, a, b):
        seg = x[int(a * sr):int(b * sr)]
        n = 1 << 20
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
        k = int(np.argmax(spec))
        y0, y1, y2 = np.log(spec[k - 1:k + 2])
        return (k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)) * sr / n

    def level_db(self, x, sr, a, b):
        seg = x[int(a * sr):int(b * sr)]
        return 10 * math.log10(np.mean(seg ** 2) + 1e-20)

    def test_equal_temperament_a4(self):
        x, sr = self.render("0 on 69 100\n0.6 off 69\n", sets=["octave=0", *STEADY])
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.2, 0.55), 440.0), 0.0, delta=0.5)

    def test_octave_selector(self):
        x, sr = self.render("0 on 69 100\n0.6 off 69\n", sets=["octave=-2", *STEADY])
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.2, 0.55), 110.0), 0.0, delta=0.5)

    def test_transpose_semitones_and_cents(self):
        x, sr = self.render("0 on 69 100\n0.6 off 69\n", sets=["octave=-1", "transposeSemitones=-5", "transposeCents=-30", *STEADY])
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.2, 0.55), 440.0), -1200 - 500 - 30, delta=0.5)

    def test_seven_octaves_down(self):
        # The sparrow's top whistle (C8 at octave 0, about 4.3 kHz) lands near 34 Hz.
        x, sr = self.render("0 on 108 100\n1.0 off 108\n", SPARROW, ["octave=-7", *STEADY])
        expected = 440.0 * 2 ** ((108 - 69 + 0.49 - 84) / 12)
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.3, 0.95), expected), 0, delta=1)

    def test_sparrow_temperament(self):
        # Root G: B is tuned -34 cents and C +49, giving the measured 366 and 549 cent steps.
        score = "0 on 67 100\n0.5 off 67\n0.7 on 71 100\n1.2 off 71\n1.4 on 72 100\n1.9 off 72\n"
        x, sr = self.render(score, SPARROW, ["velResponse=0", *STEADY])
        g = self.steady_pitch(x, sr, 0.15, 0.45)
        b = self.steady_pitch(x, sr, 0.85, 1.15)
        c = self.steady_pitch(x, sr, 1.55, 1.85)
        self.assertAlmostEqual(cents(b, g), 366, delta=1)
        self.assertAlmostEqual(cents(c, g), 549, delta=1)

    def test_scoop_direction_auto(self):
        # First note of a phrase falls in from above; the next note rises in from below.
        score = "0 on 69 127\n0.4 off 69\n0.55 on 69 127\n0.95 off 69\n"
        sets = ["octave=0", "scoopDepth=100", "scoopTime=80", "wobblePitch=0", "driftAmount=0", "driftSag=0", "chirpThreshold=128"]
        x, sr = self.render(score, sets=sets)
        song = pa.analyse(x, sr)
        self.assertEqual(len(song.notes), 2)
        self.assertGreater(song.notes[0].scoop_cents, 40)
        self.assertLess(song.notes[1].scoop_cents, -40)

    def test_chirp_only_on_hard_hits(self):
        score_soft = "0 on 79 80\n0.6 off 79\n"
        score_hard = "0 on 79 120\n0.6 off 79\n"
        soft, sr = self.render(score_soft, SPARROW, ["echoWet=0"])
        hard, _ = self.render(score_hard, SPARROW, ["echoWet=0"])
        # The chirp sounds in the first 40 ms; the whistle itself waits for the 180 ms lead.
        self.assertLess(self.level_db(soft, sr, 0.0, 0.04), self.level_db(soft, sr, 0.2, 0.4) - 1)
        self.assertGreater(self.level_db(hard, sr, 0.0, 0.04), -40)
        self.assertLess(self.level_db(hard, sr, 0.08, 0.17), -60)  # silent gap before the whistle
        self.assertGreater(self.level_db(hard, sr, 0.3, 0.5), -30)

    def test_breath_mode_needs_breath(self):
        silent, sr = self.render("0 on 69 100\n0.5 off 69\n", sets=["breathMode=true"])
        blown, _ = self.render("0 cc 2 100\n0 on 69 100\n0.5 off 69\n", sets=["breathMode=true"])
        self.assertLess(np.abs(silent).max(), 1e-6)
        self.assertGreater(self.level_db(blown, sr, 0.2, 0.4), -40)

    def test_echo_off_means_silence_after_release(self):
        # The beak resonance rings for a few milliseconds after the release, then silence.
        x, sr = self.render("0 on 69 100\n0.4 off 69\n", sets=["echoWet=0", "release=10"])
        self.assertLess(np.abs(x[int(0.51 * sr):]).max(), 1e-4)

    def test_default_echo_level(self):
        # The sparrow's default echo starts about 19 dB below the note, like the recording.
        x, sr = self.render("0.1 on 79 90\n0.5 off 79\n", SPARROW, ["wobbleLevel=0"], tail=1.2)
        tail = self.level_db(x, sr, 0.54, 0.59) - self.level_db(x, sr, 0.3, 0.45)
        self.assertAlmostEqual(tail, -19, delta=2.5)

    def test_legato_glides_without_new_attack(self):
        score = "0 on 69 100\n0.4 on 71 100\n0.45 off 69\n0.8 off 71\n"
        x, sr = self.render(score, sets=["octave=0", "glideAmount=1", "glideTime=50", *STEADY])
        song = pa.analyse(x, sr, floor_db=20)
        self.assertEqual(len(song.notes), 1, "legato notes should stay connected")
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.55, 0.75), 440.0), 200, delta=1)

    def test_unknown_parameter_is_rejected(self):
        d = Path(self.tmp.name)
        (d / "s.txt").write_text("0 on 60 100\n")
        r = subprocess.run([self.render_bin, "--score", str(d / "s.txt"), "--out", str(d / "o.wav"), "--set", "nope=1"],
                           capture_output=True)
        self.assertNotEqual(r.returncode, 0)


class AnalysisTests(unittest.TestCase):
    def test_measures_a_synthetic_whistle(self):
        sr = 48000
        t = np.arange(int(0.5 * sr)) / sr
        f = 3220.0 * 2 ** ((20 * t) / 1200)  # 3.22 kHz rising 20 cents per second
        x = 0.3 * np.sin(2 * np.pi * np.cumsum(f) / sr) * np.minimum(1, t / 0.03) * np.minimum(1, (0.5 - t) / 0.01)
        x = np.concatenate([np.zeros(sr // 10), x, np.zeros(sr // 10)])
        song = pa.analyse(x, sr)
        self.assertEqual(len(song.notes), 1)
        n = song.notes[0]
        self.assertAlmostEqual(cents(n.pitch_hz, 3220.0 * 2 ** (5 / 1200)), 0, delta=3)
        self.assertAlmostEqual(n.drift_cents_per_s, 20, delta=3)
        self.assertAlmostEqual(n.duration, 0.5, delta=0.03)


if __name__ == "__main__":
    unittest.main()
