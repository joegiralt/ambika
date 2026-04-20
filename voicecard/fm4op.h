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

// TX81Z waveform types derived from the sine table.
enum FmWaveform {
  FM_WAVE_SINE,         // W1: Full sine
  FM_WAVE_HALF_SINE,    // W2: Positive half, then silence
  FM_WAVE_ABS_SINE,     // W3: Full-wave rectified sine
  FM_WAVE_QUARTER_SINE, // W4: First quarter, rest silence
  FM_WAVE_HALF_ABS,     // W5: Rectified double sine, first half only
  FM_WAVE_TRIPLE_ABS,   // W6: Three rectified bumps, then silence
  FM_WAVE_PULSE_SINE,   // W7: Alternating positive/negative bumps
  FM_WAVE_SAW_SINE,     // W8: Staircase-like derived waveform
  FM_WAVE_LAST
};

// TX81Z algorithm topologies.
enum FmAlgorithm {
  FM_ALG_1,  // 4->3->2->1->out (full serial)
  FM_ALG_2,  // (3+4)->2->1->out
  FM_ALG_3,  // (4->3)+2 -> 1->out
  FM_ALG_4,  // (4->3)+(2->1)->out (two parallel pairs)
  FM_ALG_5,  // (4->2)+(4->3->1)->out
  FM_ALG_6,  // 4->(1+2+3)->out (one mod, three carriers)
  FM_ALG_7,  // (4->1)+2+3->out
  FM_ALG_8,  // 1+2+3+4->out (all carriers, additive)
  FM_ALG_LAST
};

// Patch field reinterpretation for FM4OP mode.
// These map onto existing patch byte offsets when osc[0].shape == WAVEFORM_FM4OP.
//
// osc[0].shape      = WAVEFORM_FM4OP (mode selector)
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

struct FmOperator {
  uint16_t phase;
  uint16_t phase_increment;
};

class Fm4Op {
 public:
  Fm4Op() { }

  void Init() {
    for (uint8_t i = 0; i < 4; ++i) {
      op_[i].phase = 0;
      op_[i].phase_increment = 0;
    }
    feedback_state_[0] = 0;
    feedback_state_[1] = 0;
  }

  // Scale modulation: operator output × level, attenuated for musical range.
  static inline int16_t ScaleMod(uint8_t op_out, uint8_t level) {
    return (static_cast<int16_t>(op_out - 128) * level) >> 4;
  }

  // Compute the output of a single operator waveform.
  static inline uint8_t RenderWaveform(uint8_t waveform, uint16_t phase) {
    switch (waveform) {
      case FM_WAVE_SINE:
      default:
        return InterpolateSample(wav_res_sine, phase);

      case FM_WAVE_HALF_SINE:
        if (phase < 0x8000) {
          return InterpolateSample(wav_res_sine, phase << 1);
        }
        return 128;

      case FM_WAVE_ABS_SINE:
        return InterpolateSample(wav_res_sine, (phase & 0x7FFF) << 1);

      case FM_WAVE_QUARTER_SINE:
        if (phase < 0x4000) {
          return InterpolateSample(wav_res_sine, phase << 2);
        }
        return 128;

      case FM_WAVE_HALF_ABS:
        if (phase < 0x8000) {
          return InterpolateSample(wav_res_sine, (phase & 0x3FFF) << 2);
        }
        return 128;

      case FM_WAVE_TRIPLE_ABS:
        if (phase < 0x8000) {
          uint16_t tripled = static_cast<uint16_t>(phase * 3);
          return InterpolateSample(wav_res_sine, (tripled & 0x7FFF) << 1);
        }
        return 128;

      case FM_WAVE_PULSE_SINE: {
        uint8_t val = InterpolateSample(wav_res_sine, (phase & 0x3FFF) << 2);
        if (phase >= 0x8000) {
          return 256 - val;
        }
        return val;
      }

      case FM_WAVE_SAW_SINE:
        if (phase < 0x4000) {
          return InterpolateSample(wav_res_sine, phase << 2);
        } else if (phase < 0x8000) {
          return 255;
        } else if (phase < 0xC000) {
          return InterpolateSample(wav_res_sine, phase << 2);
        }
        return 0;
    }
  }

