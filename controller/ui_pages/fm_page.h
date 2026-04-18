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

#ifndef CONTROLLER_UI_PAGES_FM_PAGE_H_
#define CONTROLLER_UI_PAGES_FM_PAGE_H_

#include "controller/ui_pages/ui_page.h"

namespace ambika {

// FM parameter definitions for each knob position.
// Each entry: patch byte offset, min, max.
struct FmParam {
  uint8_t offset;
  uint8_t min_value;
  uint8_t max_value;
};

class FmPage : public UiPage {
 public:
  FmPage() { }

  static void OnInit(PageInfo* info);
  static uint8_t OnIncrement(int8_t increment);
  static uint8_t OnClick();
  static uint8_t OnPot(uint8_t index, uint8_t value);
  static uint8_t OnKey(uint8_t key);

  static void UpdateScreen();
  static void UpdateLeds();

  static const prog_EventHandlers event_handlers_;

  // Check if the active part's osc1 is set to FM4OP.
  static uint8_t IsFm4OpActive();

 private:
  static void PrintFmValue(char* buffer, uint8_t value, uint8_t param_type);
  static uint8_t page_index_;  // 0 = ops 1-2, 1 = ops 3-4

  DISALLOW_COPY_AND_ASSIGN(FmPage);
};

}  // namespace ambika

#endif  // CONTROLLER_UI_PAGES_FM_PAGE_H_
