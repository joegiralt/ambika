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
#include "voicecard/oscillator.h"
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
volatile uint16_t isr_count asm("bench_isr_count") = 0;
volatile uint16_t underruns = 0;  // no longer counted

// Timer1 CTC at 510 cycles stands in for the Timer2 overflow (simavr does not
// fire the phase-correct overflow); same period as the real audio ISR.
ISR(TIMER1_COMPA_vect, ISR_NAKED) {
  AUDIO_ISR(
    "lds  r24, bench_isr_count   \n\t"
    "subi r24, 0xff              \n\t"
    "sts  bench_isr_count, r24   \n\t"
    "lds  r24, bench_isr_count+1 \n\t"
    "sbci r24, 0xff              \n\t"
    "sts  bench_isr_count+1, r24 \n\t");
}

static const uint8_t kBlocks = 4;
volatile uint16_t cycles[kBlocks];
volatile uint16_t isr_per_block[kBlocks];
volatile uint8_t done = 0;
namespace ambika { volatile uint16_t bench_mark[8]; }
#ifdef BENCH_PROFILE
volatile uint16_t marks[kBlocks][8];
#else
volatile uint16_t marks[1][8];  // RAM is tight: 2 KB minus the voice
#endif
volatile uint8_t dbg_vca = 0, dbg_env2 = 0, dbg_engine = 0, dbg_rx = 0;

void __attribute__((noinline)) bench_done() { asm volatile("nop"); }

