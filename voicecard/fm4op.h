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

struct FmRenderParams;

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
  static const prog_uint16_t exp_[4 * 256] PROGMEM;
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
    // exp_ holds four shifts (a's bits 8-9); the rest is a shift by 4, 8
    // or 12 (bits 10-11). Identical to a 13-copy table.
    int16_t v = pgm_read_word(&exp_[a & 0x3FF]) >> ((a >> 10) << 2);
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
  // RenderC is the reference; on the AVR, RenderAsm does the same work with
  // the phases in registers and the operators inlined (about 60% of the
  // cycles). bench/ with BENCH_FMTEST checks they agree sample for sample.
  void Render(
      uint8_t algorithm,
      const uint8_t* w,
      const uint16_t* att,
      uint16_t feedback_gain,
      uint16_t* buffer,
      uint8_t size) {
#ifdef __AVR__
    RenderAsm(algorithm, w, att, feedback_gain, buffer, size);
#else
    RenderC(algorithm, w, att, feedback_gain, buffer, size);
#endif
  }

  void RenderC(
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

#ifdef __AVR__
  void RenderAsm(
      uint8_t algorithm,
      const uint8_t* w,
      const uint16_t* att,
      uint16_t feedback_gain,
      uint16_t* buffer,
      uint8_t size);
  // Routing per algorithm: for operators 1-3, a bit mask of which operator
  // outputs (bit j = op j+1) feed its phase; then the carriers. Op 4 has
  // the feedback only. Same routing as the switch in Sample().
  static const prog_uint8_t routing_[8][4] PROGMEM;
  // Static so RenderAsm has no stack frame: the loop owns Y.
  static struct FmRenderParams params_;
#endif

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
  uint8_t t, q;
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
    "mov  %[q], %B[v]         \n\t"   // q = a >> 10: shift by 4q after
    "lsr  %[q]                \n\t"
    "lsr  %[q]                \n\t"
    "andi %B[v], 0x03         \n\t"   // Z = &exp_[a & 0x3FF]
    "movw r30, %A[v]          \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ex]))  \n\t"
    "sbci r31, hi8(-(%[ex]))  \n\t"
    "lpm  %A[v], Z+           \n\t"
    "lpm  %B[v], Z            \n\t"
    "tst  %[q]                \n\t"
    "breq 6f                  \n\t"
    "cpi  %[q], 2             \n\t"
    "brlo 5f                  \n\t"
    "mov  %A[v], %B[v]        \n\t"   // >> 8
    "clr  %B[v]               \n\t"
    "cpi  %[q], 3             \n\t"
    "brne 6f                  \n\t"
    "swap %A[v]               \n\t"   // and >> 4 more: q == 3
    "andi %A[v], 0x0F         \n\t"
    "rjmp 6f                  \n\t"
    "5: swap %A[v]            \n\t"   // >> 4
    "andi %A[v], 0x0F         \n\t"
    "swap %B[v]               \n\t"
    "mov  %[q], %B[v]         \n\t"
    "andi %[q], 0xF0          \n\t"
    "or   %A[v], %[q]         \n\t"
    "andi %B[v], 0x0F         \n\t"
    "6: sbrs %[t], 1          \n\t"   // negative ? -v : v
    "rjmp 7f                  \n\t"
    "com  %B[v]               \n\t"
    "neg  %A[v]               \n\t"
    "sbci %B[v], 0xFF         \n\t"
    "rjmp 7f                  \n\t"
    "8: clr  %A[v]            \n\t"
    "clr  %B[v]               \n\t"
    "7:                       \n\t"
    : [v] "=&d" (v), [t] "=&d" (t), [q] "=&d" (q)
    : [w] "r" (wave), [p] "r" (phase), [att] "r" (attenuation),
      [ls] "i" (log_sin_), [ex] "i" (exp_)
    : "r30", "r31", "cc");
  return static_cast<int16_t>(v);
}
#endif  // __AVR__


