# Carcosa v2.0
## Custom firmware for the Ambika polysynth

Carcosa replaces the stock Ambika firmware with a focused set of synthesis engines, expanded envelope capabilities, and a streamlined interface.

Based on the original Ambika firmware by Emilie Gillet (Mutable Instruments).

---

## Synthesis Modes

Carcosa has two categories of sound engines: **classic** dual-oscillator modes and **special** single-voice modes.

### Classic Modes

Classic modes work like the original Ambika. Two independent oscillators mixed together, processed through the analog filter.

| Waveform | Parameter knob |
|----------|---------------|
| **none** | Silence |
| **saw** | — |
| **square** | Pulse width (0 = 50% square, 1-127 = variable PWM) |
| **triangle** | Waveshaping amount |
| **sine** | — |
| **pad** | Detune spread (4 detuned saws, supersaw-style) |
| **noise** | Filter color (0-63 = low-pass, 64-127 = high-pass) |

In classic modes, both osc1 and osc2 are active. The mixer page controls balance, crossmod, sub oscillator, noise, fuzz, and crush.

### Special Modes

Special modes take over the entire voice. The mixer page parameters are repurposed for that engine's controls. Each has a dedicated UI with 1-2 pages of parameters.

#### 4-Op FM (fm4op)

TX81Z-style frequency modulation with 4 operators.

**Page 1:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | algo | 1-8 | Algorithm (operator routing topology) |
| 2 | fbk | 0-127 | Feedback on operator 4 |
| 3 | wav1 | sin-saw | Operator 1 waveform (8 TX81Z waveforms) |
| 4 | wav2 | sin-saw | Operator 2 waveform |
| 5 | rat1 | 0.5-127 | Operator 1 frequency ratio |
| 6 | fin1 | -64/+63 | Operator 1 fine detune |
| 7 | rat2 | 0.5-127 | Operator 2 frequency ratio |
| 8 | fin2 | -64/+63 | Operator 2 fine detune |

**Page 2:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | wav3 | sin-saw | Operator 3 waveform |
| 2 | wav4 | sin-saw | Operator 4 waveform |
| 3 | lvl1 | 0-127 | Operator 1 output level |
| 4 | lvl2 | 0-127 | Operator 2 output level |
| 5 | rat3 | 0.5-127 | Operator 3 frequency ratio |
| 6 | fin3 | -64/+63 | Operator 3 fine detune |
| 7 | lvl3 | 0-127 | Operator 3 output level |
| 8 | lvl4 | 0-127 | Operator 4 output level |

**The 8 algorithms:**

```
1: 4->3->2->1        Full serial chain
2: (3+4)->2->1       Two modulators into carrier chain
3: (4->3)+2->1       Parallel mod + carrier into final carrier
4: (4->3)+(2->1)     Two parallel pairs
5: (4->2)+(4->3->1)  Shared modulator, two paths
6: 4->(1+2+3)        One modulator, three carriers
7: (4->1)+2+3        One modulated + two free carriers
8: 1+2+3+4           All carriers (additive)
```

**Operator waveforms:** sin, half, abs, qtr, habs, tri, pls, saw

The mod matrix destination `prm1` controls total FM depth and can be modulated by envelopes/LFOs for dynamic timbral control.

#### Karplus-Strong (pluck)

Physical model of a plucked string. A noise burst excites a tuned delay line with filtered feedback.

**Page 1:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | damp | 0-127 | Damping (high = darker, faster decay) |
| 2 | colr | 0-127 | Excitation brightness |
| 3 | dcay | 0-127 | Overall decay rate |
| 4 | body | 0-127 | Body resonance (comb filter at half delay) |
| 5 | rang | -24/+24 | Pitch range (semitones) |
| 6 | tune | -64/+63 | Fine tuning |
| 7 | pos | 0-127 | Pluck position (changes harmonic content) |
| 8 | exc | nois/clic/brit/dark | Excitation type |

**Page 2:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | rate | 0-127 | Ensemble LFO speed |
| 2 | dpth | 0-127 | Ensemble depth (3 read heads from same delay) |
| 3 | sprd | 0-127 | Ensemble spread (phase offset between heads) |
| 4 | wmix | 0-127 | Ensemble wet/dry mix |
| 5 | stif | 0-127 | String stiffness (inharmonic partials, bell-like) |
| 6 | feed | 0-127 | Extra feedback (longer sustain, self-oscillation) |

