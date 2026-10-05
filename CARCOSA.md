# Carcosa v3.02
## Custom firmware for the Ambika polysynth

Carcosa replaces the stock Ambika firmware with a focused set of synthesis engines, expanded envelope capabilities, and a streamlined interface.

Based on the original Ambika firmware by Emilie Gillet (Mutable Instruments).

This manual covers what Carcosa adds or changes. Everything inherited from the Ambika (the filter, the modulation matrix, parts and multi, the arpeggiator, the library and system settings) works as described in the [original Ambika manual](https://pichenettes.github.io/mutable-instruments-diy-archive/ambika/manual/).

### What's new in 3.0

- **12-bit audio:** FM, Karplus-Strong and West Coast now send the voicecard DAC all 12 bits instead of 8, removing the 8-bit hiss (about 24 dB less noise). The classic engine is still 8-bit, like the stock Ambika.
- **FM rebuilt to match the TX81Z:** the real TX81Z algorithms, waveforms and log-sine/volume ROMs, bit-exact against a reference; carrier levels now apply. All 128 TX81Z factory voices are in Bank T.
- **Karplus-Strong rebuilt:** in tune, down to ~102 Hz, 3x sustain, a real body resonance, metallic excitation color, and a clean chorus.
- **West Coast rebuilt:** in tune at low notes, 16-bit folding, bias and symmetry now distinct.
- FM, KS and West Coast render at half rate (19.6 kHz) with interpolation, to fit the voicecard's CPU.

**Breaking changes:** FM patches made on 2.x may sound different or be silent: carrier levels now matter (old default patches had op1 at level 20, which is now ~80 dB down), and algorithms 3, 4, 5 and 7 now use the real TX81Z routings. KS body/chorus/color and WC bias/symmetry changed meaning. Flash the controller and all voicecards together; settings reset to defaults on first boot.

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

FM, Karplus-Strong and West Coast render at half the sample rate (19.6 kHz) and are interpolated back up, so each fits in the voicecard's CPU time. They send the voicecard DAC full 12-bit samples; the classic engine stays 8-bit (see Audio path).

#### 4-Op FM (fm4op)

TX81Z-style frequency modulation with 4 operators.

**Page 1:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | algo | 1-8 | Algorithm (operator routing topology) |
| 2 | fbk | 0-127 | Feedback on operator 4 |
| 3 | wav1 | W1-W8 | Operator 1 waveform (8 TX81Z waveforms) |
| 4 | wav2 | W1-W8 | Operator 2 waveform |
| 5 | rat1 | 0.50-25.95 | Operator 1 frequency ratio (the 64 TX81Z ratios) |
| 6 | fin1 | -64/+63 | Operator 1 fine detune (1/256 of its frequency per step) |
| 7 | rat2 | 0.50-25.95 | Operator 2 frequency ratio (the 64 TX81Z ratios) |
| 8 | fin2 | -64/+63 | Operator 2 fine detune (1/256 of its frequency per step) |

**Page 2:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | wav3 | W1-W8 | Operator 3 waveform |
| 2 | wav4 | W1-W8 | Operator 4 waveform |
| 3 | lvl1 | 0-127 | Operator 1 output level (0.75 dB per step, 127 = full) |
| 4 | lvl2 | 0-127 | Operator 2 output level |
| 5 | rat3 | 0.50-25.95 | Operator 3 frequency ratio (the 64 TX81Z ratios) |
| 6 | fin3 | -64/+63 | Operator 3 fine detune (1/256 of its frequency per step) |
| 7 | lvl3 | 0-127 | Operator 3 output level |
| 8 | lvl4 | 0-127 | Operator 4 output level |

Operator 4's ratio and fine detune aren't on these pages; set them by MIDI CC (24 and 25) or through the mod matrix (`rat4`).

**The 8 algorithms:**

The same 8 algorithms as the TX81Z (operator 4 has the feedback):

```
1: 4->3->2->1          Full serial chain                   carrier: 1
2: (4+3)->2->1         Two modulators into 2               carrier: 1
3: (4 + (3->2))->1     4 and the 3->2 stack both into 1    carrier: 1
4: ((4->3) + 2)->1     The 4->3 stack and 2 both into 1    carrier: 1
5: (4->3) + (2->1)     Two stacks                          carriers: 1, 3
6: 4->(1+2+3)          One modulator, three carriers       carriers: 1, 2, 3
7: (4->3) + 2 + 1      One stack, two free carriers        carriers: 1, 2, 3
8: 1+2+3+4             All carriers (additive)             carriers: all
```

Carrier levels set volume; modulator levels set brightness. (Before 3.0, algorithms 3, 4, 5 and 7 used different routings.)

**Operator waveforms:** the 8 TX81Z waves. W1 sine, W2 sin² (peakier sine), W3/W4 = first half of W1/W2 then silence, W5/W6 = W1/W2 at double speed in the first half then silence, W7/W8 = two positive W1/W2 humps in the first half then silence.

**Levels and modulation depth:** Operator levels work like the TX81Z: 0.75 dB per step, 127 = full, 0 = off (64 is about -47 dB). Carrier levels set each carrier's volume; modulator levels set modulation depth, up to ±4 cycles of phase at 127 like the TX81Z. Per-operator envelopes (env 4-7) add to the level's attenuation. When several carriers sum past full scale they clip (the TX81Z has more headroom here; the voicecard's 12-bit DAC doesn't), so lower carrier levels in algorithms 5-8.

**Feedback:** operator 4 feeds back on itself. The knob is exponential like the TX81Z's FB 1-7: every 16 steps doubles the amount, and 112 equals FB 7.

**TX81Z accuracy:** the operator path (algorithms, waveforms, log-sine and volume ROMs, modulation depth, feedback) matches the TX81Z sample for sample. FM renders at half the sample rate (19.6 kHz) so four operators fit in the voicecard's CPU time. Differences from a real TX81Z: ADSR envelopes instead of its rate-based ones (timed from the chip's envelope rates, with rate scaling fixed at middle C), no fixed-frequency operators, and approximate keyboard level scaling and per-operator velocity. The converted voices do those two with mod-matrix routes from note and velocity to operator levels, so their stored levels read lower than the TX81Z's OUT values. The converter also turns every modulator down 4.5 dB (`MOD_TRIM` in `make_patches.py`, 0 for exact TX81Z levels), since the voices sound harsh without the TX81Z's own output stage.

