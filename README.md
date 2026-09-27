# Carcosa
## Alternative firmware for the Ambika polysynth

Carcosa is a custom firmware for the [Mutable Instruments Ambika](https://pichenettes.github.io/mutable-instruments-diy-archive/ambika/) that replaces the stock oscillator set with focused synthesis engines while keeping the analog signal path and 6-voice architecture intact.

**[Download the latest release](https://github.com/joegiralt/ambika/releases)**

**[Read the full manual](CARCOSA.md)**

### Synthesis Engines

| Mode | Description |
|------|-------------|
| saw, square, triangle, sine | Classic dual-oscillator subtractive (unchanged, 8-bit like the stock Ambika) |
| pad | 4-voice detuned supersaw |
| noise | Filtered noise with variable color |
| fm4op | TX81Z-accurate 4-operator FM: the 8 TX81Z algorithms, W1-W8 waveforms, log-domain levels and feedback; the 128 TX81Z factory voices come in the release's patches |
| pluck | Karplus-Strong with fractional-delay tuning, long sustain, soundbox body, metallic dispersion, chorus, 4 excitation types |
| wcoast | Buchla-style wavefolder (16+ folds) with symmetry and bias, 2-pole color filter, FM, self-sync, sub oscillator |
| wshape | Waveshaping through nonlinear transfer function |

### Additional Features

- 12-bit output for FM, Karplus-Strong and West Coast, which render at 19.6 kHz with interpolation (the classic engine stays 8-bit)
- 7 envelopes; envelopes 1-3 have curve modes: exponential, linear, looping, looping linear
- Factory patches: TX81Z voices (banks C and T), plus 48 patches that move the engines with the voice LFO and looping envelopes (banks M and I)
- Each engine keeps its own settings, and the OSC button opens the active engine's page
- Analog slop parameter for vintage poly character
- Smart patch randomizer with per-section depth control
- Synthesis mode select: press S1, turn the encoder, press S1 again

So far Carcosa 3.0 has only been run on a Xena (an Ambika clone); see [Known Issues](CARCOSA.md#known-issues).

### What was removed

Step sequencer, CZ synthesis (9 variants), old 2-op FM, 8-bit land, dirty PWM, vowel synthesis, and wavetable oscillators (16 + wavequence).

---

## Install

Download all 7 .BIN files from the [releases page](https://github.com/joegiralt/ambika/releases) and copy them to the root of your SD card. For the factory patches, unzip the release's `PATCHES.zip` to the root of the card too (it replaces banks C, I, M and T).

1. Hold **S8** during power-on to flash the controller (AMBIKA.BIN)
2. Go to the OS info page, select each voicecard port (1-6), press **S4** to flash each

Flash the controller first, then all 6 voicecards. On first boot the settings reset: part 1 gets all 6 voices on MIDI channel 1.

For detailed instructions and recovery, see the [original Ambika firmware update guide](https://pichenettes.github.io/mutable-instruments-diy-archive/ambika/firmware/).

To revert to stock Ambika firmware, place the original files on the SD card and reflash.

## Build from source

You'll need make, gcc-avr, avr-libc and python3. The release builds use avr-gcc 5.4; other versions produce different sizes, and the voicecard has little flash to spare.

```
sudo apt-get install gcc-avr make avr-libc python3
# The submodule's git:// URL no longer works; fetch avrlib over https.
git -c submodule.avrlib.url=https://github.com/pichenettes/avril.git submodule update --init avrlib
```

Build the voicecard and the controller (point AVRLIB_TOOLS_PATH at the directory holding avr-gcc):
```
make all bin AVRLIB_TOOLS_PATH=/usr/bin/
make -f controller/makefile all bin AVRLIB_TOOLS_PATH=/usr/bin/
```

Copy the firmware and the factory patches to an SD card (`make_patches.py` generates the patches):
```
./flash_sd.sh /mnt/sdcard
```

Run the voicecard DSP tests on your computer (needs g++):
```
sh voicecard/test/run.sh
```

## Credits

Original Ambika firmware by Emilie Gillet (Mutable Instruments). Released under GPL v3.0.

Carcosa firmware by Joseph Martin Giralt.