The `prm1` mod destination controls damping and can be modulated for expressive playing.

#### West Coast (wcoast)

Buchla-style wavefolder with built-in FM and sub-oscillator.

**Page 1:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | wave | sin/tri | Base waveform |
| 2 | fold | 0-127 | Fold depth (harmonics increase with depth) |
| 3 | sym | 0-127 | Symmetry (even/odd harmonic balance, 64=center) |
| 4 | bias | 0-127 | DC offset into folder |
| 5 | fmdp | 0-127 | FM modulation depth |
| 6 | fmrt | -24/+24 | FM modulator ratio |
| 7 | colr | 0-127 | Post-fold brightness |
| 8 | drve | 0-127 | Pre-fold drive |

**Page 2:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | flds | 0-127 | Number of fold stages (1-6) |
| 2 | gain | 0-127 | Input gain before folding |
| 3 | env | 0-127 | Envelope-to-fold depth (timbral VCA) |
| 4 | sub | 0-127 | Sub-oscillator level (one octave down) |
| 5 | sync | 0-127 | Self-sync amount (metallic tones) |

Route an envelope to `prm1` (fold depth) for the classic west coast behavior where timbre and amplitude are linked.

#### Waveshaping (wshape)

Drives a sine wave through a nonlinear waveshaper. Works as a normal oscillator (can be used on osc1 or osc2 alongside other waveforms).

| Knob | Description |
|------|-------------|
| prm | Drive amount (0 = clean sine, 127 = heavily shaped) |
| rng | Pitch range |
| tun | Fine tuning |

---

## Navigation

### Classic mode (osc/mixer pages)

- **S1 press**: Toggle between oscillator page and mixer page
- **Encoder**: Scroll through parameters, click to edit

### Entering special modes

1. While on the osc or mixer page, **press S1** — LED 1 starts blinking (mode select active)
2. **Turn encoder** to cycle through: classic, fm4op, pluck, wcoast
3. **Press S1 again** to confirm and exit mode select

Mode select also works from inside any special mode page. Pressing any other button exits mode select automatically.

### Inside special mode pages

- **S1 press**: Toggle between page 1 and page 2
- **Encoder**: Scroll through parameters on the current page, click to edit
- **Knobs 1-8**: Direct control of the 8 parameters shown on screen

### Other pages

All other pages (filter, envelope, modulation, part, arpeggiator, multi, performance, system) work exactly like the original Ambika.

---

## Envelopes

Carcosa has **7 envelopes**. Envelopes 1-3 have paired LFOs and curve selection (exponential, linear, looping). Envelopes 4-7 are simple ADSR — lightweight extra modulation sources for per-operator FM control, layered modulation, or any creative routing.

The ENV/LFO page selector (knob 1) cycles through all 7 envelopes. When env 1-3 is selected, the top row shows LFO rate, shape, and envelope curve. When env 4-7 is selected, the top row is blank — just ADSR on the bottom row.

All 7 envelopes are available as modulation sources in the mod matrix (env1 through env7).

Envelopes 1-3 have a selectable curve mode (knob 4, labeled `mode`):

| Mode | Behavior |
|------|----------|
| **exp** | Exponential ADSR (original Ambika behavior) |
| **lin** | Linear ADSR (SH-101 style, snappy) |
| **loop** | Cycling AD with exponential curves (Maths/function generator) |
| **lpln** | Cycling AD with linear curves (triangle/ramp LFO) |

In loop modes:
- **attk** = rise time
- **dec** = fall time
- **sus** = loop floor (how low the cycle goes before retriggering)
- **rel** = release time (when key is released, looping stops and envelope falls to zero)

Each envelope's mode is independent. Use exponential ADSR on the VCA, looping linear on the filter, etc.

---

## Slop

The **slop** parameter on the part page (knob 3) adds per-note random variation to pitch and envelope timing. This simulates the analog component drift that gives vintage polysynths their character.

- **0**: Pristine digital (no variation)
- **Low values (10-30)**: Subtle warmth, slight detuning between voices
- **High values (60-127)**: Wobbly, unstable, heavily vintage

Each note-on generates fresh random offsets. In a chord, each voice drifts differently.

