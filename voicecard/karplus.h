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
#include "voicecard/fm4op.h"

using namespace avrlib;

namespace ambika {

// The string: a ring buffer. At the engine's half sample rate (19.6 kHz),
// 192 samples reach down to about 102 Hz.
static const uint8_t kKarplusBufferSize = 192;

// Body resonance (a state-variable band-pass after the string), 8-bit
// fractions at the engine's 19.6 kHz: 2 sin(pi 195 / 19607) and 1/Q = 1/2
// (a wide resonance, roughly 100-300 Hz). Both are powers of two, so the
// filter is shifts: 16/256 = >> 4, 128/256 = >> 1.
static const uint8_t kBodyFrequencyShift = 4;
static const uint8_t kBodyDampingShift = 1;

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

// One-pole low-pass coefficient for the excitation color, by the number of
// two-point-average passes the original ran (0-7): the same -3 dB point as
// that many passes, in one pass.
static const prog_uint8_t kKsColorK[8] PROGMEM = {
    0, 187, 165, 150, 138, 129, 122, 116,
};

// 2^24 / (256 + 8 i), i = 0..65: the reciprocal for the fractional-delay
// allpass coefficient, interpolated, instead of a 32-bit division.
static const prog_uint16_t kKsReciprocal[66] PROGMEM = {
    65535, 63550, 61681, 59919, 58254, 56680, 55188, 53773,
    52429, 51150, 49932, 48771, 47663, 46603, 45590, 44620,
    43691, 42799, 41943, 41121, 40330, 39569, 38836, 38130,
    37449, 36792, 36158, 35545, 34953, 34380, 33825, 33288,
    32768, 32264, 31775, 31301, 30840, 30394, 29959, 29537,
    29127, 28728, 28340, 27962, 27594, 27236, 26887, 26546,
    26214, 25891, 25575, 25267, 24966, 24672, 24385, 24105,
    23831, 23564, 23302, 23046, 22795, 22550, 22310, 22075,
    21845, 21620
};

// Loop state and the block's parameters, in one struct so the assembly
// render can address it all from Y (ldd Y+q, q < 64). Offsets are fixed:
// RenderAsm hard-codes them, and the static_asserts below hold them.
struct KsState {
  int16_t lp_state;       // 0   loop low-pass
  int16_t ap_x1, ap_y1;   // 2   fractional-delay allpass
  int16_t disp_x1, disp_y1;  // 6   dispersion allpass
  int32_t dc;             // 10  DC leak: 2^13 x the running average
  int16_t body_low, body_band;  // 14  body resonance
  uint16_t head1, head2;  // 18  chorus head delays, samples in 8.8
  int16_t head_step1, head_step2;  // 22  per-sample glide this block
  uint16_t c_abs;         // 26  allpass coefficient |c| in Q16
  uint8_t lp;             // 28  loop low-pass coefficient
  uint8_t flags;          // 29  bit0 disp, bit1 stiff, bit2 decay,
                          //     bit3 chorus, bit4 body, bit5 c negative
  uint8_t disp;           // 30  dispersion coefficient
  uint8_t stiff;          // 31  stiffness >> 1
  uint8_t stiff_adj;      // 32  2 * stiff_offset - 2 (bytes past the
                          //     advanced read pointer)
  uint8_t decay;          // 33  loss per trip
  uint8_t ens_mix;        // 34
  uint8_t body_amount;    // 35
  uint8_t body_cut;       // 36
  uint8_t body_trim;      // 37
  uint8_t round;          // 38  alternates 0/1: unbiased allpass rounding
  uint8_t n;              // 39  ring read distance
  uint8_t count;          // 40
  uint16_t rp;            // 41  RenderAsm: read pointer in, ring base
  uint16_t base;          // 43  (there is no register left to pass them)
};

// Galois LFSR step (x^16 + x^14 + x^13 + x^11, as avrlib's Random), written
// as a branch: gcc's code for the negate-and-mask form is twice as long.
#define KS_LFSR_STEP(r) \
    do { uint8_t lsb_ = (r) & 1; (r) >>= 1; if (lsb_) (r) ^= 0xb400; } while (0)

class KarplusStrong {
 public:
  KarplusStrong() { }

  // The loop state lives in Fm4Op::params_, the per-block scratch the FM and
  // West Coast renders use (one engine renders at a time, and an engine
  // change re-runs Init), so it costs no RAM of its own: the voicecard has
  // ~150 bytes of stack left and the pluck fill plus the assembly render's
  // saved registers need some of it. The bench passes its own states.
  static KsState* DefaultState() {
#ifdef __AVR__
    return reinterpret_cast<KsState*>(&Fm4Op::params_);
#else
    static KsState host_state;  // the host build has no params_
    return &host_state;
#endif
  }
  void Init(KsState* state = DefaultState()) {
    s = state;
    write_ = 0;
    excited_ = 0;
    s->lp_state = 0;
    s->ap_x1 = s->ap_y1 = 0;
    s->disp_x1 = s->disp_y1 = 0;
    s->dc = 0;
    s->body_low = s->body_band = 0;
    s->head1 = s->head2 = 96 << 8;
    s->round = 0;
    ens_lfo_phase_ = 0;
    last_lp_ = 0;
    last_disp_ = 0;
    last_period_ = 0;
    for (uint8_t i = 0; i < kKarplusBufferSize; ++i) {
      delay_line_[i] = 0;
    }
  }

