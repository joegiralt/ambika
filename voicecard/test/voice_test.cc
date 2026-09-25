// Host-side tests for the voicecard engines. Run: sh voicecard/test/run.sh

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "voicecard/voice.h"
#include "voicecard/audio_out.h"
#include "voicecard/fm4op.h"
#include "voicecard/resources.h"

namespace ambika { CaptureBuffer audio_buffer; }
using namespace ambika;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
    printf("\n"); ++failures; } } while (0)

static Patch* patch() {
  return reinterpret_cast<Patch*>(voice.mutable_patch_data());
}

// FM patch: algorithm 1, all sines at ratio 1.00, only op1 audible,
// flat per-op envelopes, no modulation routed anywhere.
static void SetupFm() {
  voice.Init();
  Patch* p = patch();
  p->padding[2] = ENGINE_FM4OP;
  p->padding[0] = 0;  // feedback
  p->osc[0].parameter = FM_ALG_1;
  p->osc[1].shape = 0;      // op1|op2 sine
  p->osc[1].parameter = 0;  // op3|op4 sine
  p->osc[0].range = p->osc[1].range = p->mix_balance = p->mix_parameter = 4;
  p->osc[0].detune = p->osc[1].detune = 0;
  p->mix_op = p->mix_sub_osc_shape = 0;
  p->mix_sub_osc = 127;  // op1 level
  p->mix_noise = p->mix_fuzz = p->mix_crush = 0;
  for (uint8_t i = 0; i < kNumModulations; ++i) {
    p->modulation[i].source = MOD_SRC_CONSTANT_256;
    p->modulation[i].destination = MOD_DST_FILTER_RESONANCE;
    p->modulation[i].amount = 0;
  }
  for (uint8_t i = 0; i < kNumExtraEnvelopes; ++i) {
    p->extra_env_lfo[i].attack = 0;
    p->extra_env_lfo[i].sustain = 127;
  }
}

static const int kMaxSamples = 40000;
static uint8_t out[kMaxSamples];     // 8-bit view (12-bit sample >> 4)
static uint16_t out16[kMaxSamples];  // full 12-bit samples

// Plays a note (MIDI number) and captures num_blocks blocks after a warmup.
static int Play(uint8_t midi_note, int num_blocks) {
  voice.ResetEngines();
  voice.Trigger((midi_note + 12) << 7, 127, 0);
  for (int i = 0; i < 20; ++i) {
    audio_buffer.size = 0;
    voice.ProcessBlock();
  }
  int n = 0;
  for (int b = 0; b < num_blocks; ++b) {
    audio_buffer.size = 0;
    voice.ProcessBlock();
    for (int i = 0; i < audio_buffer.size; ++i) {
      out16[n + i] = audio_buffer.data[i];
      out[n + i] = audio_buffer.data[i] >> 4;
    }
    n += audio_buffer.size;
  }
  return n;
}

static int PeakToPeak(int n) {
  int lo = 255, hi = 0;
  for (int i = 0; i < n; ++i) {
    if (out[i] < lo) lo = out[i];
    if (out[i] > hi) hi = out[i];
  }
  return hi - lo;
}

// Measured frequency in cycles per sample, from interpolated upward
// crossings of the center line.
static double MeasureFrequency(int n) {
  double first = -1, last = -1;
  int crossings = 0;
  for (int i = 1; i < n; ++i) {
    double a = out[i - 1] - 127.5, b = out[i] - 127.5;
    if (a < 0 && b >= 0) {
      double t = i - 1 + a / (a - b);
      if (first < 0) first = t;
      last = t;
      ++crossings;
    }
  }
  return crossings > 1 ? (crossings - 1) / (last - first) : 0;
}

// The exact (unquantized) frequency the pitch table asks for.
static double ExpectedFrequency(uint8_t midi_note) {
  int16_t ref = ((midi_note + 12) << 7) - kPitchTableStart;
  double scale = 1.0;
  while (ref < 0) { ref += kOctave; scale *= 0.5; }
  return lut_res_oscillator_increments[ref >> 1] * scale / 65536.0;
}

