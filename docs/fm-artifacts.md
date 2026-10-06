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

Budget per block: 40 samples × 510 cycles = **20,400**, wall time including
the audio ISR. Verified on hardware: the bare sine at 20,150 was clean,
DynoWurlie at 22,500 lapped, so the simulator is within about 1 % of the
card.

Cuts made on 6 Oct, all output-identical (host renders byte for byte, or
the bench's sample-for-sample comparison against the C reference):

| step | FM bare sine | LatelyBass | DynoWurlie | classic sine |
|---|---|---|---|---|
| this morning | 27,500 | 29,500 | 29,800 | 23,600 |
| leaf C ISR (VCA lookup out of the ISR) | 25,000 | 27,000 | | |
| assembly ISR (`AUDIO_ISR`, 95 cycles/sample from 157) | 22,400 | 24,100 | | |
| block into the ring with `WriteBlock` | 21,850 | 23,800 | | 20,060 |
| sliding-window `ExpandHalfRate`, skip zero mod slots | 20,150 | 22,250 | 22,500 | |
| classic: skip the noise/fuzz loop at zero gain | | | | 17,700 |
| `Fm4Op::RenderAsm`: phases in registers, inlined operators | 17,800 | 19,900 | 20,300 | |

What the FM sine block is now: control rate 2,900, render 8,800, expansion
1,570, ring write 750, ISR 3,800, loop overhead the rest.

Hardware after the ISR/ring/expansion build (d4fe36d): bare sine clean at
A2, A3, A5 (0 bursts/s from 108–128); classic sine still lapping (183/s);
DynoWurlie 103/s. The assembly render build (5f5bb0a) is to be captured.

Still over or at the edge: DynoWurlie 20,300 in the simulator. Next levers,
all smaller: the control rate (7 envelopes + matrix, 2,900–3,350), the
expansion in assembly (~600), the once-per-block TXC wait in the ISR.

The assembly lives in `voicecard/audio_out.h` (ISR) and `voicecard/fm4op.h`
(operator, render). `bench/run.sh ... -DBENCH_FMTEST` and `-DBENCH_OPTEST`
prove the assembly against the C versions; run them after touching either.

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