  // Render a block of FM4OP audio.
  // Reads operator configuration directly from the patch data.
  void Render(
      uint8_t algorithm,
      const uint8_t* op_waveform,   // 4 waveform types
      const uint8_t* op_level,      // 4 output levels
      uint8_t feedback,
      uint16_t base_increment,
      uint8_t* buffer,
      uint8_t size) {

    while (size--) {
      // Advance all operator phases.
      for (uint8_t i = 0; i < 4; ++i) {
        op_[i].phase += op_[i].phase_increment;
      }

      // Apply feedback to op4 (same as TX81Z convention).
      // Feedback state stored centered at 0 (signed).
      int16_t fb = (feedback_state_[0] + feedback_state_[1]) >> 1;
      int16_t fb_mod = (fb * static_cast<int16_t>(feedback)) >> 4;

      uint8_t op4_out = RenderWaveform(op_waveform[3],
          op_[3].phase + static_cast<uint16_t>(fb_mod));
      // Store centered at 0 for proper signed feedback.
      feedback_state_[1] = feedback_state_[0];
      feedback_state_[0] = static_cast<int8_t>(op4_out - 128);

      // Render remaining operators and route per algorithm.
      uint8_t out;
      switch (algorithm) {
        case FM_ALG_1: {
          // 4->3->2->1->out (full serial)
          int16_t mod3 = ScaleMod(op4_out, op_level[3]);
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase + mod3);
          int16_t mod2 = ScaleMod(op3_out, op_level[2]);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase + mod2);
          int16_t mod1 = ScaleMod(op2_out, op_level[1]);
          out = RenderWaveform(op_waveform[0],
              op_[0].phase + mod1);
          break;
        }

        case FM_ALG_2: {
          // (3+4)->2->1->out
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase);
          int16_t mod2 = ScaleMod(op4_out, op_level[3]) +
              ScaleMod(op3_out, op_level[2]);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase + mod2);
          int16_t mod1 = ScaleMod(op2_out, op_level[1]);
          out = RenderWaveform(op_waveform[0],
              op_[0].phase + mod1);
          break;
        }

        case FM_ALG_3: {
          // (4->3) + 2 -> 1->out
          int16_t mod3 = ScaleMod(op4_out, op_level[3]);
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase + mod3);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase);
          int16_t mod1 = ScaleMod(op3_out, op_level[2]) +
              ScaleMod(op2_out, op_level[1]);
          out = RenderWaveform(op_waveform[0],
              op_[0].phase + mod1);
          break;
        }

        case FM_ALG_4: {
          // (4->3) + (2->1) -> out (two parallel pairs)
          int16_t mod3 = ScaleMod(op4_out, op_level[3]);
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase + mod3);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase);
          int16_t mod1 = ScaleMod(op2_out, op_level[1]);
          uint8_t op1_out = RenderWaveform(op_waveform[0],
              op_[0].phase + mod1);
          out = (op1_out >> 1) + (op3_out >> 1);
          break;
        }

        case FM_ALG_5: {
          // (4->2) + (4->3->1) -> out
          int16_t mod3 = ScaleMod(op4_out, op_level[3]);
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase + mod3);
          int16_t mod1 = ScaleMod(op3_out, op_level[2]);
          uint8_t op1_out = RenderWaveform(op_waveform[0],
              op_[0].phase + mod1);
          int16_t mod2 = ScaleMod(op4_out, op_level[3]);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase + mod2);
          out = (op1_out >> 1) + (op2_out >> 1);
          break;
        }

        case FM_ALG_6: {
          // 4->(1+2+3)->out (one mod, three carriers)
          int16_t mod = ScaleMod(op4_out, op_level[3]);
          uint8_t op1_out = RenderWaveform(op_waveform[0],
              op_[0].phase + mod);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase + mod);
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase + mod);
          out = (op1_out / 3) + (op2_out / 3) + (op3_out / 3);
          break;
        }

        case FM_ALG_7: {
          // (4->1)+2+3->out
          int16_t mod1 = ScaleMod(op4_out, op_level[3]);
          uint8_t op1_out = RenderWaveform(op_waveform[0],
              op_[0].phase + mod1);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase);
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase);
          out = (op1_out / 3) + (op2_out / 3) + (op3_out / 3);
          break;
        }

        case FM_ALG_8:
        default: {
          // 1+2+3+4->out (all carriers, additive)
          uint8_t op1_out = RenderWaveform(op_waveform[0],
              op_[0].phase);
          uint8_t op2_out = RenderWaveform(op_waveform[1],
              op_[1].phase);
          uint8_t op3_out = RenderWaveform(op_waveform[2],
              op_[2].phase);
          out = (op1_out >> 2) + (op2_out >> 2) +
                (op3_out >> 2) + (op4_out >> 2);
          break;
        }
      }
      *buffer++ = out;
    }
  }

  // TX81Z frequency ratio table — 64 entries, 8.8 fixed-point.
  // Coarse ratio byte (0-63) indexes into this table.
  // Values: 0.50, 0.71, 0.78, 0.87, 1.00, 1.41, 1.57, 1.73, 2.00, ...
  static const prog_uint16_t tx81z_ratios_[] PROGMEM;

  // Set operator phase increment using TX81Z-style ratio lookup.
  void SetOperatorIncrement(uint8_t op_index, uint16_t base_increment,
                            uint8_t coarse_ratio, int8_t fine_detune) {
    // Look up the ratio from the TX81Z table (8.8 fixed-point).
    uint8_t idx = coarse_ratio & 0x3F;
    uint16_t ratio_fp = ResourcesManager::Lookup<uint16_t, uint8_t>(
        tx81z_ratios_, idx);
    // Multiply base increment by ratio: (base * ratio) >> 8.
    uint32_t product = static_cast<uint32_t>(base_increment) * ratio_fp;
    uint16_t increment = product >> 8;
    // Fine detune: small pitch offset.
    if (fine_detune > 0) {
      increment += (increment >> 8) * fine_detune;
    } else if (fine_detune < 0) {
      increment -= (increment >> 8) * (-fine_detune);
    }
    op_[op_index].phase_increment = increment;
  }

  FmOperator* mutable_op(uint8_t i) { return &op_[i]; }

 private:
  FmOperator op_[4];
  int8_t feedback_state_[2];

  DISALLOW_COPY_AND_ASSIGN(Fm4Op);
};

}  // namespace ambika

#endif  // VOICECARD_FM4OP_H_