#ifdef BENCH_SUBTEST
#define SUB_OSCILLATOR_NO_DEFINITIONS
#include "voicecard/sub_oscillator.h"
volatile uint16_t subtest_blocks = 0;
volatile uint16_t subtest_mismatches = 0;
volatile uint16_t subtest_first[3];
static uint8_t sub_c[kAudioBlockSize], sub_a[kAudioBlockSize];
static void SubTest() {
  static const uint8_t shapes[3] = { 0, 1, 4 };
  static const uint8_t amounts[2] = { 30, 127 };
  for (uint8_t sh = 0; sh < 3; ++sh) {
    for (uint8_t am = 0; am < 2; ++am) {
      uint24_t inc; inc.integral = 0x0234 + sh * 0x301; inc.fractional = 0x77;
      SubOscillator so;
      so.set_increment(inc);
      for (uint8_t blk = 0; blk < 3; ++blk) {
        for (uint8_t i = 0; i < kAudioBlockSize; ++i) { sub_c[i] = (i * 37 + blk * 11) & 0xFF; sub_a[i] = sub_c[i]; }
        uint24_t ph = SubOscillator::phase();
        SubOscillator::force_c_loop_ = 1; so.Render(shapes[sh], sub_c, amounts[am]);
        uint24_t ph_c = SubOscillator::phase();
        SubOscillator::set_phase(ph);
        SubOscillator::force_c_loop_ = 0; so.Render(shapes[sh], sub_a, amounts[am]);
        uint24_t ph_a = SubOscillator::phase();
        ++subtest_blocks;
        if (ph_c.integral != ph_a.integral || ph_c.fractional != ph_a.fractional) ++subtest_mismatches;
        for (uint8_t k = 0; k < kAudioBlockSize; ++k) {
          if (sub_c[k] != sub_a[k]) {
            if (!subtest_mismatches) { subtest_first[0] = sh * 16 + am; subtest_first[1] = sub_c[k]; subtest_first[2] = sub_a[k]; }
            ++subtest_mismatches;
          }
        }
      }
    }
  }
}
#endif
#ifdef BENCH_PMTEST
volatile uint16_t pmtest_blocks = 0;
volatile uint16_t pmtest_mismatches = 0;
volatile uint16_t pmtest_first[3];
static uint8_t pm_in[kAudioBlockSize];
static uint16_t pm_ref[kAudioBlockSize];
static void PmTest() {
  static const uint8_t gains[4][4] = { {255, 0, 255, 0}, {200, 55, 255, 0}, {255, 0, 100, 155}, {128, 127, 0, 255} };
  for (uint8_t g = 0; g < 4; ++g) {
    for (uint8_t blk = 0; blk < 3; ++blk) {
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) pm_in[i] = (i * 53 + blk * 7 + g) & 0xFF;
      uint8_t w = blk == 1 ? 100 : 8;
      AudioRing::write_ptr_ = w;
      Voice::PostMixC(pm_in, 0x5A + blk, gains[g][0], gains[g][1], gains[g][2], gains[g][3]);
      for (uint8_t k = 0; k < kAudioBlockSize; ++k) pm_ref[k] = AudioRing::buffer_[(w + k) & 127];
      uint8_t wp_c = AudioRing::write_ptr_;
      AudioRing::write_ptr_ = w;
      Voice::PostMixAsm(pm_in, 0x5A + blk, gains[g][0], gains[g][1], gains[g][2], gains[g][3]);
      ++pmtest_blocks;
      if (AudioRing::write_ptr_ != wp_c) ++pmtest_mismatches;
      for (uint8_t k = 0; k < kAudioBlockSize; ++k) {
        uint16_t a = AudioRing::buffer_[(w + k) & 127];
        if (a != pm_ref[k]) {
          if (!pmtest_mismatches) { pmtest_first[0] = g * 64 + k; pmtest_first[1] = pm_ref[k]; pmtest_first[2] = a; }
          ++pmtest_mismatches;
        }
      }
    }
  }
}
#endif
#ifdef BENCH_KSTEST
// KarplusStrong::RenderC against RenderAsm: the same pluck (same RNG seed)
// into two strings, then four blocks each over excitation x metallic x
// damping x chorus x (body, stiffness, sustain, decay) x period.
#include "voicecard/karplus.h"
volatile uint16_t kstest_blocks = 0;
volatile uint16_t kstest_mismatches = 0;
volatile uint16_t kstest_first[4];
volatile uint16_t kstest_offsets[4];
static KarplusStrong ks_c, ks_a;
static uint16_t ks_out_c[20], ks_out_a[20];
static void KsTest() {
  kstest_offsets[0] = offsetof(KsState, dc);
  kstest_offsets[1] = offsetof(KsState, c_abs);
  kstest_offsets[2] = offsetof(KsState, round);
  kstest_offsets[3] = offsetof(KsState, base);
  for (uint8_t cfg = 0; cfg < 64; ++cfg) {
    uint8_t exc = cfg & 3;
    uint8_t color = (cfg & 4) ? 100 : 30;
    uint8_t damping = (cfg & 8) ? 90 : 20;
    uint8_t mix = (cfg & 16) ? 90 : 0, depth = (cfg & 16) ? 60 : 0;
    uint8_t body = (cfg & 32) ? 80 : 0, stiff = (cfg & 32) ? 60 : 0;
    uint8_t sustain = (cfg & 32) ? 50 : 0, decay = (cfg & 32) ? 40 : 0;
    uint16_t period = (14 << 8) + cfg * 700;
    ks_c.Init(); ks_a.Init();
    Random::Seed(1234 + cfg); ks_c.Trigger(exc, color, 64);
    Random::Seed(1234 + cfg); ks_a.Trigger(exc, color, 64);
    for (uint8_t blk = 0; blk < 4; ++blk) {
      ks_c.SetupBlock(period, damping, decay, body, 40, depth, 40, mix, stiff, sustain, color, 20);
      ks_c.RenderC(ks_out_c);
      ks_a.SetupBlock(period, damping, decay, body, 40, depth, 40, mix, stiff, sustain, color, 20);
      ks_a.RenderAsm(ks_out_a);
      ++kstest_blocks;
      for (uint8_t k = 0; k < 20; ++k) {
        if (ks_out_c[k] != ks_out_a[k]) {
          if (!kstest_mismatches) { kstest_first[0] = cfg; kstest_first[1] = blk * 32 + k; kstest_first[2] = ks_out_c[k]; kstest_first[3] = ks_out_a[k]; }
          ++kstest_mismatches;
        }
      }
    }
  }
}
#endif
#ifdef BENCH_WCTEST
// WestCoast::RenderC against RenderAsm: both from Init, same parameters,
// over waveform x fm x sync x colour x sub x (bias, symmetry), four blocks.
#include "voicecard/westcoast.h"
volatile uint16_t wctest_blocks = 0;
volatile uint16_t wctest_mismatches = 0;
volatile uint16_t wctest_first[4];
static WestCoast wc_c, wc_a;
static uint16_t wc_out_c[20], wc_out_a[20];
static void WcTest() {
  for (uint8_t cfg = 0; cfg < 64; ++cfg) {
    uint8_t wave = cfg & 1;
    uint8_t fm = (cfg & 2) ? 60 : 0;
    uint8_t sync = (cfg & 4) ? 40 : 0;
    uint8_t color = (cfg & 8) ? 127 : 60;
    uint8_t sub = (cfg & 16) ? 50 : 0;
    uint8_t bias = (cfg & 32) ? 20 : 64, sym = (cfg & 32) ? 100 : 64;
    uint8_t fold = (cfg & 32) ? 110 : 40;
    wc_c.Init(); wc_a.Init();
    for (uint8_t blk = 0; blk < 4; ++blk) {
      uint32_t inc = 0x00012345UL + blk * 0x111 + cfg * 7;
      wc_c.RenderC(wave, fold, sym, bias, fm, (cfg & 2) ? 2 : -1, 30, color, 20, 40,
                   sub, sync, 200 - blk * 30, inc, wc_out_c, 20);
      wc_a.RenderAsm(wave, fold, sym, bias, fm, (cfg & 2) ? 2 : -1, 30, color, 20, 40,
                     sub, sync, 200 - blk * 30, inc, wc_out_a, 20);
      ++wctest_blocks;
      for (uint8_t k = 0; k < 20; ++k) {
        if (wc_out_c[k] != wc_out_a[k]) {
          if (!wctest_mismatches) { wctest_first[0] = cfg; wctest_first[1] = blk * 32 + k; wctest_first[2] = wc_out_c[k]; wctest_first[3] = wc_out_a[k]; }
          ++wctest_mismatches;
        }
      }
    }
  }
}
#endif
#ifdef BENCH_EXPTEST
// ExpandHalfRateToRing against ExpandHalfRateC: same input and history,
// output read back from the ring, one block starting near the ring's end.
volatile uint16_t exptest_blocks = 0;
volatile uint16_t exptest_mismatches = 0;
volatile uint16_t exptest_first[3];
volatile uint16_t exptest_dbg[12];  // in[0..3], C out[0..3], asm out[0..3] of block 0
static uint16_t exp_in[kAudioBlockSize / 2], exp_out[kAudioBlockSize];
static void ExpTest() {
  uint16_t seed = 0xBEEF;
  for (uint8_t blk = 0; blk < 6; ++blk) {
    uint16_t* wide = Voice::render_wide();
    for (uint8_t i = 0; i < kAudioBlockSize / 2; ++i) {
      seed = seed * 31421 + 6927;
      exp_in[i] = blk == 2 ? (i & 1 ? 4095 : 0) : (seed >> 4);  // 12-bit; one block of full swings
      wide[5 + i] = exp_in[i];  // the engines render after the history
    }
    uint8_t w = blk == 3 ? 100 : 0;
    AudioRing::write_ptr_ = w;
    Voice::ExpandHalfRateC();                       // writes the ring at w
    for (uint8_t k = 0; k < kAudioBlockSize; ++k) exp_out[k] = AudioRing::buffer_[(w + k) & 127];
    for (uint8_t i = 0; i < kAudioBlockSize / 2; ++i) wide[5 + i] = exp_in[i];
    AudioRing::write_ptr_ = w;
    Voice::ExpandHalfRateToRing();
    ++exptest_blocks;
    if (blk == 0) {
      for (uint8_t k = 0; k < 4; ++k) { exptest_dbg[k] = exp_in[k]; exptest_dbg[4 + k] = exp_out[k]; exptest_dbg[8 + k] = AudioRing::buffer_[(w + k) & 127]; }
    }
    for (uint8_t k = 0; k < kAudioBlockSize; ++k) {
      uint16_t a = AudioRing::buffer_[(w + k) & 127];
      if (a != exp_out[k]) {
        if (!exptest_mismatches) { exptest_first[0] = blk * 64 + k; exptest_first[1] = exp_out[k]; exptest_first[2] = a; }
        ++exptest_mismatches;
      }
    }
  }
}
#endif
#ifdef BENCH_OSCTEST
// The assembly wavetable loop against the C one: saw and square (parameter 0
// takes the wavetable path), several notes, increments and parameters, from
// the same phase state.
volatile uint16_t osctest_blocks = 0;
volatile uint16_t osctest_mismatches = 0;
volatile uint16_t osctest_first[4];
static uint8_t osc_buf_c[kAudioBlockSize], osc_buf_a[kAudioBlockSize];
static uint8_t osc_nosync[kAudioBlockSize], osc_syncout[kAudioBlockSize];
static Oscillator c, a;  // per-instance state, zeroed per configuration
static void OscTest() {
  static const uint8_t shapes[3] = { WAVEFORM_SAW, WAVEFORM_SQUARE, WAVEFORM_SQUARE };
  static const uint8_t notes[3] = { 36, 57, 84 };
  static const uint8_t params[3] = { 0, 40, 200 };
  for (uint8_t sh = 0; sh < 3; ++sh) {
    for (uint8_t nt = 0; nt < 3; ++nt) {
      for (uint8_t pr = 0; pr < 3; ++pr) {
        // sh 1: square at pulse width 0 (wavetable); sh 2: PWM
        uint8_t param = sh == 1 ? 0 : (sh == 2 ? 40 + pr * 80 : params[pr]);
        uint24_t inc; inc.integral = 0x0123 * (nt + 1) + pr * 77; inc.fractional = 0x45 + nt;
        memset(&c, 0, sizeof(c)); memset(&a, 0, sizeof(a));
        c.set_parameter(param); a.set_parameter(param);
        for (uint8_t blk = 0; blk < 5; ++blk) {
          c.force_c_loop_ = 1; a.force_c_loop_ = 0;
          c.Render(shapes[sh], notes[nt], inc, osc_nosync, osc_syncout, 0, osc_buf_c);
          a.Render(shapes[sh], notes[nt], inc, osc_nosync, osc_syncout, 0, osc_buf_a);
          ++osctest_blocks;
          for (uint8_t k = 0; k < kAudioBlockSize; ++k) {
            if (osc_buf_c[k] != osc_buf_a[k]) {
              if (!osctest_mismatches) { osctest_first[0] = sh; osctest_first[1] = nt * 16 + pr; osctest_first[2] = osc_buf_c[k]; osctest_first[3] = osc_buf_a[k]; }
              ++osctest_mismatches;
            }
          }
        }
      }
    }
  }
}
#endif
#ifdef BENCH_FMTEST
// RenderC against RenderAsm: every algorithm, several waveform/level sets,
// with and without feedback, phases advancing from the same state.
volatile uint16_t fmtest_blocks = 0;
volatile uint16_t fmtest_mismatches = 0;
volatile uint16_t fmtest_first[4];  // algorithm, set, sample, c value, asm value
static void FmTest() {
  // The last set puts attenuations in every runtime shift range of exp_.
  static const uint8_t waves[4][4] = {{0,0,0,0},{1,2,3,4},{5,6,7,1},{0,1,0,2}};
  static const uint16_t atts[4][4] = {{0,0,0,0},{0x100,0x200,0x080,0x300},{0x000,0xD00,0x600,0x040},{0x680,0x4C0,0x7F0,0x300}};
  static uint16_t out_c[20], out_a[20];
  Fm4Op c, a;
  for (uint8_t alg = 0; alg < 8; ++alg) {
    for (uint8_t set = 0; set < 4; ++set) {
      for (uint8_t fbi = 0; fbi < 3; ++fbi) {
        uint16_t gain = fbi == 0 ? 0 : (fbi == 1 ? 0x0800 : 0xFFFF);
        c.Init(); a.Init();
        for (uint8_t i = 0; i < 4; ++i) {
          uint32_t inc = 0x00123457UL * (i + 3) + alg * 0x1357;
          c.mutable_op(i)->phase_increment = inc; a.mutable_op(i)->phase_increment = inc;
          c.mutable_op(i)->phase = 0xABCD0000UL * i; a.mutable_op(i)->phase = 0xABCD0000UL * i;
        }
        for (uint8_t blk = 0; blk < 6; ++blk) {
          c.RenderC(alg, waves[set], atts[set], gain, out_c, 20);
          a.RenderAsm(alg, waves[set], atts[set], gain, out_a, 20);
          ++fmtest_blocks;
          for (uint8_t k = 0; k < 20; ++k) {
            if (out_c[k] != out_a[k]) {
              if (!fmtest_mismatches) { fmtest_first[0] = alg; fmtest_first[1] = set * 16 + fbi; fmtest_first[2] = out_c[k]; fmtest_first[3] = out_a[k]; }
              ++fmtest_mismatches;
            }
          }
        }
      }
    }
  }
}
#endif
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
#ifdef BENCH_SUBTEST
  SubTest();
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_PMTEST
  PmTest();
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_KSTEST
  KsTest();
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_WCTEST
  WcTest();
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_EXPTEST
  ExpTest();
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_OSCTEST
  OscTest();
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_FMTEST
  FmTest();
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
#ifdef BENCH_TRIGGER
  // Cycles for a note-on (the Karplus-Strong pluck fill runs here, between
  // blocks). Needs BENCH_NOISR (free-running timer).
  {
    uint16_t t0 = TCNT1;
    voice.Trigger(BENCH_NOTE << 7, 100, 0);
    uint16_t t1 = TCNT1;
    cycles[0] = t1 - t0;
  }
  done = 1;
  bench_done();
  while (1) { }
#endif
#ifdef BENCH_SMOOTH
  // Largest sample-to-sample step in the rendered blocks of this patch: a
  // held sine at A3 moves at most ~75 DAC steps per sample. Catches the
  // 16-bit int traps the host tests cannot.
  volatile uint16_t max_step = 0;
  {
    uint16_t prev = 2048;
    for (uint8_t blk = 0; blk < 8; ++blk) {
      audio_buffer.Flush();
      voice.ProcessBlock();
      for (uint8_t k = 0; k < kAudioBlockSize; ++k) {
        uint16_t v = AudioRing::buffer_[k];
        uint16_t step = v > prev ? v - prev : prev - v;
        if (blk > 0 && step > max_step) max_step = step;
        prev = v;
      }
    }
  }
  cycles[0] = max_step;
  done = 1;
  bench_done();
  while (1) { }
#endif
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
#ifdef BENCH_PROFILE
    for (uint8_t k = 0; k < 8; ++k) marks[i][k] = bench_mark[k];
    marks[i][7] = t1;
#endif
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
