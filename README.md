# Prairie Bird Voice

A VST3 instrument for Cubase that plays a realistic bird voice from the keyboard.
Keys set the pitch; the plugin makes each note behave like a bird. The first
preset is the white-throated sparrow, with more Saskatchewan species to follow.

The design lives in the *Prairie Bird Voice — Plugin Design* doc; this repo is
build stages 2–4 of its plan: the voice engine, a test panel, and the
render-and-measure tools.

## What's here

| Folder | What it is |
|---|---|
| `dsp/` | The voice engine in plain C++17, no JUCE: tuning, scoop, glide, drift, wobble, whistle source, beak resonance, intro chirp, forest echo. `Params.h` lists every setting. |
| `plugin/` | The JUCE wrapper: VST3 and standalone app, host parameters, factory presets, a plain test panel. |
| `presets/` | Bird presets as JSON, shared by the plugin and the tools. |
| `tools/render/` | `pbv_render`: plays a score through the engine into a WAV file, no Cubase needed. |
| `tools/analysis/` | `measure.py` (what a recording contains) and `match.py` (the matching loop). |
| `tests/` | Engine and analysis tests. |

## Getting the plugin

**Easiest:** every push to `main` builds the Windows plugin on GitHub. Open the
repo's **Actions** tab, pick the latest *Build* run, and download
`PrairieBirdVoice-windows`. Copy `Prairie Bird Voice.vst3` into
`C:\Program Files\Common Files\VST3`, then rescan plugins in Cubase.

**Building on your PC** (Visual Studio Build Tools with C++, CMake, Git):

```
git clone https://github.com/JuniperusMusic/PrairieBirdsVST.git
cd PrairieBirdsVST
cmake -S . -B build -A x64
cmake --build build --config Release
```

CMake picks the newest Visual Studio it finds. The first configure downloads JUCE 9.0.4. The plugin lands in
`build/plugin/PrairieBirdVoice_artefacts/Release/VST3/`. Add
`-DPBV_COPY_PLUGIN=ON` to copy it into the VST3 folder automatically (run the
terminal as administrator).

## Playing it

- Load **Prairie Bird Voice** on an instrument track and pick **White-throated
  Sparrow** from the preset menu.
- The sparrow's root is G. Its three whistles are **C5, B4, G4** (MIDI 72, 71,
  67): play them as separate notes, about 0.6, 0.45 and 0.35 s long. At the
  sparrow's octave +3 they sound at the bird's real pitch, about 3.1-4.3 kHz.
- The octave selector transposes the voice up to 7 octaves (0 to +7; the
  plugin opens at +3), with Semitone (-11 to +11) and Cents (-100 to +100)
  steps. The bird's intervals stay the same.
- Strike the first key hard (velocity 100 or more) for the intro chirp. The
  whistle then starts about 180 ms later, like the bird.
- Overlapping (legato) notes change without a new attack: the loudness dips
  briefly (Legato dip, Legato dip time) and the pitch moves at the quietest
  point, like a bird articulating. Set the dip to 0 dB for a plain slur.
- **Breath mode** on: CC2 (the Yamaha breath controller) sets the loudness and
  no breath means no sound; the breath shapes each attack, and Breath attack
  minimum stops a note from snapping on when you are already blowing hard.
  Off: velocity and the onset set the loudness.
- **Breath setup:** if your controller never reads zero at rest, switch on
  *Learn breath range*, breathe from rest to your hardest, and switch it off:
  Breath floor lands just above your resting value and Breath ceiling at your
  peak. Breath curve shapes the response (above 1 = more room at soft breath).
  Bird presets leave these settings alone.
- **Overblow** (breath mode): harder breath climbs the overtone series, to 2x,
  3x, 4x and 5x the note, each at its own threshold (Overtone 2 at ... 5 at;
  1 = never). Hysteresis keeps it from flickering at a threshold, and Overblow
  time sets how quickly each jump happens.

Not built yet (later build stages): warble, trill and flurry (stage 6); the
per-note tuning editor and Scala files (stage 7); MIDI learn and the
nanoKONTROL2 map (stage 8); the Retrologue-style panel (stage 9). Their
settings already exist, so presets and Cubase projects keep working when they
arrive.

## Rendering and matching

```
# Render a score to WAV
build/tools/render/pbv_render --preset presets/white_throated_sparrow.json --score song.txt --out song.wav

# Measure a recording (times in seconds)
python tools/analysis/measure.py recordings/XC993033.wav --start 29.0 --end 32.3 --propose-temperament

# Match a preset to recordings, one --ref per song
python tools/analysis/match.py --preset presets/white_throated_sparrow.json --out matched.json \
    --ref recordings/XC993033.wav@1.6-4.6 --ref recordings/XC993033.wav@19.0-22.0 \
    --ref recordings/XC993033.wav@29.0-32.3 --ref recordings/XC993033.wav@46.3-49.5 \
    --keep-renders renders/
```

Python needs `pip install numpy scipy soundfile`. Keep recordings in
`recordings/` (git ignores it): they are for measuring only and never go into
the plugin or the repo.

Score files have one event per line: `time on key velocity`, `time off key`,
`time cc number value`, `time bend value` (0–16383), `time at value`.

## Licences

JUCE is used under its AGPLv3 option and the VST3 SDK under MIT (see the
design doc's licensing section). The plugin contains no recorded audio.