// #0: the sample-rate reducer belongs to the classic mixer. Special engines
// reuse mix_crush for their own parameters (FM: op4 level).
static void TestSpecialEnginesDoNotCrush() {
  SetupFm();
  patch()->mix_crush = 100;
  Play(57, 1);
  CHECK(voice.crush() == 1, "FM op4 level 100 sets crush to %d", voice.crush());
}

// #1: routing a source to an FM destination must change the sound.
static void TestModMatrixReachesFmLevels() {
  SetupFm();
  int n = Play(57, 10);
  uint8_t dry[kMaxSamples];
  memcpy(dry, out, n);

  SetupFm();
  patch()->modulation[0].source = MOD_SRC_CONSTANT_256;
  patch()->modulation[0].destination = MOD_DST_MIX_NOISE;  // "lvl 2"
  patch()->modulation[0].amount = 63;
  Play(57, 10);
  CHECK(memcmp(dry, out, n) != 0, "mod matrix -> lvl 2 changed nothing");
}

// #2: carrier levels scale carriers.
static void TestCarrierLevelsAreApplied() {
  for (uint8_t alg = 0; alg < FM_ALG_LAST; ++alg) {
    SetupFm();
    patch()->osc[0].parameter = alg;
    patch()->mix_sub_osc = 0;  // every level 0
    int n = Play(57, 10);
    CHECK(PeakToPeak(n) <= 2, "algorithm %d, all levels 0: peak-to-peak %d",
          alg + 1, PeakToPeak(n));
  }
}

// Attenuation is log2 in 4.8 fixed point: 256 = 6 dB = half amplitude.
static void TestAttenuationIsLogarithmic() {
  int full = Fm4Op::Operator(0, 256, 0);  // sine peak
  int half = Fm4Op::Operator(0, 256, 256);
  CHECK(abs(full - 2 * half) <= 2, "6 dB: %d vs %d", full, half);
  CHECK(Fm4Op::Operator(0, 256, 13 << 8) == 0, "13 octaves down not silent");
}

// Feedback: 0 is off, knob 112 equals OPZ FB 7 (sum >> 3, i.e. 8192/65536),
// and every 16 steps doubles, like each OPZ FB step.
static void TestFeedbackCurve() {
  CHECK(Fm4Op::FeedbackGain(0) == 0, "knob 0 not off");
  CHECK(Fm4Op::FeedbackGain(112) == 8192, "knob 112 = %u, want 8192",
        Fm4Op::FeedbackGain(112));
  for (uint8_t k = 1; k + 16 <= 127; ++k) {
    int a = Fm4Op::FeedbackGain(k), b = Fm4Op::FeedbackGain(k + 16);
    CHECK(abs(b - 2 * a) <= 1, "knob %d -> %d not double of %d", k + 16, b, a);
  }
}