**Factory voices:** Bank T holds all 128 TX81Z factory voices, converted from the TX81Z ROM by `make_patches.py` (see Factory Patches). Converted voices use a hidden transpose byte (the TX81Z's TRPS), which the FM page doesn't show yet. Voices that rely on fixed-frequency operators (mostly drums and effects, such as D25 Bass Drum and the snares) track pitch here, so they sound off; voices that lean on the TX81Z's rate-based envelopes or LFO tremolo can also differ, since those are approximated.

The mod matrix shows FM-specific destination names when in FM mode: `algo`, `lvl1`-`lvl4`, `rat3`, `rat4`, plus the shared pitch destinations. Route envelopes/LFOs to operator levels for dynamic FM timbres. Envelopes 4-7 are hard-wired to operators 1-4 as per-operator envelopes.

#### Karplus-Strong (pluck)

Physical model of a plucked string. A noise burst excites a tuned delay line with filtered feedback. The string is tuned with a fractional (allpass) delay, so it's in tune within 5 cents from A2 to A5 (within 20 above), and reaches down to about 102 Hz (G#2). Below that, notes play at the lowest pitch the string can reach.

**Page 1:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | damp | 0-127 | Damping: bright ring (0) to dark, muted thud (127) |
| 2 | colr | 0-127 | Excitation brightness; above 64 the string turns metallic (overtones stretched sharp, more on higher notes) |
| 3 | dcay | 0-127 | Decay: 0 = longest ring, higher = shorter |
| 4 | body | 0-127 | Soundbox: boom around 110-220 Hz and softer highs, like a string on a hollow box (never moves the pitch) |
| 5 | rang | -24/+24 | Pitch range (semitones) |
| 6 | tune | -64/+63 | Fine tuning |
| 7 | pos | 0-127 | Pluck position (changes harmonic content) |
| 8 | exc | nois/clic/brit/dark | Excitation type |

**Page 2:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | rate | 0-127 | Chorus LFO speed, about 0.3-10 Hz |
| 2 | dpth | 0-127 | Chorus depth: how far the two extra read heads glide (subtle doubling to deep detune) |
| 3 | sprd | 0-127 | Chorus spread: LFO phase offset between the two heads (0 = together, 127 = opposite) |
| 4 | wmix | 0-127 | Chorus wet/dry mix |
| 5 | stif | 0-127 | Blends in an earlier point on the string: a comb-filter coloration (for real stiffness, use `colr` above 64) |
| 6 | feed | 0-127 | Sustain: reduces damping loss for a longer ring (never self-oscillates) |

**Sustain:** strings ring about three times longer than a plain Karplus-Strong loop, and `damp`, `dcay` and `feed` shape it from there.

**Metallic color:** above 64, `colr` adds dispersion to the string: its overtones stretch progressively sharp, like a stiff steel string, more on higher notes (a shimmer on low notes, bell-like at the top). The fundamental stays in tune.

**Chorus (ensemble):** two extra read heads glide through the string's recent history, swept by a slow LFO, so the copies detune gently against the string. Turn up `wmix` to hear it.

The mod matrix shows KS-specific destination names: `damp`, `colr`, `pitch`, `body`, `ensdep`, `stif`, `ensmix`, `feedbk`. Everything with a destination name is modulatable.

#### West Coast (wcoast)

Buchla-style wavefolder with built-in FM and sub-oscillator. The pages have no range or tune knobs; transpose with MIDI CC 14 (range) and 15 (fine tune), as on the classic osc 1.

**Page 1:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | wave | sin/tri | Base waveform |
| 2 | fold | 0-127 | Fold depth (harmonics increase with depth) |
| 3 | sym | 0-127 | Symmetry: gives the wave's two halves different gain, tilting it to one side (64 = symmetric) |
| 4 | bias | 0-127 | Bias: an offset before the folder's gain, sliding the fold pattern (64 = centered; strongest at high fold) |
| 5 | fmdp | 0-127 | FM modulation depth |
| 6 | fmrt | -24/+24 | FM modulator ratio |
| 7 | colr | 0-127 | Post-fold brightness |
| 8 | drve | 0-127 | Pre-fold drive |

**Page 2:**

| Knob | Label | Range | Description |
|------|-------|-------|-------------|
| 1 | flds | 0-127 | Fold stages: currently has no effect |
| 2 | gain | 0-127 | Input gain before folding |
| 3 | env | 0-127 | Envelope-to-fold depth (timbral VCA) |
| 4 | sub | 0-127 | Sub-oscillator level (one octave down) |
| 5 | sync | 0-127 | Self-sync amount (metallic tones) |

**Wavefolder** uses a quadratic gain curve (1x at fold=0, 33x at fold=127) creating up to 16 folds, similar to a Buchla 259's harmonic density. It works at 16-bit resolution. The **color** filter is a 2-pole low-pass (12 dB/oct) that sweeps from dark fundamental-only to bright full harmonics (at 124 and up it's bypassed). Low notes are in tune down to E1.

