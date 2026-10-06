# FM engine artifacts — open work

Status as of 6 Oct 2026, on `engines-overhaul`. Written down here because this
repo has no issue tracker.

## The complaint

Held FM notes have audible inharmonic content underneath them: **a rattle,
like a guitar string too loose buzzing against the frets**. It is there on a
bare sine operator with no modulation, at every pitch, and it needs the VCF
closed past the midpoint to disappear.

## Cause, measured on the hardware — CPU overrun, not DSP

Captured 6 Oct 2026 from the synth's analog output (MOTU input 5) while
driving it over MIDI with `utils/ambika_midi.py`: a bare sine operator
(algorithm 1, op1 only, W1, ratio 1.00, level 127, filter open) held at
A2, A3 and A5, plus a classic-engine sine.

The sine is clean between events, but about **128 times a second a 1.5 ms
chunk of 3.3 ms-old audio is played again** — a fifth of the time is stale
buffer. It does not depend on pitch, on patch loads, or on parameter traffic.
Spectrally that is a comb at the lap rate around every component, which is
the rattle; the earlier "phase truncation" reading of host renders was wrong
about the cause (the −58 dBc spurs exist, but they are 50 dB under this).

| held note | glitches/s | each | stale audio |
|---|---|---|---|
| FM sine A2 / A3 / A5 | 108–129 | 0.9–1.6 ms | 9–20 % |
| classic sine A3 | 164 | 4 ms | 65 % |

Mechanism, in `voicecard/voicecard.cc`: the audio ISR calls
`audio_buffer.ImmediateRead()` every sample with no underrun check. When
`Voice::ProcessBlock` takes longer than 40 sample periods the ISR laps the
128-sample ring buffer and replays whatever is there; `writable()` then wraps
and the writer thinks it has room, so the two chase each other and every lap
is a burst of old samples. The pitch between bursts is right; the chunks make
the FFT peak land on a comb line, which is why the capture read 245 Hz for A3.

### Cycle budget, measured in simavr (`bench/`)

Budget per block: 40 samples × 510 cycles = **20,400**. The audio ISR cost
about **157 cycles per sample, 6,300 per block** (17 push/pop pairs because
of two non-inlined Strobe calls and a multiply, the VCA lookup once per
block). Made a leaf on 6 Oct (`AudioOutTick` in `voicecard/audio_out.h`: VCA
lookup moved to the main loop, chip select strobed directly): **~122 cycles
per sample, 4,900 per block**, 9 push/pop pairs, no calls. That leaves about
**15,500 cycles for `ProcessBlock`**. Nothing fits:

| patch | ProcessBlock | wall per block with ISR | over |
|---|---|---|---|
| FM bare sine | 19,000 | 27,500 (25,000 with the leaner ISR) | 33 % (23 %) |
| FM, four carriers | 19,350 | 27,600 | 35 % |
| classic sine | 16,560 | 23,600 | 16 % |
| T078 LatelyBass | 20,500 | 29,500 | 45 % |
| T013 DynoWurlie | — | 29,800 | 46 % |

Where the FM sine's 19,000 go: control rate (matrix, 7 envelopes) 3,450;
`Fm4Op::Render`, 20 half-rate samples × 4 operators, **11,970** (≈150 per
operator-sample, silent operators included); `ExpandHalfRate` four-point fit
2,440; writing the block 1,090. The classic sine spends 7,670 of its 16,560
in the noise/fuzz post-mix loop with both at zero.

So the FM engine needs to lose about 5,000 cycles per block (25 %), or the
ISR must get cheaper, or both. Levers, biggest first:

- ISR: done, 157 → ~122 cycles; what remains is the interrupt entry and
  exit, nine register saves and the ring buffer and SPI accesses. An
  assembly ISR might reach ~90. Not yet heard on hardware.
- `Fm4Op::Render`: 150 cycles per operator-sample. Skip operators whose
  attenuation is `kFmSilent` before the table lookups; the 32-bit phase
  accumulate and two PROGMEM lookups are the rest.
- `ExpandHalfRate`: the four-point fit costs 2,440; midpoint was cheaper.
- Classic: skip the noise/fuzz loop when both gains are zero (~6,000).
- A cheap guard in the ISR (`readable()` check, repeat the last sample) would
  stop the rattle turning into stale chunks, but with a steady overrun it just
  becomes a pitch drop; it is not a fix.

Not verified: the ISR cost on hardware (simavr's USART timing), and that
`Voice::ProcessBlock` in simavr matches the chip cycle for cycle; the lap rate
predicted from the bench (~33 % late) matches the captured 20 % stale audio
within the crude model, which is as close as this gets without a scope.

## Also present, lower priority

1. **Half-rate imaging** — fixed with the four-point reconstruction fit
   (−52 to −56 dBc). On hardware judged "slightly better".
2. **Phase truncation** in `Fm4Op::Operator` (10-bit waveform index): discrete
   spurs at −56 to −60 dBc. `FM_PHASE_DITHER` in `voicecard/fm4op.h` trades
   them for broadband noise; it costs cycles, so it waits on the budget above.
3. `MOD_TRIM` in `make_patches.py` (modulator levels −4.5 dB) is a voicing
   decision, untouched.

## Tools

```sh
# drive the synth and capture (MOTU input 5 = channel index 4)
python3 utils/ambika_midi.py load_sine; python3 utils/ambika_midi.py note_on 57 127
pw-record --target <node> --channels 20 --rate 48000 --format s32 x.wav
python3 utils/glitch_count.py x.wav 1.0,3.5,sine57

# cycle bench in simavr (docker image avr-bench: gcc-avr simavr gdb-avr)
sh bench/run.sh patch_sine 57                      # wall cycles with the ISR
sh bench/run.sh patch_sine 57 "-DBENCH_NOISR -DBENCH_PROFILE"   # ProcessBlock split
BUILD_ROOT=build_x/ sh bench/run.sh patch_lately 45   # parallel runs need their own build dir

# host-side spur / SNR analysis and patch renders (dry: no VCF, VCA, output stage)
sh voicecard/test/analyze.sh noise
sh voicecard/test/analyze.sh render out.wav PATCH/BANK/T/078.PAT 45
```

`bench/` links the real voicecard sources; `BENCH_PROFILE` enables the
`BENCH_MARK` hooks in `voicecard/voice.cc`, which are empty in the firmware
build. `voicecard/test/run.sh` stays green.
