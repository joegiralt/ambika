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

static const uint16_t kKarplusBufferSize = 192;

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
    lp_state_ = 0;
    for (uint16_t i = 0; i < kKarplusBufferSize; ++i) {
      delay_line_[i] = 0;
    }
  }

  // Excite the string.
  void Trigger(uint8_t excitation_type, uint8_t color, uint8_t position) {
    // Fill the ENTIRE buffer — SetPitch may change delay_length_ after this.
    for (uint16_t i = 0; i < kKarplusBufferSize; ++i) {
      int16_t sample;
      switch (excitation_type) {
        case KS_EXC_CLICK:
          // Sharp impulse — very percussive attack.
          sample = (i < 2) ? 16383 : ((i < 4) ? -16383 : 0);
          break;
        case KS_EXC_BRIGHT:
          // Full-range noise with extra high frequency content.
          sample = (static_cast<int16_t>(Random::GetByte()) - 128) << 7;
          sample += (static_cast<int16_t>(Random::GetByte()) - 128) << 5;
          break;
        case KS_EXC_DARK:
          // Heavily filtered noise — warm, muted pluck.
          if (i > 0) {
            sample = (delay_line_[i - 1] * 3 +
                ((static_cast<int16_t>(Random::GetByte()) - 128) << 6)) >> 2;
          } else {
            sample = (static_cast<int16_t>(Random::GetByte()) - 128) << 5;
          }
          break;
        default:
          // Standard noise burst.
          sample = (static_cast<int16_t>(Random::GetByte()) - 128) << 7;
          break;
      }
      delay_line_[i] = sample;
    }

    // Pluck position comb filter.
    if (position > 4) {
      uint16_t notch_period = (kKarplusBufferSize * position) >> 7;
      if (notch_period > 1 && notch_period < kKarplusBufferSize) {
        for (uint16_t i = 0; i < kKarplusBufferSize - notch_period; ++i) {
          delay_line_[i] = (delay_line_[i] + delay_line_[i + notch_period]) >> 1;
        }
      }
    }

    // Excitation color low-pass.
    uint8_t filter_passes = (127 - color) >> 4;
    for (uint8_t pass = 0; pass < filter_passes; ++pass) {
      for (uint16_t i = 1; i < kKarplusBufferSize; ++i) {
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

    // Damping: IIR low-pass cutoff in the feedback loop.
    // 0 = bright (high cutoff), 127 = very dark (low cutoff).
    // The IIR filter creates dramatically different tones vs the 2-tap FIR.
    uint8_t lp_cutoff = 255 - (damping << 1);
    if (lp_cutoff < 2) lp_cutoff = 2;
    // Pitch scaling: less filtering for shorter strings.
    if (delay_length_ < 64) {
      lp_cutoff = lp_cutoff + ((255 - lp_cutoff) >> 1);  // push toward bright
    } else if (delay_length_ < 96) {
      lp_cutoff = lp_cutoff + ((255 - lp_cutoff) >> 2);
    }

    // Decay: energy loss per delay-line cycle.
    // 0 = infinite sustain, 127 = fast fade.
    uint8_t decay_amount = decay >> 1;

    // Stiffness offset scales with delay length.
    uint16_t stiff_offset = (delay_length_ >> 3) + 1;

    // Feedback as sustain: pushes cutoff higher (less loss = longer ring).
    lp_cutoff = lp_cutoff + (((255 - lp_cutoff) * feedback) >> 8);

    // Advance ensemble LFO.
    uint16_t lfo_inc = static_cast<uint16_t>(ens_rate + 1) << 4;

    while (size--) {
      ens_lfo_phase_ += lfo_inc;

      uint16_t read_pos = write_pos_ + 1;
      if (read_pos >= delay_length_) read_pos = 0;

      int16_t current = delay_line_[read_pos];
      uint16_t next_pos = read_pos + 1;
      if (next_pos >= delay_length_) next_pos = 0;
      int16_t next = delay_line_[next_pos];

      // KS core: standard 2-sample average (the string physics).
      int16_t avg = (current >> 1) + (next >> 1);

      // Damping: IIR one-pole low-pass. This is what shapes the tone.
      // High cutoff = bright (metallic), low cutoff = dark (muted thud).
      lp_state_ += (static_cast<int32_t>(avg - lp_state_) * lp_cutoff) >> 8;
      int16_t filtered = lp_state_;

      // Stiffness: allpass-like mixing with a point further in the delay.
      // Adds inharmonic partials (like piano string stiffness).
      if (stiffness > 4) {
        uint16_t stiff_pos = read_pos + stiff_offset;
        if (stiff_pos >= delay_length_) stiff_pos -= delay_length_;
        int16_t stiff_sample = delay_line_[stiff_pos];
        uint8_t sa = stiffness >> 1;  // 0-63 range for stability
        filtered = (static_cast<int32_t>(filtered) * (256 - sa) +
                    static_cast<int32_t>(stiff_sample) * sa) >> 8;
      }

      // Decay: apply energy loss once per cycle through the buffer.
      // Use a counter that wraps at delay_length_.
      if (decay_amount && read_pos == 0) {
        for (uint16_t i = 0; i < delay_length_; ++i) {
          delay_line_[i] -= (delay_line_[i] * decay_amount) >> 9;
        }
      }

      // Body resonance: comb filter at 1/3 string length.
      // Emphasizes the 3rd harmonic and its multiples.
      if (body > 4) {
        uint16_t body_pos = read_pos + (delay_length_ / 3);
        if (body_pos >= delay_length_) body_pos -= delay_length_;
        int16_t body_sample = delay_line_[body_pos];
        uint8_t body_amt = body >> 1;  // 0-63 range for stability
        filtered = (static_cast<int32_t>(filtered) * (256 - body_amt) +
                    static_cast<int32_t>(body_sample) * body_amt) >> 8;
      }

      delay_line_[read_pos] = filtered;
      write_pos_ = read_pos;

      // Ensemble: three read heads.
      int16_t output = filtered;
      if (ens_mix > 0 && ens_depth > 0) {
        uint8_t lfo_8 = ens_lfo_phase_ >> 8;
        int8_t lfo_val = (ens_lfo_phase_ & 0x8000)
            ? static_cast<int8_t>(255 - lfo_8)
            : static_cast<int8_t>(lfo_8);

        int16_t offset2 = (static_cast<int16_t>(lfo_val) * ens_depth) >> 7;
        int16_t offset3 = -offset2 + (ens_spread >> 1);

        int16_t p2 = (static_cast<int16_t>(read_pos) + offset2) %
            static_cast<int16_t>(delay_length_);
        if (p2 < 0) p2 += delay_length_;
        int16_t p3 = (static_cast<int16_t>(read_pos) + offset3) %
            static_cast<int16_t>(delay_length_);
        if (p3 < 0) p3 += delay_length_;

        int16_t head2 = delay_line_[static_cast<uint16_t>(p2)];
        int16_t head3 = delay_line_[static_cast<uint16_t>(p3)];

        uint8_t dry = 255 - ens_mix;
        int32_t mixed = static_cast<int32_t>(filtered) * dry +
                        (static_cast<int32_t>(head2) + head3) * (ens_mix >> 1);
        output = mixed >> 8;
      }

      // Truncate to 8-bit at output only (centered at 128).
      int16_t out8 = (output >> 7) + 128;
      if (out8 > 255) out8 = 255;
      if (out8 < 0) out8 = 0;
      *buffer++ = static_cast<uint8_t>(out8);
    }
  }

  // Simple render (backwards compatible).
  void Render(uint8_t damping, uint8_t* buffer, uint8_t size) {
    Render(damping, 0, 0, 0, 0, 0, 0, 0, 0, buffer, size);
  }

 private:
  int16_t delay_line_[kKarplusBufferSize];  // 16-bit for smooth decay
  int16_t lp_state_;                        // IIR low-pass state for damping
  uint16_t write_pos_;
  uint16_t delay_length_;
  uint8_t excited_;
  uint16_t ens_lfo_phase_;

  DISALLOW_COPY_AND_ASSIGN(KarplusStrong);
};

}  // namespace ambika

#endif  // VOICECARD_KARPLUS_H_
