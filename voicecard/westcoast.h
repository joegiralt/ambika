// Copyright 2026 Ambika contributors.
//
// West coast (Buchla-style) oscillator with wavefolder and FM.

#ifndef VOICECARD_WESTCOAST_H_
#define VOICECARD_WESTCOAST_H_

#include "avrlib/base.h"
#include "avrlib/op.h"
#include "voicecard/fm4op.h"
#include "voicecard/resources.h"

using namespace avrlib;

namespace ambika {

enum WcWaveform {
  WC_WAVE_SINE,
  WC_WAVE_TRIANGLE,
  WC_WAVE_LAST
};

// Patch field mapping for west coast mode:
// Page 1:
//   osc[0].parameter (1)  = fold depth
//   osc[0].range     (2)  = pitch range
//   osc[0].detune    (3)  = fine tune
//   osc[1].shape     (4)  = base waveform
//   osc[1].parameter (5)  = symmetry
//   osc[1].range     (6)  = FM depth
//   osc[1].detune    (7)  = FM ratio
//   mix_balance      (8)  = bias
//   mix_op           (9)  = drive
//   mix_parameter    (10) = color
// Page 2:
//   mix_sub_osc_shape(11) = fold stages (not used by the engine)
//   mix_sub_osc      (12) = input gain
//   mix_noise        (13) = envelope-to-fold amount
//   mix_fuzz         (14) = sub-harmonic level
//   mix_crush        (15) = sync amount

class WestCoast {
 public:
  WestCoast() { }

  void Init() {
    phase_ = mod_phase_ = sub_phase_ = sync_phase_ = 0;
    lp_state1_ = lp_state2_ = 0;
  }

  // Buchla-style wavefolder on a signed 16-bit sample: reflect at full
  // scale, as many times as needed, in constant time (a triangle wave of the
  // input with period 2^17).
  static inline int16_t Fold(int32_t x) {
    uint32_t u = static_cast<uint32_t>(x + 32768) & 0x1FFFF;
    if (u & 0x10000) {
      u = 0x1FFFF - u;
    }
    return static_cast<int16_t>(u - 32768);
  }

  // Everything that is constant over a block, computed once.
  struct Setup {
    uint32_t mod_increment;
    uint32_t sub_increment;
    uint32_t sync_increment;
    int16_t half_bias;
    int16_t gain_positive;
    int16_t gain_negative;
    uint8_t lp;
  };
  static void SetupBlock(
      Setup* p, uint8_t fold_depth, uint8_t symmetry, uint8_t bias,
      int8_t fm_ratio, uint8_t drive, uint8_t color, uint8_t input_gain,
      uint8_t env_to_fold, uint8_t sync_amount, uint8_t env_value,
      uint32_t phase_increment) {
    // FM modulator increment.
    uint32_t mod_increment;
    if (fm_ratio <= 0) {
      uint8_t shift = 1 - fm_ratio;
      if (shift > 24) shift = 24;
      mod_increment = phase_increment >> shift;
    } else {
      mod_increment = phase_increment * static_cast<uint8_t>(fm_ratio);
    }
    uint32_t sub_increment = phase_increment >> 1;
    uint32_t sync_increment = sync_amount ?
        phase_increment + ((phase_increment >> 5) * sync_amount) : 0;

    // Fold amount: depth, plus drive, input gain and the envelope.
    uint16_t amount = fold_depth + (drive >> 1) + (input_gain >> 1);
    if (env_to_fold) {
      amount += U8U8MulShift8(env_value, env_to_fold);
    }
    if (amount > 127) amount = 127;
    // Quadratic gain: 0 -> 1x, 64 -> ~9x, 127 -> ~33x. In 1/128ths, with
    // the input gain folded in.
    // At most ~8290 (~12400 with symmetry): fits int16, so the per-sample
    // multiply below can use avr-gcc's fast 16x16->32 routine.
    int16_t gain = 128 + ((amount * amount) >> 2);
    if (input_gain) {
      gain = (static_cast<uint32_t>(gain) * (128 + input_gain)) >> 7;
    }
    // Bias: a DC offset added before the gain, so at high fold it slides the
    // whole fold pattern (64 = centered, up to +/-half scale). At half scale
    // here, like the sample below, so the sum fits 16 bits.
    int16_t half_bias = (static_cast<int16_t>(bias) - 64) * 128;
    // Symmetry: different gains for the positive and negative halves
    // (0.5x-1.5x), so the waveform leans to one side (64 = symmetric).
    int16_t lean = static_cast<int16_t>(symmetry) - 64;
    int16_t gain_positive = gain + ((static_cast<int32_t>(gain) * lean) >> 7);
    int16_t gain_negative = gain - ((static_cast<int32_t>(gain) * lean) >> 7);

    // Color: 2-pole low-pass after the folder; 124+ is bypassed.
    uint8_t lp = 16 + (color << 1) > 255 ? 255 : 16 + (color << 1);


    p->mod_increment = mod_increment;
    p->sub_increment = sub_increment;
    p->sync_increment = sync_increment;
    p->half_bias = half_bias;
    p->gain_positive = gain_positive;
    p->gain_negative = gain_negative;
    p->lp = lp;
  }

