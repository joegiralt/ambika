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
//   osc[0].shape     (0)  = WAVEFORM_WESTCOAST
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
//   mix_sub_osc_shape(11) = fold stages (1-6)
//   mix_sub_osc      (12) = input gain
//   mix_noise        (13) = envelope-to-fold amount
//   mix_fuzz         (14) = sub-harmonic level
//   mix_crush        (15) = sync amount
//   padding[1]       (105)= pitch range (moved)

class WestCoast {
 public:
  WestCoast() { }

  void Init() {
    phase_ = 0;
    mod_phase_ = 0;
    sub_phase_ = 0;
    sync_phase_ = 0;
    lp_state1_ = 128;
    lp_state2_ = 128;
  }

  // Buchla-style wavefolder.
  // Quadratic gain curve: gentle at low settings, extreme at high.
  // fold_depth 0 = clean sine, 64 = ~4 folds, 127 = ~16 folds.
  static inline uint8_t Fold(int16_t input, uint8_t fold_depth,
                             uint8_t symmetry, uint8_t bias) {
    // Bias and symmetry: DC offsets that shift the fold point.
    // Symmetry creates even harmonics, bias shifts the waveform.
    input += static_cast<int8_t>(bias - 64);
    if (symmetry != 64) {
      input += (static_cast<int16_t>(symmetry) - 64);
    }

    // Quadratic gain: matches real wavefolder knob feel.
    // 0→1x, 32→~3x, 64→~9x, 96→~18x, 127→~33x
    if (fold_depth > 0) {
      uint16_t gain = 128 + (static_cast<uint16_t>(fold_depth) * fold_depth >> 2);
      int32_t g = static_cast<int32_t>(input) * gain;
      input = g >> 7;
    }

    // Iterative fold: reflect at ±127 boundaries.
    // Each reflection adds harmonic content.
    for (uint8_t i = 0; i < 16; ++i) {
      if (input > 127) {
        input = 254 - input;
      } else if (input < -128) {
        input = -256 - input;
      } else {
        break;
      }
    }

    if (input > 127) input = 127;
    if (input < -128) input = -128;

    return static_cast<uint8_t>(input + 128);
  }

  void Render(
      uint8_t base_waveform,
      uint8_t fold_depth,
      uint8_t symmetry,
      uint8_t bias,
      uint8_t fm_depth,
      int8_t fm_ratio,
      uint8_t drive,
      uint8_t color,
      uint8_t fold_stages,
      uint8_t input_gain,
      uint8_t env_to_fold,
      uint8_t sub_level,
      uint8_t sync_amount,
      uint8_t env_value,
      uint16_t phase_increment,
      uint8_t* buffer,
      uint8_t size) {

    // FM modulator increment.
    uint16_t mod_increment;
    if (fm_ratio <= 0) {
      uint8_t shift = 1 - fm_ratio;
      if (shift > 15) shift = 15;
      mod_increment = phase_increment >> shift;
    } else if (fm_ratio == 1) {
      mod_increment = phase_increment;
    } else {
      mod_increment = phase_increment * static_cast<uint8_t>(fm_ratio);
    }

    // Sub-oscillator: one octave down.
    uint16_t sub_increment = phase_increment >> 1;

    // Combine fold depth with drive, input gain, and envelope.
    uint16_t effective_fold = fold_depth;
    // Drive and input gain boost the fold amount.
    effective_fold += (drive >> 1) + (input_gain >> 1);
    // Envelope-to-fold: the classic Buchla trick.
    // Sweeping fold with an envelope creates the plucked timbre.
    if (env_to_fold > 0) {
      effective_fold += (static_cast<uint16_t>(env_value) * env_to_fold) >> 8;
    }
    if (effective_fold > 127) effective_fold = 127;

    // Sync increment (for self-sync, faster than fundamental).
    uint16_t sync_increment = 0;
    if (sync_amount > 0) {
      sync_increment = phase_increment +
          ((static_cast<uint32_t>(phase_increment) * sync_amount) >> 5);
    }

    // Color: 2-pole low-pass (12dB/oct) for steep rolloff after folder.
    // 0 = very dark (only fundamental), 127 = bright (all harmonics).
    // Min coeff 16 ensures fundamental passes through at darkest setting.
    uint16_t lp16 = 16 + (static_cast<uint16_t>(color) << 1);
    uint8_t lp_coeff = (lp16 > 255) ? 255 : lp16;

    while (size--) {
      // FM modulation.
      mod_phase_ += mod_increment;
      uint16_t fm_mod = 0;
      if (fm_depth > 0) {
        uint16_t mod_val = InterpolateSine16(mod_phase_);
        fm_mod = (static_cast<int32_t>(mod_val - 32768) * fm_depth) >> 8;
      }

      // Self-sync: reset phase when sync oscillator wraps.
      if (sync_amount > 0) {
        uint16_t old_sync = sync_phase_;
        sync_phase_ += sync_increment;
        if (sync_phase_ < old_sync) {
          phase_ = 0;
        }
      }

      // Base oscillator.
      phase_ += phase_increment;
      int16_t sample;

      if (base_waveform == WC_WAVE_TRIANGLE) {
        uint16_t tri_phase = phase_ + fm_mod;
        if (tri_phase & 0x8000) {
          sample = static_cast<int16_t>(tri_phase >> 7) - 128;
        } else {
          sample = 127 - static_cast<int16_t>(tri_phase >> 7);
        }
      } else {
        uint16_t sine = InterpolateSine16(phase_ + fm_mod);
        sample = static_cast<int16_t>(sine >> 8) - 128;
      }

      // Apply input gain to the raw waveform before folding.
      if (input_gain > 0) {
        sample = (sample * (128 + input_gain)) >> 7;
      }

      // Wavefolder.
      uint8_t folded = Fold(sample, effective_fold, symmetry, bias);

      // Post-fold color: 2-pole IIR for steep rolloff (12dB/oct).
      if (color < 124) {
        // Pole 1
        lp_state1_ += (static_cast<int16_t>(folded - lp_state1_) * lp_coeff) >> 8;
        // Pole 2
        lp_state2_ += (static_cast<int16_t>(lp_state1_ - lp_state2_) * lp_coeff) >> 8;
        folded = lp_state2_;
      }

      // Mix in sub-harmonic.
      if (sub_level > 0) {
        sub_phase_ += sub_increment;
        uint8_t sub = InterpolateSample(wav_res_sine, sub_phase_);
        folded = ((uint16_t)folded * (256 - sub_level) +
                  (uint16_t)sub * sub_level) >> 8;
      }

      *buffer++ = folded;
    }
  }

 private:
  uint16_t phase_;
  uint16_t mod_phase_;
  uint16_t sub_phase_;
  uint16_t sync_phase_;
  uint8_t lp_state1_;
  uint8_t lp_state2_;

  DISALLOW_COPY_AND_ASSIGN(WestCoast);
};

}  // namespace ambika

#endif  // VOICECARD_WESTCOAST_H_