  // Excite the string: fill the whole ring with the pluck. This runs
  // between audio blocks, on note-on, so it has to be cheap: the old
  // index-and-divide version took 40,000-65,000 cycles and lapped the
  // output ring on every pluck.
  void Trigger(uint8_t excitation_type, uint8_t color, uint8_t position) {
    int16_t* d = delay_line_;
    uint16_t rng = Random::state();
    int32_t sum = 0;
    switch (excitation_type) {
      case KS_EXC_CLICK:
        for (uint8_t i = 0; i < kKarplusBufferSize; ++i) {
          d[i] = (i < 2) ? 8191 : ((i < 4) ? -8191 : 0);
        }
        break;
      case KS_EXC_BRIGHT:
        for (uint8_t i = kKarplusBufferSize; i--; ) {
          KS_LFSR_STEP(rng);
          int16_t a = static_cast<int8_t>((rng >> 8) ^ 0x80);
          int16_t b = static_cast<int8_t>(rng ^ 0x80);
          int16_t v = a * 48 + b * 16;
          sum += v;
          *d++ = v;
        }
        break;
      case KS_EXC_DARK: {
        int16_t prev = 0;
        for (uint8_t i = kKarplusBufferSize; i--; ) {
          KS_LFSR_STEP(rng);
          int16_t noise = static_cast<int8_t>((rng >> 8) ^ 0x80);
          // First sample: noise * 16; then (prev * 3 + noise * 32) / 4.
          prev = i == kKarplusBufferSize - 1
              ? noise * 16
              : ((prev * 3 + noise * 32) >> 2);
          sum += prev;
          *d++ = prev;
        }
        break;
      }
      default:
        for (uint8_t i = kKarplusBufferSize >> 1; i--; ) {
          KS_LFSR_STEP(rng);
          int16_t v = static_cast<int16_t>(static_cast<int8_t>((rng >> 8) ^ 0x80)) * 64;
          sum += v;
          *d++ = v;
          v = static_cast<int16_t>(static_cast<int8_t>(rng ^ 0x80)) * 64;
          sum += v;
          *d++ = v;
        }
        break;
    }
    Random::Seed(rng);

    // One pass over the ring does the rest: the pluck position comb filter,
    // the excitation color low-pass (the original ran up to seven passes of
    // a two-point average; this is a one-pole with the same bandwidth), and
    // the removal of the burst's average (a KS loop passes DC without loss,
    // so any offset would stay in the string for its whole life). The mean
    // is sum / 192, as (sum >> 6) * 85 >> 8, within 0.4%; the loop's DC leak
    // takes the rest.
    int16_t mean = S16U8MulShift8(sum >> 6, 85);
    uint8_t notch = position > 4
        ? (static_cast<uint16_t>(kKarplusBufferSize) * position) >> 7 : 0;
    uint8_t comb_left = (notch > 1 && notch < kKarplusBufferSize)
        ? kKarplusBufferSize - notch : 0;
    uint8_t k = pgm_read_byte(&kKsColorK[(127 - color) >> 4]);
    int16_t* p = delay_line_;
    const int16_t* q = delay_line_ + notch;
    int16_t state = *p - mean;
    for (uint8_t i = kKarplusBufferSize; i--; ) {
      int16_t v = *p - mean;
      if (comb_left) {
        v = (v + (*q++ - mean)) >> 1;
        --comb_left;
      }
      if (k) {
        state += S16U8MulShift8(v - state, k);
        v = state;
      }
      *p++ = v;
    }

    write_ = 0;
    s->lp_state = 0;
    s->ap_x1 = s->ap_y1 = 0;
    s->disp_x1 = s->disp_y1 = 0;
    s->dc = 0;
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
    SetupBlock(period, damping, decay, body, ens_rate, ens_depth,
               ens_spread, ens_mix, stiffness, feedback, color, size);
#ifdef __AVR__
    RenderAsm(buffer);
#else
    RenderC(buffer);
#endif
  }