---

## Randomizer

Access via the performance button group (press 3 times: performance -> knob assign -> randomizer).

| Knob | Section |
|------|---------|
| 1 | Oscillator randomization depth |
| 2 | Filter randomization depth |
| 3 | Envelope randomization depth |
| 4 | Modulation matrix randomization depth |
| 5 | Mixer randomization depth |
| 6 | LFO randomization depth |
| 7 | Global depth (scales all others) |
| 8 | Shows "push" — click encoder to trigger |

Set knobs to 0 to lock that section. Turn up for more variation. Click the encoder to generate a new random patch. Each click produces a different result.

---

## Firmware Update

### Via SD Card

1. Run `./flash_sd.sh /mnt/sdcard` (or copy `AMBIKA.BIN` and `VOICE1-6.BIN` manually)
2. Insert SD card into Ambika
3. **Hold S8 during power-on** to flash the controller
4. After reboot, go to OS info page, select each voicecard port (1-6), press S4 to flash

Flash the controller first, then the voicecards.

### Recovery

If the firmware crashes on boot, **hold S8 during power-on** to force the bootloader to read from SD card. This always works regardless of firmware state.

To revert to stock Ambika firmware, place the original `AMBIKA.BIN` and `VOICE*.BIN` files on the SD card and flash.

---

## Technical Notes

### Memory Usage

| | Voicecard (ATmega328p) | Controller (ATmega644p) |
|---|---|---|
| Flash | 29,806 / 32,256 (92%) | 57,504 / 61,440 (94%) |
| RAM | 1,458 / 2,048 (71%) | 3,828 / 4,096 (93%) |

### Changes from Stock Ambika

**Removed:** CZ synthesis (9 variants), old 2-op FM, 8-bit land, dirty PWM, vowel synthesis, wavetable oscillators (16 + wavequence), step sequencer.

**Added:** 4-op FM, Karplus-Strong, west coast wavefolder, waveshaping, looping envelopes, analog slop, smart randomizer, dedicated UI pages for special modes.

**Bug fixes:** Note stack uninitialized variables, transient generator off-by-one, SPI bulk write bounds check, page group initialization size.

### Patch Compatibility

Existing Ambika patches using saw, square, triangle, sine, pad, or noise will work unchanged. Patches using removed waveforms (CZ, wavetable, etc.) will have their oscillator shape reset to `none` on first load.

---

## MIDI CC Reference

All synthesis modes respond to MIDI CCs. The special modes reuse existing patch byte offsets, so the same CC numbers control different parameters depending on the active mode.

### Global (all modes)

| CC | Parameter |
|----|-----------|
| 108 | FM feedback (fm4op mode) |
| 109 | Slop |
| 110 | Envelope curve (env 1-3 only) |
| 111 | Extra envelope attack (env 4-7) |
| 112 | Extra envelope decay (env 4-7) |
| 113 | Extra envelope sustain (env 4-7) |
| 114 | Extra envelope release (env 4-7) |

### Classic Mode

| CC | Parameter |
|----|-----------|
| 16 | Osc 1 waveform |
| 17 | Osc 1 parameter |
| 14 | Osc 1 range |
| 15 | Osc 1 detune |
| 18 | Osc 2 waveform |
| 19 | Osc 2 parameter |
| 20 | Osc 2 range |
| 21 | Osc 2 detune |
| 22 | Mix balance |
| 23 | Mix operator |
| 24 | Mix parameter |
| 25 | Sub osc shape |
| 26 | Sub osc level |
| 27 | Noise level |
| 12 | Fuzz |
| 13 | Crush |
| 3 | Filter cutoff |
| 9 | Filter resonance |

### 4-Op FM Mode

| CC | FM parameter |
|----|-------------|
| 17 | Algorithm |
| 108 | Feedback |
| 18 | Op 1/2 waveform (packed) |
| 19 | Op 3/4 waveform (packed) |
| 14 | Op 1 ratio |
| 15 | Op 1 fine |
| 20 | Op 2 ratio |
| 21 | Op 2 fine |
| 22 | Op 3 ratio |
| 23 | Op 3 fine |
| 24 | Op 4 ratio |
| 25 | Op 4 fine |
| 26 | Op 1 level |
| 27 | Op 2 level |
| 12 | Op 3 level |
| 13 | Op 4 level |

