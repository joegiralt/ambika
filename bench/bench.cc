// Cycle bench: the real voicecard main loop and audio ISR, with a fixed patch
// and a held note, timing Voice::ProcessBlock with Timer1 in normal mode.
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "voicecard/voicecard.h"
#include "avrlib/boot.h"
#include "avrlib/gpio.h"
#include "avrlib/parallel_io.h"
#include "avrlib/timer.h"
#include "voicecard/audio_out.h"
#include "voicecard/leds.h"
#include "voicecard/resources.h"
#include "voicecard/voice.h"
#include "voicecard/voicecard_rx.h"
#include "bench/patches.h"

using namespace avrlib;
using namespace ambika;

#ifndef BENCH_PATCH
#define BENCH_PATCH patch_sine
#endif
#ifndef BENCH_NOTE
#define BENCH_NOTE 57
#endif

static const uint8_t kPinVcaOut = 3;
static const uint8_t kPinVcfResonanceOut = 5;
static const uint8_t kPinVcfCutoffOut = 6;
PwmOutput<kPinVcfCutoffOut> vcf_cutoff_out;
PwmOutput<kPinVcfResonanceOut> vcf_resonance_out;
PwmOutput<kPinVcaOut> vca_out;
ParallelPort<PortC, PARALLEL_TRIPLE_LOW> vcf_mode;
volatile uint16_t isr_count = 0;
volatile uint16_t underruns = 0;

// Timer1 CTC at 510 cycles stands in for the Timer2 overflow (simavr does not
// fire the phase-correct overflow); same period as the real audio ISR.
ISR(TIMER1_COMPA_vect) {
  ++isr_count;
  if (!audio_buffer.readable()) ++underruns;
  AudioOutTick();
}

static const uint8_t kBlocks = 8;
volatile uint16_t cycles[kBlocks];
volatile uint16_t isr_per_block[kBlocks];
volatile uint8_t done = 0;
namespace ambika { volatile uint16_t bench_mark[6]; }
volatile uint16_t marks[kBlocks][6];
volatile uint8_t dbg_vca = 0, dbg_env2 = 0, dbg_engine = 0, dbg_rx = 0;

void __attribute__((noinline)) bench_done() { asm volatile("nop"); }

#ifdef BENCH_OPCYCLES
int16_t __attribute__((noinline)) OperatorCNoInline(uint8_t w, uint16_t p, uint16_t a) {
  return Fm4Op::OperatorC(w, p, a);
}
#endif
#ifdef BENCH_OPTEST
// Compare the assembly Operator against OperatorC on every wave and 10-bit
// phase, with the phase's upper bits set too, for a set of attenuations.
volatile uint32_t optest_cases = 0;
volatile uint16_t optest_mismatches = 0;
volatile uint16_t optest_first[5];  // wave, phase, att, c, asm
static const uint16_t kAtts[] PROGMEM = {
  0, 1, 0x7F, 0x100, 0x3FF, 0x7FF, 0x858, 0x859, 0x85A, 0xA00, 0xCFF, 0xD00,
  0xD01, 0xFFF, 13 << 8 };
static void OperatorTest() {
  for (uint8_t wave = 0; wave < 8; ++wave) {
    for (uint16_t ph = 0; ph < 1024; ++ph) {
      for (uint8_t hi = 0; hi < 2; ++hi) {
        uint16_t phase = ph | (hi ? 0xFC00 : 0);
        for (uint8_t k = 0; k < sizeof(kAtts) / 2; ++k) {
          uint16_t att = pgm_read_word(&kAtts[k]);
          int16_t c = Fm4Op::OperatorC(wave, phase, att);
          int16_t a = Fm4Op::Operator(wave, phase, att);
          ++optest_cases;
          if (c != a) {
            if (!optest_mismatches) {
              optest_first[0] = wave; optest_first[1] = phase;
              optest_first[2] = att; optest_first[3] = c; optest_first[4] = a;
            }
            ++optest_mismatches;
          }
        }
      }
    }
  }
}
#endif