  // The block's parameters from the patch values, into s->
  void SetupBlock(uint16_t period,
                  uint8_t damping, uint8_t decay, uint8_t body,
                  uint8_t ens_rate, uint8_t ens_depth, uint8_t ens_spread,
                  uint8_t ens_mix, uint8_t stiffness, uint8_t feedback,
                  uint8_t color, uint8_t size) {
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
    lp = 255 - U8U8MulShift8(255 - lp, 85);  // (255 - lp) / 3

    // Tuning. The loop delays by N (the ring read distance), plus S for the
    // weighted two-point average, plus the low-pass's delay (1 - a) / a,
    // plus a fractional allpass delay d in [0.1, 1.1) that makes up the rest
    // exactly (Jaffe & Smith). All in 8.8 fixed point.
    // (16-bit division: (256 - lp) * 256 is at most 65024.)
    uint8_t disp = color > 64 ? ((color - 64) * 143) >> 6 : 0;  // up to 140
    // The tuning (n, the allpass coefficient and the dispersion) depends
    // only on these three; a plucked note mostly holds its pitch, so the
    // divisions and 32-bit products below run only when one changes.
    if (period == last_period_ && lp == last_lp_ && disp == last_disp_) {
      goto tuned;
    }
    last_period_ = period;
    last_disp_ = disp;
    if (lp != last_lp_) {
      last_lp_ = lp;
      lp_delay_ = (static_cast<uint16_t>(256 - lp) << 8) / lp;
    }
    {
    int32_t rest = static_cast<int32_t>(period) - kAverageWeight - lp_delay_;

    // Metallic: above the middle of the excitation color range, a
    // dispersion allpass (a = -disp/256, up to -0.55) makes higher partials
    // see less delay, so overtones stretch sharp like a stiff steel string
    // (more on higher notes, as on a real one). Its delay at the
    // fundamental, D (1 + (1 - D^2) w^2 / 12) with D = (1 - a) / (1 + a) and
    // w = 2 pi / period, is taken out of the string length.
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
    // sign (it is only slightly negative, for d just above 1):
    // (256 - d) (2^24 / (256 + d)) >> 8, the reciprocal from the table.
    uint8_t ri = d >> 3;
    uint16_t r0 = pgm_read_word(&kKsReciprocal[ri]);
    uint16_t r1 = pgm_read_word(&kKsReciprocal[ri + 1]);
    uint16_t r = r0 - (((r0 - r1) * (d & 7)) >> 3);
    int32_t c = (static_cast<int32_t>(256 - d) * r) >> 8;
    s->n = n;
    s->c_abs = c < 0 ? -c : c;
    s->disp = disp;
    s->flags = c < 0 ? 32 : 0;
    s->stiff_adj = ((n >> 3) + 1) * 2 - 2;
    }
   tuned:
    s->lp = lp;
    s->stiff = stiffness >> 1;
    s->decay = (decay * 43) >> 8;  // (decay / 2) / 3
    s->ens_mix = ens_mix;
    // Ensemble (a chorus): two read heads glide through the string's
    // history around 96 samples back (+/-63), swept by a slow triangle LFO,
    // so the copies detune gently (Doppler). The LFO runs once per block:
    // 0.3-10 Hz, the second head offset in phase by the spread; each head's
    // delay then glides linearly to its new target within the block.
    uint8_t chorus = ens_mix && ens_depth;
    s->head_step1 = s->head_step2 = 0;
    if (chorus) {
      uint8_t inc = 1 + ((static_cast<uint16_t>(ens_rate) * ens_rate) >> 9);
      ens_lfo_phase_ += static_cast<uint16_t>(inc) * size;
      uint16_t target1 = HeadDelay(ens_lfo_phase_, ens_depth);
      uint16_t target2 = HeadDelay(ens_lfo_phase_ + (ens_spread << 8), ens_depth);
      // / size, which is always 20 here (half a block): x 205 / 4096.
      s->head_step1 = S16U8MulShift8(static_cast<int16_t>(target1 - s->head1), 205) >> 4;
      s->head_step2 = S16U8MulShift8(static_cast<int16_t>(target2 - s->head2), 205) >> 4;
    }
    // Body amounts, once per block: a concave curve so half body already
    // gives ~3/4 of the effect.
    uint8_t body_curve = 255 - (body << 1);
    uint8_t body_amount = 255 - ((static_cast<uint16_t>(body_curve) * body_curve) >> 8);
    s->body_amount = body_amount;
    s->body_cut = (body_amount * 230) >> 8;   // 0.9x high-pass
    s->body_trim = (body_amount * 140) >> 8;  // ~-7 dB level
    s->flags = (s->flags & 32) | (s->disp ? 1 : 0) | (stiffness > 4 ? 2 : 0) |
               (s->decay ? 4 : 0) | (chorus ? 8 : 0) | (body > 4 ? 16 : 0);
    s->count = size;
  }

