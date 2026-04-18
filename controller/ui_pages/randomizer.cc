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
// Smart patch randomizer implementation.

#include "controller/ui_pages/randomizer.h"

#include "avrlib/random.h"
#include "avrlib/string.h"

#include "controller/display.h"
#include "controller/leds.h"
#include "controller/multi.h"
#include "controller/parameter.h"
#include "controller/ui.h"

namespace ambika {

using namespace avrlib;

/* static */
uint8_t Randomizer::section_depth_[RANDOM_SECTION_LAST];

/* static */
uint8_t Randomizer::triggered_;

static const prog_char random_labels[] PROGMEM =
    "osc filt env  mod  mix  lfo  dpth trg ";

/* static */
const prog_EventHandlers Randomizer::event_handlers_ PROGMEM = {
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
void Randomizer::OnInit(PageInfo* info) {
  triggered_ = 0;
}

/* static */
uint8_t Randomizer::OnIncrement(int8_t increment) {
  return 1;
}

/* static */
uint8_t Randomizer::OnClick() {
  Randomize();
  triggered_ = 30;  // Flash counter for visual feedback.
  return 1;
}

/* static */
uint8_t Randomizer::OnPot(uint8_t index, uint8_t value) {
  if (index < RANDOM_SECTION_LAST) {
    section_depth_[index] = value;
  }
  return 1;
}

/* static */
uint8_t Randomizer::OnKey(uint8_t key) {
  if (key < 6) {
    Ui::mutable_state()->active_part = key;
    return 1;
  }
  return 0;
}

/* static */
void Randomizer::RandomizeRange(
    uint8_t* data, uint8_t start, uint8_t end,
    uint8_t min_val, uint8_t max_val, uint8_t depth) {
  if (depth == 0) return;
  for (uint8_t i = start; i < end; ++i) {
    // Generate a random offset scaled by depth.
    int16_t current = data[i];
    int16_t range = max_val - min_val;
    int16_t offset = static_cast<int8_t>(Random::GetByte());
    offset = (offset * depth * range) >> 14;
    current += offset;
    if (current < min_val) current = min_val;
    if (current > max_val) current = max_val;
    data[i] = static_cast<uint8_t>(current);
  }
}

/* static */
void Randomizer::Randomize() {
  Part* part = multi.mutable_part(Ui::state().active_part);
  uint8_t* patch = part->mutable_raw_patch_data();

  // Global depth multiplier (knob 7).
  uint8_t global = section_depth_[RANDOM_SECTION_DEPTH];
  if (global == 0) global = 127;  // Default to full if not set.

  // --- Oscillators (offset 0-7) ---
  uint8_t osc_depth = (section_depth_[RANDOM_SECTION_OSC] * global) >> 7;
  if (osc_depth) {
    // Waveform shapes: offset 0, 4
    for (uint8_t i = 0; i < 2; ++i) {
      uint8_t idx = i * 4;  // osc[0].shape=0, osc[1].shape=4
      if (Random::GetByte() < osc_depth * 2) {
        patch[idx] = Random::GetByte() % WAVEFORM_LAST;
      }
    }
    // Parameters: offset 1, 5
    RandomizeRange(patch, 1, 2, 0, 127, osc_depth);
    RandomizeRange(patch, 5, 6, 0, 127, osc_depth);
    // Range: offset 2, 6 (signed, keep in -12..12)
    RandomizeRange(patch, 2, 3, 0, 48, osc_depth);  // Will be interpreted as int8
    RandomizeRange(patch, 6, 7, 0, 48, osc_depth);
    // Detune: offset 3, 7
    RandomizeRange(patch, 3, 4, 0, 128, osc_depth);
    RandomizeRange(patch, 7, 8, 0, 128, osc_depth);
  }

  // --- Mixer (offset 8-15) ---
  uint8_t mix_depth = (section_depth_[RANDOM_SECTION_MIXER] * global) >> 7;
  if (mix_depth) {
    RandomizeRange(patch, 8, 9, 0, 63, mix_depth);    // mix balance
    // Skip mix_op (9) — keep current operator
    RandomizeRange(patch, 10, 11, 0, 63, mix_depth);   // mix parameter
    // Skip sub osc shape (11)
    RandomizeRange(patch, 12, 13, 0, 63, mix_depth);   // sub osc level
    RandomizeRange(patch, 13, 14, 0, 63, mix_depth);   // noise level
    RandomizeRange(patch, 14, 15, 0, 63, mix_depth);   // fuzz
    RandomizeRange(patch, 15, 16, 0, 31, mix_depth);   // crush
  }

  // --- Filter (offset 16-23) ---
  uint8_t filt_depth = (section_depth_[RANDOM_SECTION_FILTER] * global) >> 7;
  if (filt_depth) {
    RandomizeRange(patch, 16, 17, 0, 127, filt_depth);  // cutoff
    RandomizeRange(patch, 17, 18, 0, 63, filt_depth);   // resonance
    // Skip filter mode (18) — keep current
    // Skip filter 2 (19-21)
    RandomizeRange(patch, 22, 23, 0, 63, filt_depth);   // filter env
    RandomizeRange(patch, 23, 24, 0, 63, filt_depth);   // filter lfo
  }

  // --- Envelopes (offset 24-47, 3x8 bytes) ---
  uint8_t env_depth = (section_depth_[RANDOM_SECTION_ENVELOPES] * global) >> 7;
  if (env_depth) {
    for (uint8_t env = 0; env < 3; ++env) {
      uint8_t base = 24 + env * 8;
      RandomizeRange(patch, base, base + 4, 0, 127, env_depth);  // ADSR
    }
  }

  // --- LFOs (within envelope blocks, offset 28-29, 36-37, 44-45) ---
  uint8_t lfo_depth = (section_depth_[RANDOM_SECTION_LFOS] * global) >> 7;
  if (lfo_depth) {
    for (uint8_t lfo = 0; lfo < 3; ++lfo) {
      uint8_t base = 24 + lfo * 8;
      // LFO shape (offset +4)
      if (Random::GetByte() < lfo_depth * 2) {
        patch[base + 4] = Random::GetByte() % LFO_WAVEFORM_LAST;
      }
      // LFO rate (offset +5)
      RandomizeRange(patch, base + 5, base + 6, 0, 127, lfo_depth);
    }
  }

  // --- Modulation matrix (offset 50-91, 14x3 bytes) ---
  uint8_t mod_depth = (section_depth_[RANDOM_SECTION_MODULATION] * global) >> 7;
  if (mod_depth) {
    for (uint8_t mod = 0; mod < 14; ++mod) {
      uint8_t base = 50 + mod * 3;
      // Only randomize if depth is high enough — mod matrix is sensitive.
      if (Random::GetByte() < mod_depth) {
        // Source
        patch[base] = Random::GetByte() % MOD_SRC_LAST;
      }
      if (Random::GetByte() < mod_depth) {
        // Destination
        patch[base + 1] = Random::GetByte() % MOD_DST_LAST;
      }
      // Amount: always randomize with depth scaling
      RandomizeRange(patch, base + 2, base + 3, 0, 126, mod_depth);
    }
  }

  // Push the modified patch to the voicecards.
  part->TouchPatch();
}

/* static */
void Randomizer::UpdateScreen() {
  char* buffer = display.line_buffer(0);
  // Top row: section labels
  for (uint8_t i = 0; i < 40; ++i) {
    buffer[i] = pgm_read_byte(random_labels + i);
  }

  buffer = display.line_buffer(1);
  // Bottom row: depth values for each section.
  for (uint8_t i = 0; i < 7; ++i) {
    uint8_t pos = i * 5;
    uint8_t val = section_depth_[i];
    // Display as 0-99 to fit in 3 chars.
    uint8_t display_val = (val * 100) >> 7;
    if (display_val > 99) display_val = 99;
    buffer[pos] = ' ';
    buffer[pos + 1] = display_val >= 10 ? ('0' + display_val / 10) : ' ';
    buffer[pos + 2] = '0' + display_val % 10;
    buffer[pos + 3] = ' ';
    buffer[pos + 4] = ' ';
  }
  // Last position: show trigger feedback.
  if (triggered_) {
    buffer[35] = '*';
    buffer[36] = 'G';
    buffer[37] = 'O';
    buffer[38] = '*';
    --triggered_;
  } else {
    buffer[35] = 'p';
    buffer[36] = 'u';
    buffer[37] = 's';
    buffer[38] = 'h';
  }
  buffer[39] = ' ';
}

/* static */
void Randomizer::UpdateLeds() {
  leds.set_pixel(LED_7, 0x0f);
  leds.set_pixel(LED_8, 0x0f);
}

}  // namespace ambika