int main(void) {
  sei();
#ifdef BENCH_OPTEST
  OperatorTest();
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_OPCYCLES
  // Cycles per call: asm Operator, C Operator (not inlined), and one
  // Fm4Op::Sample for algorithm 1 with no feedback.
  TCCR1A = 0; TCCR1B = 1;
  {
    uint16_t t0, t1;
    volatile int16_t sink = 0;
    t0 = TCNT1;
    for (uint8_t i = 0; i < 100; ++i) sink += Fm4Op::Operator(0, i * 7, 0x100);
    t1 = TCNT1; cycles[0] = (t1 - t0) / 100;
    t0 = TCNT1;
    for (uint8_t i = 0; i < 100; ++i) sink += OperatorCNoInline(0, i * 7, 0x100);
    t1 = TCNT1; cycles[1] = (t1 - t0) / 100;
    Fm4Op fm; fm.Init();
    for (uint8_t i = 0; i < 4; ++i) fm.mutable_op(i)->phase_increment = 0x01234567UL * (i + 1);
    t0 = TCNT1;
    for (uint8_t i = 0; i < 100; ++i) sink += fm.Sample(FM_ALG_1, 0, 0, 0, 0, 0x100, 0x100, 0x100, 0x100, 0);
    t1 = TCNT1; cycles[2] = (t1 - t0) / 100;
    t0 = TCNT1;
    for (uint8_t i = 0; i < 100; ++i) sink += fm.Sample(FM_ALG_8, 0, 0, 0, 0, 0x100, 0x100, 0x100, 0x100, 0);
    t1 = TCNT1; cycles[3] = (t1 - t0) / 100;
    t0 = TCNT1;
    for (uint8_t i = 0; i < 100; ++i) { }
    t1 = TCNT1; cycles[4] = (t1 - t0) / 100;  // loop overhead
  }
  done = 1;
  bench_done();
  while (1) { }
#endif
  UCSR0B = 0;
  dac_interface.Init();
  rx_led.set_mode(DIGITAL_OUTPUT);
  note_led.set_mode(DIGITAL_OUTPUT);
  vcf_cutoff_out.Init();
  vcf_resonance_out.Init();
  vca_out.Init();
  vcf_mode.set_mode(DIGITAL_OUTPUT);
  voicecard_rx.Init();
  voice.Init();
  log_vca.set_mode(DIGITAL_INPUT);
  log_vca.High();
  Timer<0>::set_prescaler(1);
  Timer<0>::set_mode(TIMER_PWM_PHASE_CORRECT);
#ifdef BENCH_NOISR
  TCCR1A = 0; TCCR1B = 1;  // free running, 1 cycle per tick
#else
  TCCR1A = 0; TCCR1B = (1 << WGM12) | 1; OCR1A = 509; TIMSK1 = (1 << OCIE1A);
#endif
  memcpy_P(voice.mutable_patch_data(), BENCH_PATCH, sizeof(Patch));
  voice.ResetEngines();
  // Like the real main loop, blocks run before any note arrives, which gives
  // the envelopes their parameters.
#ifdef BENCH_NOISR
  audio_buffer.Flush();
#endif
  voice.ProcessBlock();
  voice.Trigger(BENCH_NOTE << 7, 100, 0);
  // Warm up a few blocks so envelopes leave the attack.
  for (uint8_t i = 0; i < 8; ++i) {
#ifndef BENCH_NOISR
    while (audio_buffer.writable() < kAudioBlockSize) { }
#else
    audio_buffer.Flush();
#endif
    voice.ProcessBlock(); AudioOutUpdateVca();
  }
  dbg_vca = voice.vca(); dbg_env2 = voice.modulation_source(MOD_SRC_ENV_2);
  dbg_engine = voice.mutable_patch_data()[106]; dbg_rx = voicecard_rx.writable();
  for (uint8_t i = 0; i < kBlocks; ++i) {
#ifndef BENCH_NOISR
    while (audio_buffer.writable() < kAudioBlockSize) { }
#else
    audio_buffer.Flush();
#endif
    uint16_t i0 = isr_count;
    uint16_t t0 = TCNT1;
    voice.ProcessBlock();
    uint16_t t1 = TCNT1;
    for (uint8_t k = 0; k < 6; ++k) marks[i][k] = bench_mark[k];
    marks[i][5] = t1;
    uint16_t n = isr_count - i0;
    isr_per_block[i] = n;
#ifdef BENCH_NOISR
    cycles[i] = t1 - t0;
#else
    cycles[i] = static_cast<int32_t>(n) * 510 + (static_cast<int16_t>(t1) - static_cast<int16_t>(t0));
#endif
    vcf_cutoff_out.Write(voice.cutoff());
    vcf_resonance_out.Write(voice.resonance());
    AudioOutUpdateVca();
    voicecard_rx.Process();
  }
  done = 1;
  bench_done();
  while (1) { }
}
