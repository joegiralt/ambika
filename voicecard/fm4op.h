// Copyright 2026 Ambika contributors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// -----------------------------------------------------------------------------
//
// 4-operator FM synthesis engine, TX81Z style.

#ifndef VOICECARD_FM4OP_H_
#define VOICECARD_FM4OP_H_

#include "avrlib/base.h"
#include "avrlib/op.h"
#include "voicecard/resources.h"

using namespace avrlib;

namespace ambika {

// 16-bit sine table (512 entries + 1 wrap), defined in voice.cc.
extern const prog_uint16_t wav_res_sine16[] PROGMEM;

// 16-bit sine interpolation for FM — 512 entries, returns 0-65535.
static inline uint16_t InterpolateSine16(uint16_t phase) {
  // 9-bit index (512 entries), 7-bit fractional
  uint16_t index = phase >> 7;
  uint8_t frac = (phase << 1) & 0xFE;
  uint16_t a = ResourcesManager::Lookup<uint16_t, uint16_t>(
      wav_res_sine16, index);
  uint16_t b = ResourcesManager::Lookup<uint16_t, uint16_t>(
      wav_res_sine16, index + 1);
  // Linear interpolation. Neighbouring entries differ by at most ~403, so
  // the difference fits a fast 16x8 multiply.
  return a + S16U8MulShift8(static_cast<int16_t>(b - a), frac);
}

// The 8 TX81Z (YM2414 "OPZ") waveforms, as modelled by ymfm (ymfm_opz.cpp).
// W2 is sin^2 with sign; ymfm infers it from the manual's diagrams.
enum FmWaveform {
  FM_WAVE_W1,  // sine
  FM_WAVE_W2,  // sin^2
  FM_WAVE_W3,  // W1 first half, then silence
  FM_WAVE_W4,  // W2 first half, then silence
  FM_WAVE_W5,  // W1 at double speed in the first half, then silence
  FM_WAVE_W6,  // W2 at double speed in the first half, then silence
  FM_WAVE_W7,  // two positive W1 humps in the first half, then silence
  FM_WAVE_W8,  // two positive W2 humps in the first half, then silence
  FM_WAVE_LAST
};

// TX81Z algorithms (panel operator numbers; OP4 has the feedback).
enum FmAlgorithm {
  FM_ALG_1,  // 4->3->2->1
  FM_ALG_2,  // (4+3)->2->1
  FM_ALG_3,  // (4 + (3->2))->1
  FM_ALG_4,  // ((4->3) + 2)->1
  FM_ALG_5,  // (4->3) + (2->1)
  FM_ALG_6,  // 4->(1+2+3)
  FM_ALG_7,  // (4->3) + 2 + 1
  FM_ALG_8,  // 1+2+3+4
  FM_ALG_LAST
};

// Patch field reinterpretation for FM4OP mode.
// These map onto existing patch byte offsets when padding[2] == ENGINE_FM4OP.
//
// osc[0].shape      = unused
// osc[0].parameter  = algorithm (0-7)
// osc[0].range      = op1 coarse ratio
// osc[0].detune     = op1 fine detune
// osc[1].shape      = op1 waveform (low nibble) | op2 waveform (high nibble)
// osc[1].parameter  = op3 waveform (low nibble) | op4 waveform (high nibble)
// osc[1].range      = op2 coarse ratio
// osc[1].detune     = op2 fine detune
// mix_balance       = op3 coarse ratio
// mix_op            = op3 fine detune
// mix_parameter     = op4 coarse ratio
// mix_sub_osc_shape = op4 fine detune
// mix_sub_osc       = op1 output level
// mix_noise         = op2 output level
// mix_fuzz          = op3 output level
// mix_crush         = op4 output level
// padding[0]        = feedback level
// padding[3]        = transpose (signed semitones)

// 10.22 phase: the high word holds the 10-bit waveform index (so reading it
// costs nothing), the 22 fractional bits keep low notes in tune.
struct FmOperator {
  uint32_t phase;
  uint32_t phase_increment;
};

// Like the OPZ, operators work in the log domain: a quarter-wave log-sine
// table plus an attenuation (level + envelope, in 4.8 fixed point log2:
// 256 = 6 dB), turned back into a 14-bit sample by one exp table lookup.
// No multiplies per sample, except for feedback.
static const uint16_t kFmSilent = 13 << 8;  // below the 14-bit floor

class Fm4Op {
 public:
  Fm4Op() { }

