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
// Karplus-Strong plucked string synthesis.

#ifndef VOICECARD_KARPLUS_H_
#define VOICECARD_KARPLUS_H_

#include "avrlib/base.h"
#include "avrlib/op.h"
#include "avrlib/random.h"

using namespace avrlib;

namespace ambika {

static const uint16_t kKarplusBufferSize = 384;

// Excitation types.
enum KsExcitation {
  KS_EXC_NOISE,    // White noise burst
  KS_EXC_CLICK,    // Short impulse
  KS_EXC_BRIGHT,   // Filtered noise (bright)
  KS_EXC_DARK,     // Filtered noise (dark)
  KS_EXC_LAST
};

// Patch field mapping for KS mode:
// osc[0].shape     (offset 0)  = WAVEFORM_KS_PLUCK
// osc[0].parameter (offset 1)  = damping (brightness/decay)
// osc[0].range     (offset 2)  = pitch range
// osc[0].detune    (offset 3)  = fine tune
// osc[1].shape     (offset 4)  = excitation type (KsExcitation)
// osc[1].parameter (offset 5)  = excitation color (brightness of burst)
// osc[1].range     (offset 6)  = decay rate modifier
// osc[1].detune    (offset 7)  = pluck position (changes harmonics)
// mix_balance      (offset 8)  = body resonance

class KarplusStrong {
 public:
  KarplusStrong() { }

  void Init() {
    write_pos_ = 0;
    delay_length_ = kKarplusBufferSize;
    excited_ = 0;
    for (uint16_t i = 0; i < kKarplusBufferSize; ++i) {
      delay_line_[i] = 128;
    }
  }

  // Excite the string.
  void Trigger(uint8_t excitation_type, uint8_t color, uint8_t position) {
    // Fill delay line based on excitation type.
    for (uint16_t i = 0; i < delay_length_; ++i) {
      uint8_t sample;
      switch (excitation_type) {
        case KS_EXC_CLICK:
          sample = (i < 4) ? 255 : 128;
          break;
        case KS_EXC_BRIGHT: {
          uint8_t noise = Random::GetByte();
          // High-pass: subtract running average.
          sample = (noise >> 1) + 64 + (Random::GetByte() >> 2);
          break;
        }
        case KS_EXC_DARK: {
          uint8_t noise = Random::GetByte();
          // Simple low-pass: average with previous.
          if (i > 0) {
            sample = (noise >> 1) + (delay_line_[i - 1] >> 1);
          } else {
            sample = noise;
          }
          break;
        }
        default:  // KS_EXC_NOISE
          sample = Random::GetByte();
          break;
      }
      delay_line_[i] = sample;
    }

    // Apply pluck position: create a comb-like notch at position harmonics.
    // position 0 = pluck at bridge (all harmonics), 127 = pluck at middle.
    if (position > 4) {
      uint16_t notch_period = (delay_length_ * position) >> 7;
      if (notch_period > 1 && notch_period < delay_length_) {
        for (uint16_t i = 0; i < delay_length_ - notch_period; ++i) {
          int16_t mixed = (static_cast<int16_t>(delay_line_[i]) +
                          static_cast<int16_t>(delay_line_[i + notch_period])) >> 1;
          delay_line_[i] = static_cast<uint8_t>(mixed);
        }
      }
    }

    // Apply excitation color (brightness): low-pass the initial excitation.
    // Lower color = darker excitation.
    uint8_t filter_passes = (127 - color) >> 4;  // 0-7 passes
    for (uint8_t pass = 0; pass < filter_passes; ++pass) {
      for (uint16_t i = 1; i < delay_length_; ++i) {
        delay_line_[i] = (delay_line_[i] >> 1) + (delay_line_[i - 1] >> 1);
      }
    }

    write_pos_ = 0;
    excited_ = 1;
  }

  // Simple trigger (backwards compatible).
  void Trigger() {
    Trigger(KS_EXC_NOISE, 127, 0);
  }

  void SetPitch(uint16_t phase_increment) {
    // delay_length = sample_rate / frequency
    // phase_increment represents frequency as a fraction of the sample rate.
    // For accurate pitch: len = 65536 / phase_increment (full 16-bit division).
    if (phase_increment < 1) {
      phase_increment = 1;
    }
    uint16_t len = static_cast<uint16_t>(
        static_cast<uint32_t>(65536) / phase_increment);
    if (len < 2) len = 2;
    if (len >= kKarplusBufferSize) len = kKarplusBufferSize - 1;
    delay_length_ = len;
  }

