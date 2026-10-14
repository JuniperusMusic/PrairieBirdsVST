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
- The sparrow's root is G. Its three whistles are **C8, B7, G7** (MIDI 108,
  107, 103): play them as separate notes, about 0.6, 0.45 and 0.35 s long.
  At octave -2 they sound two octaves below the bird, near 1 kHz; at octave 0
  they sound at the bird's real 3-4.4 kHz.
- Strike the first key hard (velocity 100 or more) for the intro chirp. The
  whistle then starts about 180 ms later, like the bird.
- Overlapping (legato) notes glide without a new attack.
- **Breath mode** on: CC2 (the Yamaha breath controller) sets the loudness and
  no breath means no sound. Off: velocity and the attack set the loudness.

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
