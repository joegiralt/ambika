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
// Smart patch randomizer. Each knob controls randomization depth
// for a different section of the patch. Click encoder to trigger.

#ifndef CONTROLLER_UI_PAGES_RANDOMIZER_H_
#define CONTROLLER_UI_PAGES_RANDOMIZER_H_

#include "controller/ui_pages/ui_page.h"

namespace ambika {

// Randomization sections controlled by each knob.
enum RandomSection {
  RANDOM_SECTION_OSC,        // Knob 1: Oscillator shapes, params, range, detune
  RANDOM_SECTION_FILTER,     // Knob 2: Filter cutoff, resonance, mode, env/lfo
  RANDOM_SECTION_ENVELOPES,  // Knob 3: ADSR times for all 3 envelopes
  RANDOM_SECTION_MODULATION, // Knob 4: Mod matrix sources, destinations, amounts
  RANDOM_SECTION_MIXER,      // Knob 5: Mix balance, sub, noise, fuzz, crush
  RANDOM_SECTION_LFOS,       // Knob 6: LFO shapes, rates
  RANDOM_SECTION_DEPTH,      // Knob 7: Global depth (scales all others)
  RANDOM_SECTION_LAST
};

class Randomizer : public UiPage {
 public:
  Randomizer() { }

  static void OnInit(PageInfo* info);
  static uint8_t OnIncrement(int8_t increment);
  static uint8_t OnClick();
  static uint8_t OnPot(uint8_t index, uint8_t value);
  static uint8_t OnKey(uint8_t key);

  static void UpdateScreen();
  static void UpdateLeds();

  static const prog_EventHandlers event_handlers_;

 private:
  static void Randomize();
  static void RandomizeRange(uint8_t* data, uint8_t start, uint8_t end,
                             uint8_t min_val, uint8_t max_val, uint8_t depth);

  static uint8_t section_depth_[RANDOM_SECTION_LAST];
  static uint8_t triggered_;

  DISALLOW_COPY_AND_ASSIGN(Randomizer);
};

}  // namespace ambika

#endif  // CONTROLLER_UI_PAGES_RANDOMIZER_H_
