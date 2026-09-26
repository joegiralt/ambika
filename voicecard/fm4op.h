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
  static inline int16_t Operator(
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
      const uint8_t* w,           // 4 waveforms
      const uint16_t* att,        // 4 attenuations (Attenuation())
      uint16_t feedback_gain)     // FeedbackGain()
      __attribute__((always_inline)) {  // one caller: Render
    uint16_t p[4];
    for (uint8_t i = 0; i < 4; ++i) {
      op_[i].phase += op_[i].phase_increment;
      p[i] = op_[i].phase >> 16;
    }

    int16_t fb = 0;
    if (feedback_gain) {
      fb = (static_cast<int32_t>(feedback_[0] + feedback_[1]) *
            feedback_gain) >> 16;
    }
    int16_t op4 = Operator(w[3], p[3] + fb, att[3]);
    feedback_[1] = feedback_[0];
    feedback_[0] = op4;

    // Routing as on the chip (checked sample for sample against ymfm's
    // output_4op in voicecard/test). A modulator feeds output >> 1.
    int16_t sum, op3, op2;
    switch (algorithm) {
      case FM_ALG_1:  // 4->3->2->1
        op3 = Operator(w[2], p[2] + (op4 >> 1), att[2]);
        op2 = Operator(w[1], p[1] + (op3 >> 1), att[1]);
        sum = Operator(w[0], p[0] + (op2 >> 1), att[0]);
        break;
      case FM_ALG_2:  // (4+3)->2->1
        op3 = Operator(w[2], p[2], att[2]);
        op2 = Operator(w[1], p[1] + ((op4 + op3) >> 1), att[1]);
        sum = Operator(w[0], p[0] + (op2 >> 1), att[0]);
        break;
      case FM_ALG_3:  // (4 + (3->2))->1
        op3 = Operator(w[2], p[2], att[2]);
        op2 = Operator(w[1], p[1] + (op3 >> 1), att[1]);
        sum = Operator(w[0], p[0] + ((op4 + op2) >> 1), att[0]);
        break;
      case FM_ALG_4:  // ((4->3) + 2)->1
        op3 = Operator(w[2], p[2] + (op4 >> 1), att[2]);
        op2 = Operator(w[1], p[1], att[1]);
        sum = Operator(w[0], p[0] + ((op3 + op2) >> 1), att[0]);
        break;
      case FM_ALG_5:  // (4->3) + (2->1)
        op3 = Operator(w[2], p[2] + (op4 >> 1), att[2]);
        op2 = Operator(w[1], p[1], att[1]);
        sum = Operator(w[0], p[0] + (op2 >> 1), att[0]) + op3;
        break;
      case FM_ALG_6:  // 4->(1+2+3)
        op4 >>= 1;
        sum = Operator(w[0], p[0] + op4, att[0]) +
              Operator(w[1], p[1] + op4, att[1]) +
              Operator(w[2], p[2] + op4, att[2]);
        break;
      case FM_ALG_7:  // (4->3) + 2 + 1
        sum = Operator(w[0], p[0], att[0]) +
              Operator(w[1], p[1], att[1]) +
              Operator(w[2], p[2] + (op4 >> 1), att[2]);
        break;
      case FM_ALG_8:  // 1+2+3+4
      default:
        sum = Operator(w[0], p[0], att[0]) +
              Operator(w[1], p[1], att[1]) +
              Operator(w[2], p[2], att[2]) + op4;
        break;
    }
    return sum;
  }

  // Render a block of 12-bit DAC samples (centered on 2048).
  void Render(
      uint8_t algorithm,
      const uint8_t* w,
      const uint16_t* att,
      uint16_t feedback_gain,
      uint16_t* buffer,
      uint8_t size) {
    while (size--) {
      int16_t out = Sample(algorithm, w, att, feedback_gain);
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

  DISALLOW_COPY_AND_ASSIGN(Fm4Op);
};

}  // namespace ambika

#endif  // VOICECARD_FM4OP_H_