  // phase_increment: 16.8 fixed point (ComputePhaseIncrementFine).
  // RenderC is the reference and the host version; on the AVR RenderAsm
  // does the same work in one assembly loop (bench/ BENCH_WCTEST compares
  // them over 64 feature combinations).
  void Render(
      uint8_t base_waveform, uint8_t fold_depth, uint8_t symmetry,
      uint8_t bias, uint8_t fm_depth, int8_t fm_ratio, uint8_t drive,
      uint8_t color, uint8_t input_gain, uint8_t env_to_fold,
      uint8_t sub_level, uint8_t sync_amount, uint8_t env_value,
      uint32_t phase_increment, uint16_t* buffer, uint8_t size) {
#ifdef __AVR__
    RenderAsm(base_waveform, fold_depth, symmetry, bias, fm_depth, fm_ratio,
              drive, color, input_gain, env_to_fold, sub_level, sync_amount,
              env_value, phase_increment, buffer, size);
#else
    RenderC(base_waveform, fold_depth, symmetry, bias, fm_depth, fm_ratio,
            drive, color, input_gain, env_to_fold, sub_level, sync_amount,
            env_value, phase_increment, buffer, size);
#endif
  }

#ifdef __AVR__
  void RenderAsm(
      uint8_t base_waveform, uint8_t fold_depth, uint8_t symmetry,
      uint8_t bias, uint8_t fm_depth, int8_t fm_ratio, uint8_t drive,
      uint8_t color, uint8_t input_gain, uint8_t env_to_fold,
      uint8_t sub_level, uint8_t sync_amount, uint8_t env_value,
      uint32_t phase_increment, uint16_t* buffer, uint8_t size);
#endif