#ifdef __AVR__
// Everything the loop needs, addressed from Y with ldd. Offsets are the
// numbers in the assembly.
struct FmRenderParams {
  uint16_t att[4];       // 0
  uint8_t w[4];          // 8
  uint8_t mod_mask[4];   // 12  (index 3 unused: op 4 has only feedback)
  uint8_t carriers;      // 16
  uint16_t feedback_gain;  // 17
  int16_t fb[2];         // 19  feedback_[0], feedback_[1]
  int16_t out[4];        // 23  operator outputs, this sample
  uint8_t count;         // 31
  FmOperator* op;        // 32  the four phases (written back at the end)
  uint32_t inc[4];       // 34  phase increments, op 1 first
};

// Register use inside the loop:
//   r2-r5 phase of op 1, r6-r9 op 2, r10-r13 op 3, r14-r17 op 4 (low first)
//   r18:r19 operator result       r20 scratch       r21 waveform
//   r22:r23 attenuation           r24:r25 phase in / sum
//   X = output buffer, Y = params, Z = tables, op_ while adding increments
// The operator is the same algorithm as OperatorC in fixed registers,
// written out once per operator (no call), with a short path for W1. Its
// result is also stored to out[] for the operators it feeds.
inline __attribute__((noinline)) void Fm4Op::RenderAsm(
    uint8_t algorithm,
    const uint8_t* w,
    const uint16_t* att,
    uint16_t feedback_gain,
    uint16_t* buffer,
    uint8_t size) {
  FmRenderParams& params = params_;
  for (uint8_t i = 0; i < 4; ++i) {
    params.att[i] = att[i];
    params.w[i] = w[i];
    params.mod_mask[i] = pgm_read_byte(&routing_[algorithm][i]);
  }
  params.carriers = params.mod_mask[3];
  params.feedback_gain = feedback_gain;
  params.fb[0] = feedback_[0];
  params.fb[1] = feedback_[1];
  params.count = size;
  params.op = op_;
  for (uint8_t i = 0; i < 4; ++i) {
    params.inc[i] = op_[i].phase_increment;
  }
  // Y is gcc's frame pointer here, so it is saved and restored by hand
  // rather than declared clobbered.
  // Y is gcc's frame pointer here, so it is saved and restored by hand
  // rather than declared clobbered.
  asm volatile(
    "push r28                 \n\t"
    "push r29                 \n\t"
    "ldi  r28, lo8(%[pp])     \n\t"
    "ldi  r29, hi8(%[pp])     \n\t"
    "ldd  r30, Y+32           \n\t"
    "ldd  r31, Y+33           \n\t"
    /* phases into r2..r17 */
    "ld   r2, Z+              \n\t" "ld   r3, Z+              \n\t"
    "ld   r4, Z+              \n\t" "ld   r5, Z+              \n\t"
    "adiw r30, 4              \n\t"
    "ld   r6, Z+              \n\t" "ld   r7, Z+              \n\t"
    "ld   r8, Z+              \n\t" "ld   r9, Z+              \n\t"
    "adiw r30, 4              \n\t"
    "ld   r10, Z+             \n\t" "ld   r11, Z+             \n\t"
    "ld   r12, Z+             \n\t" "ld   r13, Z+             \n\t"
    "adiw r30, 4              \n\t"
    "ld   r14, Z+             \n\t" "ld   r15, Z+             \n\t"
    "ld   r16, Z+             \n\t" "ld   r17, Z+             \n\t"

    /* ---- per sample ---- */
    "10:                      \n\t"
    /* op 4: input = ((fb0 + fb1) * gain) >> 16, or 0 */
    "clr  r24                 \n\t"
    "clr  r25                 \n\t"
    "ldd  r22, Y+17           \n\t"
    "ldd  r23, Y+18           \n\t"
    "movw r18, r22            \n\t"
    "or   r18, r19            \n\t"
    "breq 11f                 \n\t"
    "ldd  r18, Y+19           \n\t"
    "ldd  r19, Y+20           \n\t"
    "ldd  r20, Y+21           \n\t"
    "ldd  r21, Y+22           \n\t"
    "add  r18, r20            \n\t"
    "adc  r19, r21            \n\t"   /* s = fb0 + fb1 in r18:r19, gain r22:r23 */
    "mul  r18, r22            \n\t"   /* 32-bit product: r20 r21 r24 r25 */
    "movw r20, r0             \n\t"
    "mul  r18, r23            \n\t"
    "add  r21, r0             \n\t"
    "adc  r24, r1             \n\t"
    "brcc .+2                 \n\t"
    "inc  r25                 \n\t"
    "mulsu r19, r22           \n\t"   /* signed high byte of s x low byte of gain */
    "brcc .+2                 \n\t"
    "dec  r25                 \n\t"
    "add  r21, r0             \n\t"
    "adc  r24, r1             \n\t"
    "brcc .+2                 \n\t"
    "inc  r25                 \n\t"
    "mulsu r19, r23           \n\t"
    "add  r24, r0             \n\t"
    "adc  r25, r1             \n\t"   /* r24:r25 = product >> 16 */
    "11:                      \n\t"
    "ldd  r18, Y+46           \n\t" "add  r14, r18            \n\t"
    "ldd  r18, Y+47           \n\t" "adc  r15, r18            \n\t"
    "ldd  r18, Y+48           \n\t" "adc  r16, r18            \n\t"
    "ldd  r18, Y+49           \n\t" "adc  r17, r18            \n\t"
    "add  r24, r16            \n\t"
    "adc  r25, r17            \n\t"
    "ldd  r21, Y+11           \n\t"
    "ldd  r22, Y+6            \n\t"
    "ldd  r23, Y+7            \n\t"
    "tst  r21                 \n\t"   /* W1 is the common case: no wave tests */
    "brne 21f                 \n\t"
    "movw r30, r24            \n\t"
    "mov  r20, r31            \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "rjmp 25f                 \n\t"
    "21: mov  r20, r21        \n\t"
    "andi r20, 6              \n\t"
    "breq 22f                 \n\t"
    "sbrc r25, 1              \n\t"
    "rjmp 29f                 \n\t"
    "22: movw r30, r24        \n\t"
    "sbrs r21, 2              \n\t"
    "rjmp 23f                 \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "sbrc r21, 1              \n\t"
    "andi r31, 1              \n\t"
    "23: mov  r20, r31        \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "25: ldi  r31, 0          \n\t"   /* a = log_sin_[quarter] */
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ls]))  \n\t"
    "sbci r31, hi8(-(%[ls]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "sbrs r21, 0              \n\t"
    "rjmp 24f                 \n\t"
    "lsl  r18                 \n\t"   /* W2: a <<= 1, capped at kZero */
    "rol  r19                 \n\t"
    "ldi  r30, 0x08           \n\t"
    "cpi  r18, 0x5A           \n\t"
    "cpc  r19, r30            \n\t"
    "brlo 24f                 \n\t"
    "ldi  r18, 0x59           \n\t"
    "ldi  r19, 0x08           \n\t"
    "24: add  r18, r22        \n\t"   /* a += attenuation */
    "adc  r19, r23            \n\t"
    "rjmp 26f                 \n\t"
    "29: ldi  r18, 0x59       \n\t"   /* second half of W3-W8: kZero, positive */
    "ldi  r19, 0x08           \n\t"
    "clr  r20                 \n\t"
    "add  r18, r22            \n\t"
    "adc  r19, r23            \n\t"
    "26: cpi  r19, 0x0D       \n\t"   /* a >= kFmSilent: 0 */
    "brsh 28f                 \n\t"
    "mov  r24, r19            \n\t"   /* q = a >> 10; v = exp_[a & 0x3FF] >> 4q */
    "lsr  r24                 \n\t"
    "lsr  r24                 \n\t"
    "andi r19, 0x03           \n\t"
    "movw r30, r18            \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ex]))  \n\t"
    "sbci r31, hi8(-(%[ex]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "tst  r24                 \n\t"
    "breq 32f                 \n\t"
    "cpi  r24, 2              \n\t"
    "brlo 31f                 \n\t"
    "mov  r18, r19            \n\t"   /* >> 8 */
    "clr  r19                 \n\t"
    "cpi  r24, 3              \n\t"
    "brne 32f                 \n\t"
    "swap r18                 \n\t"   /* and >> 4 more: q == 3 */
    "andi r18, 0x0F           \n\t"
    "rjmp 32f                 \n\t"
    "31: swap r18             \n\t"   /* >> 4 */
    "andi r18, 0x0F           \n\t"
    "swap r19                 \n\t"
    "mov  r24, r19            \n\t"
    "andi r24, 0xF0           \n\t"
    "or   r18, r24            \n\t"
    "andi r19, 0x0F           \n\t"
    "32: sbrs r20, 1          \n\t"
    "rjmp 27f                 \n\t"
    "com  r19                 \n\t"   /* negative half */
    "neg  r18                 \n\t"
    "sbci r19, 0xFF           \n\t"
    "rjmp 27f                 \n\t"
    "28: clr  r18             \n\t"
    "clr  r19                 \n\t"
    "27:                      \n\t"
    "std  Y+29, r18           \n\t"
    "std  Y+30, r19           \n\t"
    "ldd  r20, Y+19           \n\t" "std  Y+21, r20           \n\t"
    "ldd  r20, Y+20           \n\t" "std  Y+22, r20           \n\t"
    "std  Y+19, r18           \n\t"
    "std  Y+20, r19           \n\t"
    /* op 3: only op 4 can feed it */
    "clr  r24                 \n\t"
    "clr  r25                 \n\t"
    "ldd  r20, Y+14           \n\t"
    "sbrs r20, 3              \n\t"
    "rjmp 12f                 \n\t"
    "movw r24, r18            \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "12:                      \n\t"
    "ldd  r18, Y+42           \n\t" "add  r10, r18            \n\t"
    "ldd  r18, Y+43           \n\t" "adc  r11, r18            \n\t"
    "ldd  r18, Y+44           \n\t" "adc  r12, r18            \n\t"
    "ldd  r18, Y+45           \n\t" "adc  r13, r18            \n\t"
    "add  r24, r12            \n\t"
    "adc  r25, r13            \n\t"
    "ldd  r21, Y+10           \n\t"
    "ldd  r22, Y+4            \n\t"
    "ldd  r23, Y+5            \n\t"
    "tst  r21                 \n\t"   /* W1 is the common case: no wave tests */
    "brne 21f                 \n\t"
    "movw r30, r24            \n\t"
    "mov  r20, r31            \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "rjmp 25f                 \n\t"
    "21: mov  r20, r21        \n\t"
    "andi r20, 6              \n\t"
    "breq 22f                 \n\t"
    "sbrc r25, 1              \n\t"
    "rjmp 29f                 \n\t"
    "22: movw r30, r24        \n\t"
    "sbrs r21, 2              \n\t"
    "rjmp 23f                 \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "sbrc r21, 1              \n\t"
    "andi r31, 1              \n\t"
    "23: mov  r20, r31        \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "25: ldi  r31, 0          \n\t"   /* a = log_sin_[quarter] */
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ls]))  \n\t"
    "sbci r31, hi8(-(%[ls]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "sbrs r21, 0              \n\t"
    "rjmp 24f                 \n\t"
    "lsl  r18                 \n\t"   /* W2: a <<= 1, capped at kZero */
    "rol  r19                 \n\t"
    "ldi  r30, 0x08           \n\t"
    "cpi  r18, 0x5A           \n\t"
    "cpc  r19, r30            \n\t"
    "brlo 24f                 \n\t"
    "ldi  r18, 0x59           \n\t"
    "ldi  r19, 0x08           \n\t"
    "24: add  r18, r22        \n\t"   /* a += attenuation */
    "adc  r19, r23            \n\t"
    "rjmp 26f                 \n\t"
    "29: ldi  r18, 0x59       \n\t"   /* second half of W3-W8: kZero, positive */
    "ldi  r19, 0x08           \n\t"
    "clr  r20                 \n\t"
    "add  r18, r22            \n\t"
    "adc  r19, r23            \n\t"
    "26: cpi  r19, 0x0D       \n\t"   /* a >= kFmSilent: 0 */
    "brsh 28f                 \n\t"
    "mov  r24, r19            \n\t"   /* q = a >> 10; v = exp_[a & 0x3FF] >> 4q */
    "lsr  r24                 \n\t"
    "lsr  r24                 \n\t"
    "andi r19, 0x03           \n\t"
    "movw r30, r18            \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ex]))  \n\t"
    "sbci r31, hi8(-(%[ex]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "tst  r24                 \n\t"
    "breq 32f                 \n\t"
    "cpi  r24, 2              \n\t"
    "brlo 31f                 \n\t"
    "mov  r18, r19            \n\t"   /* >> 8 */
    "clr  r19                 \n\t"
    "cpi  r24, 3              \n\t"
    "brne 32f                 \n\t"
    "swap r18                 \n\t"   /* and >> 4 more: q == 3 */
    "andi r18, 0x0F           \n\t"
    "rjmp 32f                 \n\t"
    "31: swap r18             \n\t"   /* >> 4 */
    "andi r18, 0x0F           \n\t"
    "swap r19                 \n\t"
    "mov  r24, r19            \n\t"
    "andi r24, 0xF0           \n\t"
    "or   r18, r24            \n\t"
    "andi r19, 0x0F           \n\t"
    "32: sbrs r20, 1          \n\t"
    "rjmp 27f                 \n\t"
    "com  r19                 \n\t"   /* negative half */
    "neg  r18                 \n\t"
    "sbci r19, 0xFF           \n\t"
    "rjmp 27f                 \n\t"
    "28: clr  r18             \n\t"
    "clr  r19                 \n\t"
    "27:                      \n\t"
    "std  Y+27, r18           \n\t"
    "std  Y+28, r19           \n\t"
    /* op 2: op 3 and/or op 4 */
    "clr  r24                 \n\t"
    "clr  r25                 \n\t"
    "ldd  r20, Y+13           \n\t"
    "sbrs r20, 2              \n\t"
    "rjmp 13f                 \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "13: sbrs r20, 3          \n\t"
    "rjmp 14f                 \n\t"
    "ldd  r18, Y+29           \n\t"
    "ldd  r19, Y+30           \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "14: asr  r25             \n\t"
    "ror  r24                 \n\t"
    "ldd  r18, Y+38           \n\t" "add  r6, r18            \n\t"
    "ldd  r18, Y+39           \n\t" "adc  r7, r18            \n\t"
    "ldd  r18, Y+40           \n\t" "adc  r8, r18            \n\t"
    "ldd  r18, Y+41           \n\t" "adc  r9, r18            \n\t"
    "add  r24, r8             \n\t"
    "adc  r25, r9             \n\t"
    "ldd  r21, Y+9            \n\t"
    "ldd  r22, Y+2            \n\t"
    "ldd  r23, Y+3            \n\t"
    "tst  r21                 \n\t"   /* W1 is the common case: no wave tests */
    "brne 21f                 \n\t"
    "movw r30, r24            \n\t"
    "mov  r20, r31            \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "rjmp 25f                 \n\t"
    "21: mov  r20, r21        \n\t"
    "andi r20, 6              \n\t"
    "breq 22f                 \n\t"
    "sbrc r25, 1              \n\t"
    "rjmp 29f                 \n\t"
    "22: movw r30, r24        \n\t"
    "sbrs r21, 2              \n\t"
    "rjmp 23f                 \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "sbrc r21, 1              \n\t"
    "andi r31, 1              \n\t"
    "23: mov  r20, r31        \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "25: ldi  r31, 0          \n\t"   /* a = log_sin_[quarter] */
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ls]))  \n\t"
    "sbci r31, hi8(-(%[ls]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "sbrs r21, 0              \n\t"
    "rjmp 24f                 \n\t"
    "lsl  r18                 \n\t"   /* W2: a <<= 1, capped at kZero */
    "rol  r19                 \n\t"
    "ldi  r30, 0x08           \n\t"
    "cpi  r18, 0x5A           \n\t"
    "cpc  r19, r30            \n\t"
    "brlo 24f                 \n\t"
    "ldi  r18, 0x59           \n\t"
    "ldi  r19, 0x08           \n\t"
    "24: add  r18, r22        \n\t"   /* a += attenuation */
    "adc  r19, r23            \n\t"
    "rjmp 26f                 \n\t"
    "29: ldi  r18, 0x59       \n\t"   /* second half of W3-W8: kZero, positive */
    "ldi  r19, 0x08           \n\t"
    "clr  r20                 \n\t"
    "add  r18, r22            \n\t"
    "adc  r19, r23            \n\t"
    "26: cpi  r19, 0x0D       \n\t"   /* a >= kFmSilent: 0 */
    "brsh 28f                 \n\t"
    "mov  r24, r19            \n\t"   /* q = a >> 10; v = exp_[a & 0x3FF] >> 4q */
    "lsr  r24                 \n\t"
    "lsr  r24                 \n\t"
    "andi r19, 0x03           \n\t"
    "movw r30, r18            \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ex]))  \n\t"
    "sbci r31, hi8(-(%[ex]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "tst  r24                 \n\t"
    "breq 32f                 \n\t"
    "cpi  r24, 2              \n\t"
    "brlo 31f                 \n\t"
    "mov  r18, r19            \n\t"   /* >> 8 */
    "clr  r19                 \n\t"
    "cpi  r24, 3              \n\t"
    "brne 32f                 \n\t"
    "swap r18                 \n\t"   /* and >> 4 more: q == 3 */
    "andi r18, 0x0F           \n\t"
    "rjmp 32f                 \n\t"
    "31: swap r18             \n\t"   /* >> 4 */
    "andi r18, 0x0F           \n\t"
    "swap r19                 \n\t"
    "mov  r24, r19            \n\t"
    "andi r24, 0xF0           \n\t"
    "or   r18, r24            \n\t"
    "andi r19, 0x0F           \n\t"
    "32: sbrs r20, 1          \n\t"
    "rjmp 27f                 \n\t"
    "com  r19                 \n\t"   /* negative half */
    "neg  r18                 \n\t"
    "sbci r19, 0xFF           \n\t"
    "rjmp 27f                 \n\t"
    "28: clr  r18             \n\t"
    "clr  r19                 \n\t"
    "27:                      \n\t"
    "std  Y+25, r18           \n\t"
    "std  Y+26, r19           \n\t"
    /* op 1: op 2, 3, 4 */
    "clr  r24                 \n\t"
    "clr  r25                 \n\t"
    "ldd  r20, Y+12           \n\t"
    "sbrs r20, 1              \n\t"
    "rjmp 15f                 \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "15: sbrs r20, 2          \n\t"
    "rjmp 16f                 \n\t"
    "ldd  r18, Y+27           \n\t"
    "ldd  r19, Y+28           \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "16: sbrs r20, 3          \n\t"
    "rjmp 17f                 \n\t"
    "ldd  r18, Y+29           \n\t"
    "ldd  r19, Y+30           \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "17: asr  r25             \n\t"
    "ror  r24                 \n\t"
    "ldd  r18, Y+34           \n\t" "add  r2, r18            \n\t"
    "ldd  r18, Y+35           \n\t" "adc  r3, r18            \n\t"
    "ldd  r18, Y+36           \n\t" "adc  r4, r18            \n\t"
    "ldd  r18, Y+37           \n\t" "adc  r5, r18            \n\t"
    "add  r24, r4             \n\t"
    "adc  r25, r5             \n\t"
    "ldd  r21, Y+8            \n\t"
    "ldd  r22, Y+0            \n\t"
    "ldd  r23, Y+1            \n\t"
    "tst  r21                 \n\t"   /* W1 is the common case: no wave tests */
    "brne 21f                 \n\t"
    "movw r30, r24            \n\t"
    "mov  r20, r31            \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "rjmp 25f                 \n\t"
    "21: mov  r20, r21        \n\t"
    "andi r20, 6              \n\t"
    "breq 22f                 \n\t"
    "sbrc r25, 1              \n\t"
    "rjmp 29f                 \n\t"
    "22: movw r30, r24        \n\t"
    "sbrs r21, 2              \n\t"
    "rjmp 23f                 \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "sbrc r21, 1              \n\t"
    "andi r31, 1              \n\t"
    "23: mov  r20, r31        \n\t"
    "sbrc r31, 0              \n\t"
    "com  r30                 \n\t"
    "25: ldi  r31, 0          \n\t"   /* a = log_sin_[quarter] */
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ls]))  \n\t"
    "sbci r31, hi8(-(%[ls]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "sbrs r21, 0              \n\t"
    "rjmp 24f                 \n\t"
    "lsl  r18                 \n\t"   /* W2: a <<= 1, capped at kZero */
    "rol  r19                 \n\t"
    "ldi  r30, 0x08           \n\t"
    "cpi  r18, 0x5A           \n\t"
    "cpc  r19, r30            \n\t"
    "brlo 24f                 \n\t"
    "ldi  r18, 0x59           \n\t"
    "ldi  r19, 0x08           \n\t"
    "24: add  r18, r22        \n\t"   /* a += attenuation */
    "adc  r19, r23            \n\t"
    "rjmp 26f                 \n\t"
    "29: ldi  r18, 0x59       \n\t"   /* second half of W3-W8: kZero, positive */
    "ldi  r19, 0x08           \n\t"
    "clr  r20                 \n\t"
    "add  r18, r22            \n\t"
    "adc  r19, r23            \n\t"
    "26: cpi  r19, 0x0D       \n\t"   /* a >= kFmSilent: 0 */
    "brsh 28f                 \n\t"
    "mov  r24, r19            \n\t"   /* q = a >> 10; v = exp_[a & 0x3FF] >> 4q */
    "lsr  r24                 \n\t"
    "lsr  r24                 \n\t"
    "andi r19, 0x03           \n\t"
    "movw r30, r18            \n\t"
    "lsl  r30                 \n\t"
    "rol  r31                 \n\t"
    "subi r30, lo8(-(%[ex]))  \n\t"
    "sbci r31, hi8(-(%[ex]))  \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z              \n\t"
    "tst  r24                 \n\t"
    "breq 32f                 \n\t"
    "cpi  r24, 2              \n\t"
    "brlo 31f                 \n\t"
    "mov  r18, r19            \n\t"   /* >> 8 */
    "clr  r19                 \n\t"
    "cpi  r24, 3              \n\t"
    "brne 32f                 \n\t"
    "swap r18                 \n\t"   /* and >> 4 more: q == 3 */
    "andi r18, 0x0F           \n\t"
    "rjmp 32f                 \n\t"
    "31: swap r18             \n\t"   /* >> 4 */
    "andi r18, 0x0F           \n\t"
    "swap r19                 \n\t"
    "mov  r24, r19            \n\t"
    "andi r24, 0xF0           \n\t"
    "or   r18, r24            \n\t"
    "andi r19, 0x0F           \n\t"
    "32: sbrs r20, 1          \n\t"
    "rjmp 27f                 \n\t"
    "com  r19                 \n\t"   /* negative half */
    "neg  r18                 \n\t"
    "sbci r19, 0xFF           \n\t"
    "rjmp 27f                 \n\t"
    "28: clr  r18             \n\t"
    "clr  r19                 \n\t"
    "27:                      \n\t"
    /* sum of carriers: op 1 is in r18:r19 */
    "ldd  r20, Y+16           \n\t"
    "clr  r24                 \n\t"
    "clr  r25                 \n\t"
    "sbrs r20, 0              \n\t"
    "rjmp 30f                 \n\t"
    "movw r24, r18            \n\t"
    "30: sbrs r20, 1          \n\t"
    "rjmp 31f                 \n\t"
    "ldd  r18, Y+25           \n\t"
    "ldd  r19, Y+26           \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "31: sbrs r20, 2          \n\t"
    "rjmp 32f                 \n\t"
    "ldd  r18, Y+27           \n\t"
    "ldd  r19, Y+28           \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "32: sbrs r20, 3          \n\t"
    "rjmp 33f                 \n\t"
    "ldd  r18, Y+29           \n\t"
    "ldd  r19, Y+30           \n\t"
    "add  r24, r18            \n\t"
    "adc  r25, r19            \n\t"
    "33:                      \n\t"
    /* out >>= 2; clip to -2048..2047; + 2048; store */
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "ldi  r18, 0x00           \n\t"
    "ldi  r19, 0x08           \n\t"
    "cp   r24, r18            \n\t"
    "cpc  r25, r19            \n\t"
    "brlt 34f                 \n\t"
    "ldi  r24, 0xFF           \n\t"
    "ldi  r25, 0x07           \n\t"
    "34: ldi  r18, 0x00       \n\t"
    "ldi  r19, 0xF8           \n\t"
    "cp   r24, r18            \n\t"
    "cpc  r25, r19            \n\t"
    "brge 35f                 \n\t"
    "ldi  r24, 0x00           \n\t"
    "ldi  r25, 0xF8           \n\t"
    "35: subi r25, 0xF8       \n\t"
    "st   X+, r24             \n\t"
    "st   X+, r25             \n\t"
    "ldd  r20, Y+31           \n\t"
    "dec  r20                 \n\t"
    "std  Y+31, r20           \n\t"
    "breq 40f                 \n\t"
    "rjmp 10b                 \n\t"
    "40:                      \n\t"
    /* phases back to op_ */
    "ldd  r30, Y+32           \n\t"
    "ldd  r31, Y+33           \n\t"
    "st   Z+, r2              \n\t" "st   Z+, r3              \n\t"
    "st   Z+, r4              \n\t" "st   Z+, r5              \n\t"
    "adiw r30, 4              \n\t"
    "st   Z+, r6              \n\t" "st   Z+, r7              \n\t"
    "st   Z+, r8              \n\t" "st   Z+, r9              \n\t"
    "adiw r30, 4              \n\t"
    "st   Z+, r10             \n\t" "st   Z+, r11             \n\t"
    "st   Z+, r12             \n\t" "st   Z+, r13             \n\t"
    "adiw r30, 4              \n\t"
    "st   Z+, r14             \n\t" "st   Z+, r15             \n\t"
    "st   Z+, r16             \n\t" "st   Z+, r17             \n\t"
    "eor  r1, r1              \n\t"
    "pop  r29                 \n\t"
    "pop  r28                 \n\t"
    : "+x" (buffer)
    : [pp] "i" (&params_), [ls] "i" (log_sin_), [ex] "i" (exp_)
    : "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12",
      "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21", "r22",
      "r23", "r24", "r25", "r30", "r31", "r0", "r1", "cc", "memory");
  feedback_[0] = params.fb[0];
  feedback_[1] = params.fb[1];
}
#endif  // __AVR__

}  // namespace ambika

#endif  // VOICECARD_FM4OP_H_