Route an envelope to `fold` (fold depth) for the classic west coast behavior where timbre and amplitude are linked. The `env` parameter on page 2 does this directly — the signature Buchla pluck sound. The mod matrix shows WC-specific destination names: `fold`, `sym`, `pitch`, `bias`, `colr`, `envf`, `gain`, `sub`, `sync`, all modulatable.

#### Waveshaping (wshape)

Drives a sine wave through a nonlinear waveshaper. Works as a normal oscillator (can be used on osc1 or osc2 alongside other waveforms).

| Knob | Description |
|------|-------------|
| prm | Drive amount (0 = clean sine, 127 = heavily shaped) |
| rng | Pitch range |
| tun | Fine tuning |

### Which envelopes each engine uses

| Engine | Envelope 1 | Envelope 2 | Envelopes 3-7 |
|--------|------------|------------|---------------|
| Classic | free | VCA (in the factory patches) | free |
| FM | free | VCA | 3 free; 4-7 are operators 1-4's envelopes |
| Karplus-Strong | free | VCA | free |
| West Coast | fold envelope (`env` on page 2) | VCA | free |

Envelope 2 drives the VCA only through the mod matrix, as in the stock init patch, so any envelope can take that job.

---

## Factory Patches

| Bank | Contents |
|------|----------|
| C | FM presets C001-C006: TX81Z LatelyBass, ElectroPno, Full Brass, Xylophone, SynString and 16 8 4 2 F |
| I | 24 understated, IDM-ish patches (I000-I023): 8 FM, 8 Karplus-Strong, 8 West Coast |
| M | 24 patches with bold movement (M000-M023): 8 FM, 8 Karplus-Strong, 8 West Coast |
| T | The 128 TX81Z factory voices, A01-D32 as T000-T127 (slot = bank × 32 + number − 1, with A=0 to D=3, so C15 LatelyBass is T078) |

