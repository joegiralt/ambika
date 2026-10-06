# FM engine artifacts — open work

Status as of 6 Oct 2026, on `engines-overhaul`. Written down here because this
repo has no issue tracker.

## The complaint

Held FM notes have audible inharmonic content underneath them. Described from
the hardware as a hiss, then more precisely as **a rattle, like a guitar string
too loose buzzing against the frets**. It is there on a bare sine operator with
no modulation, at every pitch, and it needs the VCF closed past the midpoint to
disappear — by which point the patch is unusably dull.

It is present on every Carcosa release so far. It is not the patch conversion:
a single sine operator with nothing routed anywhere shows it.

## Two causes, measured

Use `voicecard/test/analyze.sh` to reproduce any of this.

### 1. Half-rate reconstruction imaging — FIXED

FM and Karplus-Strong render at half the sample rate. `Voice::ExpandHalfRate`
filled the gaps with the midpoint of two neighbours, which leaves the image at
(19.6 kHz − f) only about 10 dB down. Every FM component between 5 and 9 kHz
got a loud inharmonic twin between 10 and 14 kHz.

Replaced with a four-point fit over the three previous samples and the current
one — causal, so no lookahead, no delay, no block-boundary special case.
Measured through `Voice::ProcessBlock`, old against new:

| note | midpoint | four-point | gain |
|------|----------|------------|------|
| 440 Hz | −54.2 dBc | −55.8 dBc | +1.6 dB |
| 880 Hz | −45.7 dBc | −55.8 dBc | +10.1 dB |
| 1761 Hz | −33.9 dBc | −52.1 dBc | +18.2 dB |

On hardware this was judged "slightly better" — real, but not the complaint.

### 2. Phase truncation — OPEN

`Fm4Op::Operator` indexes the waveform with a 10-bit phase. Truncating there
leaves **discrete, inharmonic spurs** at about −56 to −60 dBc, spread across the
spectrum and unrelated to the fundamental — a 110 Hz sine throws a spur at
5010 Hz. Inharmonic partials beating against a held note is exactly a rattle,
and it explains why it is inaudible on short notes and obvious on held ones.

This is the remaining artifact. The four-point fix above does not touch it.

## Candidate fix, not yet accepted

Phase dither, behind `FM_PHASE_DITHER` in `voicecard/fm4op.h`. A 16-bit Galois
LFSR dithers the truncation, trading the discrete spurs for broadband noise at
the same total power:

| tone | without dither | with dither |
|------|----------------|-------------|
| 110 Hz | −59.3 dBc | −76.5 dBc |
| 1758 Hz | −60.0 dBc | −76.9 dBc |
| 7034 Hz | −56.3 dBc | −77.4 dBc |

About 17–21 dB off the worst discrete spur, for 2.6 dB more broadband noise.

**Not enabled in the firmware build.** What is still unknown:

- Whether it actually sounds better. Rendered A/B did not settle it, because
  the renders lack the analog filter and did not sound enough like the synth.
- **CPU cost.** Four LFSR steps per sample on the voicecard, which is already
  the busiest part of the system. Flash looks like roughly 94 bytes against
  1180 free; cycles are the real question and have not been measured.

If dither is rejected, the other lever is real phase resolution: interpolating
the log-sine lookup, or a larger table. Roughly 512 bytes for about 6 dB, which
is a poor trade compared to dither — but it removes energy instead of spreading
it.

## Next step

Capture the synth's real analog output on a machine with a sound card, driving
it over MIDI, and analyse that instead of host renders. That closes the gap the
rendered A/B could not: the VCF, VCA and output stage are hardware and are not
modelled anywhere in these tools, so everything measured here is drier and more
exposed than what comes out of the synth.

Worth capturing, per build:

- a bare sine operator held at several pitches, filter wide open
- `T013 A14 DynoWurlie` (low ratios, was judged clean)
- `T078 C15 LatelyBass` and `T127 D32 Efem Toms` (bright, worst case)
- the same notes with the VCF at the position where the rattle disappears

## Tools

```sh
# spur and SNR of one sine operator through the real Fm4Op code
sh voicecard/test/analyze.sh noise
sh voicecard/test/analyze.sh noise -DFM_PHASE_DITHER

# render real factory patches to a WAV through Voice::ProcessBlock
sh voicecard/test/analyze.sh render out.wav PATCH/BANK/T/013.PAT 57 \
                                         PATCH/BANK/T/078.PAT 45

# the patch files come from
python3 make_patches.py /tmp/patches
```

Both build the real firmware sources. `voicecard/test/run.sh` still runs the
correctness tests and should stay green.

## Also unresolved

`Voice::ExpandHalfRate`'s gain at 7 kHz is only about 2 dB — near Nyquist for
the half-rate render, no reconstruction filter helps much. Bright voices with
real energy up there will always image. Lowering modulator levels reduces it at
the source; `MOD_TRIM` in `make_patches.py` is the lever, currently 6 steps
(4.5 dB) applied flat to every modulator. Weighting it by the operator's
frequency ratio would target the voices that actually alias and leave low-ratio
voices like DynoWurlie alone. Not attempted — it is a voicing decision.