  // The loop, in C: the reference for RenderAsm (bench KSTEST compares
  // them sample for sample) and the host build.
  void RenderC(uint16_t* buffer) {
    const uint8_t n = s->n;
    const uint8_t lp = s->lp;
    const uint8_t flags = s->flags;
    const uint16_t c_abs = s->c_abs;
    uint8_t size = s->count;
    while (size--) {
      // The two oldest samples of the string, averaged with weight S on the
      // older one ("decay stretching", Jaffe & Smith): S (1 - S) sets the
      // loss, a third of a plain 50/50 average's.
      int16_t r = write_ - n;
      if (r < 0) r += kKarplusBufferSize;
      int16_t r1 = r - 1;
      if (r1 < 0) r1 += kKarplusBufferSize;
      // (The string is clamped to +/-16384, so the difference fits 16 bits.)
      int16_t avg = delay_line_[r] +
          S16U8MulShift8(delay_line_[r1] - delay_line_[r], kAverageWeight);

      // (Blends are written a + (b - a) k with one multiply: every string
      // value is clamped before it is written, so differences fit 16 bits,
      // AVR's int.)
      s->lp_state += S16U8MulShift8(avg - s->lp_state, lp);
      int16_t x = s->lp_state;

      // Fractional delay: y = c (x - y1) + x1. The string runs within
      // +/-8191, so x - y1 fits 16 bits; clamp the allpass's brief overshoot.
      int16_t ap = S16U16MulShift16(x - s->ap_y1, c_abs);
      // The multiply rounds down, losing half a unit per sample; a KS loop
      // keeps DC forever, so that would build into an offset. Adding 0 and
      // 1 alternately makes the rounding unbiased.
      s->round ^= 1;
      int16_t y = ((flags & 32) ? -ap : ap) + s->ap_x1 + s->round;
      // Clamp here too, so the allpass's own state can never grow past what
      // the 16-bit difference above can hold.
      y = Clamp(y);
      s->ap_x1 = x;
      s->ap_y1 = y;

      if (flags & 1) {
        // y = a (x - y1) + x1 with a = -disp/256.
        int16_t v = s->disp_x1 - S16U8MulShift8(y - s->disp_y1, s->disp);
        v = Clamp(v);
        s->disp_x1 = y;
        s->disp_y1 = v;
        y = v;
      }

      // Stiffness: blend in another point of the string.
      if (flags & 2) {
        int16_t p = r + (s->stiff_adj >> 1) + 1;
        if (p >= kKarplusBufferSize) p -= kKarplusBufferSize;
        y += S16U8MulShift8(delay_line_[p] - y, s->stiff);
      }

      // DC leak (cutoff ~0.4 Hz, far below any note): rounding errors
      // random-walk in a KS loop, whose DC mode has no loss at all. dc
      // holds 2^13 x the running average; its high word x 8 reads it back
      // without a slow 32-bit shift. Per sample, not per block: holding
      // the estimate for a block delays it ~30 samples, which at the
      // fundamental turns the leak into positive feedback (the host decay
      // test caught it).
      s->dc += y;
      int16_t dc = static_cast<int16_t>(s->dc >> 16) * 8;
      s->dc -= dc;
      y -= dc;

      // Decay: every sample passes once per period, so this is the loss
      // per trip around the string.
      if (flags & 4) {
        y -= S16U8MulShift8(y, s->decay) >> 1;
      }

      y = Clamp(y);
      delay_line_[write_] = y;
      if (++write_ >= kKarplusBufferSize) write_ = 0;

      // Ensemble: the two chorus heads, blended in by the mix.
      int16_t output = y;
      if (flags & 8) {
        s->head1 += s->head_step1;
        s->head2 += s->head_step2;
        int16_t heads = (ReadHead(s->head1) >> 1) + (ReadHead(s->head2) >> 1);
        output = y + S16U8MulShift8(heads - y, s->ens_mix);
      }

      // Body: a soundbox after the string. A Chamberlin state-variable
      // filter at ~195 Hz, Q 2 (input scaled by 1/4 so its states fit 16
      // bits): its low-pass is added and most of its high-pass removed.
      // Outside the loop, so it colours the tone but can't move the pitch.
      int16_t out = output >> 2;  // 12-bit DAC sample (string at half scale)
      if (flags & 16) {
        int16_t in = out;
        s->body_low += s->body_band >> kBodyFrequencyShift;
        int16_t high = in - s->body_low - (s->body_band >> kBodyDampingShift);
        s->body_band += high >> kBodyFrequencyShift;
        // At full body: 2x the (resonant) low-pass added and 0.9x of the
        // high-pass taken away: about +12 dB at 110-220 Hz, -17 dB at 880 Hz
        // and -22 dB above. The low-pass, not the band-pass: at Q 2 the
        // band-pass still passes 15% at 1.3 kHz and would refill the highs.
        // Then the level comes down ~7 dB so the boost can't clip. Mixed at
        // half scale so it all fits 16 bits.
        int16_t half = (out >> 1) + S16U8MulShift8(s->body_low, s->body_amount) -
            (S16U8MulShift8(high, s->body_cut) >> 1);
        half -= S16U8MulShift8(half, s->body_trim);
        out = half * 2;
      }
      if (out > 2047) out = 2047;
      if (out < -2048) out = -2048;
      *buffer++ = out + 2048;
    }
  }

#ifdef __AVR__
  void RenderAsm(uint16_t* buffer);
#endif

