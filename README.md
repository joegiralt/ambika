# Carcosa
## Alternative firmware for the Ambika polysynth

Carcosa is a custom firmware for the [Mutable Instruments Ambika](https://pichenettes.github.io/mutable-instruments-diy-archive/ambika/) that replaces the stock oscillator set with focused synthesis engines while keeping the analog signal path and 6-voice architecture intact.

**[Download the latest release](https://github.com/joegiralt/ambika/releases)**

**[Read the full manual](CARCOSA.md)**

### Synthesis Engines

| Mode | Description |
|------|-------------|
| saw, square, triangle, sine | Classic dual-oscillator subtractive (unchanged) |
| pad | 4-voice detuned supersaw |
| noise | Filtered noise with variable color |
| fm4op | 4-operator FM with 8 TX81Z algorithms, 8 waveforms, exponential levels, 16-bit feedback |
| pluck | Karplus-Strong with IIR damping, 3-head ensemble chorus, stiffness, 4 excitation types |
| wcoast | Buchla-style wavefolder (16+ folds), 2-pole color filter, FM, self-sync, sub oscillator |
| wshape | Waveshaping through nonlinear transfer function |

### Additional Features

- Per-envelope curve modes: exponential, linear, looping, looping linear
- Analog slop parameter for vintage poly character
- Smart patch randomizer with per-section depth control
- S1+encoder synthesis mode navigation

### What was removed

Step sequencer, CZ synthesis (9 variants), old 2-op FM, 8-bit land, dirty PWM, vowel synthesis, and wavetable oscillators (16 + wavequence).

---

## Install

Download all 7 .BIN files from the [releases page](https://github.com/joegiralt/ambika/releases) and copy them to the root of your SD card.

1. Hold **S8** during power-on to flash the controller (AMBIKA.BIN)
2. Go to the OS info page, select each voicecard port (1-6), press **S4** to flash each

Flash the controller first, then all 6 voicecards.

For detailed instructions and recovery, see the [original Ambika firmware update guide](https://pichenettes.github.io/mutable-instruments-diy-archive/ambika/firmware/).

To revert to stock Ambika firmware, place the original files on the SD card and reflash.

## Build from source

You'll need:
- make
- gcc-avr
- avr-libc

```
sudo apt-get install gcc-avr make avr-libc
git submodule update --init
export AVRPATH=$(which avr-gcc)
sed "s|AVRLIB_TOOLS_PATH ?=.*|AVRLIB_TOOLS_PATH ?= $(dirname $AVRPATH)/|" avrlib/makefile.mk > mkfiletmp
mv mkfiletmp avrlib/makefile.mk
```

Build voicecard:
```
make all && make bin
```

Build controller:
```
make -f controller/makefile && make -f controller/makefile bin
```

Flash to SD card:
```
./flash_sd.sh /mnt/sdcard
```

## Credits

Original Ambika firmware by Emilie Gillet (Mutable Instruments). Released under GPL v3.0.

Carcosa firmware by Joseph Martin Giralt.
