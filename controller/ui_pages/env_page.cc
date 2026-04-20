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
// Custom UI page for 7-envelope selector (env1-3 with LFO, env4-7 ADSR only).

#include "controller/ui_pages/env_page.h"

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

/* static */
uint8_t EnvPage::selector_;

// LFO shape short names (4 chars each). Only first 4 basic shapes shown by
// name; wavetable shapes displayed as number.
static const prog_char lfo_shape_names[] PROGMEM =
    "tri " "sqr " "s&h " "ramp";

// Envelope curve short names (4 chars each).
static const prog_char env_curve_names[] PROGMEM =
    "exp " "lin " "loop" "lpln";

// Top-row labels for selector 0-2 (4 chars each, knobs 0-3).
static const prog_char env_top_labels_lfo[] PROGMEM =
    "lfeg" "rate" "shap" "curv";

// Bottom-row labels (knobs 4-7), same for all selectors.
static const prog_char env_bot_labels[] PROGMEM =
    "attk" "dec " "sus " "rel ";

// Top-row label for selector 3-6 knob 0 only.
static const prog_char env_top_label_sel[] PROGMEM = "lfeg";

/* static */
const prog_EventHandlers EnvPage::event_handlers_ PROGMEM = {
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

static inline uint8_t* GetPatchData() {
  return multi.mutable_part(ui.state().active_part)->mutable_raw_patch_data();
}

/* static */
uint8_t EnvPage::AdsrOffset(uint8_t knob) {
  // knob: 0=attack, 1=decay, 2=sustain, 3=release
  if (selector_ < 3) {
    return 24 + selector_ * 8 + knob;
  } else {
    return 112 + (selector_ - 3) * 4 + knob;
  }
}

/* static */
void EnvPage::OnInit(PageInfo* info) {
  UiPage::OnInit(info);
  active_control_ = 0;
  selector_ = 0;
}

/* static */
uint8_t EnvPage::OnIncrement(int8_t increment) {
  if (edit_mode_ != EDIT_IDLE) {
    uint8_t* patch = GetPatchData();
    uint8_t ctrl = active_control_;

    if (ctrl == 0) {
      // Selector: 0-6.
      int16_t val = selector_ + increment;
      if (val < 0) val = 0;
      if (val > 6) val = 6;
      selector_ = val;
    } else if (ctrl >= 4) {
      // ADSR knob (ctrl 4-7 -> knob 0-3).
      uint8_t offset = AdsrOffset(ctrl - 4);
      int16_t val = patch[offset] + increment;
      if (val < 0) val = 0;
      if (val > 127) val = 127;
      patch[offset] = val;
      multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
          VOICECARD_DATA_PATCH, offset, patch[offset]);
    } else if (selector_ < 3) {
      // LFO controls (ctrl 1-3) only for env 1-3.
      uint8_t base = 24 + selector_ * 8;
      if (ctrl == 1) {
        // Rate (offset +5), 0-127.
        uint8_t offset = base + 5;
        int16_t val = patch[offset] + increment;
        if (val < 0) val = 0;
        if (val > 127) val = 127;
        patch[offset] = val;
        multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
            VOICECARD_DATA_PATCH, offset, patch[offset]);
      } else if (ctrl == 2) {
        // Shape (offset +4), 0 to LFO_WAVEFORM_LAST-1.
        uint8_t offset = base + 4;
        int16_t val = patch[offset] + increment;
        if (val < 0) val = 0;
        if (val >= LFO_WAVEFORM_LAST) val = LFO_WAVEFORM_LAST - 1;
        patch[offset] = val;
        multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
            VOICECARD_DATA_PATCH, offset, patch[offset]);
      } else {
        // Curve (offset +6), 0 to ENVELOPE_CURVE_LAST-1.
        uint8_t offset = base + 6;
        int16_t val = patch[offset] + increment;
        if (val < 0) val = 0;
        if (val >= ENVELOPE_CURVE_LAST) val = ENVELOPE_CURVE_LAST - 1;
        patch[offset] = val;
        multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
            VOICECARD_DATA_PATCH, offset, patch[offset]);
      }
    }
    // else: ctrl 1-3 and selector >= 3 — ignore (no LFO for extra envs).
    edit_mode_ = EDIT_STARTED_BY_ENCODER;
  } else {
    // Navigate controls.
    int8_t new_control = active_control_ + increment;
    if (new_control >= 0 && new_control < 8) {
      // Skip LFO knobs (1-3) when selector >= 3.
      if (selector_ >= 3 && new_control >= 1 && new_control <= 3) {
        new_control = (increment > 0) ? 4 : 0;
      }
      if (new_control >= 0 && new_control < 8) {
        active_control_ = new_control;
      }
    }
  }
  return 1;
}

/* static */
uint8_t EnvPage::OnClick() {
  if (edit_mode_ == EDIT_IDLE) {
    edit_mode_ = EDIT_STARTED_BY_ENCODER;
  } else {
    edit_mode_ = EDIT_IDLE;
  }
  return 1;
}

