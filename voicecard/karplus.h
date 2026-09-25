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

// The string: a ring buffer. At the engine's half sample rate (19.6 kHz),
// 192 samples reach down to about 102 Hz.
static const uint8_t kKarplusBufferSize = 192;

// Excitation types.
enum KsExcitation {
  KS_EXC_NOISE,    // White noise burst
  KS_EXC_CLICK,    // Short impulse
  KS_EXC_BRIGHT,   // Filtered noise (bright)
  KS_EXC_DARK,     // Filtered noise (dark)
  KS_EXC_LAST
};

// Patch field mapping for KS mode:
// osc[0].parameter (offset 1)  = damping (brightness/decay)
// osc[0].range     (offset 2)  = pitch range
// osc[0].detune    (offset 3)  = fine tune
// osc[1].shape     (offset 4)  = excitation type (KsExcitation)
// osc[1].parameter (offset 5)  = excitation color (brightness of burst)
// osc[1].range     (offset 6)  = decay rate modifier
// osc[1].detune    (offset 7)  = pluck position (changes harmonics)
// mix_balance      (offset 8)  = body resonance
// mix_op           (offset 9)  = ensemble rate
// mix_parameter    (offset 10) = ensemble depth
// mix_sub_osc_shape(offset 11) = ensemble spread
// mix_sub_osc      (offset 12) = ensemble mix
// mix_noise        (offset 13) = stiffness
// mix_fuzz         (offset 14) = sustain (feedback)

class KarplusStrong {
 public:
  KarplusStrong() { }

  void Init() {
    write_ = 0;
    excited_ = 0;
    lp_state_ = 0;
    ap_x1_ = ap_y1_ = 0;
    for (uint8_t i = 0; i < kKarplusBufferSize; ++i) {
      delay_line_[i] = 0;
    }
  }

  // Excite the string: fill the whole ring with the pluck.
  void Trigger(uint8_t excitation_type, uint8_t color, uint8_t position) {
    for (uint8_t i = 0; i < kKarplusBufferSize; ++i) {
      int16_t noise = static_cast<int16_t>(Random::GetByte()) - 128;
      int16_t sample;
      switch (excitation_type) {
        case KS_EXC_CLICK:
          sample = (i < 2) ? 8191 : ((i < 4) ? -8191 : 0);
          break;
        case KS_EXC_BRIGHT:
          sample = noise * 48 +
              (static_cast<int16_t>(Random::GetByte()) - 128) * 16;
          break;
        case KS_EXC_DARK:
          sample = i ? (delay_line_[i - 1] * 3 + noise * 32) / 4 : noise * 16;
          break;
        default:
          sample = noise * 64;
          break;
      }
      delay_line_[i] = sample;
    }

    // Pluck position comb filter.
    if (position > 4) {
      uint8_t notch = (static_cast<uint16_t>(kKarplusBufferSize) * position) >> 7;
      if (notch > 1 && notch < kKarplusBufferSize) {
        for (uint8_t i = 0; i < kKarplusBufferSize - notch; ++i) {
          delay_line_[i] = (delay_line_[i] + delay_line_[i + notch]) / 2;
        }
      }
    }

    // Excitation color low-pass.
    uint8_t filter_passes = (127 - color) >> 4;
    for (uint8_t pass = 0; pass < filter_passes; ++pass) {
      for (uint8_t i = 1; i < kKarplusBufferSize; ++i) {
        delay_line_[i] = delay_line_[i] / 2 + delay_line_[i - 1] / 2;
      }
    }

    write_ = 0;
    lp_state_ = 0;
    ap_x1_ = ap_y1_ = 0;
    excited_ = 1;
  }