  void Init() {
#ifdef FM_PHASE_DITHER
    dither_ = 0xACE1;
#endif
    for (uint8_t i = 0; i < 4; ++i) {
      op_[i].phase = 0;
      op_[i].phase_increment = 0;
    }
    feedback_[0] = 0;
    feedback_[1] = 0;
  }

  static const prog_uint16_t log_sin_[256] PROGMEM;
  static const prog_uint16_t exp_[kFmSilent] PROGMEM;
  static const prog_uint16_t env_to_attenuation_[256] PROGMEM;
  static const prog_uint16_t feedback_gain_[16] PROGMEM;

  // One operator: waveform `wave` at 10-bit phase (higher bits ignored),
  // attenuated. Returns a signed 14-bit sample.
  //
  // The C version is the reference (bit-exact against ymfm in
  // voicecard/test). On the AVR, gcc at -Os would not inline it and spent
  // about 90 cycles per call; the assembly below does the same work in about
  // 50. bench/ checks the two agree on every input (BENCH_OPTEST).
  static inline int16_t OperatorC(
      uint8_t wave, uint16_t phase, uint16_t attenuation) {
    // The chip has no true zero: "silent" is the smallest sine value.
    static const uint16_t kZero = 0x859;  // log_sin_[0]
    uint16_t a;
    uint8_t negative = 0;
    if ((wave & 6) && (phase & 0x200)) {
      a = kZero;  // W3-W8: second half
    } else {
      if (wave & 4) {  // W5-W8: double speed
        phase <<= 1;
        if (wave & 2) {
          phase &= 0x1FF;  // W7/W8: positive humps only
        }
      }
      uint8_t quarter = phase;
      if (phase & 0x100) {
        quarter = ~quarter;
      }
      a = pgm_read_word(&log_sin_[quarter]);
      if (wave & 1) {
        a <<= 1;  // sin^2 in the log domain, capped like the chip
        if (a > kZero) {
          a = kZero;
        }
      }
      negative = (phase & 0x200) != 0;
    }
    a += attenuation;
    if (a >= kFmSilent) {
      return 0;
    }
    int16_t v = pgm_read_word(&exp_[a]);
    return negative ? -v : v;
  }

#ifdef __AVR__
  static int16_t Operator(uint8_t wave, uint16_t phase, uint16_t attenuation)
      __attribute__((noinline));
#else
  static inline int16_t Operator(
      uint8_t wave, uint16_t phase, uint16_t attenuation) {
    return OperatorC(wave, phase, attenuation);
  }
#endif
  // Level 0-127 in 0.75 dB steps (127 = full, 0 = off) plus a linear
  // envelope 0-255, as one attenuation. Computed once per block.
  static inline uint16_t Attenuation(uint8_t level, uint8_t envelope) {
    if (level == 0) {
      return kFmSilent;
    }
    if (level > 127) {
      level = 127;
    }
    uint16_t a = (127 - level) * 32 +
        pgm_read_word(&env_to_attenuation_[envelope]);
    return a > kFmSilent ? kFmSilent : a;
  }

  // Feedback knob 0-127 to a gain (x/65536) on the sum of op4's last two
  // outputs. Exponential like the OPZ's FB 1-7: 16 knob steps per doubling,
  // knob 112 = FB 7.
  static inline uint16_t FeedbackGain(uint8_t knob) {
    if (knob == 0) {
      return 0;
    }
    return pgm_read_word(&feedback_gain_[knob & 15]) >> (7 - (knob >> 4));
  }