/* static */
uint8_t EnvPage::OnPot(uint8_t index, uint8_t value) {
  uint8_t* patch = GetPatchData();

  if (index == 0) {
    // Selector pot: scale 0-127 to 0-6.
    uint8_t val = (static_cast<uint16_t>(value) * 7) >> 7;
    if (val > 6) val = 6;
    selector_ = val;
    active_control_ = 0;
    edit_mode_ = EDIT_STARTED_BY_POT;
    return 1;
  }

  if (index >= 4) {
    // ADSR knob.
    uint8_t offset = AdsrOffset(index - 4);
    patch[offset] = value;
    multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
        VOICECARD_DATA_PATCH, offset, patch[offset]);
    active_control_ = index;
    edit_mode_ = EDIT_STARTED_BY_POT;
    return 1;
  }

  // Knobs 1-3: LFO rate, shape, curve — only for selector 0-2.
  if (selector_ >= 3) return 1;

  uint8_t base = 24 + selector_ * 8;
  if (index == 1) {
    // Rate.
    uint8_t offset = base + 5;
    patch[offset] = value;
    multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
        VOICECARD_DATA_PATCH, offset, patch[offset]);
  } else if (index == 2) {
    // Shape.
    uint8_t offset = base + 4;
    uint8_t val = (static_cast<uint16_t>(value) * LFO_WAVEFORM_LAST) >> 7;
    if (val >= LFO_WAVEFORM_LAST) val = LFO_WAVEFORM_LAST - 1;
    patch[offset] = val;
    multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
        VOICECARD_DATA_PATCH, offset, patch[offset]);
  } else {
    // Curve.
    uint8_t offset = base + 6;
    uint8_t val = (static_cast<uint16_t>(value) * ENVELOPE_CURVE_LAST) >> 7;
    if (val >= ENVELOPE_CURVE_LAST) val = ENVELOPE_CURVE_LAST - 1;
    patch[offset] = val;
    multi.mutable_part(ui.state().active_part)->WriteToAllVoices(
        VOICECARD_DATA_PATCH, offset, patch[offset]);
  }

  active_control_ = index;
  edit_mode_ = EDIT_STARTED_BY_POT;
  return 1;
}

/* static */
uint8_t EnvPage::OnKey(uint8_t key) {
  // S1 toggles to the VOICE_LFO page (same as original behavior).
  return 0;
}

/* static */
void EnvPage::UpdateScreen() {
  uint8_t* patch = GetPatchData();

  for (uint8_t i = 0; i < 8; ++i) {
    uint8_t line = i < 4 ? 0 : 1;
    uint8_t row = (i & 3) * 10;
    char* buffer = display.line_buffer(line) + row;

    if (row != 0) buffer[0] = kDelimiter;
    if ((row + 10) != kLcdWidth) buffer[10] = kDelimiter;

    if (i == 0) {
      // Selector cell: "lfeg  N"
      memcpy_P(&buffer[1], env_top_label_sel, 4);
      if (i == active_control_ && buffer[1] >= 'a' && buffer[1] <= 'z') {
        buffer[1] -= 0x20;
      }
      buffer[5] = ' ';
      buffer[6] = ' ';
      buffer[7] = ' ';
      buffer[8] = ' ';
      buffer[9] = '1' + selector_;
    } else if (i >= 4) {
      // ADSR cell.
      memcpy_P(&buffer[1], env_bot_labels + (i - 4) * 4, 4);
      if (i == active_control_ && buffer[1] >= 'a' && buffer[1] <= 'z') {
        buffer[1] -= 0x20;
      }
      buffer[5] = ' ';
      uint8_t offset = AdsrOffset(i - 4);
      UnsafeItoa<int16_t>(patch[offset], 4, &buffer[6]);
      AlignRight(&buffer[6], 4);
    } else if (selector_ < 3) {
      // LFO controls (knobs 1-3) for env 1-3.
      memcpy_P(&buffer[1], env_top_labels_lfo + i * 4, 4);
      if (i == active_control_ && buffer[1] >= 'a' && buffer[1] <= 'z') {
        buffer[1] -= 0x20;
      }
      buffer[5] = ' ';

      uint8_t base = 24 + selector_ * 8;
      if (i == 1) {
        // Rate: numeric.
        UnsafeItoa<int16_t>(patch[base + 5], 4, &buffer[6]);
        AlignRight(&buffer[6], 4);
      } else if (i == 2) {
        // Shape: name or number.
        uint8_t shape = patch[base + 4];
        if (shape < 4) {
          memcpy_P(&buffer[6], lfo_shape_names + shape * 4, 4);
        } else {
          // Wavetable shape: show as number.
          UnsafeItoa<int16_t>(shape, 4, &buffer[6]);
          AlignRight(&buffer[6], 4);
        }
      } else {
        // Curve: name.
        uint8_t curve = patch[base + 6];
        if (curve < ENVELOPE_CURVE_LAST) {
          memcpy_P(&buffer[6], env_curve_names + curve * 4, 4);
        } else {
          buffer[6] = ' ';
          buffer[7] = ' ';
          buffer[8] = ' ';
          buffer[9] = '?';
        }
      }
    } else {
      // Blank cell for knobs 1-3 when selector >= 3.
      for (uint8_t j = 1; j < 10; ++j) buffer[j] = ' ';
    }
  }
}

/* static */
void EnvPage::UpdateLeds() {
  leds.set_pixel(LED_1, 0xf0);
  leds.set_pixel(LED_2, 0x0f);
}

}  // namespace ambika
