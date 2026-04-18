// Copyright 2026 Ambika contributors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Custom UI page for Karplus-Strong plucked string synthesis.

#ifndef CONTROLLER_UI_PAGES_KS_PAGE_H_
#define CONTROLLER_UI_PAGES_KS_PAGE_H_

#include "controller/ui_pages/ui_page.h"

namespace ambika {

class KsPage : public UiPage {
 public:
  KsPage() { }

  static void OnInit(PageInfo* info);
  static uint8_t OnIncrement(int8_t increment);
  static uint8_t OnClick();
  static uint8_t OnPot(uint8_t index, uint8_t value);
  static uint8_t OnKey(uint8_t key);

  static void UpdateScreen();
  static void UpdateLeds();

  static const prog_EventHandlers event_handlers_;

  static uint8_t IsKsActive();

 private:
  static uint8_t page_index_;
  DISALLOW_COPY_AND_ASSIGN(KsPage);
};

}  // namespace ambika

#endif  // CONTROLLER_UI_PAGES_KS_PAGE_H_