### Karplus-Strong Mode

| CC | KS parameter |
|----|-------------|
| 17 | Damping |
| 19 | Excitation color |
| 20 | Decay rate |
| 21 | Pluck position |
| 18 | Excitation type |
| 22 | Body resonance |
| 23 | Ensemble rate |
| 24 | Ensemble depth |
| 25 | Ensemble spread |
| 26 | Ensemble mix |
| 27 | Stiffness |
| 12 | Extra feedback |

### West Coast Mode

| CC | WC parameter |
|----|-------------|
| 18 | Base waveform |
| 17 | Fold depth |
| 19 | Symmetry |
| 22 | Bias |
| 20 | FM depth |
| 21 | FM ratio |
| 24 | Color |
| 23 | Drive |
| 25 | Fold stages |
| 26 | Input gain |
| 27 | Env to fold |
| 12 | Sub level |
| 13 | Sync |

---

## FAQ

**Can I go back to the stock Ambika firmware?**
Yes. Put the original AMBIKA.BIN and VOICE*.BIN files on the SD card and hold S8 during power-on. The bootloader is never modified — you can always reflash.

**Will my existing patches work?**
Patches using saw, square, triangle, sine, pad, or noise will sound the same. Patches using removed waveforms (CZ, wavetables, vowel, 8-bit, etc.) will have their oscillator reset to `none` on first load. Filter settings, envelopes, and mod matrix are unchanged.

**Do I need to flash all 6 voicecards?**
Yes. The voicecard firmware contains the new synthesis engines. If you only flash the controller, the voicecards won't understand the new waveform types and you'll get silence or wrong sounds.

**My Ambika shows garbage on the screen after flashing. Is it bricked?**
No. Hold S8 during power-on to force the bootloader to reflash from SD card. This always works regardless of what firmware is running.

**Does this work with all voicecard types (4P, SVF, SMR)?**
Yes. Carcosa only changes the digital oscillator code. The analog filter on each voicecard is untouched and works exactly as before.

**Can I use the special modes (FM, pluck, wcoast) with the analog filter?**
Yes. The filter is in the analog signal path after the DAC. Every synthesis mode runs through it. FM through the SSM2164 ladder filter sounds great.

**Why remove the wavetables? I used those.**
The 16 wavetable banks consumed 10.3 KB of flash on a 32 KB chip. Removing them freed enough space for the FM, Karplus-Strong, and west coast engines combined. The tradeoff is intentional — fewer but deeper sound sources.

**Why remove the step sequencer?**
The step sequencer consumed nearly 2 KB of controller flash and 384 bytes of RAM. Most Ambika owners sequence externally via MIDI. The arpeggiator is still present. Removing the sequencer gave enough headroom to keep all the new UI pages stable.

**Can I use FM on one part and subtractive on another?**
Yes. Each part selects its own synthesis mode independently. Part 1 can be FM while part 2 plays saw through the filter. The mode is stored per-patch.

**How does the slop parameter work?**
Each note-on generates a small random pitch offset and envelope timing variation. The amount is controlled by the slop knob on the part page. In a chord, each voice gets a different random value, simulating the component drift of analog polysynths.

**How do looping envelopes work?**
Set the envelope mode to `loop` or `lpln`. The envelope cycles attack → decay → attack → decay continuously while the key is held. The sustain knob sets the floor of the cycle. When the key is released, the envelope falls to zero at the release rate. This turns an envelope into a function generator, similar to Make Noise Maths.

**What are the 8 FM waveforms?**
They match the TX81Z: sine, half-sine, absolute sine, quarter sine, half-absolute, triple-absolute, pulse sine, and saw sine. All are derived from the existing sine table with zero extra flash cost.

**Is this open source?**
Yes, and it always will be. Carcosa exists because Emilie Gillet open-sourced the Ambika firmware. That firmware exists because of open-source AVR toolchains, open hardware designs, and a community that shares knowledge freely. We stand on the shoulders of giants. GPL v3.0 — fork it, modify it, improve it, share it. That's the deal.

---

## License

GPL v3.0, same as the original Ambika firmware.

Original developer: Emilie Gillet (emilie.o.gillet@gmail.com)
Carcosa firmware: Joseph Martin Giralt
