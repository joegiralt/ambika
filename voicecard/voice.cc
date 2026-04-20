// Copyright 2011 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// Main synthesis engine.

#include "voicecard/voice.h"

#include "voicecard/audio_out.h"
#include "voicecard/leds.h"
#include "voicecard/oscillator.h"
#include "voicecard/sub_oscillator.h"
#include "voicecard/transient_generator.h"

using namespace avrlib;

namespace ambika {

/* extern */
Voice voice;

Oscillator osc_1;
Oscillator osc_2;
SubOscillator sub_osc;
TransientGenerator transient_generator;


/* <static> */

Patch Voice::patch_;
uint8_t *Voice::patch_data_ = reinterpret_cast<uint8_t*> (&Voice::patch_);
Part Voice::part_;
uint8_t *Voice::part_data_ = reinterpret_cast<uint8_t*> (&Voice::part_);

Lfo Voice::voice_lfo_;
Envelope Voice::envelope_[kNumTotalEnvelopes];
uint8_t Voice::gate_;
int16_t Voice::pitch_increment_;
int16_t Voice::pitch_target_;
int16_t Voice::pitch_value_;
uint8_t Voice::modulation_sources_[kNumModulationSources];
int8_t Voice::modulation_destinations_[kNumModulationDestinations];
int16_t Voice::dst_[kNumModulationDestinations];
uint8_t Voice::buffer_[kAudioBlockSize];
uint8_t Voice::osc2_buffer_[kAudioBlockSize];
uint8_t Voice::sync_state_[kAudioBlockSize];
uint8_t Voice::no_sync_[kAudioBlockSize];
uint8_t Voice::dummy_sync_state_[kAudioBlockSize];
Fm4Op Voice::fm4op_;
KarplusStrong Voice::karplus_;
WestCoast Voice::westcoast_;
uint8_t Voice::last_engine_ = 0xFF;

// TX81Z frequency ratios as 8.8 fixed-point (ratio × 256).
const prog_uint16_t Fm4Op::tx81z_ratios_[] PROGMEM = {
  128,  182,  200,  223,  256,  361,  402,  443,
  512,  722,  768,  804,  886, 1024, 1085, 1206,
 1280, 1329, 1446, 1536, 1608, 1772, 1792, 1810,
 2010, 2048, 2171, 2214, 2304, 2412, 2532, 2560,
 2657, 2813, 2816, 2893, 3072, 3100, 3215, 3256,
 3328, 3543, 3584, 3610, 3617, 3840, 3981, 3986,
 4019, 4342, 4421, 4429, 4703, 4823, 4872, 5064,
 5225, 5315, 5427, 5627, 5757, 6029, 6200, 6643,
};

// TX81Z-style exponential output level curve (0.75 dB/step).
// level 0 = silence, level 127 = full amplitude (255).
const prog_uint8_t Fm4Op::level_to_amplitude_[128] PROGMEM = {
    0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   4,   4,
    4,   5,   5,   6,   6,   7,   7,   8,   9,  10,  10,  11,  12,  14,  15,  16,
   18,  19,  21,  23,  25,  27,  29,  32,  35,  38,  42,  45,  49,  54,  59,  64,
   70,  76,  83,  90,  99, 108, 117, 128, 139, 152, 166, 181, 197, 215, 234, 255,
};

// 512-entry 16-bit sine table for FM synthesis.
// Phase-shifted to match original table (trough at index 0).
const prog_uint16_t wav_res_sine16[] PROGMEM = {
      1,     3,    10,    23,    40,    62,    89,   121,
    158,   200,   247,   299,   355,   417,   483,   554,
    630,   711,   797,   887,   982,  1083,  1187,  1297,
   1411,  1531,  1654,  1783,  1916,  2054,  2196,  2343,
   2495,  2651,  2812,  2977,  3146,  3321,  3499,  3682,
   3870,  4061,  4257,  4458,  4662,  4871,  5084,  5301,
   5523,  5748,  5978,  6211,  6449,  6690,  6936,  7185,
   7438,  7695,  7956,  8221,  8489,  8761,  9036,  9315,
   9598,  9884, 10173, 10466, 10763, 11062, 11365, 11671,
  11980, 12293, 12608, 12927, 13248, 13573, 13900, 14230,
  14563, 14899, 15237, 15578, 15922, 16268, 16617, 16968,
  17321, 17677, 18035, 18395, 18758, 19122, 19489, 19858,
  20228, 20601, 20975, 21351, 21729, 22108, 22489, 22872,
  23256, 23641, 24028, 24416, 24806, 25196, 25588, 25981,
  26375, 26770, 27166, 27562, 27960, 28358, 28756, 29156,
  29556, 29956, 30357, 30758, 31160, 31561, 31963, 32365,
  32768, 33170, 33572, 33974, 34375, 34777, 35178, 35579,
  35979, 36379, 36779, 37177, 37575, 37973, 38369, 38765,
  39160, 39554, 39947, 40339, 40729, 41119, 41507, 41894,
  42279, 42663, 43046, 43427, 43806, 44184, 44560, 44934,
  45307, 45677, 46046, 46413, 46777, 47140, 47500, 47858,
  48214, 48567, 48918, 49267, 49613, 49957, 50298, 50636,
  50972, 51305, 51635, 51962, 52287, 52608, 52927, 53242,
  53555, 53864, 54170, 54473, 54772, 55069, 55362, 55651,
  55937, 56220, 56499, 56774, 57046, 57314, 57579, 57840,
  58097, 58350, 58599, 58845, 59086, 59324, 59557, 59787,
  60012, 60234, 60451, 60664, 60873, 61077, 61278, 61474,
  61665, 61853, 62036, 62214, 62389, 62558, 62723, 62884,
  63040, 63192, 63339, 63481, 63619, 63752, 63881, 64004,
  64124, 64238, 64348, 64452, 64553, 64648, 64738, 64824,
  64905, 64981, 65052, 65118, 65180, 65236, 65288, 65335,
  65377, 65414, 65446, 65473, 65495, 65512, 65525, 65532,
  65535, 65532, 65525, 65512, 65495, 65473, 65446, 65414,
  65377, 65335, 65288, 65236, 65180, 65118, 65052, 64981,
  64905, 64824, 64738, 64648, 64553, 64452, 64348, 64238,
  64124, 64004, 63881, 63752, 63619, 63481, 63339, 63192,
  63040, 62884, 62723, 62558, 62389, 62214, 62036, 61853,
  61665, 61474, 61278, 61077, 60873, 60664, 60451, 60234,
  60012, 59787, 59557, 59324, 59086, 58845, 58599, 58350,
  58097, 57840, 57579, 57314, 57046, 56774, 56499, 56220,
  55937, 55651, 55362, 55069, 54772, 54473, 54170, 53864,
  53555, 53242, 52927, 52608, 52287, 51962, 51635, 51305,
  50972, 50636, 50298, 49957, 49613, 49267, 48918, 48567,
  48214, 47858, 47500, 47140, 46777, 46413, 46046, 45677,
  45307, 44934, 44560, 44184, 43806, 43427, 43046, 42663,
  42279, 41894, 41507, 41119, 40729, 40339, 39947, 39554,
  39160, 38765, 38369, 37973, 37575, 37177, 36779, 36379,
  35979, 35579, 35178, 34777, 34375, 33974, 33572, 33170,
  32768, 32365, 31963, 31561, 31160, 30758, 30357, 29956,
  29556, 29156, 28756, 28358, 27960, 27562, 27166, 26770,
  26375, 25981, 25588, 25196, 24806, 24416, 24028, 23641,
  23256, 22872, 22489, 22108, 21729, 21351, 20975, 20601,
  20228, 19858, 19489, 19122, 18758, 18395, 18035, 17677,
  17321, 16968, 16617, 16268, 15922, 15578, 15237, 14899,
  14563, 14230, 13900, 13573, 13248, 12927, 12608, 12293,
  11980, 11671, 11365, 11062, 10763, 10466, 10173,  9884,
   9598,  9315,  9036,  8761,  8489,  8221,  7956,  7695,
   7438,  7185,  6936,  6690,  6449,  6211,  5978,  5748,
   5523,  5301,  5084,  4871,  4662,  4458,  4257,  4061,
   3870,  3682,  3499,  3321,  3146,  2977,  2812,  2651,
   2495,  2343,  2196,  2054,  1916,  1783,  1654,  1531,
   1411,  1297,  1187,  1083,   982,   887,   797,   711,
    630,   554,   483,   417,   355,   299,   247,   200,
    158,   121,    89,    62,    40,    23,    10,     3,
      1,
};
/* </static> */

static const prog_Patch init_patch PROGMEM = {
  // Oscillators
  //WAVEFORM_WAVETABLE_1 + 1, 63, -24, 0,
  WAVEFORM_NONE, 0, 0, 0,
  WAVEFORM_NONE, 0, 0, 0,
  
  // Mixer
  32, OP_SUM, 0, WAVEFORM_SUB_OSC_SQUARE_1, 0, 0, 0, 0, 

  // Filter
  127, 0, 0, 0, 0, 0, 63, 0,
  // ADSR
  0, 40, 20, 60, 0, 0, 0, 0,
  0, 40, 20, 60, 0, 0, 0, 0,
  0, 40, 20, 60, 0, 0, 0, 0,
  
  LFO_WAVEFORM_TRIANGLE, 16,
  
  // Routing
  MOD_SRC_LFO_1, MOD_DST_OSC_1, 0,
  MOD_SRC_ENV_1, MOD_DST_OSC_2, 0,
  MOD_SRC_LFO_1, MOD_DST_OSC_1, 0,
  MOD_SRC_ENV_1, MOD_DST_OSC_2, 0,
  MOD_SRC_ENV_1, MOD_DST_PARAMETER_1, 0,
  MOD_SRC_LFO_1, MOD_DST_PARAMETER_2, 0,
  MOD_SRC_LFO_2, MOD_DST_MIX_BALANCE, 0,
  MOD_SRC_LFO_4, MOD_DST_PARAMETER_1, 63,
  MOD_SRC_ENV_4, MOD_DST_PARAMETER_1, 0,
  MOD_SRC_ENV_5, MOD_DST_PARAMETER_2, 0,
  MOD_SRC_ENV_2, MOD_DST_VCA, 32,
  MOD_SRC_VELOCITY, MOD_DST_VCA, 0,
  MOD_SRC_PITCH_BEND, MOD_DST_OSC_1_2_COARSE, 0,
  MOD_SRC_LFO_1, MOD_DST_OSC_1_2_COARSE, 0,
  
  // Modifiers
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  
  // Padding
  0, 0, 0, 0, 0, 0, 0, 0,

  // Extra envelope-LFO settings (env4-7: ADSR + LFO shape/rate/curve/retrigger)
  0, 40, 20, 60, LFO_WAVEFORM_TRIANGLE, 0, 0, 0,
  0, 40, 20, 60, LFO_WAVEFORM_TRIANGLE, 0, 0, 0,
  0, 40, 20, 60, LFO_WAVEFORM_TRIANGLE, 0, 0, 0,
  0, 40, 20, 60, LFO_WAVEFORM_TRIANGLE, 0, 0, 0,
};

/* static */
void Voice::Init() {
  pitch_value_ = 0;
  for (uint8_t i = 0; i < kNumTotalEnvelopes; ++i) {
    envelope_[i].Init();
  }
  memset(no_sync_, 0, kAudioBlockSize);
  ResourcesManager::Load(&init_patch, 0, &patch_);
  ResetAllControllers();
  part_.volume = 127;
  part_.portamento_time = 0;
  part_.legato = 0;
  Kill();
}

/* static */
void Voice::ResetAllControllers() {
  modulation_sources_[MOD_SRC_PITCH_BEND] = 128;
  modulation_sources_[MOD_SRC_AFTERTOUCH] = 0;
  modulation_sources_[MOD_SRC_WHEEL] = 0;
  modulation_sources_[MOD_SRC_WHEEL_2] = 0;
  modulation_sources_[MOD_SRC_EXPRESSION] = 0;
  modulation_sources_[MOD_SRC_CONSTANT_4] = 4;
  modulation_sources_[MOD_SRC_CONSTANT_8] = 8;
  modulation_sources_[MOD_SRC_CONSTANT_16] = 16;
  modulation_sources_[MOD_SRC_CONSTANT_32] = 32;
  modulation_sources_[MOD_SRC_CONSTANT_64] = 64;
  modulation_sources_[MOD_SRC_CONSTANT_128] = 128;
  modulation_sources_[MOD_SRC_CONSTANT_256] = 255;
}

/* static */
void Voice::TriggerEnvelope(uint8_t stage) {
  for (uint8_t i = 0; i < kNumTotalEnvelopes; ++i) {
    TriggerEnvelope(i, stage);
  }
}

/* static */
void Voice::TriggerEnvelope(uint8_t index, uint8_t stage) {
  envelope_[index].Trigger(stage);
}

/* static */
void Voice::Trigger(uint16_t note, uint8_t velocity, uint8_t legato) {
  pitch_target_ = note;

  // Slop: per-note random pitch offset (patch_.padding[1]).
  uint8_t slop = patch_.padding[1];
  if (slop > 0) {
    int8_t rnd = static_cast<int8_t>(Random::GetByte());
    // Scale: slop=127 gives ±~8 units (about ±0.5 semitone).
    int16_t offset = (static_cast<int16_t>(rnd) * slop) >> 8;
    pitch_target_ += offset;
  }

  if (!part_.legato || !legato) {
    gate_ = 255;
    TriggerEnvelope(ATTACK);
    transient_generator.Trigger();
    if (patch_.padding[2] == ENGINE_KS_PLUCK) {
      karplus_.Trigger(
          patch_.osc[1].shape,      // excitation type
          patch_.osc[1].parameter,  // excitation color
          patch_.osc[1].detune);    // pluck position
    }
    modulation_sources_[MOD_SRC_VELOCITY] = velocity;
    modulation_sources_[MOD_SRC_RANDOM] = Random::state_msb();
    osc_2.Reset();
  }
  if (pitch_value_ == 0 || (part_.legato && !legato)) {
    pitch_value_ = pitch_target_;
  }
  int16_t delta = pitch_target_ - pitch_value_;
  int32_t increment = ResourcesManager::Lookup<uint16_t, uint8_t>(
      lut_res_env_portamento_increments,
      part_.portamento_time);
  pitch_increment_ = (delta * increment) >> 16;
  if (pitch_increment_ == 0) {
    if (delta < 0) {
      pitch_increment_ = -1;
    } else {
      pitch_increment_ = 1;
    }
  }
}

/* static */
void Voice::Release() {
  gate_ = 0;
  TriggerEnvelope(RELEASE);
}

/* static */
inline void Voice::LoadSources() {
  static uint8_t ops[9];
  
  // Rescale the value of each modulation sources. Envelopes are in the
  // 0-16383 range ; just like pitch. All are scaled to 0-255.
  modulation_sources_[MOD_SRC_NOISE] = Random::GetByte();
  for (uint8_t i = 0; i < kNumTotalEnvelopes; ++i) {
    modulation_sources_[MOD_SRC_ENV_1 + i] = envelope_[i].Render();
  }
  modulation_sources_[MOD_SRC_NOTE] = U14ShiftRight6(pitch_value_);
  modulation_sources_[MOD_SRC_GATE] = gate_;
  modulation_sources_[MOD_SRC_LFO_4] = voice_lfo_.Render(
      patch_.voice_lfo_shape);

  // Apply the modulation operators
  for (uint8_t i = 0; i < kNumModifiers; ++i) {
    if (!patch_.modifier[i].op) {
      continue;
    }
    uint8_t x = patch_.modifier[i].operands[0];
    uint8_t y = patch_.modifier[i].operands[1];
    x = modulation_sources_[x];
    y = modulation_sources_[y];
    uint8_t op = patch_.modifier[i].op;
    if (op <= MODIFIER_LE) {
      if (x > y) {
        ops[4] = x;  ops[7] = 255;
        ops[5] = y;  ops[8] = 0;
      } else {
        ops[4] = y;  ops[7] = 0;
        ops[5] = x;  ops[8] = 255;
      }
      ops[1] = (x >> 1) + (y >> 1);
      ops[2] = U8U8MulShift8(x, y);
      ops[3] = S8U8MulShift8(x + 128, y) + 128;
      ops[6] = x ^ y;
      modulation_sources_[MOD_SRC_OP_1 + i] = ops[op];
    } else if (op == MODIFIER_QUANTIZE) {
      uint8_t mask = 0;
      while (y >>= 1) {
        mask >>= 1;
        mask |= 0x80;
      }
      modulation_sources_[MOD_SRC_OP_1 + i] = x & mask;
    } else if (op == MODIFIER_LAG_PROCESSOR) {
      y >>= 2;
      ++y;
      uint16_t v = U8U8Mul(256 - y, modulation_sources_[MOD_SRC_OP_1 + i]);
      v += U8U8Mul(y, x);
      modulation_sources_[MOD_SRC_OP_1 + i] = v >> 8;
    }
  }

  modulation_destinations_[MOD_DST_VCA] = part_.volume << 1;
  
  // Load and scale to 0-16383 the initial value of each modulated parameter.
  dst_[MOD_DST_OSC_1] = dst_[MOD_DST_OSC_2] = 8192;
  dst_[MOD_DST_OSC_1_2_COARSE] = dst_[MOD_DST_OSC_1_2_FINE] = 8192;
  dst_[MOD_DST_PARAMETER_1] = U8U8Mul(patch_.osc[0].parameter, 128);
  dst_[MOD_DST_PARAMETER_2] = U8U8Mul(patch_.osc[1].parameter, 128);

  dst_[MOD_DST_MIX_BALANCE] = patch_.mix_balance << 8;
  dst_[MOD_DST_MIX_PARAM] = patch_.mix_parameter << 8;
  dst_[MOD_DST_MIX_FUZZ] = patch_.mix_fuzz << 8;
  dst_[MOD_DST_MIX_CRUSH] = patch_.mix_crush << 8;
  dst_[MOD_DST_MIX_NOISE] = patch_.mix_noise << 8;
  dst_[MOD_DST_MIX_SUB_OSC] = patch_.mix_sub_osc << 8;

  uint16_t cutoff = U8U8Mul(patch_.filter[0].cutoff, 128);
  dst_[MOD_DST_FILTER_CUTOFF] = S16ClipU14(cutoff + pitch_value_ - 8192);
  dst_[MOD_DST_FILTER_RESONANCE] = patch_.filter[0].resonance << 8;
  
  dst_[MOD_DST_ATTACK] = 8192;
  dst_[MOD_DST_DECAY] = 8192;
  dst_[MOD_DST_RELEASE] = 8192;
  dst_[MOD_DST_LFO_4] = U8U8Mul(patch_.voice_lfo_rate, 128);
}


/* static */
inline void Voice::ProcessModulationMatrix() {
  // Apply the modulations in the modulation matrix.
  for (uint8_t i = 0; i < kNumModulations; ++i) {
    int8_t amount = patch_.modulation[i].amount;

    // The rate of the last modulation is adjusted by the wheel.
    if (i == kNumModulations - 1) {
      amount = S8U8MulShift8(amount, modulation_sources_[MOD_SRC_WHEEL]);
    }
    uint8_t source = patch_.modulation[i].source;
    uint8_t destination = patch_.modulation[i].destination;
    uint8_t source_value = modulation_sources_[source];
    if (destination != MOD_DST_VCA) {
      int16_t modulation = dst_[destination];
      if ((source >= MOD_SRC_LFO_1 && source <= MOD_SRC_LFO_4) ||
           source == MOD_SRC_PITCH_BEND ||
           source == MOD_SRC_NOTE) {
        // These sources are "AC-coupled" (128 = no modulation).
        modulation += S8S8Mul(amount, source_value + 128);
      } else {
        modulation += S8U8Mul(amount, source_value);
      }
      dst_[destination] = S16ClipU14(modulation);
    } else {
      // The VCA modulation is multiplicative, not additive. Yet another
      // Special case :(.
      if (amount < 0) {
        amount = -amount;
        source_value = 255 - source_value;
      }
      if (amount != 63) {
        source_value = U8Mix(255, source_value, amount << 2);
      }
      modulation_destinations_[MOD_DST_VCA] = U8U8MulShift8(
            modulation_destinations_[MOD_DST_VCA],
            source_value);
    }
  }
}

/* static */
inline void Voice::UpdateDestinations() {
  // Hardcoded filter modulations.
  // By default, the resonance tracks the note. Tracking works best when the
  // transistors are thermically coupled. You can disable tracking by applying
  // a negative modulation from NOTE to CUTOFF.
  uint16_t cutoff = dst_[MOD_DST_FILTER_CUTOFF];
  cutoff = S16ClipU14(cutoff + S8U8Mul(patch_.filter_env,
      modulation_sources_[MOD_SRC_ENV_2]));
  cutoff = S16ClipU14(cutoff + S8S8Mul(patch_.filter_lfo,
      modulation_sources_[MOD_SRC_LFO_2] + 128));
  
  // Store in memory all the updated parameters.
  modulation_destinations_[MOD_DST_FILTER_CUTOFF] = U14ShiftRight6(cutoff);
  modulation_destinations_[MOD_DST_FILTER_RESONANCE] = U14ShiftRight6(
      dst_[MOD_DST_FILTER_RESONANCE]);
  modulation_destinations_[MOD_DST_MIX_CRUSH] = (
      dst_[MOD_DST_MIX_CRUSH] >> 8) + 1;

  osc_1.set_parameter(U15ShiftRight7(dst_[MOD_DST_PARAMETER_1]));
  osc_1.set_fm_parameter(patch_.osc[0].range + 36);
  osc_2.set_parameter(U15ShiftRight7(dst_[MOD_DST_PARAMETER_2]));
  osc_2.set_fm_parameter(patch_.osc[1].range + 36);
  
  int8_t attack_mod = U15ShiftRight7(dst_[MOD_DST_ATTACK]) - 64;
  int8_t decay_mod = U15ShiftRight7(dst_[MOD_DST_DECAY]) - 64;
  int8_t release_mod = U15ShiftRight7(dst_[MOD_DST_RELEASE]) - 64;

  // Envelope slop: per-voice random ADSR variation.
  uint8_t slop = patch_.padding[1];
  int8_t env_slop = 0;
  if (slop > 0) {
    env_slop = static_cast<int8_t>(
        (static_cast<int16_t>(modulation_sources_[MOD_SRC_RANDOM]) - 128) *
        slop) >> 8;
  }

  for (int i = 0; i < kNumEnvLfoSlots; ++i) {
    int16_t new_attack = patch_.env_lfo[i].attack;
    new_attack = Clip(new_attack + attack_mod + env_slop, 0, 127);
    int16_t new_decay = patch_.env_lfo[i].decay;
    new_decay = Clip(new_decay + decay_mod + env_slop, 0, 127);
    int16_t new_release = patch_.env_lfo[i].release;
    new_release = Clip(new_release + release_mod + env_slop, 0, 127);
    envelope_[i].Update(
          new_attack,
          new_decay,
          patch_.env_lfo[i].sustain,
          new_release,
          patch_.env_lfo[i].envelope_curve);
  }
  for (int i = 0; i < kNumExtraEnvelopes; ++i) {
    envelope_[kNumEnvLfoSlots + i].Update(
        patch_.extra_env_lfo[i].attack,
        patch_.extra_env_lfo[i].decay,
        patch_.extra_env_lfo[i].sustain,
        patch_.extra_env_lfo[i].release,
        patch_.extra_env_lfo[i].envelope_curve);
  }
  
  voice_lfo_.set_phase_increment(
      ResourcesManager::Lookup<uint16_t, uint8_t>(
          lut_res_lfo_increments, U14ShiftRight6(dst_[MOD_DST_LFO_4]) >> 1));
}

/* static */
inline void Voice::RenderOscillators() {
  // Apply portamento.
  int16_t base_pitch = pitch_value_ + pitch_increment_;
  if ((pitch_increment_ > 0) ^ (base_pitch < pitch_target_)) {
    base_pitch = pitch_target_;
    pitch_increment_ = 0;
  }
  pitch_value_ = base_pitch;

  // -4 / +4 semitones by the vibrato and pitch bend (coarse).
  // -0.5 / +0.5 semitones by the vibrato and pitch bend (fine).
  base_pitch += (dst_[MOD_DST_OSC_1_2_COARSE] - 8192) >> 4;
  base_pitch += (dst_[MOD_DST_OSC_1_2_FINE] - 8192) >> 7;

  uint8_t engine = patch_.padding[2];

  // Reset engine state when engine type changes.
  if (engine != last_engine_) {
    last_engine_ = engine;
    fm4op_.Init();
    karplus_.Init();
    westcoast_.Init();
  }

  // --- 4-op FM mode ---
  if (engine == ENGINE_FM4OP) {
    uint16_t base_increment = ComputePhaseIncrement(base_pitch);

    // Set up operator phase increments from patch fields.
    // Coarse ratio is a TX81Z-style index (0-63) into the ratio table.
    fm4op_.SetOperatorIncrement(0, base_increment,
        static_cast<uint8_t>(patch_.osc[0].range), patch_.osc[0].detune);
    fm4op_.SetOperatorIncrement(1, base_increment,
        static_cast<uint8_t>(patch_.osc[1].range), patch_.osc[1].detune);
    fm4op_.SetOperatorIncrement(2, base_increment,
        patch_.mix_balance, static_cast<int8_t>(patch_.mix_op));
    fm4op_.SetOperatorIncrement(3, base_increment,
        patch_.mix_parameter, static_cast<int8_t>(patch_.mix_sub_osc_shape));

    // Extract operator waveforms from packed nibbles.
    uint8_t op_waveform[4];
    op_waveform[0] = patch_.osc[1].shape & 0x0F;
    op_waveform[1] = (patch_.osc[1].shape >> 4) & 0x0F;
    op_waveform[2] = patch_.osc[1].parameter & 0x0F;
    op_waveform[3] = (patch_.osc[1].parameter >> 4) & 0x0F;
    // Clamp to valid range.
    for (uint8_t i = 0; i < 4; ++i) {
      if (op_waveform[i] >= FM_WAVE_LAST) {
        op_waveform[i] = FM_WAVE_SINE;
      }
    }

    // Operator output levels — TX81Z signal chain:
    // 1. Exponential curve on raw patch level (TL → amplitude)
    // 2. Multiply by per-op envelope (linear amplitude modulation)
    uint8_t op_level[4];
    op_level[0] = patch_.mix_sub_osc;
    op_level[1] = patch_.mix_noise;
    op_level[2] = patch_.mix_fuzz;
    op_level[3] = patch_.mix_crush;
    for (uint8_t i = 0; i < 4; ++i) {
      uint8_t amp = pgm_read_byte(&Fm4Op::level_to_amplitude_[op_level[i]]);
      op_level[i] = U8U8MulShift8(amp, modulation_sources_[MOD_SRC_ENV_4 + i]);
    }

    // Algorithm from osc[0].parameter.
    uint8_t algorithm = patch_.osc[0].parameter;
    if (algorithm >= FM_ALG_LAST) {
      algorithm = FM_ALG_1;
    }

    // Feedback from padding[0].
    uint8_t feedback = patch_.padding[0];

    // Render into buffer.
    fm4op_.Render(algorithm, op_waveform, op_level, feedback,
                  base_increment, buffer_, kAudioBlockSize);

    // Fill osc2 buffer with silence so the mixer doesn't add garbage.
    memset(osc2_buffer_, 128, kAudioBlockSize);
    return;
  }

  // --- Karplus-Strong mode ---
  if (engine == ENGINE_KS_PLUCK) {
    int16_t ks_pitch = base_pitch + S8U8Mul(patch_.osc[0].range, 128)
        + patch_.osc[0].detune;
    uint16_t ks_increment = ComputePhaseIncrement(ks_pitch);
    karplus_.SetPitch(ks_increment);

    // KS parameters from patch fields.
    uint8_t damping = U15ShiftRight7(dst_[MOD_DST_PARAMETER_1]);
    uint8_t decay = patch_.osc[1].range;       // decay rate
    uint8_t body = patch_.mix_balance;          // body resonance
    // Page 2 params: ensemble + stiffness + feedback.
    uint8_t ens_rate = patch_.mix_op;           // ensemble rate
    uint8_t ens_depth = patch_.mix_parameter;   // ensemble depth
    uint8_t ens_spread = patch_.mix_sub_osc_shape; // ensemble spread
    uint8_t ens_mix = patch_.mix_sub_osc;       // ensemble wet/dry
    uint8_t stiffness = patch_.mix_noise;       // string stiffness
    uint8_t feedback = patch_.mix_fuzz;          // extra feedback
    karplus_.Render(damping, decay, body,
                    ens_rate, ens_depth, ens_spread, ens_mix,
                    stiffness, feedback,
                    buffer_, kAudioBlockSize);

    memset(osc2_buffer_, 128, kAudioBlockSize);
    return;
  }

  // --- West Coast mode ---
  if (engine == ENGINE_WESTCOAST) {
    int16_t wc_pitch = base_pitch + S8U8Mul(patch_.osc[0].range, 128)
        + patch_.osc[0].detune;
    uint16_t wc_increment = ComputePhaseIncrement(wc_pitch);

    uint8_t fold = U15ShiftRight7(dst_[MOD_DST_PARAMETER_1]);
    uint8_t env_val = modulation_sources_[MOD_SRC_ENV_1];
    westcoast_.Render(
        patch_.osc[1].shape,           // base waveform (sin/tri)
        fold,                           // fold depth (modulatable)
        patch_.osc[1].parameter,       // symmetry
        patch_.mix_balance,            // bias
        patch_.osc[1].range,           // FM depth
        patch_.osc[1].detune,          // FM ratio
        patch_.mix_op,                 // drive
        patch_.mix_parameter,          // color
        patch_.mix_sub_osc_shape,      // fold stages
        patch_.mix_sub_osc,            // input gain
        patch_.mix_noise,              // env-to-fold
        patch_.mix_fuzz,               // sub level
        patch_.mix_crush,              // sync amount
        env_val,                        // envelope value for env-to-fold
        wc_increment,
        buffer_,
        kAudioBlockSize);

    memset(osc2_buffer_, 128, kAudioBlockSize);
    return;
  }

  // --- Normal oscillator rendering ---
  // Update the oscillator parameters.
  for (uint8_t i = 0; i < kNumOscillators; ++i) {
    int16_t pitch = base_pitch;
    // -36 / +36 semitones by the range controller.
    pitch += S8U8Mul(patch_.osc[i].range, 128);
    // -1 / +1 semitones by the detune controller.
    pitch += patch_.osc[i].detune;
    // -16 / +16 semitones by the routed modulations.
    pitch += (dst_[MOD_DST_OSC_1 + i] - 8192) >> 2;

    if (pitch >= kHighestNote) {
      pitch = kHighestNote;
    }
    // Extract the pitch increment from the pitch table.
    int16_t ref_pitch = pitch - kPitchTableStart;
    uint8_t num_shifts = 0;
    while (ref_pitch < 0) {
      ref_pitch += kOctave;
      ++num_shifts;
    }
    uint24_t increment;
    increment.integral = ResourcesManager::Lookup<uint16_t, uint16_t>(
        lut_res_oscillator_increments, ref_pitch >> 1);
    increment.fractional = 0;
    // Divide the pitch increment by the number of octaves we had to transpose
    // to get a value in the lookup table.
    while (num_shifts--) {
      increment = U24ShiftRight(increment);
    }

    // Now the oscillators can recompute all their internal variables!
    int8_t midi_note = U15ShiftRight7(pitch) - 12;
    if (midi_note < 0) {
      midi_note = 0;
    }
    if (i == 0) {
      sub_osc.set_increment(U24ShiftRight(increment));
      osc_1.Render(
          patch_.osc[0].shape,
          midi_note,
          increment,
          no_sync_,
          sync_state_,
          buffer_);
    } else {
      osc_2.Render(
          patch_.osc[1].shape,
          midi_note,
          increment,
          patch_.mix_op == OP_SYNC ? sync_state_ : no_sync_,
          dummy_sync_state_,
          osc2_buffer_);
    }
  }
}

/* static */
void Voice::ProcessBlock() {
  LoadSources();
  ProcessModulationMatrix();
  UpdateDestinations();
  
  // Skip the oscillator rendering code if the VCA output has converged to
  // a small value.
  if (vca() < 2) {
    for (uint8_t i = 0; i < kAudioBlockSize; i += 2) {
      audio_buffer.Overwrite2(128, 128);
    }
    return;
  }

  RenderOscillators();

  uint8_t is_fm4op = (patch_.padding[2] == ENGINE_FM4OP);
  uint8_t is_special = is_fm4op ||
      (patch_.padding[2] == ENGINE_KS_PLUCK) ||
      (patch_.padding[2] == ENGINE_WESTCOAST);

  // In FM4OP mode, skip oscillator mixing and sub-osc (those patch fields
  // are reinterpreted as FM parameters).
  if (!is_special) {
  uint8_t op = patch_.mix_op;
  uint8_t osc_2_gain = U14ShiftRight6(dst_[MOD_DST_MIX_BALANCE]);
  uint8_t osc_1_gain = ~osc_2_gain;
  uint8_t wet_gain = U14ShiftRight6(dst_[MOD_DST_MIX_PARAM]);
  uint8_t dry_gain = ~wet_gain;

  // Mix oscillators.
  switch (op) {
    case OP_RING_MOD:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        uint8_t mix = U8Mix(
            buffer_[i],
            osc2_buffer_[i],
            osc_1_gain,
            osc_2_gain);
        uint8_t ring = S8S8MulShift8(
            buffer_[i] + 128,
            osc2_buffer_[i] + 128) + 128;
        buffer_[i] = U8Mix(mix, ring, dry_gain, wet_gain);
      }
      break;
    case OP_XOR:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        uint8_t mix = U8Mix(
            buffer_[i],
            osc2_buffer_[i],
            osc_1_gain,
            osc_2_gain);
        uint8_t xord = buffer_[i] ^ osc2_buffer_[i];
        buffer_[i] = U8Mix(mix, xord, dry_gain, wet_gain);
      }
      break;
    case OP_FOLD:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        uint8_t mix = U8Mix(
            buffer_[i],
            osc2_buffer_[i],
            osc_1_gain,
            osc_2_gain);
        buffer_[i] = U8Mix(mix, mix + 128, dry_gain, wet_gain);
      }
      break;
    case OP_BITS:
      {
        wet_gain >>= 5;
        wet_gain = 255 - ((1 << wet_gain) - 1);
        for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
          buffer_[i] = U8Mix(
              buffer_[i],
              osc2_buffer_[i],
              osc_1_gain,
              osc_2_gain) & wet_gain;
        }
        break;
      }
    default:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        buffer_[i] = U8Mix(buffer_[i], osc2_buffer_[i], osc_1_gain, osc_2_gain);
      }
      break;
  }
  
  // Mix-in sub oscillator or transient generator.
  uint8_t sub_gain = U15ShiftRight7(dst_[MOD_DST_MIX_SUB_OSC]);
  if (patch_.mix_sub_osc_shape < WAVEFORM_SUB_OSC_CLICK) {
    sub_osc.Render(patch_.mix_sub_osc_shape, buffer_, sub_gain);
  } else {
    sub_gain <<= 1;
    transient_generator.Render(patch_.mix_sub_osc_shape, buffer_, sub_gain);
  }
  } // end if (!is_fm4op)

  // Post-mix processing.
  if (is_special) {
    // In FM4OP/KS mode, skip noise/fuzz post-processing.
    // Write directly to audio buffer.
    for (uint8_t i = 0; i < kAudioBlockSize; i += 2) {
      audio_buffer.Overwrite2(buffer_[i], buffer_[i + 1]);
    }
  } else {
    uint8_t noise = Random::state_msb();
    uint8_t noise_gain = U15ShiftRight7(dst_[MOD_DST_MIX_NOISE]);
    uint8_t signal_gain = ~noise_gain;
    uint8_t post_wet = U14ShiftRight6(dst_[MOD_DST_MIX_FUZZ]);
    uint8_t post_dry = ~post_wet;

    // Mix with noise, and apply distortion. The loop processes samples by 2 to
    // avoid some of the overhead of audio_buffer.Overwrite()
    for (uint8_t i = 0; i < kAudioBlockSize;) {
      uint8_t signal_noise_a, signal_noise_b;
      noise = (noise * 73) + 1;
      signal_noise_a = U8Mix(buffer_[i++], noise, signal_gain, noise_gain);
      uint8_t a = U8Mix(
          signal_noise_a,
          ResourcesManager::Lookup<uint8_t, uint8_t>(
              wav_res_distortion, signal_noise_a),
          post_dry, post_wet);

      noise = (noise * 73) + 1;
      signal_noise_b = U8Mix(buffer_[i++], noise, signal_gain, noise_gain);
      uint8_t b = U8Mix(
            signal_noise_b,
            ResourcesManager::Lookup<uint8_t, uint8_t>(
                wav_res_distortion, signal_noise_b),
            post_dry, post_wet);
      audio_buffer.Overwrite2(a, b);
    }
  }
}

}  // namespace ambika
