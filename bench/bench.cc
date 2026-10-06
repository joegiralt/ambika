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
Gpio<PortB, 0> log_vca;
UartSpiMaster<UartSpiPort0, Gpio<PortD, 2>, 2> dac_interface;
static uint8_t update_vca;
static const uint8_t dac_scale = 16;
volatile uint16_t isr_count = 0;
volatile uint16_t underruns = 0;

// Timer1 CTC at 510 cycles stands in for the Timer2 overflow (simavr does not
// fire the phase-correct overflow); same period as the real audio ISR.
ISR(TIMER1_COMPA_vect) {
  static uint8_t sample_counter = 0;
  static Word vca_12bits;
  ++isr_count;
  if (update_vca) {
    dac_interface.Strobe();
    update_vca = 0;
    dac_interface.Overwrite(vca_12bits.bytes[1]);
    dac_interface.Overwrite(vca_12bits.bytes[0]);
    dac_interface.Wait();
    uint16_t next_vca_value;
    if (log_vca.is_low()) {
      next_vca_value = ambika::ResourcesManager::Lookup<uint16_t, uint8_t>(
          lut_res_vca_linearization, voice.vca());
    } else {
      next_vca_value = voice.vca() * dac_scale;
    }
    vca_12bits.value = next_vca_value | 0x1000;
  }
  if (!audio_buffer.readable()) ++underruns;
  uint16_t sample = audio_buffer.ImmediateRead();
  if (++sample_counter >= voice.crush()) {
    dac_interface.Strobe();
    sample_counter = 0;
    Word sample_12bits;
    sample_12bits.value = sample | 0x9000;
    dac_interface.Overwrite(sample_12bits.bytes[1]);
    dac_interface.Overwrite(sample_12bits.bytes[0]);
  }
#ifdef BENCH_RX
  voicecard_rx.Receive();
#endif
}

static const uint8_t kBlocks = 8;
volatile uint16_t cycles[kBlocks];
volatile uint16_t isr_per_block[kBlocks];
volatile uint8_t done = 0;
namespace ambika { volatile uint16_t bench_mark[6]; }
volatile uint16_t marks[kBlocks][6];
volatile uint8_t dbg_vca = 0, dbg_env2 = 0, dbg_engine = 0, dbg_rx = 0;

void __attribute__((noinline)) bench_done() { asm volatile("nop"); }

int main(void) {
  sei();
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
    voice.ProcessBlock(); update_vca = 1;
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
    update_vca = 1;
#ifdef BENCH_RX
    voicecard_rx.Process();
#endif
  }
  done = 1;
  bench_done();
  while (1) { }
}
