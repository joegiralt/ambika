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

#ifndef CONTROLLER_UI_PAGES_ENV_PAGE_H_
#define CONTROLLER_UI_PAGES_ENV_PAGE_H_

#include "controller/ui_pages/ui_page.h"

namespace ambika {

class EnvPage : public UiPage {
 public:
  EnvPage() { }

  static void OnInit(PageInfo* info);
  static uint8_t OnIncrement(int8_t increment);
  static uint8_t OnClick();
  static uint8_t OnPot(uint8_t index, uint8_t value);
  static uint8_t OnKey(uint8_t key);

  static void UpdateScreen();
  static void UpdateLeds();

  static const prog_EventHandlers event_handlers_;

 private:
  static uint8_t selector_;  // 0-6: which envelope

  // Compute the patch byte offset for ADSR knob (0-3) given current selector.
  static uint8_t AdsrOffset(uint8_t knob);

  DISALLOW_COPY_AND_ASSIGN(EnvPage);
};

}  // namespace ambika

#endif  // CONTROLLER_UI_PAGES_ENV_PAGE_H_
