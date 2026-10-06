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
// Ring buffer with the same shape as avrlib's RingBuffer, with public members
// so the assembly audio ISR in audio_out.h can reach the pointers and storage.
// size is a power of two, at most 256. Like avrlib's, reads do not check for
// data and writes do not check for room: the callers do.

#ifndef VOICECARD_RING_H_
#define VOICECARD_RING_H_

#include <string.h>

#include "avrlib/base.h"

namespace ambika {

template<typename Value, uint16_t size>
struct Ring {
  static Value buffer_[size];
  static volatile uint8_t read_ptr_;
  static volatile uint8_t write_ptr_;

  static inline uint8_t writable() {
    return (read_ptr_ - write_ptr_ - 1) & (size - 1);
  }
  static inline uint8_t readable() {
    return (write_ptr_ - read_ptr_) & (size - 1);
  }
  static inline void Overwrite(Value v) {
    uint8_t w = write_ptr_;
    buffer_[w] = v;
    write_ptr_ = (w + 1) & (size - 1);
  }
  static inline void Overwrite2(Value a, Value b) {
    uint8_t w = write_ptr_;
    buffer_[w] = a;
    buffer_[w + 1] = b;
    write_ptr_ = (w + 2) & (size - 1);
  }
  static inline Value ImmediateRead() {
    uint8_t r = read_ptr_;
    Value v = buffer_[r];
    read_ptr_ = (r + 1) & (size - 1);
    return v;
  }
  // n values in one or two memcpys (the ring wraps). Cheaper than n/2
  // Overwrite2 calls: about a quarter of the cycles for a 40-sample block.
  static inline void WriteBlock(const Value* src, uint8_t n) {
    uint8_t w = write_ptr_;
    uint8_t first = size - w;
    if (first > n) first = n;
    memcpy(&buffer_[w], src, first * sizeof(Value));
    if (first < n) {
      memcpy(&buffer_[0], src + first, (n - first) * sizeof(Value));
    }
    write_ptr_ = (w + n) & (size - 1);
  }
  static inline void Flush() { write_ptr_ = read_ptr_; }
};

template<typename V, uint16_t n> V Ring<V, n>::buffer_[n];
template<typename V, uint16_t n> volatile uint8_t Ring<V, n>::read_ptr_ = 0;
template<typename V, uint16_t n> volatile uint8_t Ring<V, n>::write_ptr_ = 0;

}  // namespace ambika

#endif  // VOICECARD_RING_H_
