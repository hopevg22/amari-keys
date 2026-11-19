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
