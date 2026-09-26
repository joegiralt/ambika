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

// Body resonance (a state-variable band-pass after the string), 8-bit
// fractions at the engine's 19.6 kHz: 2 sin(pi 195 / 19607) and 1/Q = 1/2
// (a wide resonance, roughly 100-300 Hz).
static const uint8_t kBodyFrequency = 16;
static const uint8_t kBodyDamping = 128;

// Weight S of the older sample in the loop's two-point average, 8.8 fixed
// point: S (1 - S) = 1/12, a third of a plain average's 1/4.
static const uint8_t kAverageWeight = 23;

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
// osc[1].parameter (offset 5)  = excitation color (brightness of burst;
//                                 above 64 also makes the string metallic)
// osc[1].range     (offset 6)  = decay rate modifier
// osc[1].detune    (offset 7)  = pluck position (changes harmonics)
// mix_balance      (offset 8)  = body (soundbox resonance amount)
// mix_op           (offset 9)  = ensemble rate
// mix_parameter    (offset 10) = ensemble depth
// mix_sub_osc_shape(offset 11) = ensemble spread
// mix_sub_osc      (offset 12) = ensemble mix
// mix_noise        (offset 13) = stiffness
// mix_fuzz         (offset 14) = sustain (feedback)

// Dispersion allpass (a = -i/256): its delay at low frequencies,
// D = (1 - a) / (1 + a), 8.8 fixed point: round(256 (256 + i) / (256 - i)).
static const prog_uint16_t kDispersionDelay[] PROGMEM = {
    256, 258, 260, 262, 264, 266, 268, 270, 273, 275,
    277, 279, 281, 283, 286, 288, 290, 292, 295, 297,
    299, 302, 304, 307, 309, 311, 314, 316, 319, 321,
    324, 327, 329, 332, 334, 337, 340, 343, 345, 348,
    351, 354, 356, 359, 362, 365, 368, 371, 374, 377,
    380, 383, 387, 390, 393, 396, 399, 403, 406, 409,
    413, 416, 420, 423, 427, 430, 434, 438, 441, 445,
    449, 452, 456, 460, 464, 468, 472, 476, 480, 485,
    489, 493, 497, 502, 506, 511, 515, 520, 524, 529,
    534, 538, 543, 548, 553, 558, 563, 568, 574, 579,
    584, 590, 595, 601, 606, 612, 618, 624, 630, 636,
    642, 648, 654, 661, 667, 674, 680, 687, 694, 701,
    708, 715, 722, 730, 737, 745, 752, 760, 768, 776,
    784, 793, 801, 810, 818, 827, 836, 845, 855, 864,
    874, 884,
};

// w^2 / 12 for a string of P samples (w = 2 pi / P), 0.16 fixed point:
// round(65536 (4 pi^2 / 12) / P^2), for P = 8..64 (0 below).
static const prog_uint16_t kDispersionW2[] PROGMEM = {
    0, 0, 0, 0, 0, 0, 0, 0, 3369, 2662,
    2156, 1782, 1497, 1276, 1100, 958, 842, 746, 665, 597,
    539, 489, 445, 408, 374, 345, 319, 296, 275, 256,
    240, 224, 211, 198, 187, 176, 166, 157, 149, 142,
    135, 128, 122, 117, 111, 106, 102, 98, 94, 90,
    86, 83, 80, 77, 74, 71, 69, 66, 64, 62,
    60, 58, 56, 54, 53,
};

class KarplusStrong {
 public:
  KarplusStrong() { }

  void Init() {
    write_ = 0;
    excited_ = 0;
    lp_state_ = 0;
    ap_x1_ = ap_y1_ = 0;
    disp_x1_ = disp_y1_ = 0;
    dc_ = 0;
    body_low_ = body_band_ = 0;
    head1_ = head2_ = 96 << 8;
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

    // Remove the pluck's average: a KS loop passes DC without loss, so any
    // offset in the burst would stay in the string for its whole life.
    int32_t sum = 0;
    for (uint8_t i = 0; i < kKarplusBufferSize; ++i) {
      sum += delay_line_[i];
    }
    int16_t mean = sum / kKarplusBufferSize;
    for (uint8_t i = 0; i < kKarplusBufferSize; ++i) {
      delay_line_[i] -= mean;
    }

    write_ = 0;
    lp_state_ = 0;
    ap_x1_ = ap_y1_ = 0;
    disp_x1_ = disp_y1_ = 0;
    dc_ = 0;
    excited_ = 1;
  }

