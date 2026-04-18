// Copyright 2026 Ambika contributors.
//
// West coast oscillator UI page.

#include "controller/ui_pages/wc_page.h"

#include <avr/pgmspace.h>
#include "avrlib/string.h"

#include "controller/display.h"
#include "controller/leds.h"
#include "controller/multi.h"
#include "controller/voicecard_tx.h"
#include "controller/ui.h"
#include "common/patch.h"

namespace ambika {

using namespace avrlib;

static const uint8_t kWcWaveLast = 2;

// Page 1: wave fold sym  bias | fmdp fmrt colr drve
// Page 2: flds gain env  sub  | sync
//
// Patch offsets:
// Page 1:
//   osc[1].shape     (4)  = base waveform
//   osc[0].parameter (1)  = fold depth
//   osc[1].parameter (5)  = symmetry
//   mix_balance      (8)  = bias
//   osc[1].range     (6)  = FM depth
//   osc[1].detune    (7)  = FM ratio
//   mix_parameter    (10) = color
//   mix_op           (9)  = drive
// Page 2:
//   mix_sub_osc_shape(11) = fold stages
//   mix_sub_osc      (12) = input gain
//   mix_noise        (13) = env-to-fold
//   mix_fuzz         (14) = sub level
//   mix_crush        (15) = sync amount

static const uint8_t wc_page1_offsets[] PROGMEM = {
  4, 1, 5, 8, 6, 7, 10, 9,
};
static const uint8_t wc_page2_offsets[] PROGMEM = {
  11, 12, 13, 14, 15, 0xff, 0xff, 0xff,
};

enum WcParamType {
  WC_PT_WAVE,
  WC_PT_UINT7,
  WC_PT_INT8,
  WC_PT_NONE,
};

static const uint8_t wc_page1_types[] PROGMEM = {
  WC_PT_WAVE, WC_PT_UINT7, WC_PT_UINT7, WC_PT_UINT7,
  WC_PT_UINT7, WC_PT_INT8, WC_PT_UINT7, WC_PT_UINT7,
};
static const uint8_t wc_page2_types[] PROGMEM = {
  WC_PT_UINT7, WC_PT_UINT7, WC_PT_UINT7, WC_PT_UINT7,
  WC_PT_UINT7, WC_PT_NONE, WC_PT_NONE, WC_PT_NONE,
};

static const prog_char wc_page1_labels[] PROGMEM =
    "wave" "fold" "sym " "bias"
    "fmdp" "fmrt" "colr" "drve";
static const prog_char wc_page2_labels[] PROGMEM =
    "flds" "gain" "env " "sub "
    "sync" "    " "    " "    ";

static const prog_char wc_wave_names[] PROGMEM = "sin tri ";

/* static */
uint8_t WcPage::page_index_;

/* static */
const prog_EventHandlers WcPage::event_handlers_ PROGMEM = {
  OnInit, NULL, OnIncrement, OnClick, OnPot, OnKey,
  NULL, OnIdle, UpdateScreen, UpdateLeds, NULL,
};

/* static */
uint8_t WcPage::IsWcActive() {
  return multi.mutable_part(ui.state().active_part)->mutable_raw_patch_data()[0]
      == WAVEFORM_WESTCOAST;
}

static inline uint8_t* GetPatchData() {
  return multi.mutable_part(ui.state().active_part)->mutable_raw_patch_data();
}

/* static */
void WcPage::OnInit(PageInfo* info) {
  UiPage::OnInit(info);
  active_control_ = 0;
  page_index_ = 0;
}

/* static */
uint8_t WcPage::OnIncrement(int8_t increment) {
  if (edit_mode_ != EDIT_IDLE) {
    const uint8_t* offsets = page_index_ ? wc_page2_offsets : wc_page1_offsets;
    const uint8_t* types = page_index_ ? wc_page2_types : wc_page1_types;
    uint8_t offset = pgm_read_byte(&offsets[active_control_]);
    uint8_t type = pgm_read_byte(&types[active_control_]);
    if (offset == 0xff || type == WC_PT_NONE) return 1;

    uint8_t* patch = GetPatchData();
    if (type == WC_PT_WAVE) {
      int16_t val = patch[offset] + increment;
      if (val < 0) val = 0;
      if (val >= kWcWaveLast) val = kWcWaveLast - 1;
      patch[offset] = val;
    } else if (type == WC_PT_INT8) {
      int16_t val = static_cast<int8_t>(patch[offset]) + increment;
      if (val < -24) val = -24;
      if (val > 24) val = 24;
      patch[offset] = static_cast<uint8_t>(val);
    } else {
      int16_t val = patch[offset] + increment;
      if (val < 0) val = 0;
      if (val > 127) val = 127;
      patch[offset] = val;
    }
    multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
        VOICECARD_DATA_PATCH, offset, patch[offset]);
    edit_mode_ = EDIT_STARTED_BY_ENCODER;
  } else {
    int8_t new_control = active_control_ + increment;
    if (new_control < 0) {
      if (page_index_ > 0) { page_index_--; active_control_ = 7; }
    } else if (new_control >= 8) {
      if (page_index_ < 1) { page_index_++; active_control_ = 0; }
    } else {
      const uint8_t* types = page_index_ ? wc_page2_types : wc_page1_types;
      if (pgm_read_byte(&types[new_control]) != WC_PT_NONE) {
        active_control_ = new_control;
      }
    }
  }
  return 1;
}