  // period: the string's period in samples, 8.8 fixed point.
  void Render(uint16_t period,
              uint8_t damping, uint8_t decay, uint8_t body,
              uint8_t ens_rate, uint8_t ens_depth, uint8_t ens_spread,
              uint8_t ens_mix, uint8_t stiffness, uint8_t feedback,
              uint16_t* buffer, uint8_t size) {
    if (!excited_) {
      while (size--) {
        *buffer++ = 2048;
      }
      return;
    }

    // Damping: one-pole low-pass in the loop. 0 = bright, 127 = dark;
    // shorter strings get less of it; sustain pushes it brighter.
    uint8_t lp = 255 - (damping << 1);
    if (lp < 2) lp = 2;
    if (period < (64 << 8)) {
      lp += (255 - lp) >> 1;
    } else if (period < (96 << 8)) {
      lp += (255 - lp) >> 2;
    }
    lp += ((255 - lp) * feedback) >> 8;

    // Tuning. The loop delays by N (the ring read distance), plus 1/2 sample
    // for the two-point average, plus the low-pass's delay (1 - a) / a,
    // plus a fractional allpass delay d in [0.1, 1.1) that makes up the rest
    // exactly (Jaffe & Smith). All in 8.8 fixed point.
    uint16_t lp_delay = (static_cast<uint32_t>(256 - lp) << 8) / lp;
    int32_t rest = static_cast<int32_t>(period) - 128 - lp_delay;
    if (rest < (2 << 8)) rest = 2 << 8;
    uint8_t n = (rest - 26) >> 8;
    if (n > kKarplusBufferSize - 3) n = kKarplusBufferSize - 3;
    int16_t d = rest - (static_cast<int16_t>(n) << 8);
    if (d > 512) d = 512;
    // Allpass coefficient (1 - d) / (1 + d) in Q16, as a magnitude and a
    // sign (it is only slightly negative, for d just above 1).
    int32_t c = (static_cast<int32_t>(256 - d) << 16) / (256 + d);
    uint8_t c_negative = c < 0;
    uint16_t c_abs = c_negative ? -c : c;

    uint8_t decay_amount = decay >> 1;
    uint8_t stiff_offset = (n >> 3) + 1;
    uint8_t body_offset = n / 3;
    uint16_t lfo_inc = static_cast<uint16_t>(ens_rate + 1) << 5;

    while (size--) {
      // The two oldest samples of the string, averaged.
      int16_t r = write_ - n;
      if (r < 0) r += kKarplusBufferSize;
      int16_t r1 = r - 1;
      if (r1 < 0) r1 += kKarplusBufferSize;
      int16_t avg = delay_line_[r] / 2 + delay_line_[r1] / 2;

      // (Blends are written as a - a*k + b*k: no intermediate difference can
      // overflow 16 bits, which is AVR's int.)
      lp_state_ += S16U8MulShift8(avg, lp) - S16U8MulShift8(lp_state_, lp);
      int16_t x = lp_state_;

      // Fractional delay: y = c (x - y1) + x1. The string runs within
      // +/-8191, so x - y1 fits 16 bits; clamp the allpass's brief overshoot.
      int16_t ap = S16U16MulShift16(x - ap_y1_, c_abs);
      int16_t y = (c_negative ? -ap : ap) + ap_x1_;
      if (y > 16383) y = 16383;
      if (y < -16383) y = -16383;
      ap_x1_ = x;
      ap_y1_ = y;

      // Stiffness and body: blend in other points of the string.
      if (stiffness > 4) {
        int16_t p = r + stiff_offset;
        if (p >= kKarplusBufferSize) p -= kKarplusBufferSize;
        uint8_t a = stiffness >> 1;
        y += S16U8MulShift8(delay_line_[p], a) - S16U8MulShift8(y, a);
      }
      if (body > 4) {
        int16_t p = r + body_offset;
        if (p >= kKarplusBufferSize) p -= kKarplusBufferSize;
        uint8_t a = body >> 1;
        y += S16U8MulShift8(delay_line_[p], a) - S16U8MulShift8(y, a);
      }

      // Decay: every sample passes once per period, so this is the loss
      // per trip around the string.
      if (decay_amount) {
        y -= S16U8MulShift8(y, decay_amount) >> 1;
      }

      delay_line_[write_] = y;
      if (++write_ >= kKarplusBufferSize) write_ = 0;

      // Ensemble: two more read heads swept by a triangle LFO.
      int16_t output = y;
      if (ens_mix && ens_depth) {
        ens_lfo_phase_ += lfo_inc;
        uint8_t lfo_8 = ens_lfo_phase_ >> 8;
        int8_t lfo = (ens_lfo_phase_ & 0x8000)
            ? static_cast<int8_t>(255 - lfo_8) : static_cast<int8_t>(lfo_8);
        int16_t o2 = (static_cast<int16_t>(lfo) * ens_depth) >> 7;
        int16_t o3 = -o2 + (ens_spread >> 1);
        int16_t p2 = r + o2, p3 = r + o3;
        if (p2 < 0) p2 += kKarplusBufferSize;
        if (p2 >= kKarplusBufferSize) p2 -= kKarplusBufferSize;
        if (p3 < 0) p3 += kKarplusBufferSize;
        if (p3 >= kKarplusBufferSize) p3 -= kKarplusBufferSize;
        uint8_t wet = ens_mix >> 1;
        output = S16U8MulShift8(y, 255 - ens_mix) +
            S16U8MulShift8(delay_line_[p2], wet) +
            S16U8MulShift8(delay_line_[p3], wet);
      }

      // 12-bit DAC sample; the string runs at half scale (+/-8191).
      int16_t out = output >> 2;
      if (out > 2047) out = 2047;
      if (out < -2048) out = -2048;
      *buffer++ = out + 2048;
    }
  }

 private:
  int16_t delay_line_[kKarplusBufferSize];
  int16_t lp_state_;
  int16_t ap_x1_, ap_y1_;
  uint8_t write_;
  uint8_t excited_;
  uint16_t ens_lfo_phase_;

  DISALLOW_COPY_AND_ASSIGN(KarplusStrong);
};

}  // namespace ambika

#endif  // VOICECARD_KARPLUS_H_
