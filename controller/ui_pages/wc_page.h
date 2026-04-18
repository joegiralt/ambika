// Copyright 2026 Ambika contributors.
//
// West coast oscillator UI page.

#ifndef CONTROLLER_UI_PAGES_WC_PAGE_H_
#define CONTROLLER_UI_PAGES_WC_PAGE_H_

#include "controller/ui_pages/ui_page.h"

namespace ambika {

class WcPage : public UiPage {
 public:
  WcPage() { }

  static void OnInit(PageInfo* info);
  static uint8_t OnIncrement(int8_t increment);
  static uint8_t OnClick();
  static uint8_t OnPot(uint8_t index, uint8_t value);
  static uint8_t OnKey(uint8_t key);

  static void UpdateScreen();
  static void UpdateLeds();

  static const prog_EventHandlers event_handlers_;
  static uint8_t IsWcActive();

 private:
  static uint8_t page_index_;
  DISALLOW_COPY_AND_ASSIGN(WcPage);
};

}  // namespace ambika

#endif  // CONTROLLER_UI_PAGES_WC_PAGE_H_