  void RenderC(
      uint8_t base_waveform,
      uint8_t fold_depth,
      uint8_t symmetry,
      uint8_t bias,
      uint8_t fm_depth,
      int8_t fm_ratio,
      uint8_t drive,
      uint8_t color,
      uint8_t input_gain,
      uint8_t env_to_fold,
      uint8_t sub_level,
      uint8_t sync_amount,
      uint8_t env_value,
      uint32_t phase_increment,
      uint16_t* buffer,
      uint8_t size) {

    Setup p;
    SetupBlock(&p, fold_depth, symmetry, bias, fm_ratio, drive, color,
               input_gain, env_to_fold, sync_amount, env_value,
               phase_increment);
    const uint32_t mod_increment = p.mod_increment;
    const uint32_t sub_increment = p.sub_increment;
    const uint32_t sync_increment = p.sync_increment;
    const int16_t half_bias = p.half_bias;
    const int16_t gain_positive = p.gain_positive;
    const int16_t gain_negative = p.gain_negative;
    const uint8_t lp = p.lp;

    while (size--) {
      mod_phase_ += mod_increment;
      uint16_t fm = 0;
      if (fm_depth) {
        int16_t m = static_cast<int16_t>(
            InterpolateSine16(mod_phase_ >> 8) - 32768);
        fm = S16U8MulShift8(m, fm_depth);
      }

      if (sync_amount) {
        uint32_t old = sync_phase_;
        sync_phase_ += sync_increment;
        if (static_cast<uint16_t>(sync_phase_ >> 8) <
            static_cast<uint16_t>(old >> 8)) {
          phase_ = 0;
        }
      }

      phase_ += phase_increment;
      uint16_t p = (phase_ >> 8) + fm;
      int16_t sample;
      if (base_waveform == WC_WAVE_TRIANGLE) {
        // Down from full scale over the first half, back up over the second
        // (unsigned 16-bit math: AVR's int is 16 bits).
        uint16_t r = (p & 0x7FFF) << 1;
        sample = static_cast<int16_t>(
            (p & 0x8000) ? static_cast<uint16_t>(r - 32768u)
                         : static_cast<uint16_t>(32767u - r));
      } else {
        sample = static_cast<int16_t>(InterpolateSine16(p) - 32768);
      }

      // (gain in 1/128ths: sample * gain >> 7, in 32 bits.)
      int16_t x = (sample >> 1) + half_bias;
      int16_t g = x >= 0 ? gain_positive : gain_negative;
      int16_t y = Fold((static_cast<int32_t>(x) * static_cast<int32_t>(g)) >> 6);

      if (color < 124) {
        // Written as a - a*k + b*k so nothing overflows 16 bits.
        lp_state1_ += S16U8MulShift8(y, lp) - S16U8MulShift8(lp_state1_, lp);
        lp_state2_ += S16U8MulShift8(lp_state1_, lp) -
            S16U8MulShift8(lp_state2_, lp);
        y = lp_state2_;
      }

      if (sub_level) {
        sub_phase_ += sub_increment;
        // Mixed in after the folder at up to half level: the 8-bit sine
        // is plenty.
        int16_t sub = (static_cast<int16_t>(InterpolateSample(
            wav_res_sine, sub_phase_ >> 8)) - 128) * 256;
        y = y - S16U8MulShift8(y, sub_level) + S16U8MulShift8(sub, sub_level);
      }

      *buffer++ = (y >> 4) + 2048;  // 12-bit DAC sample
    }
  }

 private:
  uint32_t phase_;
  uint32_t mod_phase_;
  uint32_t sub_phase_;
  uint32_t sync_phase_;
  int16_t lp_state1_;
  int16_t lp_state2_;

