// Copyright 2012 Emilie Gillet.
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

#include "voicecard/oscillator.h"

#include "voicecard/voicecard.h"

namespace ambika {

#define UPDATE_PHASE \
  if (*sync_input_++) { \
    phase.integral = 0; \
    phase.fractional = 0; \
  } \
  phase = U24AddC(phase, phase_increment_int); \
  *sync_output_++ = phase.carry; \

// This variant has a larger register width, but yields faster code.
#define UPDATE_PHASE_MORE_REGISTERS \
  if (*sync_input++) { \
    phase.integral = 0; \
    phase.fractional = 0; \
  } \
  phase = U24AddC(phase, phase_increment_int); \
  *sync_output++ = phase.carry; \

#define BEGIN_SAMPLE_LOOP \
  uint24c_t phase; \
  uint24_t phase_increment_int; \
  phase_increment_int.integral = phase_increment_.integral; \
  phase_increment_int.fractional = phase_increment_.fractional; \
  phase.integral = phase_.integral; \
  phase.fractional = phase_.fractional; \
  uint8_t size = kAudioBlockSize; \
  uint8_t* sync_input = sync_input_; \
  uint8_t* sync_output = sync_output_; \
  while (size--) {
  
#define END_SAMPLE_LOOP \
  } \
  phase_.integral = phase.integral; \
  phase_.fractional = phase.fractional;

// ------- Silence (useful when processing external signals) -----------------
void Oscillator::RenderSilence(uint8_t* buffer) {
  uint8_t size = kAudioBlockSize;
  while (size--) {
    *buffer++ = 128;
  }
}

// ------- Band-limited PWM --------------------------------------------------
void Oscillator::RenderBandlimitedPwm(uint8_t* buffer) {
  uint8_t balance_index = U8Swap4(note_ /* - 12 play safe with Aliasing */);
  uint8_t gain_2 = balance_index & 0xf0;
  uint8_t gain_1 = ~gain_2;

  uint8_t wave_index = balance_index & 0xf;
  const prog_uint8_t* wave_1 = waveform_table[
      WAV_RES_BANDLIMITED_SAW_1 + wave_index];
  wave_index = U8AddClip(wave_index, 1, kNumZonesHalfSampleRate);
  const prog_uint8_t* wave_2 = waveform_table[
      WAV_RES_BANDLIMITED_SAW_1 + wave_index];
  
  uint16_t shift = static_cast<uint16_t>(parameter_ + 128) << 8;
  // For higher pitched notes, simply use 128
  uint8_t scale = 192 - (parameter_ >> 1);
  if (note_ > 52) {
    scale = U8Mix(scale, 102, (note_ - 52) << 2);
    scale = U8Mix(scale, 102, (note_ - 52) << 2);
  }
  phase_increment_ = U24ShiftLeft(phase_increment_);
  BEGIN_SAMPLE_LOOP
    phase = U24AddC(phase, phase_increment_int);
    *sync_output_++ = phase.carry;
    *sync_output_++ = 0;
    if (sync_input_[0] || sync_input_[1]) {
      phase.integral = 0;
      phase.fractional = 0;
    }
    sync_input_ += 2;
    
    uint8_t a = InterpolateTwoTables(
        wave_1, wave_2,
        phase.integral, gain_1, gain_2);
    a = U8U8MulShift8(a, scale);
    uint8_t b = InterpolateTwoTables(
        wave_1, wave_2,
        phase.integral + shift, gain_1, gain_2);
    b = U8U8MulShift8(b, scale);
    a = a - b + 128;
    *buffer++ = a;
    *buffer++ = a;
    size--;
  END_SAMPLE_LOOP
}

// ------- Interpolation between two waveforms from two wavetables -----------
// The position is determined by the note pitch, to prevent aliasing.

void Oscillator::RenderSimpleWavetable(uint8_t* buffer) {
  uint8_t balance_index = U8Swap4(note_);
  uint8_t gain_2 = balance_index & 0xf0;
  uint8_t gain_1 = ~gain_2;
  uint8_t wave_1_index, wave_2_index;
  if (shape_ != WAVEFORM_SINE) {
    uint8_t wave_index = balance_index & 0xf;
    uint8_t base_resource_id = shape_ == WAVEFORM_SAW ?
        WAV_RES_BANDLIMITED_SAW_0 :
        (shape_ == WAVEFORM_SQUARE ? WAV_RES_BANDLIMITED_SQUARE_0  : 
        WAV_RES_BANDLIMITED_TRIANGLE_0);
    wave_1_index = base_resource_id + wave_index;
    wave_index = U8AddClip(wave_index, 1, kNumZonesFullSampleRate);
    wave_2_index = base_resource_id + wave_index;
  } else {
    wave_1_index = WAV_RES_SINE;
    wave_2_index = WAV_RES_SINE;
  }
  const prog_uint8_t* wave_1 = waveform_table[wave_1_index];
  const prog_uint8_t* wave_2 = waveform_table[wave_2_index];

  if (shape_ != WAVEFORM_TRIANGLE) {
    BEGIN_SAMPLE_LOOP
      UPDATE_PHASE_MORE_REGISTERS
      uint8_t sample = InterpolateTwoTables(
          wave_1, wave_2,
          phase.integral, gain_1, gain_2);
      if (sample < parameter_) {
        sample += parameter_ >> 1;
      }
      *buffer++ = sample;
    END_SAMPLE_LOOP
  } else {
    // The waveshaper for the triangle is different.
    BEGIN_SAMPLE_LOOP
      UPDATE_PHASE_MORE_REGISTERS
      uint8_t sample = InterpolateTwoTables(
          wave_1, wave_2,
          phase.integral, gain_1, gain_2);
      if (sample < parameter_) {
        sample = parameter_;
      }
      *buffer++ = sample;
    END_SAMPLE_LOOP
  }
}

// ------- Quad saw (mit aliasing) -------------------------------------------
void Oscillator::RenderQuadSawPad(uint8_t* buffer) {
  uint16_t phase_spread = (
      static_cast<uint32_t>(phase_increment_.integral) * parameter_) >> 13;
  ++phase_spread;
  uint16_t phase_increment = phase_increment_.integral;
  uint16_t increments[3];
  for (uint8_t i = 0; i < 3; ++i) {
    phase_increment += phase_spread;
    increments[i] = phase_increment;
  }
  
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    data_.qs.phase[0] += increments[0];
    data_.qs.phase[1] += increments[1];
    data_.qs.phase[2] += increments[2];
    uint8_t value = (phase.integral >> 10);
    value += (data_.qs.phase[0] >> 10);
    value += (data_.qs.phase[1] >> 10);
    value += (data_.qs.phase[2] >> 10);
    *buffer++ = value;
  END_SAMPLE_LOOP
}

// ------- Low-passed, then high-passed white noise --------------------------
void Oscillator::RenderFilteredNoise(uint8_t* buffer) {
  uint16_t rng_state = data_.no.rng_state;
  if (rng_state == 0) {
    ++rng_state;
  }
  uint8_t filter_coefficient = parameter_ << 2;
  if (filter_coefficient <= 4) {
    filter_coefficient = 4;
  }
  BEGIN_SAMPLE_LOOP
    if (*sync_input_++) {
      rng_state = data_.no.rng_reset_value;
    }
    rng_state = (rng_state >> 1) ^ (-(rng_state & 1) & 0xb400);
    uint8_t noise_sample = rng_state >> 8;
    // This trick is used to avoid having a DC component (no innovation) when
    // the parameter is set to its minimal or maximal value.
    data_.no.lp_noise_sample = U8Mix(
        data_.no.lp_noise_sample,
        noise_sample,
        filter_coefficient);
    if (parameter_ >= 64) {
      *buffer++ = noise_sample - data_.no.lp_noise_sample - 128;
    } else {
      *buffer++ = data_.no.lp_noise_sample;
    }
  END_SAMPLE_LOOP
  data_.no.rng_state = rng_state;
}

// ------- Waveshaping synthesis ----------------------------------------
void Oscillator::RenderWaveshape(uint8_t* buffer) {
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    // Generate a sine wave, then scale it by the parameter to control
    // how hard we drive the waveshaper.
    uint8_t sine = InterpolateSample(wav_res_sine, phase.integral);
    // Scale the sine around center. Higher parameter = more drive.
    int16_t driven = (static_cast<int16_t>(sine) - 128) *
        (parameter_ + 1);
    driven = (driven >> 6) + 128;
    if (driven > 255) driven = 255;
    if (driven < 0) driven = 0;
    // Run through the distortion waveshaper table.
    *buffer++ = ResourcesManager::Lookup<uint8_t, uint8_t>(
        wav_res_distortion, static_cast<uint8_t>(driven));
  END_SAMPLE_LOOP
}

/* static */
const Oscillator::RenderFn Oscillator::fn_table_[] PROGMEM = {
  &Oscillator::RenderSilence,         // NONE
  &Oscillator::RenderSimpleWavetable, // SAW
  &Oscillator::RenderBandlimitedPwm,  // SQUARE
  &Oscillator::RenderSimpleWavetable, // TRIANGLE
  &Oscillator::RenderSimpleWavetable, // SINE
  &Oscillator::RenderQuadSawPad,      // PAD
  &Oscillator::RenderFilteredNoise,   // NOISE
};

}  // namespace
