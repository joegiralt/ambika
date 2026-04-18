// Copyright 2026 Ambika contributors.
//
// Custom UI page for Karplus-Strong plucked string synthesis.

#include "controller/ui_pages/ks_page.h"

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

static const uint8_t kKsExcLast = 4;

// Page 1: damp colr dcay body | rang tune pos  exc
// Page 2: rate dpth sprd wmix | stif feed
//
// Patch offsets:
//   osc[0].parameter (1)  = damping
//   osc[1].parameter (5)  = excitation color
//   osc[1].range     (6)  = decay rate
//   mix_balance      (8)  = body resonance
//   osc[0].range     (2)  = pitch range
//   osc[0].detune    (3)  = fine tune
//   osc[1].detune    (7)  = pluck position
//   osc[1].shape     (4)  = excitation type
//   mix_op           (9)  = ensemble rate
//   mix_parameter    (10) = ensemble depth
//   mix_sub_osc_shape(11) = ensemble spread
//   mix_sub_osc      (12) = ensemble mix
//   mix_noise        (13) = stiffness
//   mix_fuzz         (14) = feedback

static const uint8_t ks_page1_offsets[] PROGMEM = {
  1, 5, 6, 8, 2, 3, 7, 4,
};
static const uint8_t ks_page2_offsets[] PROGMEM = {
  9, 10, 11, 12, 13, 14, 0xff, 0xff,
};

enum KsParamType {
  KS_PT_UINT7,
  KS_PT_INT8,
  KS_PT_EXCITATION,
  KS_PT_NONE,
};

static const uint8_t ks_page1_types[] PROGMEM = {
  KS_PT_UINT7, KS_PT_UINT7, KS_PT_UINT7, KS_PT_UINT7,
  KS_PT_INT8,  KS_PT_INT8,  KS_PT_UINT7, KS_PT_EXCITATION,
};
static const uint8_t ks_page2_types[] PROGMEM = {
  KS_PT_UINT7, KS_PT_UINT7, KS_PT_UINT7, KS_PT_UINT7,
  KS_PT_UINT7, KS_PT_UINT7, KS_PT_NONE,  KS_PT_NONE,
};

static const prog_char ks_page1_labels[] PROGMEM =
    "damp" "colr" "dcay" "body"
    "rang" "tune" "pos " "exc ";
static const prog_char ks_page2_labels[] PROGMEM =
    "rate" "dpth" "sprd" "wmix"
    "stif" "feed" "    " "    ";

static const prog_char ks_exc_names[] PROGMEM =
    "noisclicbritdark";

/* static */
uint8_t KsPage::page_index_;

/* static */
const prog_EventHandlers KsPage::event_handlers_ PROGMEM = {
  OnInit, NULL, OnIncrement, OnClick, OnPot, OnKey,
  NULL, OnIdle, UpdateScreen, UpdateLeds, NULL,
};

/* static */
uint8_t KsPage::IsKsActive() {
  return multi.mutable_part(ui.state().active_part)->mutable_raw_patch_data()[0]
      == WAVEFORM_KS_PLUCK;
}

static inline uint8_t* GetPatchData() {
  return multi.mutable_part(ui.state().active_part)->mutable_raw_patch_data();
}

/* static */
void KsPage::OnInit(PageInfo* info) {
  UiPage::OnInit(info);
  active_control_ = 0;
  page_index_ = 0;
}

/* static */
uint8_t KsPage::OnIncrement(int8_t increment) {
  if (edit_mode_ != EDIT_IDLE) {
    const uint8_t* offsets = page_index_ ? ks_page2_offsets : ks_page1_offsets;
    const uint8_t* types = page_index_ ? ks_page2_types : ks_page1_types;
    uint8_t offset = pgm_read_byte(&offsets[active_control_]);
    uint8_t type = pgm_read_byte(&types[active_control_]);
    if (offset == 0xff || type == KS_PT_NONE) return 1;

    uint8_t* patch = GetPatchData();
    if (type == KS_PT_EXCITATION) {
      int16_t val = patch[offset] + increment;
      if (val < 0) val = 0;
      if (val >= kKsExcLast) val = kKsExcLast - 1;
      patch[offset] = val;
    } else if (type == KS_PT_INT8) {
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
      // Skip empty slots.
      const uint8_t* types = page_index_ ? ks_page2_types : ks_page1_types;
      if (pgm_read_byte(&types[new_control]) != KS_PT_NONE) {
        active_control_ = new_control;
      }
    }
  }
  return 1;
}

/* static */
uint8_t KsPage::OnClick() {
  if (edit_mode_ == EDIT_IDLE) {
    edit_mode_ = EDIT_STARTED_BY_ENCODER;
  } else {
    edit_mode_ = EDIT_IDLE;
  }
  return 1;
}

/* static */
uint8_t KsPage::OnPot(uint8_t index, uint8_t value) {
  const uint8_t* offsets = page_index_ ? ks_page2_offsets : ks_page1_offsets;
  const uint8_t* types = page_index_ ? ks_page2_types : ks_page1_types;
  uint8_t offset = pgm_read_byte(&offsets[index]);
  uint8_t type = pgm_read_byte(&types[index]);
  if (offset == 0xff || type == KS_PT_NONE) return 1;

  uint8_t* patch = GetPatchData();
  if (type == KS_PT_EXCITATION) {
    uint8_t val = (value * kKsExcLast) >> 7;
    if (val >= kKsExcLast) val = kKsExcLast - 1;
    patch[offset] = val;
  } else if (type == KS_PT_INT8) {
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
uint8_t KsPage::OnKey(uint8_t key) {
  if (key == SWITCH_1) {
    page_index_ = page_index_ ? 0 : 1;
    return 1;
  }
  return 0;
}

/* static */
void KsPage::UpdateScreen() {
  const prog_char* labels = page_index_ ? ks_page2_labels : ks_page1_labels;
  const uint8_t* offsets = page_index_ ? ks_page2_offsets : ks_page1_offsets;
  const uint8_t* types = page_index_ ? ks_page2_types : ks_page1_types;
  uint8_t* patch = GetPatchData();

  for (uint8_t i = 0; i < 8; ++i) {
    uint8_t line = i < 4 ? 0 : 1;
    uint8_t row = (i & 3) * 10;
    char* buffer = display.line_buffer(line) + row;

    if (row != 0) buffer[0] = kDelimiter;
    if ((row + 10) != kLcdWidth) buffer[10] = kDelimiter;

    uint8_t type = pgm_read_byte(&types[i]);
    if (type == KS_PT_NONE) {
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
      case KS_PT_EXCITATION: {
        uint8_t idx = raw;
        if (idx >= kKsExcLast) idx = 0;
        memcpy_P(&buffer[6], ks_exc_names + idx * 4, 4);
        break;
      }
      case KS_PT_INT8: {
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
void KsPage::UpdateLeds() {
  leds.set_pixel(LED_1, page_index_ == 0 ? 0xf0 : 0x0f);
  leds.set_pixel(LED_2, page_index_ == 1 ? 0xf0 : 0x0f);
}

}  // namespace ambika