  // period: the string's period in samples, 8.8 fixed point.
  void Render(uint16_t period,
              uint8_t damping, uint8_t decay, uint8_t body,
              uint8_t ens_rate, uint8_t ens_depth, uint8_t ens_spread,
              uint8_t ens_mix, uint8_t stiffness, uint8_t feedback,
              uint8_t color, uint16_t* buffer, uint8_t size) {
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
    // Strings ring about 3x longer than a plain KS loop: every loss in the
    // loop is a third as strong (for a one-pole low-pass, the loss per trip
    // scales with 1 - a).
    lp = 255 - (255 - lp) / 3;

    // Tuning. The loop delays by N (the ring read distance), plus S for the
    // weighted two-point average, plus the low-pass's delay (1 - a) / a,
    // plus a fractional allpass delay d in [0.1, 1.1) that makes up the rest
    // exactly (Jaffe & Smith). All in 8.8 fixed point.
    // (16-bit division: (256 - lp) * 256 is at most 65024.)
    uint16_t lp_delay = (static_cast<uint16_t>(256 - lp) << 8) / lp;
    int32_t rest = static_cast<int32_t>(period) - kAverageWeight - lp_delay;

    // Metallic: above the middle of the excitation color range, a
    // dispersion allpass (a = -disp/256, up to -0.55) makes higher partials
    // see less delay, so overtones stretch sharp like a stiff steel string
    // (more on higher notes, as on a real one). Its delay at the
    // fundamental, D (1 + (1 - D^2) w^2 / 12) with D = (1 - a) / (1 + a) and
    // w = 2 pi / period, is taken out of the string length.
    uint8_t disp = color > 64 ? ((color - 64) * 143) >> 6 : 0;  // up to 140
    uint8_t string_samples = period >> 8;
    if (string_samples < 24) {
      // Very short strings (around E6 and up): scale it down, or its
      // ~3.5-sample delay swamps the string and the estimate stops holding.
      disp = (static_cast<uint32_t>(disp) * string_samples * 43) >> 10;
    }
    if (disp) {
      int16_t d = pgm_read_word(&kDispersionDelay[disp]);
      rest -= d;
      if (string_samples < 64) {
        // The w^2 correction matters only on short strings (below that it
        // is under a cent).
        int32_t one_minus_d2 = 256 - ((static_cast<int32_t>(d) * d) >> 8);
        int32_t w2_12 = pgm_read_word(&kDispersionW2[string_samples]);
        rest -= (((static_cast<int32_t>(d) * one_minus_d2) >> 8) * w2_12) >> 16;
      }
    }
    if (rest < (2 << 8)) rest = 2 << 8;
    uint8_t n = (rest - 26) >> 8;
    if (n > kKarplusBufferSize - 3) n = kKarplusBufferSize - 3;
    int16_t d = rest - (static_cast<int16_t>(n) << 8);
    if (d > 512) d = 512;
    // Allpass coefficient (1 - d) / (1 + d) in Q16, as a magnitude and a
    // sign (it is only slightly negative, for d just above 1).
    int32_t c = (static_cast<int32_t>(256 - d) * 65536) / (256 + d);
    uint8_t c_negative = c < 0;
    uint16_t c_abs = c_negative ? -c : c;

    uint8_t decay_amount = (decay * 43) >> 8;  // (decay / 2) / 3
    uint8_t stiff_offset = (n >> 3) + 1;
    // Ensemble (a chorus): two read heads glide through the string's
    // history around 96 samples back (+/-63), swept by a slow triangle LFO,
    // so the copies detune gently (Doppler). The LFO runs once per block:
    // 0.3-10 Hz, the second head offset in phase by the spread; each head's
    // delay then glides linearly to its new target within the block.
    uint8_t chorus = ens_mix && ens_depth;
    int16_t head_step1 = 0, head_step2 = 0;
    if (chorus) {
      uint8_t inc = 1 + ((static_cast<uint16_t>(ens_rate) * ens_rate) >> 9);
      ens_lfo_phase_ += static_cast<uint16_t>(inc) * size;
      uint16_t target1 = HeadDelay(ens_lfo_phase_, ens_depth);
      uint16_t target2 = HeadDelay(ens_lfo_phase_ + (ens_spread << 8), ens_depth);
      // / size, which is always 20 here (half a block): x 205 / 4096.
      head_step1 = S16U8MulShift8(static_cast<int16_t>(target1 - head1_), 205) >> 4;
      head_step2 = S16U8MulShift8(static_cast<int16_t>(target2 - head2_), 205) >> 4;
    }
    // Body amounts, once per block: a concave curve so half body already
    // gives ~3/4 of the effect.
    uint8_t body_curve = 255 - (body << 1);
    uint8_t body_amount = 255 - ((static_cast<uint16_t>(body_curve) * body_curve) >> 8);
    uint8_t body_cut = (body_amount * 230) >> 8;   // 0.9x high-pass
    uint8_t body_trim = (body_amount * 140) >> 8;  // ~-7 dB level

    while (size--) {
      // The two oldest samples of the string, averaged with weight S on the
      // older one ("decay stretching", Jaffe & Smith): S (1 - S) sets the
      // loss, a third of a plain 50/50 average's.
      int16_t r = write_ - n;
      if (r < 0) r += kKarplusBufferSize;
      int16_t r1 = r - 1;
      if (r1 < 0) r1 += kKarplusBufferSize;
      // (The string is clamped to +/-16383, so the difference fits 16 bits.)
      int16_t avg = delay_line_[r] +
          S16U8MulShift8(delay_line_[r1] - delay_line_[r], kAverageWeight);

      // (Blends are written a + (b - a) k with one multiply: every string
      // value is clamped to +/-16383 before it is written, so differences
      // fit 16 bits, AVR's int.)
      lp_state_ += S16U8MulShift8(avg - lp_state_, lp);
      int16_t x = lp_state_;

      // Fractional delay: y = c (x - y1) + x1. The string runs within
      // +/-8191, so x - y1 fits 16 bits; clamp the allpass's brief overshoot.
      int16_t ap = S16U16MulShift16(x - ap_y1_, c_abs);
      // The multiply rounds down, losing half a unit per sample; a KS loop
      // keeps DC forever, so that would build into an offset. Adding 0 and
      // 1 alternately makes the rounding unbiased.
      round_ ^= 1;
      int16_t y = (c_negative ? -ap : ap) + ap_x1_ + round_;
      // Clamp here too, so the allpass's own state can never grow past what
      // the 16-bit difference above can hold.
      if (y > 16383) y = 16383;
      if (y < -16383) y = -16383;
      ap_x1_ = x;
      ap_y1_ = y;

      if (disp) {
        // y = a (x - y1) + x1 with a = -disp/256.
        int16_t v = disp_x1_ - S16U8MulShift8(y - disp_y1_, disp);
        if (v > 16383) v = 16383;
        if (v < -16383) v = -16383;
        disp_x1_ = y;
        disp_y1_ = v;
        y = v;
      }

      // Stiffness: blend in another point of the string.
      if (stiffness > 4) {
        int16_t p = r + stiff_offset;
        if (p >= kKarplusBufferSize) p -= kKarplusBufferSize;
        y += S16U8MulShift8(delay_line_[p] - y, stiffness >> 1);
      }


      // DC leak (cutoff ~0.4 Hz, far below any note): rounding errors
      // random-walk in a KS loop, whose DC mode has no loss at all. dc_
      // holds 2^13 x the running average; its high word x 8 reads it back
      // without a slow 32-bit shift.
      dc_ += y;
      int16_t dc = static_cast<int16_t>(dc_ >> 16) * 8;
      dc_ -= dc;
      y -= dc;

      // Decay: every sample passes once per period, so this is the loss
      // per trip around the string.
      if (decay_amount) {
        y -= S16U8MulShift8(y, decay_amount) >> 1;
      }

      if (y > 16383) y = 16383;
      if (y < -16383) y = -16383;
      delay_line_[write_] = y;
      if (++write_ >= kKarplusBufferSize) write_ = 0;

      // Ensemble: the two chorus heads, blended in by the mix.
      int16_t output = y;
      if (chorus) {
        head1_ += head_step1;
        head2_ += head_step2;
        int16_t heads = ReadHead(head1_) / 2 + ReadHead(head2_) / 2;
        output = y + S16U8MulShift8(heads - y, ens_mix);
      }

      // Body: a soundbox after the string. A Chamberlin state-variable
      // filter at ~195 Hz, Q 2 (input scaled by 1/4 so its states fit 16
      // bits): its low-pass is added and most of its high-pass removed.
      // Outside the loop, so it colours the tone but can't move the pitch.
      int16_t out = output >> 2;  // 12-bit DAC sample (string at half scale)
      if (body > 4) {
        int16_t in = output >> 2;
        body_low_ += S16U8MulShift8(body_band_, kBodyFrequency);
        int16_t high = in - body_low_ - S16U8MulShift8(body_band_, kBodyDamping);
        body_band_ += S16U8MulShift8(high, kBodyFrequency);
        // At full body: 2x the (resonant) low-pass added and 0.9x of the
        // high-pass taken away: about +12 dB at 110-220 Hz, -17 dB at 880 Hz
        // and -22 dB above. The low-pass, not the band-pass: at Q 2 the
        // band-pass still passes 15% at 1.3 kHz and would refill the highs.
        // Then the level comes down ~7 dB so the boost can't clip. Mixed at
        // half scale so it all fits 16 bits.
        int16_t half = (out >> 1) + S16U8MulShift8(body_low_, body_amount) -
            (S16U8MulShift8(high, body_cut) >> 1);
        half -= S16U8MulShift8(half, body_trim);
        out = half * 2;
      }
      if (out > 2047) out = 2047;
      if (out < -2048) out = -2048;
      *buffer++ = out + 2048;
    }
  }

