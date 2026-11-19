# Amari Keys — test build 0.1

Personal-use stage keyboard plugin (VST3 + Standalone) by Amari Labs. This test build is fully sample-free:

- **Organ**: 9-drawbar tonewheel engine (B3 / VOX / FARF / PIPE models), single-trigger percussion, key click, V1–C3 scanner vibrato/chorus, drive, rotary speaker (slow / fast / stop with horn and drum inertia).
- **Keys**: FM tine EP, reed EP, clav, FM bell; timbre, dynamics, decay, stereo tremolo.
- **Synth**: 2 oscillators + sub, LP24/LP12/HP/BP filter, filter & amp ADSR, LFO, poly/mono/legato + glide, pitch bend ±2.
- **Pads**: 16 synthesised drums (click pads or MIDI channel 10, notes C2–D#3 / 36–51).
- **Effects**: chorus/phaser, ping-pong delay, reverb, amp, compressor, 3-band EQ, with a target selector (all / organ / keys / synth).
- **Mix**: faders per section, main, and a keys↔synth blend crossfader. Split point (default C3), transpose, velocity curve, panic.

## Build
Push to GitHub: the Actions workflow builds `AmariKeys-Windows-VST3` and `AmariKeys-Windows-Standalone` artifacts.
Locally: `cmake -B build && cmake --build build --config Release --target AmariKeys_VST3`
(JUCE 8.0.12 is fetched automatically; pass `-DJUCE_DIR=...` to use a local copy.)

Install: copy `Amari Keys.vst3` to `C:\Program Files\Common Files\VST3\` and rescan in Ableton.

## MY SAMPLES (build 0.2)
In the KEYS section press **LOAD FOLDER** (a folder of WAV/AIFF/FLAC/OGG files) or **LOAD SFZ**.
Folder files are mapped by the note in their name: `Piano_C4_v2.wav`, `A0v1.flac`, `C#4.wav`, `Db3_mf.wav`, `060.wav`.
Velocity layers come from `v1`, `v2`… or `pp/p/mp/mf/f/ff` (soft → loud). Gaps between sampled notes are filled by repitching the nearest sample.
Files with release / harmonics / pedal / noise in the name are skipped. Samples are held in memory as 16-bit (max 30 s each).
The loaded path is saved with the session and reloads automatically.

## Build 0.3 — layers, programs, new look
- 8 layer groups with a fader each in the LAYER MIXER: PIANO, E.PIANO, ORGAN, KEYS, STRINGS, BRASS, PADS, SYNTH.
  LED button = layer on/off, fader = level, zone tag (click) = LO / UP / ALL. Click a channel to edit it.
- KEYS EDIT panel follows the selected PIANO / E.PIANO / KEYS layer; SYNTH EDIT follows STRINGS / BRASS / PADS / SYNTH.
  Every layer can load its own samples (LOAD FOLDER / LOAD SFZ).
- Programs: 16 slots (bank 1-4 x A-D). STORE saves the current sound to the selected slot; double-click the display to rename.
- Drum pads 1A-4D: DRUM (16 drums), PROG (pad = program 1A..4D), MIX (top row = programs A-D of the current bank, rest = drums).
  MIDI channel 10 notes 36-51 play pads 1A-4D.
- MIDI LEARN: turn it on, click any control, move a knob/slider on your controller.

## Amari Sampler (tools)
`AmariSampler/` is a small Windows app that loads any VST3 instrument, plays it note by note at several velocities,
trims each note and saves `Name_C#4_v3.wav` files plus `Name.sfz` - ready for LOAD SFZ / LOAD FOLDER in Amari Keys.
CI artifact: **AmariSampler-Windows**. Engine: `AmariSampler/MultiSampleRenderer.h` (round-trip test: `tests/SamplerRoundTrip.cpp`).
Personal use only: respect each instrument's licence and never share sampled sounds.