  // One output sample: advances the phases and returns the signed 14-bit
  // sum of the carriers. Modulators feed the next operator's phase with their
  // output >> 1, as on the OPZ (up to +/-4 cycles).
  inline int16_t Sample(
      uint8_t algorithm,
      uint8_t w0, uint8_t w1, uint8_t w2, uint8_t w3,      // waveforms
      uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3,  // Attenuation()
      uint16_t feedback_gain)     // FeedbackGain()
      __attribute__((always_inline)) {  // one caller: Render
    // Phases advanced one by one: as a loop over op_[i], gcc spent as many
    // cycles on pointer arithmetic as on the adds.
    uint16_t p0, p1, p2, p3;
#ifdef FM_PHASE_DITHER
    // Truncating the phase to the waveform index leaves discrete, inharmonic
    // spurs - they beat against a held note and rattle. Dithering the
    // truncation trades them for broadband noise at the same total power,
    // which is far less noticeable. 16-bit Galois LFSR, a few instructions.
    dither_ = (dither_ >> 1) ^ (-(dither_ & 1) & 0xB400);
    uint16_t d = dither_;
    uint16_t p[4];
    for (uint8_t i = 0; i < 4; ++i) {
      op_[i].phase += op_[i].phase_increment;
      p[i] = (op_[i].phase + d) >> 16;
      d = (d << 5) | (d >> 11);   // decorrelate the four operators
    }
    p0 = p[0]; p1 = p[1]; p2 = p[2]; p3 = p[3];
#else
    op_[0].phase += op_[0].phase_increment; p0 = op_[0].phase >> 16;
    op_[1].phase += op_[1].phase_increment; p1 = op_[1].phase >> 16;
    op_[2].phase += op_[2].phase_increment; p2 = op_[2].phase >> 16;
    op_[3].phase += op_[3].phase_increment; p3 = op_[3].phase >> 16;
#endif

    int16_t fb = 0;
    if (feedback_gain) {
      fb = (static_cast<int32_t>(feedback_[0] + feedback_[1]) *
            feedback_gain) >> 16;
    }
    int16_t op4 = Operator(w3, p3 + fb, a3);
    feedback_[1] = feedback_[0];
    feedback_[0] = op4;

    // Routing as on the chip (checked sample for sample against ymfm's
    // output_4op in voicecard/test). A modulator feeds output >> 1.
    int16_t sum, op3, op2;
    switch (algorithm) {
      case FM_ALG_1:  // 4->3->2->1
        op3 = Operator(w2, p2 + (op4 >> 1), a2);
        op2 = Operator(w1, p1 + (op3 >> 1), a1);
        sum = Operator(w0, p0 + (op2 >> 1), a0);
        break;
      case FM_ALG_2:  // (4+3)->2->1
        op3 = Operator(w2, p2, a2);
        op2 = Operator(w1, p1 + ((op4 + op3) >> 1), a1);
        sum = Operator(w0, p0 + (op2 >> 1), a0);
        break;
      case FM_ALG_3:  // (4 + (3->2))->1
        op3 = Operator(w2, p2, a2);
        op2 = Operator(w1, p1 + (op3 >> 1), a1);
        sum = Operator(w0, p0 + ((op4 + op2) >> 1), a0);
        break;
      case FM_ALG_4:  // ((4->3) + 2)->1
        op3 = Operator(w2, p2 + (op4 >> 1), a2);
        op2 = Operator(w1, p1, a1);
        sum = Operator(w0, p0 + ((op3 + op2) >> 1), a0);
        break;
      case FM_ALG_5:  // (4->3) + (2->1)
        op3 = Operator(w2, p2 + (op4 >> 1), a2);
        op2 = Operator(w1, p1, a1);
        sum = Operator(w0, p0 + (op2 >> 1), a0) + op3;
        break;
      case FM_ALG_6:  // 4->(1+2+3)
        op4 >>= 1;
        sum = Operator(w0, p0 + op4, a0) +
              Operator(w1, p1 + op4, a1) +
              Operator(w2, p2 + op4, a2);
        break;
      case FM_ALG_7:  // (4->3) + 2 + 1
        sum = Operator(w0, p0, a0) +
              Operator(w1, p1, a1) +
              Operator(w2, p2 + (op4 >> 1), a2);
        break;
      case FM_ALG_8:  // 1+2+3+4
      default:
        sum = Operator(w0, p0, a0) +
              Operator(w1, p1, a1) +
              Operator(w2, p2, a2) + op4;
        break;
    }
    return sum;
  }