/* static */
uint8_t WcPage::OnClick() {
  if (edit_mode_ == EDIT_IDLE) {
    edit_mode_ = EDIT_STARTED_BY_ENCODER;
  } else {
    edit_mode_ = EDIT_IDLE;
  }
  return 1;
}

/* static */
uint8_t WcPage::OnPot(uint8_t index, uint8_t value) {
  const uint8_t* offsets = page_index_ ? wc_page2_offsets : wc_page1_offsets;
  const uint8_t* types = page_index_ ? wc_page2_types : wc_page1_types;
  uint8_t offset = pgm_read_byte(&offsets[index]);
  uint8_t type = pgm_read_byte(&types[index]);
  if (offset == 0xff || type == WC_PT_NONE) return 1;

  uint8_t* patch = GetPatchData();
  if (type == WC_PT_WAVE) {
    uint8_t val = (value * kWcWaveLast) >> 7;
    if (val >= kWcWaveLast) val = kWcWaveLast - 1;
    patch[offset] = val;
  } else if (type == WC_PT_INT8) {
    patch[offset] = static_cast<uint8_t>(static_cast<int8_t>(value) - 64);
  } else {
    patch[offset] = value;
  }

  active_control_ = index;
  edit_mode_ = EDIT_STARTED_BY_POT;
  multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
        VOICECARD_DATA_PATCH, offset, patch[offset]);
  return 1;
}

/* static */
uint8_t WcPage::OnKey(uint8_t key) {
  if (key == SWITCH_1) {
    page_index_ = page_index_ ? 0 : 1;
    return 1;
  }
  return 0;
}

/* static */
void WcPage::UpdateScreen() {
  const prog_char* labels = page_index_ ? wc_page2_labels : wc_page1_labels;
  const uint8_t* offsets = page_index_ ? wc_page2_offsets : wc_page1_offsets;
  const uint8_t* types = page_index_ ? wc_page2_types : wc_page1_types;
  uint8_t* patch = GetPatchData();

  for (uint8_t i = 0; i < 8; ++i) {
    uint8_t line = i < 4 ? 0 : 1;
    uint8_t row = (i & 3) * 10;
    char* buffer = display.line_buffer(line) + row;

    if (row != 0) buffer[0] = kDelimiter;
    if ((row + 10) != kLcdWidth) buffer[10] = kDelimiter;

    uint8_t type = pgm_read_byte(&types[i]);
    if (type == WC_PT_NONE) {
      for (uint8_t j = 1; j < 10; ++j) buffer[j] = ' ';
      continue;
    }

    memcpy_P(&buffer[1], labels + i * 4, 4);
    if (i == active_control_ && buffer[1] >= 'a' && buffer[1] <= 'z') {
      buffer[1] -= 0x20;
    }
    buffer[5] = ' ';

    uint8_t offset = pgm_read_byte(&offsets[i]);
    uint8_t raw = patch[offset];

    switch (type) {
      case WC_PT_WAVE: {
        uint8_t w = raw;
        if (w >= kWcWaveLast) w = 0;
        memcpy_P(&buffer[6], wc_wave_names + w * 4, 4);
        break;
      }
      case WC_PT_INT8: {
        UnsafeItoa<int16_t>(static_cast<int8_t>(raw), 4, &buffer[6]);
        AlignRight(&buffer[6], 4);
        break;
      }
      default:
        UnsafeItoa<int16_t>(raw, 4, &buffer[6]);
        AlignRight(&buffer[6], 4);
        break;
    }
  }
}

/* static */
void WcPage::UpdateLeds() {
  leds.set_pixel(LED_1, page_index_ == 0 ? 0xf0 : 0x0f);
  leds.set_pixel(LED_2, page_index_ == 1 ? 0xf0 : 0x0f);
}

}  // namespace ambika