// Reference: the OPZ (TX81Z) operator and algorithm path, transcribed from
// ymfm (github.com/aaronsgiles/ymfm, BSD-3-Clause): ymfm_fm.ipp
// abs_sin_attenuation / attenuation_to_volume / output_4op, ymfm_opz.cpp
// waveforms. Chip operators O1..O4 are panel OP4..OP1.
namespace opz {
static uint32_t SinAtt(uint32_t i) {
  if (i & 0x100) i = ~i;
  i &= 0xFF;
  return lround(-log2(sin((i + 0.5) * M_PI / 512)) * 256);  // == die ROM
}
static int32_t Volume(uint32_t att) {  // ymfm attenuation_to_volume
  static const uint16_t rom[256] = {
#include "opz_power_rom.inc"
  };
  return (((rom[att & 0xFF]) | 0x400) << 2) >> (att >> 8);
}
static uint32_t Wave(uint8_t w, uint32_t i) {  // 15-bit att | sign << 15
  const uint32_t zero = SinAtt(0);
  if (w >= 2 && (i & 0x200)) return zero;
  if (w >= 4) i = (i * 2) & (w >= 6 ? 0x1FF : 0x3FF);
  uint32_t a = SinAtt(i);
  if (w & 1) a = std::min<uint32_t>(2 * a, zero);
  return a | ((i >> 9 & 1) << 15);
}
static int32_t Op(uint8_t w, uint32_t phase, uint32_t env_att) {
  uint32_t s = Wave(w, phase & 0x3FF);
  int32_t v = Volume((s & 0x7FFF) + env_att);
  return (s >> 15) ? -v : v;
}
#define ALGORITHM(op2in, op3in, op4in, op1out, op2out, op3out) \
  ((op2in) | ((op3in) << 1) | ((op4in) << 4) | ((op1out) << 7) | \
   ((op2out) << 8) | ((op3out) << 9))
static const uint16_t kAlgorithms[8] = {
  ALGORITHM(1,2,3, 0,0,0), ALGORITHM(0,5,3, 0,0,0), ALGORITHM(0,2,6, 0,0,0),
  ALGORITHM(1,0,7, 0,0,0), ALGORITHM(1,0,3, 0,1,0), ALGORITHM(1,1,1, 0,1,1),
  ALGORITHM(1,0,0, 0,1,1), ALGORITHM(0,0,0, 1,1,1),
};
#undef ALGORITHM
// One sample. phase/w/att are in panel order [OP1..OP4]; fb_shift 0 = off.
static int32_t Sample(uint8_t alg, const uint16_t* phase, const uint8_t* w,
                      const uint16_t* att, uint8_t fb, int32_t* fbs) {
  uint32_t ops = kAlgorithms[alg];
  int32_t opmod = fb ? (fbs[0] + fbs[1]) >> (10 - fb) : 0;
  int32_t out[8];
  out[0] = 0;
  out[1] = Op(w[3], phase[3] + opmod, att[3]);
  fbs[0] = fbs[1];
  fbs[1] = out[1];
  out[2] = Op(w[2], phase[2] + (out[ops & 1] >> 1), att[2]);
  out[5] = out[1] + out[2];
  out[3] = Op(w[1], phase[1] + (out[(ops >> 1) & 7] >> 1), att[1]);
  out[6] = out[1] + out[3];
  out[7] = out[2] + out[3];
  int32_t r = Op(w[0], phase[0] + (out[(ops >> 4) & 7] >> 1), att[0]);
  if (ops & 0x080) r += out[1];
  if (ops & 0x100) r += out[2];
  if (ops & 0x200) r += out[3];
  return r;
}
}  // namespace opz

// #5: every algorithm, waveform and feedback setting matches the TX81Z path
// sample for sample (given the same phases and attenuations).
static void TestMatchesTx81zReference() {
  uint32_t seed = 1;
  for (uint8_t alg = 0; alg < 8; ++alg) {
    for (uint8_t fb = 0; fb <= 7; ++fb) {
      uint8_t w[4];
      uint16_t att[4];
      uint32_t inc[4], ph[4] = { 0, 0, 0, 0 };
      for (int i = 0; i < 4; ++i) {
        seed = seed * 1103515245 + 12345;
        w[i] = (seed >> 16) & 7;
        att[i] = ((seed >> 8) & 0xFF) * 4;  // 4.6 envelope << 2, like ymfm
        inc[i] = 20000 + ((seed >> 4) & 0xFFFFF);
      }
      Fm4Op fm;
      fm.Init();
      for (int i = 0; i < 4; ++i) fm.mutable_op(i)->phase_increment = inc[i];
      int32_t fbs[2] = { 0, 0 };
      int mismatches = 0;
      for (int n = 0; n < 2000; ++n) {
        uint16_t p[4];
        for (int i = 0; i < 4; ++i) { ph[i] += inc[i]; p[i] = ph[i] >> 16; }
        int32_t want = opz::Sample(alg, p, w, att, fb, fbs);
        int32_t got = fm.Sample(alg, w, att, Fm4Op::FeedbackGain(fb * 16));
        if (want != got) ++mismatches;
      }
      CHECK(mismatches == 0, "algorithm %d, FB %d: %d of 2000 samples differ",
            alg + 1, fb, mismatches);
    }
  }
}

