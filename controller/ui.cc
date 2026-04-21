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
// User interface.

#include "controller/ui.h"

#include "avrlib/string.h"

#include "controller/display.h"
#include "controller/leds.h"
#include "controller/multi.h"
#include "controller/resources.h"
#include "controller/system_settings.h"

#include "controller/ui_pages/card_info_page.h"
#include "controller/ui_pages/dialog_box.h"
#include "controller/ui_pages/knob_assigner.h"
#include "controller/ui_pages/library.h"
#include "controller/ui_pages/os_info_page.h"
#include "controller/ui_pages/parameter_editor.h"
#include "controller/ui_pages/env_page.h"
#include "controller/ui_pages/fm_page.h"
#include "controller/ui_pages/ks_page.h"
#include "controller/ui_pages/wc_page.h"
#include "controller/ui_pages/performance_page.h"
#include "controller/ui_pages/randomizer.h"
#include "controller/ui_pages/version_manager.h"
#include "controller/ui_pages/voice_assigner.h"
#include "controller/voicecard_tx.h"

namespace ambika {
  
const prog_PageInfo page_registry[] PROGMEM = {
  { PAGE_OSCILLATORS,
    &ParameterEditor::event_handlers_,
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    PAGE_MIXER, 0, 0xf0,
  },
  
  { PAGE_MIXER,
    &ParameterEditor::event_handlers_,
    { 8, 13, 12, 11, 9, 10, 14, 15 },
    PAGE_OSCILLATORS, 0, 0x0f,
  },
  
  { PAGE_FILTER,
    &ParameterEditor::event_handlers_,
    { 16, 17, 0xff, 18, 22, 23, 0xff, 0xff },
    PAGE_FILTER, 1, 0xf0,
  },
  
  { PAGE_ENV_LFO,
    &EnvPage::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    PAGE_VOICE_LFO, 2, 0xf0,
  },

  { PAGE_VOICE_LFO,
    &ParameterEditor::event_handlers_,
    { 0xff, 32, 33, 29, 0xff, 0xff, 0xff, 0xff },
    PAGE_ENV_LFO, 2, 0x0f,
  },
  
  { PAGE_MODULATIONS,
    &ParameterEditor::event_handlers_,
    { 34, 35, 36, 37, 38, 39, 40, 41 },
    PAGE_MODULATIONS, 3, 0xf0,
  },
  
  { PAGE_PART,
    &ParameterEditor::event_handlers_,
    { 42, 57, 74, 48, 43, 44, 45, 46 },
    PAGE_PART_ARPEGGIATOR, 4, 0xf0,
  },
  
  { PAGE_PART_ARPEGGIATOR,
    &ParameterEditor::event_handlers_,
    { 49, 50, 51, 52, 53, 0xff, 0xff, 0xff },
    PAGE_PART, 4, 0x0f,
  },
  
  { PAGE_MULTI,
    &VoiceAssigner::event_handlers_,
    { 58, 59, 60, 61, 0xff, 0xff, 0xff, 0xff },
    PAGE_MULTI_CLOCK, 5, 0xf0,
  },
  
  { PAGE_MULTI_CLOCK,
    &ParameterEditor::event_handlers_,
    { 62, 63, 64, 65, 0xff, 0xff, 0xff, 0xff, },
    PAGE_MULTI, 5, 0x0f,
  },
  
  { PAGE_PERFORMANCE,
    &PerformancePage::event_handlers_,
    { 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, },
    PAGE_KNOB_ASSIGN, 6, 0xf0,
  },
  
  { PAGE_KNOB_ASSIGN,
    &KnobAssigner::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_RANDOMIZER, 6, 0x0f,
  },

  { PAGE_LIBRARY,
    &Library::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_LIBRARY, 7, 0xf0,
  },

  { PAGE_VERSION_MANAGER,
    &VersionManager::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_LIBRARY, 8, 0xf0,
  },

  { PAGE_FM4OP,
    &FmPage::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_FM4OP, 0, 0xf0,
  },

  { PAGE_KS_PLUCK,
    &KsPage::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_KS_PLUCK, 0, 0xf0,
  },

  { PAGE_WESTCOAST,
    &WcPage::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_WESTCOAST, 0, 0xf0,
  },

  { PAGE_RANDOMIZER,
    &Randomizer::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_PERFORMANCE, 6, 0x0f,
  },

  { PAGE_SYSTEM_SETTINGS,
    &ParameterEditor::event_handlers_,
    { 66, 67, 71, 72, 68, 69, 0xff, 70, },
    PAGE_SYSTEM_SETTINGS, 8, 0xf0,
  },

  { PAGE_OS_INFO,
    &OsInfoPage::event_handlers_,
    { 0, 0, 0, 0, 0, 0, 0, 0, },
    PAGE_OS_INFO, 8, 0xf0,
  },
};

static const prog_uint8_t default_most_recent_page_in_group[9] PROGMEM = {
  PAGE_OSCILLATORS,
  PAGE_FILTER,
  PAGE_ENV_LFO,
  PAGE_MODULATIONS,
  PAGE_PART,
  PAGE_MULTI,
  PAGE_PERFORMANCE,
  PAGE_LIBRARY,
  PAGE_SYSTEM_SETTINGS
};

/* <static> */
UiPageNumber Ui::active_page_;
UiPageNumber Ui::most_recent_non_system_page_;
uint8_t Ui::cycle_;
uint8_t Ui::inhibit_switch_;
Encoder Ui::encoder_;
Switches Ui::switches_;
Pots Ui::pots_;

UiState Ui::state_;
EventQueue<8> Ui::queue_;
uint8_t Ui::pot_value_[8];
uint8_t Ui::s1_held_;
UiPageNumber Ui::most_recent_page_in_group_[9];

PageInfo Ui::page_info_;
EventHandlers Ui::event_handlers_;
/* </static> */

/* extern */
Ui ui;

static char line[41];

/* static */
void Ui::Init() {
  memset(&state_, 0, sizeof(UiState));
  memcpy_P(most_recent_page_in_group_, default_most_recent_page_in_group, 9);

  encoder_.Init();
  switches_.Init();
  pots_.Init();
  lcd.Init();
  display.Init();
  leds.Init();
  lcd.SetCustomCharMapRes(character_table[0], 7, 1);
  
  ShowPage(PAGE_FILTER);
  
  memset(line, ' ', 41);
  line[40] = '\0';
  inhibit_switch_ = 0;
}

/* static */
void Ui::Poll() {
  ++cycle_;
  // I
  int8_t increment = encoder_.Read();
  if (increment != 0) {
    uint8_t control_id = 0;
    if (switches_.low(0)) {
      increment *= 8;
      inhibit_switch_ = 0x01;
    }
    if (switches_.low(7)) {
      ++control_id;
      inhibit_switch_ = 0x80;
    }
    queue_.AddEvent(CONTROL_ENCODER, control_id, increment);
  }
  if (encoder_.clicked()) {
    queue_.AddEvent(CONTROL_ENCODER_CLICK, 0, 1);
  }
  
  if (!(cycle_ & 31)) {
    switches_.Read();
    uint8_t mask = 1;
    for (uint8_t i = 0; i < 8; ++i) {
      if (switches_.raised(i)) {
        if ((inhibit_switch_ & mask)) {
          inhibit_switch_ ^= mask;
        } else {
          uint8_t control = SWITCH_8;
          if (switches_.low(0)) {
            control = SWITCH_SHIFT_8;
            inhibit_switch_ = 0x01;
          }
          control -= i;
          queue_.AddEvent(CONTROL_SWITCH, control, 1);
        }
      }
      mask <<= 1;
    }
  }
  
  if (!(cycle_ & 7)) {
    display.BlinkCursor();
  }
  
  pots_.Read();
  uint8_t index = pots_.last_read();
  uint16_t value = pots_.value(index);
  if (value != pot_value_[index]) {
    pot_value_[index] = value;
    // Software correction for the neat (but incorrect) hardware layout.
    if (index < 4) {
      index = 3 - index;
    }
    queue_.AddEvent(CONTROL_POT, index, value);
  }
  
  if (voicecard_tx.sd_card_busy()) {
    display.ForceStatus(0xdb);
    leds.set_direct_pixel(LED_STATUS, 0x0f);
  }

  // O
  leds.Write();
  lcd.Tick();
}

/* static */
void Ui::ShowPageRelative(int8_t increment) {
  // Disable page scrolling for the system pages.
  if (page_info_.index >= PAGE_LIBRARY) {
    return; 
  }
  
  int8_t current_page = page_info_.index;
  current_page += increment;
  if (current_page < 0) {
    current_page = PAGE_MULTI_CLOCK;
  }
  if (current_page > PAGE_MULTI_CLOCK) {
    current_page = 0;
  }
  ShowPage(static_cast<UiPageNumber>(current_page));
  // Jump to the last control when scrolling backwards.
  if (increment >= 0) {
    (*event_handlers_.SetActiveControl)(ACTIVE_CONTROL_FIRST);
  } else {
    (*event_handlers_.SetActiveControl)(ACTIVE_CONTROL_LAST);
  }
}

const prog_uint8_t part_leds_remap[] PROGMEM = { 0, 3, 1, 4, 2, 5 };

/* static */
void Ui::DoEvents() {
  display.Tick();
  
  uint8_t redraw = 0;
  while (queue_.available()) {
    Event e = queue_.PullEvent();
    queue_.Touch();
    redraw = 1;
    if (e.control_type == 0x3 && e.control_id == 0x3f && e.value == 0xff) {
      // Dummy event, continue
      continue;
    }
    switch (e.control_type) {
      case CONTROL_ENCODER_CLICK:
        (*event_handlers_.OnClick)();
        break;
        
      case CONTROL_ENCODER:
        if (e.control_id == 0) {
          if (s1_held_) {
            // Mode select: cycle engine type via padding[2].
            // Each engine gets its own slot for the shared patch bytes
            // (offsets 0-15, 104) so switching engines preserves state.
            static const uint8_t pages[] PROGMEM = {
              PAGE_OSCILLATORS, PAGE_FM4OP,
              PAGE_KS_PLUCK, PAGE_WESTCOAST
            };

            // Shared byte offsets: 0-15 plus 104 (feedback).
            static const uint8_t kSharedCount = 17;
            static const uint8_t shared_offsets[kSharedCount] PROGMEM = {
              0, 1, 2, 3, 4, 5, 6, 7,
              8, 9, 10, 11, 12, 13, 14, 15,
              104
            };

            // Per-engine state slots, initialized with defaults.
            // [engine][byte index] — 4 engines × 17 bytes = 68 bytes.
            static uint8_t engine_state[ENGINE_LAST][kSharedCount] = {
              // ENGINE_CLASSIC: saw osc, centered mix, everything else 0.
              { WAVEFORM_SAW, 0, 0, 0, WAVEFORM_NONE, 0, 0, 0,
                32, 0, 0, 0, 0, 0, 0, 0,  0 },
              // ENGINE_FM4OP: algo 1, 1:1 ratios, gentle levels.
              { WAVEFORM_SAW, 1, 4, 0, 0, 0, 4, 0,
                4, 0, 4, 0, 20, 10, 10, 1,  0 },
              // ENGINE_KS_PLUCK: moderate damping, centered body/position.
              { 0, 40, 0, 0, 0, 64, 64, 64,
                64, 0, 0, 0, 0, 0, 0, 0,  0 },
              // ENGINE_WESTCOAST: sine, moderate fold.
              { 0, 80, 0, 0, 0, 64, 0, 0,
                64, 0, 0, 0, 0, 0, 0, 0,  0 },
            };

            uint8_t* patch = multi.mutable_part(
                state_.active_part)->mutable_raw_patch_data();
            uint8_t prev = patch[106];  // current engine
            int8_t idx = prev;
            idx += (static_cast<int8_t>(e.value) > 0) ? 1 : -1;
            if (idx < 0) idx = ENGINE_LAST - 1;
            if (idx >= ENGINE_LAST) idx = 0;

            // Save current engine's state.
            for (uint8_t i = 0; i < kSharedCount; ++i) {
              engine_state[prev][i] = patch[pgm_read_byte(&shared_offsets[i])];
            }
            // Load target engine's state.
            patch[106] = idx;
            for (uint8_t i = 0; i < kSharedCount; ++i) {
              patch[pgm_read_byte(&shared_offsets[i])] = engine_state[idx][i];
            }

            multi.mutable_part(state_.active_part)->TouchPatch();
            ShowPage(static_cast<UiPageNumber>(
                pgm_read_byte(&pages[idx])));
          } else {
            (*event_handlers_.OnIncrement)(e.value);
          }
        } else {
          int8_t new_part = state_.active_part + e.value;
          new_part = Clip(new_part, 0, kNumParts - 1);
          state_.active_part = new_part;
        }
        break;

      case CONTROL_SWITCH:
        // Double-press S1 on osc/special page toggles mode select.
        if (e.control_id == 0 && (
            active_page_ == PAGE_OSCILLATORS ||
            active_page_ == PAGE_MIXER ||
            active_page_ == PAGE_FM4OP ||
            active_page_ == PAGE_KS_PLUCK ||
            active_page_ == PAGE_WESTCOAST)) {
          s1_held_ = !s1_held_;
          break;
        }
        // Any other button exits mode select.
        s1_held_ = 0;
        if (!(*event_handlers_.OnKey)(e.control_id)) {
          if (page_info_.group == e.control_id) {
            ShowPage(page_info_.next_page);
          } else {
            ShowPage(most_recent_page_in_group_[e.control_id]);
          }
        }
        break;
        
      case CONTROL_POT:
        (*event_handlers_.OnPot)(e.control_id, e.value);
        break;
    }
  }
  
  if (queue_.idle_time_ms() > 800) {
    queue_.Touch();
    if ((*event_handlers_.OnIdle)()) {
      redraw = 1;
    }
  }
  
  if (multi.flags() & FLAG_HAS_CHANGE) {
    redraw = 1;
    multi.ClearFlag(FLAG_HAS_CHANGE);
  }
  
  if (redraw) {
    display.Clear();
    // The status icon is displayed when there is blank space at the left/right
    // of the page. We don't want the icon to display the icon to show up on the
    // right side of the page, so we fill the last character of the first line
    // with an invisible, non-space character.
    display.line_buffer(0)[39] = '\xfe';
    
    display.set_cursor_position(kLcdNoCursor);
    (*event_handlers_.UpdateScreen)();
  }
  
  leds.Clear();
  leds.set_pixel(
      LED_PART_1 + pgm_read_byte(part_leds_remap + state_.active_part), 0xf0);
  for (uint8_t i = 0; i < kNumVoices; ++i) {
    uint8_t led_index = pgm_read_byte(part_leds_remap + i);
    uint8_t velocity = voicecard_tx.voice_status(i) >> 3;
    leds.set_pixel(
        LED_PART_1 + led_index,
        velocity | leds.pixel(LED_PART_1 + led_index));
  }
  (*event_handlers_.UpdateLeds)();
  // Blink LED 1 when S1 held on osc/special page (mode select ready).
  if (s1_held_ && (
      active_page_ == PAGE_OSCILLATORS ||
      active_page_ == PAGE_FM4OP ||
      active_page_ == PAGE_KS_PLUCK ||
      active_page_ == PAGE_WESTCOAST)) {
    uint8_t blink = (cycle_ & 0x40) ? 0xf0 : 0x0f;
    leds.set_pixel(LED_1, blink);
  }
  if (system_settings.data().swap_leds_colors) {
    for (uint8_t i = 0; i < 15; ++i) {
      leds.set_pixel(i, U8Swap4(leds.pixel(i)));
    }
  }
  leds.Sync();
}

/* static */
void Ui::ShowPreviousPage() {
  // Route to the correct engine page based on padding[2].
  uint8_t engine = multi.part(state_.active_part).raw_patch_data()[106];
  switch (engine) {
    case ENGINE_FM4OP: ShowPage(PAGE_FM4OP); break;
    case ENGINE_KS_PLUCK: ShowPage(PAGE_KS_PLUCK); break;
    case ENGINE_WESTCOAST: ShowPage(PAGE_WESTCOAST); break;
    default: ShowPage(most_recent_non_system_page_); break;
  }
}

/* static */
void Ui::ShowPage(UiPageNumber page, uint8_t initialize) {
  // Flush the event queue.
  queue_.Flush();
  queue_.Touch();
  pots_.Lock(16);
  
  if (page <= PAGE_KNOB_ASSIGN) {
    most_recent_non_system_page_ = page;
  }
  active_page_ = page;

  // Load the page info structure in RAM.
  ResourcesManager::Load(page_registry, page, &page_info_);

  // Load the event handlers structure in RAM.
  ResourcesManager::Load(page_info_.event_handlers, 0, &event_handlers_);
  most_recent_page_in_group_[page_info_.group] = page;
  if (initialize) {
    (*event_handlers_.OnInit)(&page_info_);
  }
  (*event_handlers_.UpdateScreen)();
}

/* static */
void Ui::ShowDialogBox(uint8_t dialog_id, Dialog dialog, uint8_t choice) {
  // Flush the event queue.
  queue_.Flush();
  queue_.Touch();
  
  // Replace the current page by the dialog box handler.
  ResourcesManager::Load(&DialogBox::event_handlers_, 0, &event_handlers_);
  page_info_.dialog = dialog;
  page_info_.index = dialog_id;
  (*event_handlers_.OnInit)(&page_info_);
  DialogBox::set_choice(choice);
  (*event_handlers_.UpdateScreen)();
}

/* static */
void Ui::CloseDialogBox(uint8_t return_value) {
  // Return to the page that was active when the dialog was shown.
  uint8_t returning_from = page_info_.index;
  ShowPage(active_page_, 0);
  (*event_handlers_.OnDialogClosed)(returning_from, return_value);
}

}  // namespace ambika