 private:
  int16_t delay_line_[kKarplusBufferSize];
  int16_t lp_state_;
  int16_t ap_x1_, ap_y1_;
  uint8_t round_;  // alternates 0/1: unbiased rounding in the allpass
  int32_t dc_;     // DC leak state: 2^13 x the running average
  int16_t body_low_, body_band_;  // body resonance state
  int16_t disp_x1_, disp_y1_;     // dispersion allpass state
  uint8_t write_;
  uint8_t excited_;
  uint16_t ens_lfo_phase_;
  uint16_t head1_, head2_;  // chorus head delays, samples in 8.8

  // Chorus head delay for an LFO phase: 96 samples +/- up to 63 (depth),
  // triangle-shaped, 8.8 fixed point (unsigned: up to 40,832).
  static inline uint16_t HeadDelay(uint16_t phase, uint8_t depth)
      __attribute__((always_inline)) {
    uint16_t u = (phase & 0x8000) ? ~phase : phase;  // 0..32767 and back
    int16_t triangle = static_cast<int16_t>(static_cast<uint16_t>(u << 1) ^ 0x8000);
    return (96 << 8) + S16U8MulShift8(triangle, depth);
  }

  // The string's history `delay` (8.8) samples back, interpolated.
  inline int16_t ReadHead(uint16_t delay) const
      __attribute__((always_inline)) {
    int16_t i = static_cast<int16_t>(write_) - (delay >> 8);
    if (i < 0) i += kKarplusBufferSize;
    int16_t older = i ? i - 1 : kKarplusBufferSize - 1;
    int16_t a = delay_line_[i];
    return a + S16U8MulShift8(delay_line_[older] - a, delay & 0xFF);
  }

  DISALLOW_COPY_AND_ASSIGN(KarplusStrong);
};

}  // namespace ambika

#endif  // VOICECARD_KARPLUS_H_