  DISALLOW_COPY_AND_ASSIGN(WestCoast);
};


#ifdef __AVR__
// Block parameters for RenderAsm, addressed from Y. Lives in the FM render's
// static block (only one engine renders at a time): keep it <= 50 bytes.
struct WcRenderParams {
  uint32_t inc;         // 0
  uint32_t mod_inc;     // 4
  uint32_t sub_inc;     // 8
  uint32_t sync_inc;    // 12
  int16_t half_bias;    // 16
  int16_t gain_pos;     // 18
  int16_t gain_neg;     // 20
  uint8_t lp;           // 22
  uint8_t flags;        // 23  bit0 fm, bit1 sync, bit2 lp, bit3 sub, bit4 triangle
  uint8_t fm_depth;     // 24
  uint8_t sub_level;    // 25
  int16_t lp1;          // 26
  int16_t lp2;          // 28
  uint32_t sync_phase;  // 30
  uint8_t count;        // 34
};

// Register use: r2-r5 phase, r6-r9 modulator phase, r10-r13 sub phase,
// r14 flags, r15 count, r16 lp, r17 sub level, r23 fm depth, r18-r25
// scratch, X output, Y params, Z tables. r1 is zero between multiplies.
inline __attribute__((noinline)) void WestCoast::RenderAsm(
    uint8_t base_waveform, uint8_t fold_depth, uint8_t symmetry,
    uint8_t bias, uint8_t fm_depth, int8_t fm_ratio, uint8_t drive,
    uint8_t color, uint8_t input_gain, uint8_t env_to_fold,
    uint8_t sub_level, uint8_t sync_amount, uint8_t env_value,
    uint32_t phase_increment, uint16_t* buffer, uint8_t size) {
  Setup s;
  SetupBlock(&s, fold_depth, symmetry, bias, fm_ratio, drive, color,
             input_gain, env_to_fold, sync_amount, env_value,
             phase_increment);
  WcRenderParams& p = *reinterpret_cast<WcRenderParams*>(&Fm4Op::params_);
  p.inc = phase_increment;
  p.mod_inc = s.mod_increment;
  p.sub_inc = s.sub_increment;
  p.sync_inc = s.sync_increment;
  p.half_bias = s.half_bias;
  p.gain_pos = s.gain_positive;
  p.gain_neg = s.gain_negative;
  p.lp = s.lp;
  p.flags = (fm_depth ? 1 : 0) | (sync_amount ? 2 : 0) |
            (color < 124 ? 4 : 0) | (sub_level ? 8 : 0) |
            (base_waveform == WC_WAVE_TRIANGLE ? 16 : 0);
  p.fm_depth = fm_depth;
  p.sub_level = sub_level;
  p.lp1 = lp_state1_;
  p.lp2 = lp_state2_;
  p.sync_phase = sync_phase_;
  p.count = size;
  register uint32_t ph asm("r2") = phase_;
  register uint32_t mph asm("r6") = mod_phase_;
  register uint32_t sph asm("r10") = sub_phase_;
  asm volatile(
    "push r28                 \n\t"
    "push r29                 \n\t"
    "ldi  r28, lo8(%[pp])     \n\t"
    "ldi  r29, hi8(%[pp])     \n\t"
    "ldd  r14, Y+23           \n\t"   /* flags */
    "ldd  r15, Y+34           \n\t"   /* count */
    "ldd  r16, Y+22           \n\t"   /* lp */
    "ldd  r17, Y+25           \n\t"   /* sub level */
    "1:                       \n\t"
    /* mod_phase += mod_inc */
    "ldd  r18, Y+4            \n\t" "add  r6, r18             \n\t"
    "ldd  r18, Y+5            \n\t" "adc  r7, r18             \n\t"
    "ldd  r18, Y+6            \n\t" "adc  r8, r18             \n\t"
    "ldd  r18, Y+7            \n\t" "adc  r9, r18             \n\t"
    /* fm = fm_depth ? S16U8MulShift8(sine16(mod_phase >> 8) - 32768, depth) : 0 */
    "clr  r24                 \n\t"
    "clr  r25                 \n\t"
    "sbrs r14, 0              \n\t"
    "rjmp 10f                 \n\t"
    "mov  r24, r7             \n\t"   /* r24:r25 = mod_phase bytes 1,2 */
    "mov  r25, r8             \n\t"
    "movw r30, r24        \n\t"   /* Z = &sine16[ph >> 7] */
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "andi r30, 0xFE           \n\t"
    "subi r30, lo8(-(%[s16])) \n\t"
    "sbci r31, hi8(-(%[s16])) \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z+             \n\t"
    "lpm  r20, Z+             \n\t"
    "lpm  r21, Z              \n\t"
    "sub  r20, r18            \n\t"   /* diff = b - a */
    "sbc  r21, r19            \n\t"
    "mov  r22, r24        \n\t"   /* frac = (ph << 1) & 0xFE */
    "lsl  r22                 \n\t"
    "eor  r25, r25            \n\t"   /* S16U8MulShift8(diff, frac) */
    "mul  r20, r22            \n\t"
    "mov  r24, r1             \n\t"
    "mulsu r21, r22           \n\t"
    "add  r24, r0             \n\t"
    "adc  r25, r1             \n\t"
    "add  r18, r24            \n\t"
    "adc  r19, r25            \n\t"
    "eor  r1, r1              \n\t"
    "subi r19, 0x80           \n\t"   /* - 32768 */
    "ldd  r23, Y+24           \n\t"
    "eor  r25, r25   \n\t"
    "mul  r18, r23            \n\t"
    "mov  r24, r1         \n\t"
    "mulsu r19, r23           \n\t"
    "add  r24, r0         \n\t"
    "adc  r25, r1         \n\t"
    "eor  r1, r1              \n\t"
    "10:                      \n\t"
    /* sync: old = sync_phase; sync_phase += inc; if (hi16(new) < hi16(old)) phase = 0 */
    "sbrs r14, 1              \n\t"
    "rjmp 11f                 \n\t"
    "ldd  r18, Y+31           \n\t"
    "ldd  r19, Y+32           \n\t"
    "ldd  r20, Y+30           \n\t"
    "ldd  r21, Y+31           \n\t"
    "ldd  r22, Y+32           \n\t"
    "ldd  r23, Y+33           \n\t"
    "ldd  r0, Y+12            \n\t" "add  r20, r0             \n\t"
    "ldd  r0, Y+13            \n\t" "adc  r21, r0             \n\t"
    "ldd  r0, Y+14            \n\t" "adc  r22, r0             \n\t"
    "ldd  r0, Y+15            \n\t" "adc  r23, r0             \n\t"
    "std  Y+30, r20           \n\t"
    "std  Y+31, r21           \n\t"
    "std  Y+32, r22           \n\t"
    "std  Y+33, r23           \n\t"
    "cp   r21, r18            \n\t"
    "cpc  r22, r19            \n\t"
    "brsh 11f                 \n\t"
    "clr  r2                  \n\t"
    "clr  r3                  \n\t"
    "clr  r4                  \n\t"
    "clr  r5                  \n\t"
    "11:                      \n\t"
    /* phase += inc; p = (phase >> 8) + fm */
    "ldd  r18, Y+0            \n\t" "add  r2, r18             \n\t"
    "ldd  r18, Y+1            \n\t" "adc  r3, r18             \n\t"
    "ldd  r18, Y+2            \n\t" "adc  r4, r18             \n\t"
    "ldd  r18, Y+3            \n\t" "adc  r5, r18             \n\t"
    "add  r24, r3             \n\t"
    "adc  r25, r4             \n\t"
    /* sample */
    "sbrs r14, 4              \n\t"
    "rjmp 12f                 \n\t"
    "movw r18, r24            \n\t"   /* triangle: r = p << 1 */
    "lsl  r18                 \n\t"
    "rol  r19                 \n\t"
    "sbrc r25, 7              \n\t"
    "rjmp 13f                 \n\t"
    "com  r18                 \n\t"   /* 32767 - r = ~r - 32768 */
    "com  r19                 \n\t"
    "13: subi r19, 0x80       \n\t"   /* r - 32768 (or the above) */
    "rjmp 14f                 \n\t"
    "12:                      \n\t"
    "movw r30, r24        \n\t"   /* Z = &sine16[ph >> 7] */
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "lsr  r31                 \n\t" "ror  r30                 \n\t"
    "andi r30, 0xFE           \n\t"
    "subi r30, lo8(-(%[s16])) \n\t"
    "sbci r31, hi8(-(%[s16])) \n\t"
    "lpm  r18, Z+             \n\t"
    "lpm  r19, Z+             \n\t"
    "lpm  r20, Z+             \n\t"
    "lpm  r21, Z              \n\t"
    "sub  r20, r18            \n\t"   /* diff = b - a */
    "sbc  r21, r19            \n\t"
    "mov  r22, r24        \n\t"   /* frac = (ph << 1) & 0xFE */
    "lsl  r22                 \n\t"
    "eor  r25, r25            \n\t"   /* S16U8MulShift8(diff, frac) */
    "mul  r20, r22            \n\t"
    "mov  r24, r1             \n\t"
    "mulsu r21, r22           \n\t"
    "add  r24, r0             \n\t"
    "adc  r25, r1             \n\t"
    "add  r18, r24            \n\t"
    "adc  r19, r25            \n\t"
    "eor  r1, r1              \n\t"
    "subi r19, 0x80           \n\t"   /* - 32768 */
    "14:                      \n\t"
    /* x = (sample >> 1) + half_bias; g = x >= 0 ? gain_pos : gain_neg */
    "asr  r19                 \n\t"
    "ror  r18                 \n\t"
    "ldd  r20, Y+16           \n\t"
    "ldd  r21, Y+17           \n\t"
    "add  r18, r20            \n\t"
    "adc  r19, r21            \n\t"
    "ldd  r20, Y+18           \n\t"
    "ldd  r21, Y+19           \n\t"
    "sbrs r19, 7              \n\t"
    "rjmp 15f                 \n\t"
    "ldd  r20, Y+20           \n\t"
    "ldd  r21, Y+21           \n\t"
    "15:                      \n\t"
    /* bytes 0-2 of x * g (x signed, g positive) */
    "mul  r18, r20            \n\t"
    "movw r24, r0             \n\t"
    "clr  r22                 \n\t"
    "mul  r18, r21            \n\t"
    "add  r25, r0             \n\t"
    "adc  r22, r1             \n\t"
    "mulsu r19, r20           \n\t"
    "add  r25, r0             \n\t"
    "adc  r22, r1             \n\t"
    "mulsu r19, r21           \n\t"
    "add  r22, r0             \n\t"
    "eor  r1, r1              \n\t"
    /* y = Fold(product >> 6): v = bits 6-22; u = (v + 32768) mod 2^17; */
    /* mirror if bit 16; y = u - 32768 */
    "lsr  r22                 \n\t" "ror  r25                 \n\t" "ror  r24                 \n\t"
    "lsr  r22                 \n\t" "ror  r25                 \n\t" "ror  r24                 \n\t"
    "lsr  r22                 \n\t" "ror  r25                 \n\t" "ror  r24                 \n\t"
    "lsr  r22                 \n\t" "ror  r25                 \n\t" "ror  r24                 \n\t"
    "lsr  r22                 \n\t" "ror  r25                 \n\t" "ror  r24                 \n\t"
    "lsr  r22                 \n\t" "ror  r25                 \n\t" "ror  r24                 \n\t"
    "subi r25, 0x80           \n\t"   /* + 32768 into the low 16 bits ... */
    "sbci r22, 0xFF           \n\t"   /* ... carrying into bit 16 */
    "sbrs r22, 0              \n\t"
    "rjmp 16f                 \n\t"
    "com  r24                 \n\t"   /* 0x1FFFF - u */
    "com  r25                 \n\t"
    "16: subi r25, 0x80       \n\t"   /* - 32768 */
    "movw r18, r24            \n\t"   /* y */
    /* two-pole low-pass */
    "sbrs r14, 2              \n\t"
    "rjmp 17f                 \n\t"
    "ldd  r20, Y+26           \n\t"
    "ldd  r21, Y+27           \n\t"
    "eor  r25, r25   \n\t"
    "mul  r18, r16            \n\t"
    "mov  r24, r1         \n\t"
    "mulsu r19, r16           \n\t"
    "add  r24, r0         \n\t"
    "adc  r25, r1         \n\t"
    "eor  r23, r23   \n\t"
    "mul  r20, r16            \n\t"
    "mov  r22, r1         \n\t"
    "mulsu r21, r16           \n\t"
    "add  r22, r0         \n\t"
    "adc  r23, r1         \n\t"
    "sub  r24, r22            \n\t"
    "sbc  r25, r23            \n\t"
    "add  r20, r24            \n\t"   /* lp1 += (y - lp1) * k */
    "adc  r21, r25            \n\t"
    "std  Y+26, r20           \n\t"
    "std  Y+27, r21           \n\t"
    "ldd  r18, Y+28           \n\t"
    "ldd  r19, Y+29           \n\t"
    "eor  r25, r25   \n\t"
    "mul  r20, r16            \n\t"
    "mov  r24, r1         \n\t"
    "mulsu r21, r16           \n\t"
    "add  r24, r0         \n\t"
    "adc  r25, r1         \n\t"
    "eor  r23, r23   \n\t"
    "mul  r18, r16            \n\t"
    "mov  r22, r1         \n\t"
    "mulsu r19, r16           \n\t"
    "add  r22, r0         \n\t"
    "adc  r23, r1         \n\t"
    "sub  r24, r22            \n\t"
    "sbc  r25, r23            \n\t"
    "add  r18, r24            \n\t"   /* lp2 += (lp1 - lp2) * k; y = lp2 */
    "adc  r19, r25            \n\t"
    "std  Y+28, r18           \n\t"
    "std  Y+29, r19           \n\t"
    "eor  r1, r1              \n\t"
    "17:                      \n\t"
    /* sub oscillator */
    "sbrs r14, 3              \n\t"
    "rjmp 18f                 \n\t"
    "ldd  r20, Y+8            \n\t" "add  r10, r20            \n\t"
    "ldd  r20, Y+9            \n\t" "adc  r11, r20            \n\t"
    "ldd  r20, Y+10           \n\t" "adc  r12, r20            \n\t"
    "ldd  r20, Y+11           \n\t" "adc  r13, r20            \n\t"
    "ldi  r30, lo8(%[s8])     \n\t"   /* InterpolateSample(sine, sub_phase >> 8) */
    "ldi  r31, hi8(%[s8])     \n\t"
    "add  r30, r12            \n\t"
    "adc  r31, r1             \n\t"
    "mov  r22, r11            \n\t"
    "lpm  r24, Z+             \n\t"
    "lpm  r1, Z               \n\t"
    "mul  r22, r1             \n\t"
    "movw r30, r0             \n\t"
    "com  r22                 \n\t"
    "mul  r22, r24            \n\t"
    "add  r30, r0             \n\t"
    "adc  r31, r1             \n\t"
    "mov  r22, r31            \n\t"   /* s */
    "subi r22, 0x80           \n\t"   /* sub = (s - 128) << 8: high byte only */
    "eor  r25, r25   \n\t"
    "mul  r18, r17            \n\t"
    "mov  r24, r1         \n\t"
    "mulsu r19, r17           \n\t"
    "add  r24, r0         \n\t"
    "adc  r25, r1         \n\t"
    "sub  r18, r24            \n\t"   /* y -= y * level */
    "sbc  r19, r25            \n\t"
    "mulsu r22, r17           \n\t"   /* + (sub * level) >> 8: the low byte of sub is 0 */
    "add  r18, r0             \n\t"
    "adc  r19, r1             \n\t"
    "eor  r1, r1              \n\t"
    "18:                      \n\t"
    /* out = (y >> 4) + 2048 */
    "asr  r19                 \n\t" "ror  r18                 \n\t"
    "asr  r19                 \n\t" "ror  r18                 \n\t"
    "asr  r19                 \n\t" "ror  r18                 \n\t"
    "asr  r19                 \n\t" "ror  r18                 \n\t"
    "subi r19, 0xF8           \n\t"
    "st   X+, r18             \n\t"
    "st   X+, r19             \n\t"
    "dec  r15                 \n\t"
    "breq 19f                 \n\t"
    "rjmp 1b                  \n\t"
    "19:                      \n\t"
    "pop  r29                 \n\t"
    "pop  r28                 \n\t"
    : "+x" (buffer),
      [ph] "+r" (ph), [mph] "+r" (mph), [sph] "+r" (sph)
    : [pp] "i" (&Fm4Op::params_), [s16] "i" (wav_res_sine16),
      [s8] "i" (wav_res_sine)
    : "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
      "r24", "r25", "r30", "r31", "r0", "r1", "cc", "memory");
  phase_ = ph;
  mod_phase_ = mph;
  sub_phase_ = sph;
  lp_state1_ = p.lp1;
  lp_state2_ = p.lp2;
  sync_phase_ = p.sync_phase;
}
#endif  // __AVR__

}  // namespace ambika

#endif  // VOICECARD_WESTCOAST_H_
