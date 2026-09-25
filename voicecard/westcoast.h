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

  // phase_increment: 16.8 fixed point (ComputePhaseIncrementFine).
  void Render(
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

}  // namespace ambika

#endif  // VOICECARD_WESTCOAST_H_