  // Full render with all parameters.
  void Render(uint8_t damping, uint8_t decay, uint8_t body,
              uint8_t ens_rate, uint8_t ens_depth, uint8_t ens_spread,
              uint8_t ens_mix, uint8_t stiffness, uint8_t feedback,
              uint8_t* buffer, uint8_t size) {
    if (!excited_) {
      while (size--) {
        *buffer++ = 128;
      }
      return;
    }

    uint8_t coeff = 32 + (damping << 1);
    if (coeff < 32) coeff = 32;
    uint8_t decay_amount = decay >> 3;

    // Advance ensemble LFO.
    uint16_t lfo_inc = static_cast<uint16_t>(ens_rate + 1) << 4;

    while (size--) {
      // Advance LFO for ensemble read heads.
      ens_lfo_phase_ += lfo_inc;

      // Primary read head (fixed position).
      uint16_t read_pos = write_pos_ + 1;
      if (read_pos >= delay_length_) read_pos = 0;

      uint8_t current = delay_line_[read_pos];
      uint16_t next_pos = read_pos + 1;
      if (next_pos >= delay_length_) next_pos = 0;
      uint8_t next = delay_line_[next_pos];

      // KS low-pass averaging.
      uint8_t filtered = ((uint16_t)current * (256 - coeff) +
                          (uint16_t)next * coeff) >> 8;

      // Stiffness: allpass-like dispersion.
      // Mixes current with a sample from a prime-offset position,
      // creating inharmonic partials (piano/bell character).
      if (stiffness > 0) {
        uint16_t stiff_pos = read_pos + 7;  // Prime offset.
        if (stiff_pos >= delay_length_) stiff_pos -= delay_length_;
        uint8_t stiff_sample = delay_line_[stiff_pos];
        filtered = ((uint16_t)filtered * (256 - stiffness) +
                    (uint16_t)stiff_sample * stiffness) >> 8;
      }

      // Apply decay.
      if (decay_amount) {
        int16_t diff = static_cast<int16_t>(filtered) - 128;
        diff -= (diff * decay_amount) >> 8;
        filtered = 128 + diff;
      }

      // Body resonance.
      if (body > 4) {
        uint16_t body_pos = read_pos + (delay_length_ >> 1);
        if (body_pos >= delay_length_) body_pos -= delay_length_;
        uint8_t body_sample = delay_line_[body_pos];
        filtered = ((uint16_t)filtered * (256 - body) +
                    (uint16_t)body_sample * body) >> 8;
      }

      // Extra feedback for longer sustain.
      if (feedback > 0) {
        int16_t fb_val = static_cast<int16_t>(filtered) - 128;
        fb_val += (fb_val * feedback) >> 8;
        fb_val += 128;
        if (fb_val > 255) fb_val = 255;
        if (fb_val < 0) fb_val = 0;
        filtered = fb_val;
      }

      // Write back to delay line.
      delay_line_[read_pos] = filtered;
      write_pos_ = read_pos;

      // Ensemble: three read heads from same delay buffer.
      uint8_t output = filtered;
      if (ens_mix > 0 && ens_depth > 0) {
        // LFO triangle: map 16-bit phase to signed 8-bit triangle.
        uint8_t lfo_8 = ens_lfo_phase_ >> 8;
        int8_t lfo_val = (ens_lfo_phase_ & 0x8000)
            ? static_cast<int8_t>(255 - lfo_8)
            : static_cast<int8_t>(lfo_8);

        // Head 2 and 3 offsets (signed, relative to read_pos).
        int16_t offset2 = (static_cast<int16_t>(lfo_val) * ens_depth) >> 8;
        int16_t offset3 = -offset2 + (ens_spread >> 2);

        // Wrap positions into delay line range.
        int16_t p2 = (static_cast<int16_t>(read_pos) + offset2) %
            static_cast<int16_t>(delay_length_);
        if (p2 < 0) p2 += delay_length_;

        int16_t p3 = (static_cast<int16_t>(read_pos) + offset3) %
            static_cast<int16_t>(delay_length_);
        if (p3 < 0) p3 += delay_length_;

        uint8_t head2 = delay_line_[static_cast<uint16_t>(p2)];
        uint8_t head3 = delay_line_[static_cast<uint16_t>(p3)];

        // Mix: dry head + two wet heads.
        uint8_t dry = 255 - ens_mix;
        uint16_t mixed = (uint16_t)filtered * dry +
                         ((uint16_t)head2 + (uint16_t)head3) * (ens_mix >> 1);
        output = mixed >> 8;
      }

      *buffer++ = output;
    }
  }

  // Simple render (backwards compatible).
  void Render(uint8_t damping, uint8_t* buffer, uint8_t size) {
    Render(damping, 0, 0, 0, 0, 0, 0, 0, 0, buffer, size);
  }

 private:
  uint8_t delay_line_[kKarplusBufferSize];
  uint16_t write_pos_;
  uint16_t delay_length_;
  uint8_t excited_;
  uint16_t ens_lfo_phase_;

  DISALLOW_COPY_AND_ASSIGN(KarplusStrong);
};

}  // namespace ambika

#endif  // VOICECARD_KARPLUS_H_