  // Array form, for the tests.
  inline int16_t Sample(
      uint8_t algorithm, const uint8_t* w, const uint16_t* att,
      uint16_t feedback_gain) {
    return Sample(algorithm, w[0], w[1], w[2], w[3],
                  att[0], att[1], att[2], att[3], feedback_gain);
  }

  // Render a block of 12-bit DAC samples (centered on 2048).
  void Render(
      uint8_t algorithm,
      const uint8_t* w,
      const uint16_t* att,
      uint16_t feedback_gain,
      uint16_t* buffer,
      uint8_t size) {
    // Copied out of the arrays once so the inlined Sample works on registers.
    uint8_t w0 = w[0], w1 = w[1], w2 = w[2], w3 = w[3];
    uint16_t a0 = att[0], a1 = att[1], a2 = att[2], a3 = att[3];
    while (size--) {
      int16_t out = Sample(algorithm, w0, w1, w2, w3, a0, a1, a2, a3,
                           feedback_gain);
      // 14-bit sum to the 12-bit DAC. The chip doesn't clip here; the DAC's
      // range forces a choice, so several full carriers clip.
      out >>= 2;
      if (out > 2047) out = 2047;
      if (out < -2048) out = -2048;
      *buffer++ = out + 2048;
    }
  }

  // TX81Z frequency ratio table — 64 entries, 8.8 fixed-point.
  // Coarse ratio byte (0-63) indexes into this table.
  // Values: 0.50, 0.71, 0.78, 0.87, 1.00, 1.41, 1.57, 1.73, 2.00, ...
  static const prog_uint16_t tx81z_ratios_[] PROGMEM;

  // Set operator phase increment using TX81Z-style ratio lookup.
  // base_increment is 16.8 fixed point (ComputePhaseIncrementFine).
  void SetOperatorIncrement(uint8_t op_index, uint32_t base_increment,
                            uint8_t coarse_ratio, int8_t fine_detune) {
    // Look up the ratio from the TX81Z table (8.8 fixed-point).
    // Clamp, don't wrap: the UI and mod matrix can push past index 63.
    uint8_t idx = coarse_ratio > 63 ? 63 : coarse_ratio;
    uint16_t ratio_fp = ResourcesManager::Lookup<uint16_t, uint8_t>(
        tx81z_ratios_, idx);
    // 16.8 base x 8.8 ratio = 24.16; keep the low 32 bits (16.16). Split
    // so the multiply fits in 32 bits.
    uint32_t increment =
        (static_cast<uint32_t>(
            static_cast<uint16_t>(base_increment >> 8)) * ratio_fp << 8) +
        static_cast<uint32_t>(static_cast<uint8_t>(base_increment)) * ratio_fp;
    // Fine detune: small pitch offset.
    if (fine_detune > 0) {
      increment += (increment >> 8) * fine_detune;
    } else if (fine_detune < 0) {
      increment -= (increment >> 8) * (-fine_detune);
    }
    op_[op_index].phase_increment = increment >> 6;  // 16.16 -> 10.22
  }

  FmOperator* mutable_op(uint8_t i) { return &op_[i]; }

 private:
  FmOperator op_[4];
  int16_t feedback_[2];
#ifdef FM_PHASE_DITHER
  uint16_t dither_;
#endif

