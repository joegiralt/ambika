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
// Custom UI page for 4-op FM synthesis parameters.

#include "controller/ui_pages/fm_page.h"

#include <avr/pgmspace.h>
#include "avrlib/string.h"

#include "controller/display.h"
#include "controller/leds.h"
#include "controller/multi.h"
#include "controller/voicecard_tx.h"
#include "controller/ui.h"
#include "common/patch.h"

namespace ambika {

// FM constants (must match voicecard/fm4op.h).
static const uint8_t kFmWaveLast = 8;
static const uint8_t kFmAlgLast = 8;

using namespace avrlib;

/* static */
uint8_t FmPage::page_index_;

// TX81Z waveform short names.
static const prog_char fm_wave_names[] PROGMEM =
    "sin halfabs qtr habstri pls saw ";

// Algorithm short descriptions.
static const prog_char fm_alg_names[] PROGMEM =
    "4>3>2>1 3+4>2>1 4>3+2>1 43 + 21"
    "4>2+4311 4>1234>1+2+31+2+3+4";

// FM page 1: algorithm, feedback, ops 1-2
// FM page 2: ops 3-4, levels 1-4
//
// Patch byte mapping (from fm4op.h):
// osc[0].parameter (offset 1)  = algorithm
// osc[0].range (offset 2)      = op1 coarse ratio
// osc[0].detune (offset 3)     = op1 fine
// osc[1].shape (offset 4)      = op1 wave (lo) | op2 wave (hi)
// osc[1].parameter (offset 5)  = op3 wave (lo) | op4 wave (hi)
// osc[1].range (offset 6)      = op2 coarse ratio
// osc[1].detune (offset 7)     = op2 fine
// mix_balance (offset 8)       = op3 coarse ratio
// mix_op (offset 9)            = op3 fine
// mix_parameter (offset 10)    = op4 coarse ratio
// mix_sub_osc_shape (offset 11)= op4 fine
// mix_sub_osc (offset 12)      = op1 level
// mix_noise (offset 13)        = op2 level
// mix_fuzz (offset 14)         = op3 level
// mix_crush (offset 15)        = op4 level
// padding[0] (offset 104)      = feedback

// Page 1 knob layout:
//   algo  fbk   wav1  wav2
//   rat1  fin1  rat2  fin2
// Patch offsets for page 1 knobs:
static const uint8_t fm_page1_offsets[] PROGMEM = {
  1,    // algo  (osc[0].parameter)
  104,  // fbk   (padding[0])
  4,    // wav1  (osc[1].shape, low nibble)
  4,    // wav2  (osc[1].shape, high nibble)
  2,    // rat1  (osc[0].range)
  3,    // fin1  (osc[0].detune)
  6,    // rat2  (osc[1].range)
  7,    // fin2  (osc[1].detune)
};

// Page 2 knob layout:
//   wav3  wav4  lvl1  lvl2
//   rat3  fin3  lvl3  lvl4
static const uint8_t fm_page2_offsets[] PROGMEM = {
  5,    // wav3  (osc[1].parameter, low nibble)
  5,    // wav4  (osc[1].parameter, high nibble)
  12,   // lvl1  (mix_sub_osc)
  13,   // lvl2  (mix_noise)
  8,    // rat3  (mix_balance)
  9,    // fin3  (mix_op)
  14,   // lvl3  (mix_fuzz)
  15,   // lvl4  (mix_crush)
};

enum FmParamType {
  FM_PT_ALGO,
  FM_PT_FEEDBACK,
  FM_PT_WAVE_LO,
  FM_PT_WAVE_HI,
  FM_PT_RATIO,
  FM_PT_FINE,
  FM_PT_LEVEL,
};

// Parameter type for each knob on page 1.
static const uint8_t fm_page1_types[] PROGMEM = {
  FM_PT_ALGO, FM_PT_FEEDBACK, FM_PT_WAVE_LO, FM_PT_WAVE_HI,
  FM_PT_RATIO, FM_PT_FINE, FM_PT_RATIO, FM_PT_FINE,
};

// Parameter type for each knob on page 2.
static const uint8_t fm_page2_types[] PROGMEM = {
  FM_PT_WAVE_LO, FM_PT_WAVE_HI, FM_PT_LEVEL, FM_PT_LEVEL,
  FM_PT_RATIO, FM_PT_FINE, FM_PT_LEVEL, FM_PT_LEVEL,
};

// Labels for page 1 knobs (4 chars each).
static const prog_char fm_page1_labels[] PROGMEM =
    "algo" "fbk " "wav1" "wav2"
    "rat1" "fin1" "rat2" "fin2";

// Labels for page 2 knobs (4 chars each).
static const prog_char fm_page2_labels[] PROGMEM =
    "wav3" "wav4" "lvl1" "lvl2"
    "rat3" "fin3" "lvl3" "lvl4";

/* static */
const prog_EventHandlers FmPage::event_handlers_ PROGMEM = {
  OnInit,
  NULL,
  OnIncrement,
  OnClick,
  OnPot,
  OnKey,
  NULL,
  OnIdle,
  UpdateScreen,
  UpdateLeds,
  NULL,
};

/* static */
uint8_t FmPage::IsFm4OpActive() {
  Part* part = multi.mutable_part(ui.state().active_part);
  uint8_t* patch = part->mutable_raw_patch_data();
  return patch[0] == WAVEFORM_FM4OP;
}

/* static */
void FmPage::OnInit(PageInfo* info) {
  UiPage::OnInit(info);
  active_control_ = 0;
  page_index_ = 0;
}

static inline uint8_t* GetPatchData() {
  return multi.mutable_part(ui.state().active_part)->mutable_raw_patch_data();
}

/* static */
uint8_t FmPage::OnIncrement(int8_t increment) {
  if (edit_mode_ != EDIT_IDLE) {
    uint8_t* patch = GetPatchData();
    const uint8_t* offsets = page_index_ ? fm_page2_offsets : fm_page1_offsets;
    const uint8_t* types = page_index_ ? fm_page2_types : fm_page1_types;
    uint8_t offset = pgm_read_byte(&offsets[active_control_]);
    uint8_t type = pgm_read_byte(&types[active_control_]);

    if (type == FM_PT_WAVE_LO) {
      uint8_t val = patch[offset] & 0x0F;
      val = static_cast<uint8_t>(val + increment);
      if (val >= kFmWaveLast) val = (increment > 0) ? kFmWaveLast - 1 : 0;
      patch[offset] = (patch[offset] & 0xF0) | val;
    } else if (type == FM_PT_WAVE_HI) {
      uint8_t val = (patch[offset] >> 4) & 0x0F;
      val = static_cast<uint8_t>(val + increment);
      if (val >= kFmWaveLast) val = (increment > 0) ? kFmWaveLast - 1 : 0;
      patch[offset] = (patch[offset] & 0x0F) | (val << 4);
    } else if (type == FM_PT_ALGO) {
      int16_t val = patch[offset] + increment;
      if (val < 0) val = 0;
      if (val >= kFmAlgLast) val = kFmAlgLast - 1;
      patch[offset] = val;
    } else if (type == FM_PT_FINE) {
      int16_t val = static_cast<int8_t>(patch[offset]) + increment;
      if (val < -64) val = -64;
      if (val > 63) val = 63;
      patch[offset] = static_cast<uint8_t>(val);
    } else {
      // RATIO, FEEDBACK, LEVEL: 0-127
      int16_t val = patch[offset] + increment;
      if (val < 0) val = 0;
      if (val > 127) val = 127;
      patch[offset] = val;
    }

    // Push change to voicecard.
    multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
        VOICECARD_DATA_PATCH, offset, patch[offset]);
    edit_mode_ = EDIT_STARTED_BY_ENCODER;
  } else {
    int8_t new_control = active_control_ + increment;
    if (new_control < 0) {
      if (page_index_ > 0) {
        page_index_--;
        active_control_ = 7;
      }
    } else if (new_control >= 8) {
      if (page_index_ < 1) {
        page_index_++;
        active_control_ = 0;
      }
    } else {
      active_control_ = new_control;
    }
  }
  return 1;
}