// Half-rate FM must not leave strong images of the spectrum above 9.8 kHz
// (mirrored at fs/2 - f): a pure sine's image stays 30 dB down.
static double Goertzel(int n, double cycles_per_sample) {
  double c = 2 * cos(2 * M_PI * cycles_per_sample), s1 = 0, s2 = 0;
  for (int i = 0; i < n; ++i) {
    double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
    double s0 = (out[i] - 128) * w + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void TestHalfRateHasNoStrongImages() {
  SetupFm();
  int n = Play(84, 100);
  double f = ExpectedFrequency(84);
  double db = 10 * log10(Goertzel(n, 0.5 - f) / Goertzel(n, f));
  CHECK(db < -30, "image at fs/2 - f is only %.1f dB down", db);
}

// #4: low notes play in tune (within 1 cent).
static void TestLowNotesInTune() {
  const uint8_t notes[] = { 21, 28, 33, 40, 45 };
  for (uint8_t i = 0; i < sizeof(notes); ++i) {
    SetupFm();
    int n = Play(notes[i], 900);
    double cents = 1200 * log2(MeasureFrequency(n) /
                               ExpectedFrequency(notes[i]));
    CHECK(fabs(cents) < 1.0, "MIDI note %d is %.1f cents off",
          notes[i], cents);
  }
}

// --- Karplus-Strong ---

// Plucked, lightly damped, bright string; no ensemble/body/stiffness.
static void SetupKs() {
  voice.Init();
  Patch* p = patch();
  memset(p, 0, 16);
  p->padding[2] = ENGINE_KS_PLUCK;
  p->osc[0].parameter = 20;   // damping
  p->osc[1].parameter = 127;  // bright excitation
  for (uint8_t i = 0; i < kNumModulations; ++i) {
    p->modulation[i].source = MOD_SRC_CONSTANT_256;
    p->modulation[i].destination = MOD_DST_FILTER_RESONANCE;
    p->modulation[i].amount = 0;
  }
}

// Pitch by autocorrelation (zero crossings don't work on plucked strings):
// the first strong peak, refined with a parabola. In cycles per sample.
static double AutocorrFrequency(int n) {
  static double r[1300];
  int start = n / 4, len = n / 2;
  double best = 0;
  for (int lag = 8; lag < 1200 && start + len + lag < n; ++lag) {
    double num = 0, e1 = 0, e2 = 0;
    for (int i = start; i < start + len; ++i) {
      double a = out[i] - 128.0, b = out[i + lag] - 128.0;
      num += a * b; e1 += a * a; e2 += b * b;
    }
    r[lag] = num / sqrt(e1 * e2 + 1e-9);
    if (r[lag] > best) best = r[lag];
  }
  for (int lag = 9; lag < 1199; ++lag) {
    if (r[lag] > 0.9 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
      double d = 0.5 * (r[lag - 1] - r[lag + 1]) /
                 (r[lag - 1] - 2 * r[lag] + r[lag + 1]);
      return 1 / (lag + d);
    }
  }
  return 0;
}

// Frequency (cycles per sample) of the strongest partial within +/-120
// cents of `f`, searched in 1-cent steps over the whole capture.
static double PartialNear(int n, double f) {
  double best = 0, best_f = f;
  for (int c = -120; c <= 120; ++c) {
    double g = f * pow(2, c / 1200.0), p = Goertzel(n, g);
    if (p > best) { best = p; best_f = g; }
  }
  return best_f;
}

// The first note after a patch load (which resets the engines) sounds.
static void TestKsFirstNoteAfterPatchLoad() {
  SetupKs();
  int n = Play(45, 10);  // Play() resets the engines, like a patch load
  CHECK(PeakToPeak(n) > 20, "first KS note is silent (p2p %d)", PeakToPeak(n));
}

// In tune from A2 (110 Hz, near the bottom of the range) to A6: within 5
// cents through A5. Above that the string is only ~11-15 samples long at the
// engine's 19.6 kHz, where the loop filters' delay at the fundamental drifts
// from the low-frequency value the tuning compensates: within 20 cents.
static void TestKsInTune() {
  const uint8_t notes[] = { 33, 40, 45, 52, 57, 64, 69, 76, 81 };  // A2..A6
  for (uint8_t i = 0; i < sizeof(notes); ++i) {
    SetupKs();
    int n = Play(notes[i], 60);
    // The fundamental itself: with a metallic (stretched) string,
    // autocorrelation reads a compromise pulled sharp by the overtones.
    double f = PartialNear(n, ExpectedFrequency(notes[i]));
    double cents = 1200 * log2(f / ExpectedFrequency(notes[i]));
    double limit = notes[i] + 12 <= 81 ? 5 : 20;
    CHECK(fabs(cents) < limit, "KS MIDI note %d is %.1f cents off",
          notes[i] + 12, cents);
  }
}

// Plucks a KS note and runs until the AC level (20 ms windows, mean
// removed) falls 20 dB below its peak. Returns seconds (-1: never, within
// max_s) and the DC offset of the last window, in 12-bit units.
static double KsRing(uint8_t note, double max_s, double* dc_out) {
  voice.ResetEngines();
  audio_buffer.size = 0;
  voice.ProcessBlock();
  voice.Trigger((note + 12) << 7, 127, 0);
  double peak = 0, acc = 0, sum = 0, dc = 0;
  int count = 0;
  for (int blk = 0; blk < max_s * 980; ++blk) {
    audio_buffer.size = 0;
    voice.ProcessBlock();
    for (int i = 0; i < audio_buffer.size; ++i) {
      double v = audio_buffer.data[i] - 2048.0;
      acc += v * v;
      sum += v;
    }
    if (++count == 20) {
      double m = sum / 800;
      double e = sqrt(acc / 800 - m * m);
      dc = m;
      if (e > peak) peak = e;
      else if (e < peak / 10) { if (dc_out) *dc_out = dc; return blk / 980.4; }
      acc = sum = 0;
      count = 0;
    }
  }
  if (dc_out) *dc_out = dc;
  return -1;
}

// Seconds for a KS note's fundamental to fall 20 dB, measured after the
// attack (Goertzel at the fundamental, 0.3 s vs 1.3 s), same pluck each run.
static double KsFundamentalDecay(uint8_t note, uint8_t damping) {
  SetupKs();
  patch()->osc[0].parameter = damping;
  voice.ResetEngines();
  audio_buffer.size = 0;
  voice.ProcessBlock();
  Random::Seed(12345);
  voice.Trigger((note + 12) << 7, 127, 0);
  const double f = ExpectedFrequency(note);
  double level[2];
  int blk = 0;
  for (int k = 0; k < 2; ++k) {
    int start = k ? 1274 : 294, end = start + 98;  // 0.3 s and 1.3 s, 0.1 s
    double c = 2 * cos(2 * M_PI * f), s1 = 0, s2 = 0;
    for (; blk < end; ++blk) {
      audio_buffer.size = 0;
      voice.ProcessBlock();
      if (blk < start) continue;
      for (int i = 0; i < audio_buffer.size; ++i) {
        double s0 = (audio_buffer.data[i] - 2048.0) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
      }
    }
    level[k] = sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
  }
  return 20 / (20 * log10(level[0] / level[1]));
}

// Strings ring about three times longer than the original loop: at damping
// 40 the fundamental took 4.2 s (A3) and 0.8 s (A4) to fall 20 dB.
static void TestKsRingsLonger() {
  double a3 = KsFundamentalDecay(45, 40), a4 = KsFundamentalDecay(57, 40);
  CHECK(a3 >= 11, "A3 fundamental falls 20 dB in %.1f s (was 4.2)", a3);
  CHECK(a4 >= 2.1, "A4 fundamental falls 20 dB in %.1f s (was 0.8)", a4);
}

// A pluck leaves no DC offset in the string (a KS loop keeps DC forever):
// the average over 0.5 s (hundreds of cycles, so the note itself cancels)
// starting 1 s after the pluck.
static void TestKsNoDcOffset() {
  SetupKs();
  patch()->osc[0].parameter = 40;
  voice.ResetEngines();
  audio_buffer.size = 0;
  voice.ProcessBlock();
  voice.Trigger((57 + 12) << 7, 127, 0);
  double sum = 0;
  int n = 0;
  for (int blk = 0; blk < 1470; ++blk) {  // 1.5 s
    audio_buffer.size = 0;
    voice.ProcessBlock();
    if (blk < 980) continue;
    for (int i = 0; i < audio_buffer.size; ++i, ++n) {
      sum += audio_buffer.data[i] - 2048.0;
    }
  }
  double dc = sum / n;
  CHECK(fabs(dc) < 10, "string holds a DC offset of %.1f (of 2048)", dc);
}

// Body is a resonance after the string: it never moves the pitch.
static void TestKsBodyKeepsPitch() {
  const uint8_t notes[] = { 45, 57 };  // A3, A4
  const uint8_t bodies[] = { 0, 60, 120 };
  for (uint8_t i = 0; i < sizeof(notes); ++i) {
    for (uint8_t j = 0; j < sizeof(bodies); ++j) {
      SetupKs();
      patch()->mix_balance = bodies[j];
      int n = Play(notes[i], 60);
      double cents = 1200 * log2(PartialNear(n, ExpectedFrequency(notes[i])) /
                                 ExpectedFrequency(notes[i]));
      CHECK(fabs(cents) < 5, "MIDI %d at body %d is %.1f cents off",
            notes[i] + 12, bodies[j], cents);
    }
  }
}

// Body is a soundbox: a fixed resonance (~195 Hz) that boosts whatever
// falls near it whatever note is played (G3's fundamental, 196 Hz), and a
// softening of the partials well above it (A3's 4th harmonic, 880 Hz).
// Same pluck each run, so only body changes.
static double KsPartialWithBody(uint8_t note, int harmonic, uint8_t body) {
  SetupKs();
  patch()->mix_balance = body;
  Random::Seed(4321);
  int n = Play(note, 60);
  return Goertzel(n, harmonic * ExpectedFrequency(note));
}

static void TestKsBodyResonates() {
  double g3 = 10 * log10(KsPartialWithBody(43, 1, 120) /
                         KsPartialWithBody(43, 1, 0));
  CHECK(g3 > 6, "body boosts G3's 196 Hz fundamental by only %.1f dB", g3);
  // A3's 4th and 6th harmonics (880, 1320 Hz) get quieter in absolute terms.
  for (int h = 4; h <= 6; h += 2) {
    double d = 10 * log10(KsPartialWithBody(45, h, 120) /
                          KsPartialWithBody(45, h, 0));
    CHECK(d < -6, "body changes A3's harmonic %d by only %.1f dB", h, d);
  }
}

// Full body on a note right on the resonance (G3, 196 Hz) stays out of the
// output clamp: the body tilts the tone, it doesn't overdrive it.
static void TestKsBodyDoesNotClip() {
  SetupKs();
  patch()->mix_balance = 127;
  Random::Seed(4321);
  int n = Play(43, 60);
  int clipped = 0;
  for (int i = 0; i < n; ++i) {
    if (out16[i] <= 0 || out16[i] >= 4095) ++clipped;
  }
  CHECK(clipped == 0, "full body on G3 clips %d samples", clipped);
}

// Body is audible across its range, not just at the top: at body 64, A3's
// fundamental rises and its 6th harmonic falls by a few dB each.
static void TestKsBodyAtHalf() {
  double h1 = 10 * log10(KsPartialWithBody(45, 1, 64) /
                         KsPartialWithBody(45, 1, 0));
  double h6 = 10 * log10(KsPartialWithBody(45, 6, 64) /
                         KsPartialWithBody(45, 6, 0));
  CHECK(h1 - h6 > 15, "body 64 tilts A3 by only %.1f dB (H1 %+.1f, H6 %+.1f)",
        h1 - h6, h1, h6);
  double f1 = 10 * log10(KsPartialWithBody(45, 1, 127) /
                         KsPartialWithBody(45, 1, 0));
  double f6 = 10 * log10(KsPartialWithBody(45, 6, 127) /
                         KsPartialWithBody(45, 6, 0));
  CHECK(f1 - f6 > 25, "full body tilts A3 by only %.1f dB (H1 %+.1f, H6 %+.1f)",
        f1 - f6, f1, f6);
}

// Cents between A4's 8th partial and 8x its fundamental, at a given color.
static double KsStretch(uint8_t color) {
  SetupKs();
  patch()->osc[1].parameter = color;
  Random::Seed(4321);
  int n = Play(57, 900);
  double f1 = PartialNear(n, ExpectedFrequency(57));
  double f8 = PartialNear(n, 8 * f1);
  return 1200 * log2(f8 / (8 * f1));
}

// Bright excitation color makes the string metallic: overtones stretched
// sharp (dispersion). Up to the middle of the range the string stays
// harmonic.
static void TestKsBrightColorIsMetallic() {
  double bright = KsStretch(127), mid = KsStretch(64);
  CHECK(bright > 30, "color 127 stretches A4's 8th partial only %.1f cents",
        bright);
  CHECK(fabs(mid) < 5, "color 64 already stretches A4's 8th partial %.1f cents",
        mid);
}

// The ensemble is a chorus: it detunes gently, keeping the energy at the
// note's harmonics. An audio-rate LFO or stepping read heads smear energy in
// between (the dry string sits near -64 dB there).
static void TestKsChorusIsClean() {
  SetupKs();
  patch()->mix_sub_osc = 100;       // ensemble mix
  patch()->mix_parameter = 70;      // depth
  patch()->mix_op = 20;             // rate
  patch()->mix_sub_osc_shape = 40;  // spread
  Random::Seed(4321);
  int n = Play(45, 400);  // A3
  double f0 = ExpectedFrequency(45), at = 0, between = 0;
  for (int k = 1; k <= 8; ++k) {
    at += Goertzel(n, k * f0);
    between += Goertzel(n, (k + 0.5) * f0);
  }
  double db = 10 * log10(between / at);
  CHECK(db < -45, "chorus puts energy between harmonics at %.1f dB", db);
}

// Mod matrix destinations reach the KS engine ("body").
static void TestKsModMatrix() {
  SetupKs();
  int n = Play(45, 20);
  static uint8_t dry[kMaxSamples];
  memcpy(dry, out, n);
  SetupKs();
  patch()->modulation[0].destination = MOD_DST_MIX_BALANCE;  // "body"
  patch()->modulation[0].amount = 63;
  Play(45, 20);
  CHECK(memcmp(dry, out, n) != 0, "mod matrix -> body changed nothing");
}

// --- West Coast ---

// Clean sine: no fold, no FM, bias/symmetry centered, color open.
static void SetupWc() {
  voice.Init();
  Patch* p = patch();
  memset(p, 0, 16);
  p->padding[2] = ENGINE_WESTCOAST;
  p->osc[1].parameter = 64;  // symmetry centered
  p->mix_balance = 64;       // bias centered
  p->mix_parameter = 127;    // color open
  for (uint8_t i = 0; i < kNumModulations; ++i) {
    p->modulation[i].source = MOD_SRC_CONSTANT_256;
    p->modulation[i].destination = MOD_DST_FILTER_RESONANCE;
    p->modulation[i].amount = 0;
  }
}

// Low notes in tune (like FM's fine increment).
static void TestWcLowNotesInTune() {
  const uint8_t notes[] = { 16, 21, 28, 33, 45 };  // E1, A1, E2, A2, A3
  for (uint8_t i = 0; i < sizeof(notes); ++i) {
    SetupWc();
    int n = Play(notes[i], 900);
    double cents = 1200 * log2(MeasureFrequency(n) /
                               ExpectedFrequency(notes[i]));
    CHECK(fabs(cents) < 1.0, "WC MIDI note %d is %.1f cents off",
          notes[i] + 12, cents);
  }
}

// Folding works at high resolution: at full fold, a slow sine still gives
// many distinct output levels (an 8-bit folder amplifies its input steps
// into a coarse staircase).
static void TestWcFoldIsSmooth() {
  SetupWc();
  patch()->osc[0].parameter = 127;  // full fold
  int n = Play(16, 40);
  bool seen[256] = { false };
  int distinct = 0;
  for (int i = 0; i < n; ++i) {
    if (!seen[out[i]]) { seen[out[i]] = true; ++distinct; }
  }
  CHECK(distinct > 200, "full fold uses only %d output levels", distinct);
}

// Mod matrix destinations reach the WC engine ("bias").
static void TestWcModMatrix() {
  SetupWc();
  patch()->osc[0].parameter = 40;  // some fold, so bias matters
  int n = Play(33, 10);
  static uint8_t dry[kMaxSamples];
  memcpy(dry, out, n);
  SetupWc();
  patch()->osc[0].parameter = 40;
  patch()->modulation[0].destination = MOD_DST_MIX_BALANCE;  // "bias"
  patch()->modulation[0].amount = 63;
  Play(33, 10);
  CHECK(memcmp(dry, out, n) != 0, "mod matrix -> bias changed nothing");
}

// Harmonic level of `out` at cycles-per-sample f (Goertzel, windowed).
static double HarmonicDb(int n, double f, double f0) {
  return 10 * log10(Goertzel(n, f) / Goertzel(n, f0));
}

// Bias and symmetry are different controls.
static void TestWcBiasAndSymmetryDiffer() {
  SetupWc();
  patch()->osc[0].parameter = 40;
  patch()->mix_balance = 100;       // bias up
  int n = Play(33, 20);
  static uint8_t a[kMaxSamples];
  memcpy(a, out, n);
  SetupWc();
  patch()->osc[0].parameter = 40;
  patch()->osc[1].parameter = 100;  // symmetry up instead
  Play(33, 20);
  CHECK(memcmp(a, out, n) != 0, "bias and symmetry sound identical");
}

// At high fold, bias slides the fold pattern, reshaping the upper partials
// where the folds live (odd H7-H29; the low odd harmonics of a triangle
// folder barely move with a DC offset, and even ones start from nothing at
// the symmetric center, so neither says much). Bias 72, not a value where the
// shift is a whole or half fold period (those only invert the waveform).
static void TestWcBiasMovesTheFold() {
  const double f = ExpectedFrequency(33);
  static double ref[30];
  double moved = 0;
  const uint8_t bias[2] = { 64, 72 };
  for (int k = 0; k < 2; ++k) {
    SetupWc();
    patch()->osc[0].parameter = 60;
    patch()->mix_balance = bias[k];
    int n = Play(33, 200);
    for (int h = 7; h <= 29; h += 2) {
      double db = HarmonicDb(n, h * f, f);
      if (k == 0) ref[h] = db; else moved += fabs(db - ref[h]);
    }
  }
  // Old post-gain bias: ~3 dB; pre-gain bias: ~30 dB.
  CHECK(moved > 15, "bias 64->72 at fold 60 moves odd H7-H29 by only %.1f dB",
        moved);
}

// The engines send the DAC all 12 bits: a quiet sine (op1 at level 100,
// about -20 dB) still uses the low 4 bits and many distinct levels.
static void TestOutputIsTwelveBits() {
  SetupFm();
  patch()->mix_sub_osc = 100;
  int n = Play(45, 20);
  static bool seen[4096];
  memset(seen, 0, sizeof(seen));
  int distinct = 0, low_bits = 0;
  for (int i = 0; i < n; ++i) {
    if (out16[i] >= 4096) continue;
    if (!seen[out16[i]]) { seen[out16[i]] = true; ++distinct; }
    low_bits |= out16[i] & 15;
  }
  CHECK(low_bits && distinct > 300,
        "quiet sine uses %d levels, low bits %s", distinct,
        low_bits ? "used" : "never used");
}

int main() {
  TestSpecialEnginesDoNotCrush();
  TestOutputIsTwelveBits();
  TestModMatrixReachesFmLevels();
  TestCarrierLevelsAreApplied();
  TestAttenuationIsLogarithmic();
  TestFeedbackCurve();
  TestLowNotesInTune();
  TestMatchesTx81zReference();
  TestHalfRateHasNoStrongImages();
  TestKsFirstNoteAfterPatchLoad();
  TestKsInTune();
  TestKsModMatrix();
  TestKsRingsLonger();
  TestKsNoDcOffset();
  TestKsBodyKeepsPitch();
  TestKsBodyResonates();
  TestKsBodyDoesNotClip();
  TestKsBodyAtHalf();
  TestKsBrightColorIsMetallic();
  TestKsChorusIsClean();
  TestWcLowNotesInTune();
  TestWcFoldIsSmooth();
  TestWcModMatrix();
  TestWcBiasAndSymmetryDiffer();
  TestWcBiasMovesTheFold();
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures != 0;
}
