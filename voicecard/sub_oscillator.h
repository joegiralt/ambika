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

#ifndef VOICECARD_SUB_OSCILLATOR_H_
#define VOICECARD_SUB_OSCILLATOR_H_

#include "avrlib/op.h"

#include "common/patch.h"

using namespace avrlib;

namespace ambika {

class SubOscillator {
 public:
  SubOscillator() { }

  static inline void set_increment(uint24_t increment) {
    phase_increment_ = increment;
  }

  static inline void Render(uint8_t shape, uint8_t* buffer, uint8_t amount) {
    uint24_t increment = phase_increment_;
    if (shape >= 3) {
      increment = U24ShiftRight(increment);
      shape -= 3;
    }
    uint8_t size = kAudioBlockSize;
    uint8_t pulse_width = shape == 0 ? 0x80 : 0x40;
    uint8_t sub_gain = amount;
    uint8_t mix_gain = ~sub_gain;
#if defined(__AVR__)
#ifdef BENCH_SUBTEST
    if (!force_c_loop_) {
#endif
    // The loop below as one block (BENCH_SUBTEST compares them): about 25
    // cycles a sample from 45.
    uint8_t pf = phase_.fractional;
    uint16_t pi = phase_.integral;
    uint8_t tri = shape == 1, v, t, n = kAudioBlockSize;
    uint16_t acc;
    asm volatile(
      "1:                       \n\t"
      "add  %[pf], %[inf]       \n\t"   /* phase += increment */
      "adc  %A[pi], %A[ini]     \n\t"
      "adc  %B[pi], %B[ini]     \n\t"
      "tst  %[tri]              \n\t"
      "brne 2f                  \n\t"
      "ldi  %[v], 0xFF          \n\t"   /* pulse: high byte < width ? 0 : 255 */
      "cp   %B[pi], %[pw]       \n\t"
      "brsh 3f                  \n\t"
      "ldi  %[v], 0             \n\t"
      "rjmp 3f                  \n\t"
      "2: movw %A[acc], %A[pi]  \n\t"   /* triangle: tri = pi >> 7 */
      "lsl  %A[acc]             \n\t"
      "rol  %B[acc]             \n\t"
      "mov  %[v], %B[acc]       \n\t"
      "sbrs %B[pi], 7           \n\t"   /* pi & 0x8000 ? tri : ~tri */
      "com  %[v]                \n\t"
      "3: ld   %[t], X          \n\t"   /* *buffer = U8Mix(*buffer, v, mix, sub) */
      "mul  %[t], %[mg]         \n\t"
      "movw %A[acc], r0         \n\t"
      "mul  %[v], %[sg]         \n\t"
      "add  %A[acc], r0         \n\t"
      "adc  %B[acc], r1         \n\t"
      "st   X+, %B[acc]         \n\t"
      "dec  %[n]                \n\t"
      "brne 1b                  \n\t"
      "eor  r1, r1              \n\t"
      : [pf] "+r" (pf), [pi] "+r" (pi), [n] "+r" (n), "+x" (buffer),
        [v] "=&a" (v), [t] "=&a" (t), [acc] "=&r" (acc)
      : [inf] "r" (increment.fractional), [ini] "r" (increment.integral),
        [tri] "r" (tri), [pw] "r" (pulse_width),
        [mg] "a" (mix_gain), [sg] "a" (sub_gain)
      : "r0", "r1", "cc", "memory");
    phase_.fractional = pf;
    phase_.integral = pi;
    return;
#ifdef BENCH_SUBTEST
    }
#endif
#endif
    while (size--) {
      phase_ = U24Add(phase_, increment);
      uint8_t v;
      if (shape != 1) {
        v = static_cast<uint8_t>(phase_.integral >> 8) < pulse_width ? 0 : 255;
      } else {
        uint8_t tri = phase_.integral >> 7;
        v = phase_.integral & 0x8000 ? tri : ~tri;
      }
      *buffer = U8Mix(*buffer, v, mix_gain, sub_gain);
      ++buffer;
    }
  }

 private:
#ifdef BENCH_SUBTEST
 public:
  static uint8_t force_c_loop_;
  static void set_phase(uint24_t p) { phase_ = p; }
  static uint24_t phase() { return phase_; }
 private:
#endif
  // Current phase of the oscillator.
  static uint24_t phase_;
  static uint24_t phase_increment_;

  DISALLOW_COPY_AND_ASSIGN(SubOscillator);
};

// The static members are defined here: voice.cc is the only firmware file
// that includes this header. bench/ includes it too and sets this guard.
#ifndef SUB_OSCILLATOR_NO_DEFINITIONS
/* static */
uint24_t SubOscillator::phase_;

/* static */
uint24_t SubOscillator::phase_increment_;

#ifdef BENCH_SUBTEST
uint8_t SubOscillator::force_c_loop_;
#endif
#endif

}  // namespace ambika

#endif  // VOICECARD_SUB_OSCILLATOR_H_
