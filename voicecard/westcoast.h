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
    prev_sample_ = 128;
  }

  // Multi-stage wavefolder with variable stages.
  static inline uint8_t Fold(int16_t input, uint8_t fold_depth,
                             uint8_t symmetry, uint8_t bias,
                             uint8_t stages) {
    input += static_cast<int8_t>(bias - 64);

    int16_t gained = input;
    if (fold_depth > 0) {
      int32_t g = static_cast<int32_t>(input) * (128 + fold_depth * 3);
      gained = g >> 7;
    }

    if (symmetry != 64) {
      gained += (static_cast<int16_t>(symmetry) - 64);
    }

    int16_t folded = gained;
    if (stages < 1) stages = 1;
    if (stages > 6) stages = 6;

    for (uint8_t i = 0; i < stages; ++i) {
      if (folded > 127) {
        folded = 254 - folded;
      } else if (folded < -128) {
        folded = -256 - folded;
      } else {
        break;
      }
    }

    if (folded > 127) folded = 127;
    if (folded < -128) folded = -128;

    return static_cast<uint8_t>(folded + 128);
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

    // Fold stages: 1-6, mapped from 0-127.
    uint8_t stages = fold_stages > 0 ? (fold_stages >> 5) + 1 : 1;
    if (stages > 6) stages = 6;

    // Pre-fold drive + input gain.
    uint8_t effective_fold = fold_depth;
    uint16_t total_boost = drive + input_gain;
    if (total_boost > 0) {
      uint16_t boosted = fold_depth + (total_boost >> 2);
      if (boosted > 127) boosted = 127;
      effective_fold = boosted;
    }

    // Envelope-to-fold: modulate fold depth by envelope value.
    if (env_to_fold > 0) {
      uint16_t env_mod = (static_cast<uint16_t>(env_value) * env_to_fold) >> 7;
      uint16_t modded = effective_fold + env_mod;
      if (modded > 127) modded = 127;
      effective_fold = modded;
    }

    // Sync increment (for self-sync, faster than fundamental).
    uint16_t sync_increment = 0;
    if (sync_amount > 0) {
      sync_increment = phase_increment +
          ((static_cast<uint32_t>(phase_increment) * sync_amount) >> 5);
    }

    // Color filter coefficient.
    uint8_t color_clipped = (color > 127) ? 127 : color;
    uint8_t lp_coeff = (127 - color_clipped) << 1;
    if (lp_coeff < 4) lp_coeff = 4;

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
      uint8_t folded = Fold(sample, effective_fold, symmetry, bias, stages);

      // Post-fold color filter.
      if (color < 120) {
        folded = ((uint16_t)folded * (256 - lp_coeff) +
                  (uint16_t)prev_sample_ * lp_coeff) >> 8;
      }
      prev_sample_ = folded;

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
  uint8_t prev_sample_;

  DISALLOW_COPY_AND_ASSIGN(WestCoast);
};

}  // namespace ambika

#endif  // VOICECARD_WESTCOAST_H_