Banks I and M move the engines with the voicecard's own modulators: the voice LFO (LFO 4), looping envelopes, velocity, note and random. They leave the part LFOs 1-3 free for you. Their FM patches start from TX81Z voices.

All four banks come from `make_patches.py`. Install them from the release's `PATCHES.zip` or with `flash_sd.sh` (see Firmware Update).

---

## Navigation

### Classic mode (osc/mixer pages)

- **Encoder**: Scroll through parameters, click to edit. Scrolling past the last parameter moves to the mixer page (and back from the first).
- **S1**: Mode select (see below)

### Entering special modes

1. While on the osc or mixer page, **press S1** — LED 1 starts blinking (mode select active)
2. **Turn encoder** to cycle through: classic, fm4op, pluck, wcoast
3. **Press S1 again** to confirm and exit mode select

Mode select also works from inside any special mode page. Pressing any other button exits mode select automatically.

### Engine isolation

Each engine has its own state slot. When you switch engines, your current settings are saved and the target engine's settings are restored. Tweak an FM patch, switch to classic, switch back — your FM tweaks are right where you left them. No parameter bleed between engines.

### Inside special mode pages

- **Encoder**: Scroll through parameters, click to edit. Scrolling past the 8th parameter moves to page 2 (and back from its first).
- **Knobs 1-8**: Direct control of the 8 parameters shown on screen
- **S1**: Mode select (see above)

The OSC button always opens the page for the active part's engine, so the classic oscillator/mixer pages can never edit an FM, KS or West Coast patch by mistake.

### Other pages