/* static */
uint8_t FmPage::OnClick() {
  if (edit_mode_ == EDIT_IDLE) {
    edit_mode_ = EDIT_STARTED_BY_ENCODER;
  } else {
    edit_mode_ = EDIT_IDLE;
  }
  return 1;
}

/* static */
uint8_t FmPage::OnPot(uint8_t index, uint8_t value) {
  uint8_t* patch = GetPatchData();
  const uint8_t* offsets = page_index_ ? fm_page2_offsets : fm_page1_offsets;
  const uint8_t* types = page_index_ ? fm_page2_types : fm_page1_types;
  uint8_t offset = pgm_read_byte(&offsets[index]);
  uint8_t type = pgm_read_byte(&types[index]);

  // Scale 7-bit pot value to parameter range.
  if (type == FM_PT_WAVE_LO) {
    uint8_t val = (value * kFmWaveLast) >> 7;
    if (val >= kFmWaveLast) val = kFmWaveLast - 1;
    patch[offset] = (patch[offset] & 0xF0) | val;
  } else if (type == FM_PT_WAVE_HI) {
    uint8_t val = (value * kFmWaveLast) >> 7;
    if (val >= kFmWaveLast) val = kFmWaveLast - 1;
    patch[offset] = (patch[offset] & 0x0F) | (val << 4);
  } else if (type == FM_PT_ALGO) {
    uint8_t val = (value * kFmAlgLast) >> 7;
    if (val >= kFmAlgLast) val = kFmAlgLast - 1;
    patch[offset] = val;
  } else if (type == FM_PT_FINE) {
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
uint8_t FmPage::OnKey(uint8_t key) {
  if (key == SWITCH_1) {
    page_index_ = page_index_ ? 0 : 1;
    return 1;
  }
  return 0;
}

/* static */
void FmPage::UpdateScreen() {
  const prog_char* labels = page_index_ ? fm_page2_labels : fm_page1_labels;
  const uint8_t* offsets = page_index_ ? fm_page2_offsets : fm_page1_offsets;
  const uint8_t* types = page_index_ ? fm_page2_types : fm_page1_types;
  uint8_t* patch = GetPatchData();

  for (uint8_t i = 0; i < 8; ++i) {
    uint8_t line = i < 4 ? 0 : 1;
    uint8_t row = (i & 3) * 10;
    char* buffer = display.line_buffer(line) + row;

    if (row != 0) buffer[0] = kDelimiter;
    if ((row + 10) != kLcdWidth) buffer[10] = kDelimiter;

    // Label (4 chars).
    memcpy_P(&buffer[1], labels + i * 4, 4);

    // Uppercase first char if active.
    if (i == active_control_ && buffer[1] >= 'a' && buffer[1] <= 'z') {
      buffer[1] -= 0x20;
    }

    buffer[5] = ' ';

    // Value (4 chars at buffer[6..9]).
    uint8_t offset = pgm_read_byte(&offsets[i]);
    uint8_t type = pgm_read_byte(&types[i]);
    uint8_t raw = patch[offset];

    switch (type) {
      case FM_PT_ALGO:
        buffer[6] = ' ';
        buffer[7] = ' ';
        buffer[8] = ' ';
        buffer[9] = '1' + raw;
        break;

      case FM_PT_WAVE_LO: {
        uint8_t w = raw & 0x0F;
        if (w >= kFmWaveLast) w = 0;
        memcpy_P(&buffer[6], fm_wave_names + w * 4, 4);
        break;
      }

      case FM_PT_WAVE_HI: {
        uint8_t w = (raw >> 4) & 0x0F;
        if (w >= kFmWaveLast) w = 0;
        memcpy_P(&buffer[6], fm_wave_names + w * 4, 4);
        break;
      }

      case FM_PT_RATIO: {
        // TX81Z ratio display. Index 0-63 maps to ratio table.
        // Show as X.XX with one decimal for compact display.
        static const prog_char ratio_display[] PROGMEM =
            ".50 " ".71 " ".78 " ".87 " "1.0 " "1.41" "1.57" "1.73"
            "2.0 " "2.82" "3.0 " "3.14" "3.46" "4.0 " "4.24" "4.71"
            "5.0 " "5.19" "5.65" "6.0 " "6.28" "6.92" "7.0 " "7.07"
            "7.85" "8.0 " "8.48" "8.65" "9.0 " "9.42" "9.89" "10.0"
            "10.4" "11.0" "11.0" "11.3" "12.0" "12.1" "12.6" "12.7"
            "13.0" "13.8" "14.0" "14.1" "14.1" "15.0" "15.6" "15.6"
            "15.7" "17.0" "17.3" "17.3" "18.4" "18.8" "19.0" "19.8"
            "20.4" "20.8" "21.2" "22.0" "22.5" "23.6" "24.2" "26.0";
        uint8_t idx = raw & 0x3F;
        memcpy_P(&buffer[6], ratio_display + idx * 4, 4);
        break;
      }

      case FM_PT_FINE: {
        int8_t v = static_cast<int8_t>(raw);
        UnsafeItoa<int16_t>(v, 4, &buffer[6]);
        AlignRight(&buffer[6], 4);
        break;
      }

      case FM_PT_FEEDBACK:
      case FM_PT_LEVEL:
      default:
        UnsafeItoa<int16_t>(raw, 4, &buffer[6]);
        AlignRight(&buffer[6], 4);
        break;
    }
  }
}

/* static */
void FmPage::UpdateLeds() {
  leds.set_pixel(LED_1, page_index_ == 0 ? 0xf0 : 0x0f);
  leds.set_pixel(LED_2, page_index_ == 1 ? 0xf0 : 0x0f);
}

}  // namespace ambika
