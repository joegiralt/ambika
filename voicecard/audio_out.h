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
// Instance of the audio out class.

#ifndef VOICECARD_AUDIO_OUT_H_
#define VOICECARD_AUDIO_OUT_H_

#include "avrlib/base.h"
#include "avrlib/gpio.h"
#include "avrlib/ring_buffer.h"
#include "avrlib/spi.h"
#include "voicecard/resources.h"
#include "voicecard/voice.h"
#include "voicecard/voicecard.h"
#include "voicecard/voicecard_rx.h"

namespace ambika {

// 12-bit samples for the DAC, centered on 2048.
struct AudioBufferSpecs {
  typedef uint16_t Value;
  enum {
    buffer_size = 128,
    data_size = 8,
  };
};

extern avrlib::RingBuffer<AudioBufferSpecs> audio_buffer;

// The audio ISR body, shared by voicecard.cc and bench/. It has to be a leaf
// (no calls, no multiply): avr-gcc 5 then saves only the registers it uses
// instead of every call-clobbered one, which was most of the ISR's cost.
typedef avrlib::Gpio<avrlib::PortD, 2> DacChipSelect;
static avrlib::UartSpiMaster<avrlib::UartSpiPort0, DacChipSelect, 2>
    dac_interface;
static avrlib::Gpio<avrlib::PortB, 0> log_vca;
static volatile uint8_t update_vca;
static volatile uint16_t vca_word;  // DAC word for the VCA, built by the main loop

// PD2 is the DAC chip select (DacChipSelect); avrlib's Gpio::High() is not
// inlined by avr-gcc 5 at -Os, and a call here costs the whole register set.
static inline void DacStrobe() __attribute__((always_inline));
static inline void DacStrobe() {
  PORTD |= _BV(2);
  PORTD &= ~_BV(2);
}

static inline void AudioOutTick() __attribute__((always_inline));
static inline void AudioOutTick() {
  static uint8_t sample_counter = 0;
  if (update_vca) {
    DacStrobe();
    update_vca = 0;
    uint16_t v = vca_word;
    dac_interface.Overwrite(v >> 8);
    dac_interface.Overwrite(v);
    dac_interface.Wait();
  }
  uint16_t sample = audio_buffer.ImmediateRead();  // 12 bits
  if (++sample_counter >= voice.crush()) {
    DacStrobe();
    sample_counter = 0;
    dac_interface.Overwrite((sample >> 8) | 0x90);
    dac_interface.Overwrite(sample);
  }
  voicecard_rx.Receive();
}

// Once per block, from the main loop: the lookup and multiply used to be in
// the ISR.
static inline void AudioOutUpdateVca() {
  uint16_t v;
  if (log_vca.is_low()) {
    v = ResourcesManager::Lookup<uint16_t, uint8_t>(
        lut_res_vca_linearization, voice.vca());
  } else {
    v = voice.vca() * 16;
  }
  vca_word = v | 0x1000;
  update_vca = 1;
}

}  // namespace ambika

#endif  // VOICECARD_AUDIO_OUT_H_