All other pages (filter, envelope, modulation, part, arpeggiator, multi, performance, system) work like the original Ambika; see the [original manual](https://pichenettes.github.io/mutable-instruments-diy-archive/ambika/manual/).

---

## Envelopes

Carcosa has **7 envelopes**. Envelopes 1-3 have paired LFOs and curve selection (exponential, linear, looping). Envelopes 4-7 are simple ADSR — lightweight extra modulation sources for per-operator FM control, layered modulation, or any creative routing.

The ENV/LFO page selector (knob 1) cycles through all 7 envelopes. When env 1-3 is selected, the top row shows LFO rate, shape, and envelope curve. When env 4-7 is selected, the top row is blank — just ADSR on the bottom row.

All 7 envelopes are available as modulation sources in the mod matrix (env1 through env7). LFOs 1-3 are part LFOs, shared by the part's voices as on the stock Ambika; LFO 4 is the voice LFO, one per voice.

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

Each note-on generates a fresh random pitch offset, up to ±0.5 semitone at 127 (about ±12 cents at 30). Each note also gets a random offset on the attack, decay and release of envelopes 1-3, up to ±32 steps at 127, so at high slop each voice in a chord has noticeably different envelope timing.

What that changes depends on the engine:

- **Classic and Karplus-Strong:** pitch, and the timing of whatever envelopes 1-3 drive (the VCA on envelope 2 in the factory patches).
- **FM:** pitch and the VCA timing. The operator envelopes (4-7) don't vary, so slop doesn't change an FM voice's timbre.
- **West Coast:** pitch, the VCA, and envelope 1, the fold envelope, so the brightness of each pluck varies too.

(Before 3.0 the pitch offset reached a full semitone, and the envelope offset was always tiny because of a bug.)

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

1. Download the 7 .BIN files from the [latest release](https://github.com/joegiralt/ambika/releases) and copy them to the root of the SD card. For the factory patches, also unzip the release's `PATCHES.zip` to the root of the card (it replaces banks C, I, M and T, so back up any patches you saved there). If you build from source, `./flash_sd.sh /mnt/sdcard` copies the firmware and also writes the factory patches (banks C, I, M and T).
2. Insert SD card into Ambika
3. **Hold S8 during power-on** to flash the controller
4. After reboot, go to OS info page, select each voicecard port (1-6), press S4 to flash

Flash the controller first, then all 6 voicecards.

### EEPROM auto-reset

When upgrading from an older firmware version, Carcosa automatically detects stale EEPROM data and resets to factory defaults on first boot. This ensures the correct init patch, voice allocation, and engine settings are loaded. No manual factory reset needed.

After the reset, **part 1 has all 6 voices and listens on MIDI channel 1**; parts 2-6 have no voices. If nothing plays after upgrading, set part 1's MIDI channel to match your controller.

### Recovery

If the firmware crashes on boot, **hold S8 during power-on** to force the bootloader to read from SD card. This always works regardless of firmware state.

To revert to stock Ambika firmware, place the original `AMBIKA.BIN` and `VOICE*.BIN` files on the SD card and flash.

---

## Technical Notes

### Memory Usage

| | Voicecard (ATmega328p) | Controller (ATmega644p) |
|---|---|---|
| Flash | 31,006 / 32,256 (96.1%) | 61,164 / 61,440 (99.6%) |
| RAM | 1,765 / 2,048 (86%) | 3,758 / 4,096 (92%) |

Flash limits are what the bootloader leaves free. Sizes are for the v3.0 release build (avr-gcc 5.4).

### Audio path

The voicecard's DAC takes 12 bits; the original design used only 8 of them. FM, KS and West Coast compute at 14-16 bits and send all 12. The classic engine is unchanged from the stock Ambika: it renders 8-bit samples at the full 39.2 kHz and shifts them up, so it keeps the 8-bit noise floor.

FM, KS and West Coast render at 19.6 kHz and are interpolated to the voicecard's 39.2 kHz. That trade has two side effects:

- **Aliasing:** anything the engine generates above 9.8 kHz (half of 19.6 kHz) folds back into the audible band. A 12 kHz FM sideband comes out near 7.6 kHz, and no filter afterwards can tell it apart from a real 7.6 kHz partial. Bright FM settings (high modulator levels, high ratios, heavy feedback) on high notes alias the most; lowering modulator levels reduces it at the source.
- **Images:** the interpolation leaves weaker copies of the spectrum above 9.8 kHz. The analog filter does remove these, but only when its cutoff is below them. The factory patches leave the filter wide open, so closing it a little also cleans up the top end.

The alternative, full-rate synthesis, only fits the CPU at 8 bits, and 8-bit output hisses at every rate. Aliases go through the analog filter like the rest of the sound, so opening the cutoff exposes more of the digital edge and closing it smooths it. Every engine's per-block cost was measured in an AVR simulator; with every Karplus-Strong feature maxed at once (metallic color, body, chorus and stiffness), KS is the heaviest load on the voicecard.

### Known Issues

- On hardware, Carcosa 3.0 has only been run on a Xena (an Ambika clone). The engines were checked in an AVR simulator and in host tests.
- Karplus-Strong with every feature maxed at once (metallic color, body, chorus and stiffness) is the heaviest load on the voicecard. If you hear glitches, back one off.
- The very top of the Karplus-Strong range can be up to 20 cents off.
- The West Coast `flds` (fold stages) knob has no effect.
- MIDI CCs follow the classic parameter ranges (see MIDI CC Reference).
- Converted TX81Z voices with fixed-frequency operators track pitch, so drums and effects sound off.

### Changes from Stock Ambika

**Removed:** CZ synthesis (9 variants), old 2-op FM, 8-bit land, dirty PWM, vowel synthesis, wavetable oscillators (16 + wavequence), step sequencer.

**Added:** TX81Z-accurate 4-op FM with the 128 TX81Z factory voices, Karplus-Strong, west coast wavefolder, waveshaping, looping envelopes, analog slop, smart randomizer, dedicated UI pages for special modes, per-engine mod destination names, per-engine state isolation, EEPROM auto-reset, extended octave range (-4/+4), 12-bit audio output.

**Bug fixes:** filter page scroll reset (scrolling past the last control called a NULL handler and restarted the firmware), MIDI channel display (showed garbage for channels 1-16), engine switch parameter bleed, voicecard frame drops on engine switch, boot page routing, note stack uninitialized variables, transient generator off-by-one, SPI bulk write bounds check, page group initialization size. For the 3.0 fixes, see the [v3.0 release notes](https://github.com/joegiralt/ambika/releases/tag/v3.0).

### Patch Compatibility

Existing Ambika patches using saw, square, triangle, sine, pad, or noise will work unchanged. Patches using removed waveforms (CZ, wavetable, etc.) will have their oscillator shape reset to `none` on first load.

**Carcosa 2.x patches:** classic patches are unchanged. FM patches may sound different or be silent: carrier levels now apply (the old default had operator 1 at level 20, now about 80 dB down), algorithms 3, 4, 5 and 7 changed, and modulation is deeper. Raise the carrier levels (lvl1, and lvl3 or others depending on the algorithm) to bring them back. KS body, chorus and color, and West Coast bias and symmetry, changed meaning, so those patches sound different too.

---

## MIDI CC Reference

All synthesis modes respond to MIDI CCs. The special modes reuse existing patch byte offsets, so the same CC numbers control different parameters depending on the active mode.

**Limitation:** a CC is scaled to the range of the classic parameter that owns that byte, not the special engine's range. In FM mode, operator levels by CC reach only 63 (about -48 dB) and op 3/4 fine detune only a few steps; KS body and stiffness reach half their range. Use the engine pages or the mod matrix for the full range.

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
| 14 | Range |
| 15 | Fine tune |
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
| 12 | Sustain |

### West Coast Mode

| CC | WC parameter |
|----|-------------|
| 14 | Range |
| 15 | Fine tune |
| 18 | Base waveform |
| 17 | Fold depth |
| 19 | Symmetry |
| 22 | Bias |
| 20 | FM depth |
| 21 | FM ratio |
| 24 | Color |
| 23 | Drive |
| 25 | Fold stages (no effect) |
| 26 | Input gain |
| 27 | Env to fold |
| 12 | Sub level |
| 13 | Sync |

---

## FAQ

**Can I go back to the stock Ambika firmware?**
Yes. Put the original AMBIKA.BIN and VOICE*.BIN files on the SD card and hold S8 during power-on. The bootloader is never modified — you can always reflash.

**Will my existing patches work?**
Patches using saw, square, triangle, sine, pad, or noise will sound the same. Patches using removed waveforms (CZ, wavetables, vowel, 8-bit, etc.) will have their oscillator reset to `none` on first load. Filter settings, envelopes, and mod matrix are unchanged. FM, KS and West Coast patches from Carcosa 2.x may sound different; see Patch Compatibility.

**My FM patch from 2.x is silent. Why?**
Carrier levels now apply, as on the TX81Z. Old patches often have the carrier (operator 1 in algorithms 1-4) at a low level that 2.x ignored. Raise it towards 127.

**Do I need to flash all 6 voicecards?**
Yes. The voicecard firmware contains the new synthesis engines. If you only flash the controller, the voicecards won't understand the new waveform types and you'll get silence or wrong sounds.

**My Ambika shows garbage on the screen after flashing. Is it bricked?**
No. Hold S8 during power-on to force the bootloader to reflash from SD card. This always works regardless of what firmware is running.

**Does this work with all voicecard types (4P, SVF, SMR)?**
It should. Carcosa only changes the digital code, and the analog filter on each voicecard is untouched. So far it has only been run on a Xena.

**Can I use the special modes (FM, pluck, wcoast) with the analog filter?**
Yes. The filter is in the analog signal path after the DAC, and every synthesis mode runs through it. The factory patches leave it wide open; closing it is the quickest way to tame a bright FM voice.

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
The TX81Z's W1-W8: W1 sine, W2 sin² (peakier), W3/W4 the first half of W1/W2 then silence, W5/W6 W1/W2 at double speed in the first half then silence, W7/W8 two positive W1/W2 humps in the first half then silence. They're built from the TX81Z's own log-sine table.

**Why does the Karplus-Strong string stop getting lower below G#2?**
The string is a 192-sample delay line in the voicecard's 2 KB of RAM; at the engine's 19.6 kHz that reaches about 102 Hz. Lower notes play at that pitch.

**Is this open source?**
Yes, and it always will be. Carcosa exists because Emilie Gillet open-sourced the Ambika firmware. That firmware exists because of open-source AVR toolchains, open hardware designs, and a community that shares knowledge freely. We stand on the shoulders of giants. GPL v3.0 — fork it, modify it, improve it, share it. That's the deal.

---

## License

GPL v3.0, same as the original Ambika firmware.

Original developer: Emilie Gillet (emilie.o.gillet@gmail.com)
Carcosa firmware: Joseph Martin Giralt