  DISALLOW_COPY_AND_ASSIGN(Fm4Op);
};

#ifdef __AVR__
// Same algorithm as OperatorC, step for step; comments name the C lines.
// v and t need upper registers (ldi, cpi, andi); Z is the table pointer.
inline int16_t Fm4Op::Operator(
    uint8_t wave, uint16_t phase, uint16_t attenuation) {
  uint16_t v;
  uint8_t t;
  asm volatile(
    "mov  %[t], %[w]          \n\t"
    "andi %[t], 6             \n\t"   // (wave & 6) &&
    "breq 1f                  \n\t"
    "sbrc %B[p], 1            \n\t"   // (phase & 0x200): second half
    "rjmp 9f                  \n\t"
    "1: movw r30, %A[p]       \n\t"   // working phase in Z
    "sbrs %[w], 2             \n\t"   // wave & 4: double speed
    "rjmp 2f                  \n\t"
    "lsl  r30                 \n\t"   // phase <<= 1
    "rol  r31                 \n\t"
    "sbrc %[w], 1             \n\t"   // wave & 2: phase &= 0x1FF
    "andi r31, 1              \n\t"
    "2: mov  %[t], r31        \n\t"   // bit 1 of t = negative (phase bit 9)
    "sbrc r31, 0              \n\t"   // phase & 0x100: quarter = ~quarter
    "com  r30                 \n\t"
    "ldi  r31, 0              \n\t"   // Z = &log_sin_[quarter]
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ls]))  \n\t"
    "sbci r31, hi8(-(%[ls]))  \n\t"
    "lpm  %A[v], Z+           \n\t"
    "lpm  %B[v], Z            \n\t"
    "sbrs %[w], 0             \n\t"   // wave & 1: a <<= 1, capped at kZero
    "rjmp 3f                  \n\t"
    "lsl  %A[v]               \n\t"
    "rol  %B[v]               \n\t"
    "ldi  r30, 0x08           \n\t"   // a > 0x859  <=>  a >= 0x85A
    "cpi  %A[v], 0x5A         \n\t"
    "cpc  %B[v], r30          \n\t"
    "brlo 3f                  \n\t"
    "ldi  %A[v], 0x59         \n\t"
    "ldi  %B[v], 0x08         \n\t"
    "3: add  %A[v], %A[att]   \n\t"   // a += attenuation
    "adc  %B[v], %B[att]      \n\t"
    "rjmp 4f                  \n\t"
    "9: ldi  %A[v], 0x59      \n\t"   // a = kZero, negative = 0
    "ldi  %B[v], 0x08         \n\t"
    "clr  %[t]                \n\t"
    "add  %A[v], %A[att]      \n\t"
    "adc  %B[v], %B[att]      \n\t"
    "4: cpi  %B[v], 0x0D      \n\t"   // a >= kFmSilent (0xD00): return 0
    "brsh 8f                  \n\t"
    "movw r30, %A[v]          \n\t"   // Z = &exp_[a]
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ex]))  \n\t"
    "sbci r31, hi8(-(%[ex]))  \n\t"
    "lpm  %A[v], Z+           \n\t"
    "lpm  %B[v], Z            \n\t"
    "sbrs %[t], 1             \n\t"   // negative ? -v : v
    "rjmp 7f                  \n\t"
    "com  %B[v]               \n\t"
    "neg  %A[v]               \n\t"
    "sbci %B[v], 0xFF         \n\t"
    "rjmp 7f                  \n\t"
    "8: clr  %A[v]            \n\t"
    "clr  %B[v]               \n\t"
    "7:                       \n\t"
    : [v] "=&d" (v), [t] "=&d" (t)
    : [w] "r" (wave), [p] "r" (phase), [att] "r" (attenuation),
      [ls] "i" (log_sin_), [ex] "i" (exp_)
    : "r30", "r31", "cc");
  return static_cast<int16_t>(v);
}
#endif  // __AVR__

}  // namespace ambika

#endif  // VOICECARD_FM4OP_H_
