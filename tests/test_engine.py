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

    def render(self, score, preset=None, sets=(), tail=0.5, extra=()):
        d = Path(self.tmp.name)
        (d / "score.txt").write_text(score)
        out = d / "out.wav"
        cmd = [self.render_bin, "--score", str(d / "score.txt"), "--out", str(out), "--mono", "--tail", str(tail)]
        if preset:
            cmd += ["--preset", str(preset)]
        for s in sets:
            cmd += ["--set", s]
        cmd += list(extra)
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
        # + sounds higher: at the default octave +3, A4 sounds three octaves up.
        x, sr = self.render("0 on 69 100\n0.6 off 69\n", sets=[*STEADY])
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.2, 0.55), 3520.0), 0.0, delta=0.5)

    def test_transpose_semitones_and_cents(self):
        x, sr = self.render("0 on 57 100\n0.6 off 57\n", sets=["octave=1", "transposeSemitones=5", "transposeCents=30", *STEADY])
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.2, 0.55), 220.0), 1200 + 500 + 30, delta=0.5)
        x, sr = self.render("0 on 69 100\n0.6 off 69\n", sets=["octave=0", "transposeSemitones=-11", "transposeCents=-50", *STEADY])
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.2, 0.55), 440.0), -1150, delta=0.5)

    def test_seven_octaves_up(self):
        # At +7 the sparrow's top whistle (C8 at octave 0, about 4.3 kHz) is played on C1.
        x, sr = self.render("0 on 24 100\n1.0 off 24\n", SPARROW, ["octave=7", *STEADY])
        expected = 440.0 * 2 ** ((24 - 69 + 0.49 + 84) / 12)
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
        # The sparrow's default echo sits about 19 dB below the note, like the recording.
        # A pure tone's tail swings about +-6 dB with pitch, so average the song's three pitches.
        tails = []
        for key in (67, 71, 72):  # the song's keys at the sparrow's octave +3
            for seed in (1, 2, 3):
                d = Path(self.tmp.name)
                (d / "e.txt").write_text(f"0.1 on {key} 90\n0.5 off {key}\n")
                subprocess.run([self.render_bin, "--preset", str(SPARROW), "--score", str(d / "e.txt"), "--out", str(d / "e.wav"),
                                "--mono", "--tail", "1.2", "--seed", str(seed), "--set", "wobbleLevel=0"], check=True)
                x, sr = sf.read(d / "e.wav")
                tails.append(self.level_db(x, sr, 0.54, 0.59) - self.level_db(x, sr, 0.3, 0.45))
        self.assertAlmostEqual(float(np.mean(tails)), -19, delta=2)

    def test_legato_glides_without_new_attack(self):
        score = "0 on 69 100\n0.4 on 71 100\n0.45 off 69\n0.8 off 71\n"
        x, sr = self.render(score, sets=["octave=0", "legatoMode=Glide", "glideAmount=1", "glideTime=50", *STEADY])
        song = pa.analyse(x, sr, floor_db=20)
        self.assertEqual(len(song.notes), 1, "legato notes should stay connected")
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.55, 0.75), 440.0), 200, delta=1)

    def env_db(self, x, sr, win=0.004):
        n = int(win * sr)
        frames = x[: len(x) // n * n].reshape(-1, n)
        return 10 * np.log10(np.mean(frames ** 2, axis=1) + 1e-20), win

    def test_legato_dip_articulates_connected_notes(self):
        score = "0 on 69 100\n0.4 on 71 100\n0.45 off 69\n0.8 off 71\n"
        base = ["octave=0", "glideAmount=0", "echoWet=0", "legatoMode=Dip", *STEADY]
        dipped, sr = self.render(score, sets=[*base, "legatoDip=20", "legatoDipTime=40"])
        plain, _ = self.render(score, sets=[*base, "legatoDip=0"])
        for x, want in ((dipped, -14), (plain, -1)):
            db, win = self.env_db(x, sr)
            steady = np.median(db[int(0.25 / win):int(0.38 / win)])
            low = db[int(0.39 / win):int(0.45 / win)].min() - steady
            if want < -2:
                self.assertLess(low, want)
            else:
                self.assertGreater(low, want)
        # The pitch changes at the dip: B4 after it, A4 before it.
        self.assertAlmostEqual(cents(self.steady_pitch(dipped, sr, 0.2, 0.39), 440.0), 0, delta=1)
        self.assertAlmostEqual(cents(self.steady_pitch(dipped, sr, 0.5, 0.75), 440.0), 200, delta=1)

    def test_legato_carries_the_sag(self):
        # The first note sags; a connected note must not jump back up first.
        x, sr = self.render("0 on 69 100\n0.5 on 70 100\n0.9 off 70\n0.95 off 69\n",
                            sets=["octave=0", "legatoMode=Glide", "glideAmount=1", "glideTime=200", "driftSag=-100",
                                  "wobblePitch=0", "wobbleLevel=0", "driftAmount=0", "scoopDepth=0", "chirpThreshold=128", "echoWet=0"])
        tr = pa.track(x, sr)
        t, f = tr.t, tr.f0
        before = np.nanmedian(f[(t > 0.47) & (t < 0.495)])
        after = np.nanmedian(f[(t > 0.505) & (t < 0.53)])
        self.assertLess(abs(cents(after, before)), 15, "pitch jumped at the start of the glide")

    def test_breath_floor_reads_as_silence(self):
        quiet, sr = self.render("0 cc 2 25\n0 on 69 100\n0.5 off 69\n", sets=["breathMode=true", "breathFloor=30"])
        blown, _ = self.render("0 cc 2 100\n0 on 69 100\n0.5 off 69\n", sets=["breathMode=true", "breathFloor=30"])
        self.assertLess(np.abs(quiet).max(), 1e-6)
        self.assertGreater(self.level_db(blown, sr, 0.2, 0.4), -40)

    def test_breath_curve_shapes_loudness(self):
        levels = []
        for curve in ("1", "3"):
            x, sr = self.render("0 cc 2 64\n0 on 69 100\n0.5 off 69\n",
                                sets=["breathMode=true", f"breathCurve={curve}", "echoWet=0", "wobbleLevel=0"])
            levels.append(self.level_db(x, sr, 0.2, 0.4))
        # Half breath: a curve of 3 is (0.5^3)/(0.5^1) = 1/4 the level, about 12 dB quieter.
        self.assertAlmostEqual(levels[0] - levels[1], 12.0, delta=1.0)

    def test_breath_attack_minimum(self):
        # Strong breath already flowing when the key goes down: the attack still takes the minimum time.
        x, sr = self.render("0 cc 2 127\n0.1 on 69 100\n0.6 off 69\n",
                            sets=["breathMode=true", "breathAttack=40", "echoWet=0", "wobbleLevel=0", "breathBurst=0"])
        full = self.level_db(x, sr, 0.3, 0.5)
        self.assertLess(self.level_db(x, sr, 0.100, 0.106) - full, -15)
        self.assertGreater(self.level_db(x, sr, 0.145, 0.16) - full, -1)

    def test_overblow_climbs_the_series(self):
        sets = ["breathMode=true", "overblowOn=true", "breathCurve=1", "octave=0", "echoWet=0",
                "overblow2=0.6", "overblow3=0.8", "overblow4=1", "overblow5=1", "driftBreathLink=0", *STEADY]
        score = "0 cc 2 64\n0 on 57 100\n0.5 cc 2 89\n1.0 cc 2 115\n1.5 cc 2 89\n2.0 off 57\n"
        x, sr = self.render(score, sets=sets)
        f1 = self.steady_pitch(x, sr, 0.2, 0.45)   # breath 0.50: the note
        f2 = self.steady_pitch(x, sr, 0.7, 0.95)   # 0.70: 2nd overtone
        f3 = self.steady_pitch(x, sr, 1.2, 1.45)   # 0.91: 3rd overtone
        f4 = self.steady_pitch(x, sr, 1.7, 1.95)   # back to 0.70: below 0.8 - 0.05, so down to the 2nd
        self.assertAlmostEqual(cents(f1, 220.0), 0, delta=2)
        self.assertAlmostEqual(cents(f2, 440.0), 0, delta=2)
        self.assertAlmostEqual(cents(f3, 660.0), 0, delta=2)
        self.assertAlmostEqual(cents(f4, 440.0), 0, delta=2)
        off, _ = self.render(score, sets=[s for s in sets if s != "overblowOn=true"])
        self.assertAlmostEqual(cents(self.steady_pitch(off, sr, 1.2, 1.45), 220.0), 0, delta=2)

    def test_overblow_hysteresis_holds_near_the_threshold(self):
        sets = ["breathMode=true", "overblowOn=true", "breathCurve=1", "octave=0", "echoWet=0",
                "overblow2=0.6", "overblow3=1", "overblow4=1", "overblow5=1", "overblowHysteresis=0.1", "driftBreathLink=0", *STEADY]
        # Up past 0.6, then down to 0.55: inside the hysteresis band, so it stays on the 2nd overtone.
        x, sr = self.render("0 cc 2 89\n0 on 57 100\n0.5 cc 2 70\n1.0 off 57\n", sets=sets)
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.7, 0.95), 440.0), 0, delta=2)

    def test_break_mode_separates_connected_notes(self):
        # Hold A4; press B4 on top, then let it go: each change is release, a silent break, a fresh note.
        score = "0 on 69 100\n0.4 on 71 100\n0.9 off 71\n1.5 off 69\n"
        x, sr = self.render(score, sets=["octave=0", "echoWet=0", "release=10", "legatoBreak=175", *STEADY])
        full = self.level_db(x, sr, 0.2, 0.38)
        self.assertLess(self.level_db(x, sr, 0.43, 0.57) - full, -50)   # the break after pressing B4
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 0.68, 0.88), 440.0), 200, delta=1)
        self.assertLess(self.level_db(x, sr, 0.93, 1.07) - full, -50)   # the break back to A4
        self.assertAlmostEqual(cents(self.steady_pitch(x, sr, 1.18, 1.45), 440.0), 0, delta=1)

    def test_break_length(self):
        score = "0 on 69 100\n0.4 on 71 100\n1.0 off 71\n1.0 off 69\n"
        starts = []
        for gap in (100, 250):
            x, sr = self.render(score, sets=["octave=0", "echoWet=0", "release=10", "onset=5", f"legatoBreak={gap}", *STEADY])
            db, win = self.env_db(x, sr, 0.002)
            full = np.median(db[int(0.2 / win):int(0.38 / win)])
            later = np.flatnonzero(db[int(0.45 / win):] > full - 10)
            starts.append(0.45 + later[0] * win)
        self.assertAlmostEqual(starts[1] - starts[0], 0.150, delta=0.006)

    def count_dips(self, x, sr, a, b, below_db=-20):
        db, win = self.env_db(x, sr, 0.002)
        seg = db[int(a / win):int(b / win)]
        ref = np.percentile(seg, 90)
        low = seg < ref + below_db
        return int(np.sum(low[1:] & ~low[:-1]))

    def test_trill_continuous(self):
        x, sr = self.render("0 on 69 100\n1.2 off 69\n", sets=["octave=0", "echoWet=0", "onset=5", "trillDepth=1", "trillRate=10",
                                                               "trillPattern=Continuous", *STEADY])
        self.assertEqual(self.count_dips(x, sr, 0.2, 1.0), 8)

    def test_trill_doublet_holds_the_last(self):
        x, sr = self.render("0 on 69 100\n1.0 off 69\n", sets=["octave=0", "echoWet=0", "onset=5", "trillDepth=1", "trillRate=10",
                                                               "trillPattern=Doublet", *STEADY])
        self.assertEqual(self.count_dips(x, sr, 0.0, 0.95), 1)
        x, sr = self.render("0 on 69 100\n1.0 off 69\n", sets=["octave=0", "echoWet=0", "onset=5", "trillDepth=1", "trillRate=10",
                                                               "trillPattern=Triplet", *STEADY])
        self.assertEqual(self.count_dips(x, sr, 0.0, 0.95), 2)

    def test_trill_sync_follows_tempo(self):
        # Sync at 1/8 and 150 BPM: 5 trills a second, whatever the Hz rate says.
        sets = ["octave=0", "echoWet=0", "onset=5", "trillDepth=1", "trillRate=20", "trillPattern=Continuous",
                "syncMode=Sync", "syncDivision=1/8", *STEADY]
        x, sr = self.render("0 on 69 100\n1.2 off 69\n", sets=[*sets, "tempoSource=Cubase"], extra=["--bpm", "150"])
        self.assertEqual(self.count_dips(x, sr, 0.1, 1.1), 5)
        x, sr = self.render("0 on 69 100\n1.2 off 69\n", sets=[*sets, "tempoSource=Typed", "typedBpm=75"])
        self.assertIn(self.count_dips(x, sr, 0.1, 1.1), (2, 3))

    def test_warble_depth(self):
        x, sr = self.render("0 on 69 100\n1.0 off 69\n", sets=["octave=0", "echoWet=0", "warbleDepth=50", "warbleRate=5", *STEADY])
        tr = pa.track(x, sr)
        c = 1200 * np.log2(tr.f0[(tr.t > 0.3) & (tr.t < 0.9)] / 440.0)
        self.assertAlmostEqual(np.nanmax(c) - np.nanmin(c), 100, delta=12)

    def test_flurry_jumps(self):
        sets = ["octave=0", "echoWet=0", "flurrySpread=400", *STEADY]
        x, sr = self.render("0 on 69 100\n2.0 off 69\n", sets=[*sets, "flurryDensity=0.5"])
        tr = pa.track(x, sr)
        c = np.abs(1200 * np.log2(tr.f0[(tr.t > 0.2) & (tr.t < 1.9)] / 440.0))
        self.assertGreater(np.nanmax(c), 100)
        x, sr = self.render("0 on 69 100\n2.0 off 69\n", sets=[*sets, "flurryDensity=0"])
        tr = pa.track(x, sr)
        c = np.abs(1200 * np.log2(tr.f0[(tr.t > 0.2) & (tr.t < 1.9)] / 440.0))
        self.assertLess(np.nanmax(c), 5)

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