  KsState* s;

 private:
  int16_t delay_line_[kKarplusBufferSize];
  uint8_t write_;
  uint8_t excited_;
  uint16_t ens_lfo_phase_;
  uint8_t last_lp_;       // the tuning is cached by (period, lp, disp)
  uint8_t last_disp_;
  uint16_t last_period_;
  uint16_t lp_delay_;

  static inline int16_t Clamp(int16_t y) {
    if (y > 16383) y = 16383;
    if (y < -16384) y = -16384;
    return y;
  }

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

#ifdef __AVR__
// The loop in assembly, same arithmetic as RenderC. Register use:
// r2:r3 read pointer (W - 2n, wrapped; advances with W; s on entry), r4:r5 write
// pointer, r6:r7 and r8:r9 chorus head delays, r10:r11 and r12:r13 the
// ring's base and end, r14 count, r15 flags, r16 lp, r17 S, r18-r25
// scratch, X output, Y = s, Z ring reads. r1 is zero between multiplies.

// (a:b, signed 16) * (k, unsigned 8) >> 8 into r24:r25, as S16U8MulShift8.
// b and k must be in r16-r23.
#define KS_MUL8(a, b, k) \
    "eor  r25, r25            \n\t" \
    "mul  " a ", " k "        \n\t" \
    "mov  r24, r1             \n\t" \
    "mulsu " b ", " k "       \n\t" \
    "add  r24, r0             \n\t" \
    "adc  r25, r1             \n\t" \
    "eor  r1, r1              \n\t"

// Clamp r18:r19 to [-16384, 16383]: t = y + 16384 is in range iff bit 15
// is clear.
#define KS_CLAMP(l1, l2) \
    "movw r20, r18            \n\t" \
    "subi r20, lo8(-16384)    \n\t" \
    "sbci r21, hi8(-16384)    \n\t" \
    "sbrs r21, 7              \n\t" \
    "rjmp " l2 "f             \n\t" \
    "ldi  r18, 0xFF           \n\t" \
    "ldi  r20, 0x3F           \n\t" \
    "sbrc r19, 7              \n\t" \
    "rjmp " l1 "f             \n\t" \
    "mov  r19, r20            \n\t" \
    "rjmp " l2 "f             \n\t" \
    l1 ": clr  r18            \n\t" \
    "ldi  r19, 0xC0           \n\t" \
    l2 ":                     \n\t"

// ReadHead(h = hl:hh) into r24:r25. Uses r20-r23 and Z.
#define KS_READHEAD(hl, hh, l1, l2) \
    "movw r30, r4             \n\t" \
    "mov  r24, " hh "         \n\t" \
    "clr  r25                 \n\t" \
    "lsl  r24                 \n\t" \
    "rol  r25                 \n\t" \
    "sub  r30, r24            \n\t" \
    "sbc  r31, r25            \n\t" \
    "cp   r30, r10            \n\t" \
    "cpc  r31, r11            \n\t" \
    "brsh " l1 "f             \n\t" \
    "subi r30, lo8(-384)      \n\t" \
    "sbci r31, hi8(-384)      \n\t" \
    l1 ": movw r20, r30       \n\t" \
    "ld   r24, Z+             \n\t" \
    "ld   r25, Z              \n\t" \
    "movw r30, r20            \n\t" \
    "cp   r30, r10            \n\t" \
    "cpc  r31, r11            \n\t" \
    "brne " l2 "f             \n\t" \
    "movw r30, r12            \n\t" \
    l2 ": sbiw r30, 2         \n\t" \
    "ld   r22, Z+             \n\t" \
    "ld   r23, Z              \n\t" \
    "sub  r22, r24            \n\t" \
    "sbc  r23, r25            \n\t" \
    "mov  r20, " hl "         \n\t" \
    "mul  r22, r20            \n\t" \
    "mov  r21, r1             \n\t" \
    "mulsu r23, r20           \n\t" \
    "clr  r22                 \n\t" \
    "add  r21, r0             \n\t" \
    "adc  r22, r1             \n\t" \
    "eor  r1, r1              \n\t" \
    "add  r24, r21            \n\t" \
    "adc  r25, r22            \n\t"

inline __attribute__((noinline)) void KarplusStrong::RenderAsm(uint16_t* buffer) {
  int16_t r = write_ - s->n;
  if (r < 0) r += kKarplusBufferSize;
  s->rp = reinterpret_cast<uint16_t>(delay_line_ + r);
  s->base = reinterpret_cast<uint16_t>(delay_line_);
  // r2:r3 carries s in and is then the read pointer.
  register KsState* rp asm("r2") = s;
  register int16_t* wp asm("r4") = delay_line_ + write_;
  register uint16_t h1 asm("r6") = s->head1;
  register uint16_t h2 asm("r8") = s->head2;
  asm volatile(
    "push r28                 \n\t"
    "push r29                 \n\t"
    "movw r28, r2             \n\t"   /* Y = s */
    "ldd  r2, Y+41            \n\t"   /* read pointer */
    "ldd  r3, Y+42            \n\t"
    "ldd  r10, Y+43           \n\t"   /* ring base, end */
    "ldd  r11, Y+44           \n\t"
    "movw r24, r10            \n\t"
    "subi r24, lo8(-384)      \n\t"
    "sbci r25, hi8(-384)      \n\t"
    "movw r12, r24            \n\t"
    "ldd  r14, Y+40           \n\t"   /* count */
    "ldd  r15, Y+29           \n\t"   /* flags */
    "ldd  r16, Y+28           \n\t"   /* lp */
    "ldi  r17, %[S]           \n\t"
    "1:                       \n\t"
    /* older = d[r - 1] (wrapped) */
    "movw r30, r2             \n\t"
    "cp   r30, r10            \n\t"
    "cpc  r31, r11            \n\t"
    "brne 2f                  \n\t"
    "movw r30, r12            \n\t"
    "2: sbiw r30, 2           \n\t"
    "ld   r20, Z+             \n\t"
    "ld   r21, Z              \n\t"
    /* a = d[r]; r += 1 (wrapped) */
    "movw r30, r2             \n\t"
    "ld   r18, Z+             \n\t"
    "ld   r19, Z+             \n\t"
    "movw r2, r30             \n\t"
    "cp   r2, r12             \n\t"
    "cpc  r3, r13             \n\t"
    "brne 3f                  \n\t"
    "movw r2, r10             \n\t"
    "3:                       \n\t"
    /* avg = a + ((older - a) * S >> 8) */
    "sub  r20, r18            \n\t"
    "sbc  r21, r19            \n\t"
    KS_MUL8("r20", "r21", "r17")
    "add  r18, r24            \n\t"
    "adc  r19, r25            \n\t"
    /* lp_state += (avg - lp_state) * lp >> 8; x = lp_state */
    "ldd  r20, Y+0            \n\t"
    "ldd  r21, Y+1            \n\t"
    "sub  r18, r20            \n\t"
    "sbc  r19, r21            \n\t"
    KS_MUL8("r18", "r19", "r16")
    "add  r20, r24            \n\t"
    "adc  r21, r25            \n\t"
    "std  Y+0, r20            \n\t"
    "std  Y+1, r21            \n\t"
    "movw r18, r20            \n\t"
    /* allpass: t = x - ap_y1; ap = S16U16MulShift16(t, c_abs) */
    "ldd  r22, Y+2            \n\t"   /* old ap_x1 */
    "ldd  r23, Y+3            \n\t"
    "std  Y+2, r18            \n\t"   /* ap_x1 = x */
    "std  Y+3, r19            \n\t"
    "ldd  r20, Y+4            \n\t"
    "ldd  r21, Y+5            \n\t"
    "sub  r18, r20            \n\t"
    "sbc  r19, r21            \n\t"
    "ldd  r20, Y+26           \n\t"   /* c_abs */
    "ldd  r21, Y+27           \n\t"
    "push r22                 \n\t"   /* park old ap_x1: r22:r23 are the multiply's temps */
    "push r23                 \n\t"
    "eor  r22, r22            \n\t"
    "mul  r18, r20            \n\t"
    "mov  r23, r1             \n\t"
    "mulsu r19, r21           \n\t"
    "movw r24, r0             \n\t"
    "mul  r21, r18            \n\t"
    "add  r23, r0             \n\t"
    "adc  r24, r1             \n\t"
    "adc  r25, r22            \n\t"
    "mulsu r19, r20           \n\t"
    "sbc  r25, r22            \n\t"
    "add  r23, r0             \n\t"
    "adc  r24, r1             \n\t"
    "adc  r25, r22            \n\t"
    "eor  r1, r1              \n\t"
    /* y = (c negative ? -ap : ap) + old ap_x1 + round; round ^= 1 */
    "sbrs r15, 5              \n\t"
    "rjmp 4f                  \n\t"
    "com  r24                 \n\t"
    "com  r25                 \n\t"
    "adiw r24, 1              \n\t"
    "4: pop  r23              \n\t"
    "pop  r22                 \n\t"
    "add  r24, r22            \n\t"
    "adc  r25, r23            \n\t"
    "ldd  r20, Y+38           \n\t"
    "ldi  r21, 1              \n\t"
    "eor  r20, r21            \n\t"
    "std  Y+38, r20           \n\t"
    "add  r24, r20            \n\t"
    "adc  r25, r1             \n\t"
    "movw r18, r24            \n\t"
    KS_CLAMP("5", "6")
    "std  Y+4, r18            \n\t"   /* ap_y1 = y */
    "std  Y+5, r19            \n\t"
    /* dispersion: v = disp_x1 - (y - disp_y1) * disp >> 8 */
    "sbrs r15, 0              \n\t"
    "rjmp 7f                  \n\t"
    "ldd  r24, Y+8            \n\t"   /* disp_y1 */
    "ldd  r25, Y+9            \n\t"
    "movw r20, r18            \n\t"
    "sub  r20, r24            \n\t"
    "sbc  r21, r25            \n\t"
    "ldd  r22, Y+30           \n\t"
    KS_MUL8("r20", "r21", "r22")
    "ldd  r20, Y+6            \n\t"   /* disp_x1 */
    "ldd  r21, Y+7            \n\t"
    "sub  r20, r24            \n\t"
    "sbc  r21, r25            \n\t"
    "std  Y+6, r18            \n\t"   /* disp_x1 = y */
    "std  Y+7, r19            \n\t"
    "movw r18, r20            \n\t"
    KS_CLAMP("8", "9")
    "std  Y+8, r18            \n\t"   /* disp_y1 = v */
    "std  Y+9, r19            \n\t"
    "7:                       \n\t"
    /* stiffness: y += (d[r + offset] - y) * stiff >> 8 */
    "sbrs r15, 1              \n\t"
    "rjmp 10f                 \n\t"
    "movw r30, r2             \n\t"
    "ldd  r20, Y+32           \n\t"
    "add  r30, r20            \n\t"
    "adc  r31, r1             \n\t"
    "cp   r30, r12            \n\t"
    "cpc  r31, r13            \n\t"
    "brlo 11f                 \n\t"
    "subi r30, lo8(384)       \n\t"
    "sbci r31, hi8(384)       \n\t"
    "11: ld   r20, Z+         \n\t"
    "ld   r21, Z              \n\t"
    "sub  r20, r18            \n\t"
    "sbc  r21, r19            \n\t"
    "ldd  r22, Y+31           \n\t"
    KS_MUL8("r20", "r21", "r22")
    "add  r18, r24            \n\t"
    "adc  r19, r25            \n\t"
    "10:                      \n\t"
    /* dc += y; d = hi16(dc) * 8; dc -= d; y -= d */
    "ldd  r20, Y+10           \n\t"
    "ldd  r21, Y+11           \n\t"
    "ldd  r22, Y+12           \n\t"
    "ldd  r23, Y+13           \n\t"
    "clr  r24                 \n\t"
    "sbrc r19, 7              \n\t"
    "com  r24                 \n\t"
    "add  r20, r18            \n\t"
    "adc  r21, r19            \n\t"
    "adc  r22, r24            \n\t"
    "adc  r23, r24            \n\t"
    "movw r24, r22            \n\t"
    "lsl  r24                 \n\t"
    "rol  r25                 \n\t"
    "lsl  r24                 \n\t"
    "rol  r25                 \n\t"
    "lsl  r24                 \n\t"
    "rol  r25                 \n\t"
    "clr  r0                  \n\t"
    "sbrc r25, 7              \n\t"
    "com  r0                  \n\t"
    "sub  r20, r24            \n\t"
    "sbc  r21, r25            \n\t"
    "sbc  r22, r0             \n\t"
    "sbc  r23, r0             \n\t"
    "std  Y+10, r20           \n\t"
    "std  Y+11, r21           \n\t"
    "std  Y+12, r22           \n\t"
    "std  Y+13, r23           \n\t"
    "sub  r18, r24            \n\t"
    "sbc  r19, r25            \n\t"
    /* decay: y -= (y * decay >> 8) >> 1 */
    "sbrs r15, 2              \n\t"
    "rjmp 12f                 \n\t"
    "ldd  r22, Y+33           \n\t"
    KS_MUL8("r18", "r19", "r22")
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "sub  r18, r24            \n\t"
    "sbc  r19, r25            \n\t"
    "12:                      \n\t"
    KS_CLAMP("13", "14")
    /* d[write] = y; write += 1 (wrapped) */
    "movw r30, r4             \n\t"
    "st   Z+, r18             \n\t"
    "st   Z+, r19             \n\t"
    "movw r4, r30             \n\t"
    "cp   r4, r12             \n\t"
    "cpc  r5, r13             \n\t"
    "brne 15f                 \n\t"
    "movw r4, r10             \n\t"
    "15:                      \n\t"
    /* chorus: heads glide; output = y + (heads - y) * mix >> 8 */
    "sbrs r15, 3              \n\t"
    "rjmp 16f                 \n\t"
    "ldd  r20, Y+22           \n\t"
    "ldd  r21, Y+23           \n\t"
    "add  r6, r20             \n\t"
    "adc  r7, r21             \n\t"
    "ldd  r20, Y+24           \n\t"
    "ldd  r21, Y+25           \n\t"
    "add  r8, r20             \n\t"
    "adc  r9, r21             \n\t"
    KS_READHEAD("r6", "r7", "17", "18")
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "push r24                 \n\t"
    "push r25                 \n\t"
    KS_READHEAD("r8", "r9", "19", "20")
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "pop  r23                 \n\t"
    "pop  r22                 \n\t"
    "add  r24, r22            \n\t"
    "adc  r25, r23            \n\t"
    "sub  r24, r18            \n\t"
    "sbc  r25, r19            \n\t"
    "movw r20, r24            \n\t"
    "ldd  r22, Y+34           \n\t"
    KS_MUL8("r20", "r21", "r22")
    "add  r18, r24            \n\t"
    "adc  r19, r25            \n\t"
    "16:                      \n\t"
    /* out = output >> 2 */
    "asr  r19                 \n\t"
    "ror  r18                 \n\t"
    "asr  r19                 \n\t"
    "ror  r18                 \n\t"
    /* body */
    "sbrs r15, 4              \n\t"
    "rjmp 21f                 \n\t"
    "ldd  r20, Y+16           \n\t"   /* band */
    "ldd  r21, Y+17           \n\t"
    "movw r24, r20            \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "ldd  r22, Y+14           \n\t"   /* low += band >> 4 */
    "ldd  r23, Y+15           \n\t"
    "add  r22, r24            \n\t"
    "adc  r23, r25            \n\t"
    "std  Y+14, r22           \n\t"
    "std  Y+15, r23           \n\t"
    "asr  r21                 \n\t"   /* band >> 1 */
    "ror  r20                 \n\t"
    "movw r24, r18            \n\t"   /* high = in - low - band >> 1 */
    "sub  r24, r22            \n\t"
    "sbc  r25, r23            \n\t"
    "sub  r24, r20            \n\t"
    "sbc  r25, r21            \n\t"
    "movw r20, r24            \n\t"   /* r20:r21 = high */
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "ldd  r0, Y+16            \n\t"   /* band += high >> 4 */
    "add  r0, r24             \n\t"
    "std  Y+16, r0            \n\t"
    "ldd  r0, Y+17            \n\t"
    "adc  r0, r25             \n\t"
    "std  Y+17, r0            \n\t"
    "ldd  r22, Y+36           \n\t"   /* (high * cut >> 8) >> 1 */
    KS_MUL8("r20", "r21", "r22")
    "asr  r25                 \n\t"
    "ror  r24                 \n\t"
    "movw r20, r18            \n\t"   /* half = out >> 1 - that */
    "asr  r21                 \n\t"
    "ror  r20                 \n\t"
    "sub  r20, r24            \n\t"
    "sbc  r21, r25            \n\t"
    "ldd  r18, Y+14           \n\t"   /* + low * amount >> 8 */
    "ldd  r19, Y+15           \n\t"
    "ldd  r22, Y+35           \n\t"
    KS_MUL8("r18", "r19", "r22")
    "add  r20, r24            \n\t"
    "adc  r21, r25            \n\t"
    "ldd  r22, Y+37           \n\t"   /* half -= half * trim >> 8 */
    KS_MUL8("r20", "r21", "r22")
    "sub  r20, r24            \n\t"
    "sbc  r21, r25            \n\t"
    "lsl  r20                 \n\t"   /* out = half * 2 */
    "rol  r21                 \n\t"
    "movw r18, r20            \n\t"
    "21:                      \n\t"
    /* t = out + 2048; clamp to 0..4095; store */
    "subi r18, lo8(-2048)     \n\t"
    "sbci r19, hi8(-2048)     \n\t"
    "cpi  r19, 0x10           \n\t"
    "brlo 22f                 \n\t"
    "sbrc r19, 7              \n\t"
    "rjmp 23f                 \n\t"
    "ldi  r18, 0xFF           \n\t"
    "ldi  r19, 0x0F           \n\t"
    "rjmp 22f                 \n\t"
    "23: clr  r18             \n\t"
    "clr  r19                 \n\t"
    "22: st   X+, r18         \n\t"
    "st   X+, r19             \n\t"
    "dec  r14                 \n\t"
    "breq 24f                 \n\t"
    "rjmp 1b                  \n\t"
    "24:                      \n\t"
    "pop  r29                 \n\t"
    "pop  r28                 \n\t"
    : "+x" (buffer), [rp] "+r" (rp), [wp] "+r" (wp),
      [h1] "+r" (h1), [h2] "+r" (h2)
    : [S] "M" (kAverageWeight)
    : "r10", "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19",
      "r20", "r21", "r22", "r23", "r24", "r25", "r30", "r31", "r0", "r1",
      "cc", "memory");
  write_ = wp - delay_line_;
  s->head1 = h1;
  s->head2 = h2;
}
#endif  // __AVR__

}  // namespace ambika

#endif  // VOICECARD_KARPLUS_H_
