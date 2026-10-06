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
#include "voicecard/ring.h"
#include "avrlib/spi.h"
#include "voicecard/resources.h"
#include "voicecard/voice.h"
#include "voicecard/voicecard.h"
#include "voicecard/voicecard_rx.h"

namespace ambika {

// 12-bit samples for the DAC, centered on 2048.
typedef Ring<uint16_t, 128> AudioRing;
extern AudioRing audio_buffer;

typedef avrlib::Gpio<avrlib::PortD, 2> DacChipSelect;
static avrlib::UartSpiMaster<avrlib::UartSpiPort0, DacChipSelect, 2>
    dac_interface;
static avrlib::Gpio<avrlib::PortB, 0> log_vca;

// State shared between the main loop and the audio ISR (audio_out.cc).
extern volatile uint8_t update_vca;
extern volatile uint16_t vca_word;   // DAC word for the VCA, with its command bits
extern volatile uint8_t dac_crush;   // samples per DAC write (the classic crush)
extern volatile uint8_t dac_sample_counter;

// Once per block, from the main loop.
static inline void AudioOutUpdateVca() {
  uint16_t v;
  if (log_vca.is_low()) {
    v = ResourcesManager::Lookup<uint16_t, uint8_t>(
        lut_res_vca_linearization, voice.vca());
  } else {
    v = voice.vca() * 16;
  }
  dac_crush = voice.crush();
  vca_word = v | 0x1000;
  update_vca = 1;
}

// The audio ISR, one sample period: send the VCA word when the main loop has
// a new one, read a sample from the ring and send it to the DAC, poll the
// SPI slave for a byte from the controller. Hand written because the ISR
// runs 39,216 times a second and every cycle here is taken from the voice:
// the C version cost about 122 cycles, this is about 70. It saves only what
// it uses: r24, r25, r30, r31 and SREG. r1 is not assumed to be zero.
//
// Written as a macro so bench/ can insert its own counting between the save
// and the body. `extra` is a string of assembly that may use r24 and r25.
//
// I/O addresses (ATmega328P): PORTD 0x0b (DAC chip select on PD2), SPSR 0x2d,
// SPDR 0x2e, UCSR0A 0xc0, UDR0 0xc6.
#define AUDIO_ISR(extra) asm volatile( \
    "push r24                 \n\t" \
    "in   r24, __SREG__       \n\t" \
    "push r24                 \n\t" \
    "push r25                 \n\t" \
    "push r30                 \n\t" \
    "push r31                 \n\t" \
    extra \
    /* VCA word, once per block. The strobe latches the previous sample. */ \
    "lds  r24, %[uv]          \n\t" \
    "tst  r24                 \n\t" \
    "breq 1f                  \n\t" \
    "sbi  0x0b, 2             \n\t" \
    "cbi  0x0b, 2             \n\t" \
    "ldi  r24, 0              \n\t" \
    "sts  %[uv], r24          \n\t" \
    "lds  r24, %[vw]+1        \n\t" \
    "sts  0xc6, r24           \n\t" \
    "lds  r24, %[vw]          \n\t" \
    "sts  0xc6, r24           \n\t" \
    /* clear TXC0, then wait for the whole word to leave the shifter, so */ \
    /* the sample strobe below cannot cut it short */ \
    "lds  r24, 0xc0           \n\t" \
    "andi r24, 0x03           \n\t" \
    "ori  r24, 0x40           \n\t" \
    "sts  0xc0, r24           \n\t" \
    "2: lds r24, 0xc0         \n\t" \
    "sbrs r24, 6              \n\t" \
    "rjmp 2b                  \n\t" \
    "1:                       \n\t" \
    /* sample = audio_buffer.ImmediateRead(): r25 high byte, r30 low byte */ \
    "lds  r24, %[rp]          \n\t" \
    "mov  r30, r24            \n\t" \
    "ldi  r31, 0              \n\t" \
    "lsl  r30                 \n\t" \
    "rol  r31                 \n\t" \
    "subi r30, lo8(-(%[ab]))  \n\t" \
    "sbci r31, hi8(-(%[ab]))  \n\t" \
    "ldd  r25, Z+1            \n\t" \
    "ld   r30, Z              \n\t" \
    "subi r24, 0xff           \n\t" \
    "andi r24, 0x7f           \n\t" \
    "sts  %[rp], r24          \n\t" \
    /* if (++sample_counter >= crush) { counter = 0; strobe; send } */ \
    "lds  r24, %[sc]          \n\t" \
    "subi r24, 0xff           \n\t" \
    "lds  r31, %[cr]          \n\t" \
    "cp   r24, r31            \n\t" \
    "brlo 3f                  \n\t" \
    "ldi  r24, 0              \n\t" \
    "sbi  0x0b, 2             \n\t" \
    "cbi  0x0b, 2             \n\t" \
    "ori  r25, 0x90           \n\t" \
    "sts  0xc6, r25           \n\t" \
    "sts  0xc6, r30           \n\t" \
    "3: sts %[sc], r24        \n\t" \
    /* voicecard_rx.Receive(): a byte from the controller into its ring */ \
    "in   r24, 0x2d           \n\t" \
    "sbrs r24, 7              \n\t" \
    "rjmp 4f                  \n\t" \
    "ldi  r24, 0xff           \n\t" \
    "sts  %[led], r24         \n\t" \
    "in   r25, 0x2e           \n\t" \
    "out  0x2e, r24           \n\t" \
    "lds  r24, %[wp]          \n\t" \
    "mov  r30, r24            \n\t" \
    "ldi  r31, 0              \n\t" \
    "subi r30, lo8(-(%[rb]))  \n\t" \
    "sbci r31, hi8(-(%[rb]))  \n\t" \
    "st   Z, r25              \n\t" \
    "subi r24, 0xff           \n\t" \
    "sts  %[wp], r24          \n\t" \
    "4:                       \n\t" \
    "pop  r31                 \n\t" \
    "pop  r30                 \n\t" \
    "pop  r25                 \n\t" \
    "pop  r24                 \n\t" \
    "out  __SREG__, r24       \n\t" \
    "pop  r24                 \n\t" \
    "reti                     \n\t" \
    : : [uv] "i" (&update_vca), [vw] "i" (&vca_word), \
        [sc] "i" (&dac_sample_counter), [cr] "i" (&dac_crush), \
        [rp] "i" (&AudioRing::read_ptr_), [ab] "i" (AudioRing::buffer_), \
        [wp] "i" (&VoicecardProtocolRx::RxRing::write_ptr_), \
        [rb] "i" (VoicecardProtocolRx::RxRing::buffer_), \
        [led] "i" (&VoicecardProtocolRx::rx_led_counter_))

}  // namespace ambika

#endif  // VOICECARD_AUDIO_OUT_H_
