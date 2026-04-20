# Seven Envelopes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand Carcosa from 3 to 7 ADSR envelopes available as modulation sources in all synthesis modes.

**Architecture:** Grow the Patch struct from 112 to 128 bytes by appending 4 extra ADSR blocks (4 bytes each = 16 bytes). Decouple kNumLfos from kNumEnvelopes — add kNumTotalEnvelopes=7 while keeping kNumLfos=3 and kNumEnvLfoSlots=3. Remove MOD_SRC_SEQ_1 and MOD_SRC_SEQ_2 from the modulation source enum, insert MOD_SRC_ENV_4 through MOD_SRC_ENV_7 after ENV_3. Create custom EnvPage class for dynamic parameter routing based on selected envelope. Breaking change to patch format — all stored patches become invalid.

**Tech Stack:** AVR C++ (avr-gcc), ATmega328p voicecard, ATmega644p controller

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `common/patch.h` | Modify | Add ExtraEnvelopeSettings struct, expand Patch to 128 bytes, update ModulationSource enum, add kNumTotalEnvelopes |
| `voicecard/envelope.h` | No change | Envelope class already supports all needed features |
| `voicecard/voice.h` | Modify | Change envelope array size to kNumTotalEnvelopes |
| `voicecard/voice.cc` | Modify | Render and update all 7 envelopes, extend init_patch with 16 zero bytes, replace MOD_SRC_SEQ references |
| `voicecard/voicecard_rx.h` | Modify | Extend COMMAND_RETRIGGER_ENVELOPE cases from 0-2 to 0-6 |
| `controller/parameter.h` | Modify | Bump kNumParameters to 80 |
| `controller/parameter.cc` | Modify | Add env4-7 ADSR parameters, replace mod source string display with switch, update CC map |
| `controller/part.cc` | Modify | Replace MOD_SRC_SEQ references in init_patch, extend init_patch with 16 zero bytes |
| `controller/ui.h` | Modify | Add PAGE_ENV_EXTRA enum entry |
| `controller/ui.cc` | Modify | Expand envelope selector range 0-6, register EnvPage |
| `controller/ui_pages/env_page.h` | Create | Custom envelope page header |
| `controller/ui_pages/env_page.cc` | Create | Custom envelope page — routes knobs to env1-3 (stride 8) or env4-7 (stride 4) based on selector |
| `controller/ui_pages/randomizer.cc` | Modify | Extend envelope randomization to cover extra_env offsets 112-127 |
| `controller/ui_pages/os_info_page.cc` | Modify | Bump version to v2.0 |
| `CARCOSA.md` | Modify | Document 7 envelopes, update mod source list, MIDI CC table |

**Not updated (deprecated):** `utils/convert_shruthi_data.py` — hardcoded enum values no longer valid, utility is unmaintained.

---

## Data Layout

### New Patch (128 bytes)
```
Offset 0-111:   Unchanged (all existing fields at same offsets)
Offset 112-115: extra_env[0] — env4 ADSR (attack, decay, sustain, release)
Offset 116-119: extra_env[1] — env5 ADSR
Offset 120-123: extra_env[2] — env6 ADSR
Offset 124-127: extra_env[3] — env7 ADSR
```

### Constants
```
kNumEnvLfoSlots = 3  (env_lfo[3] — envelopes with paired LFOs)
kNumLfos = 3         (unchanged, decoupled from envelope count)
kNumTotalEnvelopes = 7
kNumExtraEnvelopes = 4
```

### ModulationSource Enum (new order)
```
 0: MOD_SRC_ENV_1
 1: MOD_SRC_ENV_2
 2: MOD_SRC_ENV_3
 3: MOD_SRC_ENV_4       ← new
 4: MOD_SRC_ENV_5       ← new
 5: MOD_SRC_ENV_6       ← new
 6: MOD_SRC_ENV_7       ← new
 7: MOD_SRC_LFO_1       ← was 3
 8: MOD_SRC_LFO_2       ← was 4
 9: MOD_SRC_LFO_3       ← was 5
10: MOD_SRC_LFO_4       ← was 6
11: MOD_SRC_OP_1        ← was 7
12: MOD_SRC_OP_2        ← was 8
13: MOD_SRC_OP_3        ← was 9
14: MOD_SRC_OP_4        ← was 10
15: MOD_SRC_ARP_STEP    ← was 13 (SEQ_1/SEQ_2 removed)
16: MOD_SRC_VELOCITY    ← was 14
17: MOD_SRC_AFTERTOUCH
18: MOD_SRC_PITCH_BEND
19: MOD_SRC_WHEEL
20: MOD_SRC_WHEEL_2
21: MOD_SRC_EXPRESSION
22: MOD_SRC_NOTE
23: MOD_SRC_GATE
24: MOD_SRC_NOISE
25: MOD_SRC_RANDOM
26: MOD_SRC_CONSTANT_256
27: MOD_SRC_CONSTANT_128
28: MOD_SRC_CONSTANT_64
29: MOD_SRC_CONSTANT_32
30: MOD_SRC_CONSTANT_16
31: MOD_SRC_CONSTANT_8
32: MOD_SRC_CONSTANT_4
33: MOD_SRC_LAST
```

---

### Task 1: Expand Patch struct and enums

**Files:**
- Modify: `common/patch.h`

- [ ] **Step 1: Add ExtraEnvelopeSettings struct**

```cpp
struct ExtraEnvelopeSettings {
  uint8_t attack;
  uint8_t decay;
  uint8_t sustain;
  uint8_t release;
};
```

- [ ] **Step 2: Expand Patch struct**

Add after `padding[8]`:
```cpp
  // Offset: 112-128
  ExtraEnvelopeSettings extra_env[4];
};
```

- [ ] **Step 3: Update constants**

Do NOT change `kNumEnvelopes`. Instead:
```cpp
static const uint8_t kNumEnvLfoSlots = 3;   // rename from kNumEnvelopes
static const uint8_t kNumLfos = kNumEnvLfoSlots;  // keep at 3
static const uint8_t kNumTotalEnvelopes = 7;
static const uint8_t kNumExtraEnvelopes = 4;
```

Then find/replace all `kNumEnvelopes` references:
- Uses that mean "env_lfo slots" or "LFO count" → `kNumEnvLfoSlots`
- Uses that mean "total envelope instances on voicecard" → `kNumTotalEnvelopes`

- [ ] **Step 4: Update ModulationSource enum**

Remove `MOD_SRC_SEQ_1` and `MOD_SRC_SEQ_2`. Insert `MOD_SRC_ENV_4` through `MOD_SRC_ENV_7` after `MOD_SRC_ENV_3`.

- [ ] **Step 5: Add PatchParameter entries**

```cpp
  PRM_PATCH_EXTRA_ENV_ATTACK = 112,
  PRM_PATCH_EXTRA_ENV_DECAY,
  PRM_PATCH_EXTRA_ENV_SUSTAIN,
  PRM_PATCH_EXTRA_ENV_RELEASE,
```

- [ ] **Step 6: Build voicecard to check compile**

Run: `rm -rf build/ambika_voicecard && make all 2>&1 | grep error | head 20`
Expect: errors from renamed constants — fix all references.

- [ ] **Step 7: Commit**

---

### Task 2: Update voicecard envelope rendering

**Files:**
- Modify: `voicecard/voice.h`
- Modify: `voicecard/voice.cc`
- Modify: `voicecard/voicecard_rx.h`

- [ ] **Step 1: Expand envelope array**

In `voice.h`, change `envelope_[kNumEnvelopes]` to `envelope_[kNumTotalEnvelopes]`. Update the static definition in `voice.cc`.

- [ ] **Step 2: Update LoadSources**

Replace 3 hardcoded render lines with loop:
```cpp
for (uint8_t i = 0; i < kNumTotalEnvelopes; ++i) {
  modulation_sources_[MOD_SRC_ENV_1 + i] = envelope_[i].Render();
}
```

- [ ] **Step 3: Update UpdateDestinations**

Keep existing env_lfo loop for envelopes 0-2 (uses `kNumEnvLfoSlots`). Add second loop for extra envelopes 3-6:
```cpp
for (int i = 0; i < kNumExtraEnvelopes; ++i) {
  envelope_[kNumEnvLfoSlots + i].Update(
      patch_.extra_env[i].attack,
      patch_.extra_env[i].decay,
      patch_.extra_env[i].sustain,
      patch_.extra_env[i].release);
}
```

- [ ] **Step 4: Update Init loop**

Use `kNumTotalEnvelopes` for envelope init loop. Use `kNumEnvLfoSlots` for env_lfo-specific init.

- [ ] **Step 5: Update TriggerEnvelope**

The `TriggerEnvelope(uint8_t stage)` loop that triggers ALL envelopes: use `kNumTotalEnvelopes`.

- [ ] **Step 6: Extend COMMAND_RETRIGGER_ENVELOPE in voicecard_rx.h**

Add cases for envelopes 3-6:
```cpp
case COMMAND_RETRIGGER_ENVELOPE:
case COMMAND_RETRIGGER_ENVELOPE + 1:
case COMMAND_RETRIGGER_ENVELOPE + 2:
case COMMAND_RETRIGGER_ENVELOPE + 3:
case COMMAND_RETRIGGER_ENVELOPE + 4:
case COMMAND_RETRIGGER_ENVELOPE + 5:
case COMMAND_RETRIGGER_ENVELOPE + 6:
    voice.TriggerEnvelope(command_ & 0x0f, ATTACK);
    break;
```

- [ ] **Step 7: Update init_patch in voice.cc**

Extend the PROGMEM `init_patch` with 16 zero bytes for extra_env[4]:
```cpp
  // Extra envelopes (env4-7 ADSR)
  0, 40, 20, 60,
  0, 40, 20, 60,
  0, 40, 20, 60,
  0, 40, 20, 60,
```

- [ ] **Step 8: Replace MOD_SRC_SEQ references in voice.cc init_patch**

Replace `MOD_SRC_SEQ_1` and `MOD_SRC_SEQ_2` with `MOD_SRC_ENV_4` and `MOD_SRC_ENV_5` (or zero them out).

- [ ] **Step 9: Update ResetAllControllers**

All `MOD_SRC_CONSTANT_*` enum values shifted. These are referenced by name so they auto-update, but verify.

- [ ] **Step 10: Build voicecard, check size**

Run: `rm -rf build/ambika_voicecard && make all && make bin`
Check: `avr-size build/ambika_voicecard/ambika_voicecard.elf`
Verify: RAM (data+bss) leaves >= 450 bytes free. Flash fits.

- [ ] **Step 11: Commit**

---

### Task 3: Update controller init_patch and mod source references

**Files:**
- Modify: `controller/part.cc`

- [ ] **Step 1: Replace MOD_SRC_SEQ references in init_patch**

Replace `MOD_SRC_SEQ_1` and `MOD_SRC_SEQ_2` in both the modulation matrix init and modifier init sections.

- [ ] **Step 2: Extend init_patch with extra_env bytes**

Add 16 bytes to the PROGMEM `init_patch` for the extra envelopes.

- [ ] **Step 3: Fix all kNumEnvelopes → kNumEnvLfoSlots references**

Grep part.cc for `kNumEnvelopes` and replace with appropriate constant.

- [ ] **Step 4: Build controller**

- [ ] **Step 5: Commit**

---

### Task 4: Update mod source display strings

**Files:**
- Modify: `controller/parameter.cc`

- [ ] **Step 1: Replace mod source string lookup with switch statement**

In `PrintValue`, for `UNIT_MODULATION_SOURCE`, replace the `text + value` string table lookup with a complete switch covering all 33 sources. Use PSTR for each name. Handle both short (width <= 5) and long (width > 5) variants.

```cpp
if (unit == UNIT_MODULATION_SOURCE) {
  const prog_char* name;
  switch (value) {
    case MOD_SRC_ENV_1: name = (width > 5) ? PSTR("env 1") : PSTR("env1"); break;
    case MOD_SRC_ENV_2: name = (width > 5) ? PSTR("env 2") : PSTR("env2"); break;
    // ... all 33 sources
  }
  strncpy_P(buffer, name, width);
  AlignRight(buffer, width);
  return;
}
```

- [ ] **Step 2: Build and verify**

- [ ] **Step 3: Commit**

---

### Task 5: Add controller parameters for extra envelopes

**Files:**
- Modify: `controller/parameter.h`
- Modify: `controller/parameter.cc`

- [ ] **Step 1: Add 4 parameter definitions (76-79)**

Extra envelope ADSR parameters. num_instances=4, stride=4, indexed_by a new UI state variable (see Task 6).

Actually, since we're creating a custom EnvPage, these parameters don't need the stride/indexed_by pattern — the EnvPage handles the mapping directly by reading/writing patch bytes. The parameters just need to exist for MIDI CC access.

```cpp
// 76: extra env attack (instance 0, MIDI-accessible)
{ PARAMETER_LEVEL_PATCH, PRM_PATCH_EXTRA_ENV_ATTACK,
  UNIT_RAW_UINT8, 0, 127, 4, 4, 0xff, 0xff,
  STR_RES_ATTK, STR_RES_ATTACK, STR_RES_ENVELOPE },
// 77-79: decay, sustain, release (same pattern)
```

- [ ] **Step 2: Bump kNumParameters to 80**

- [ ] **Step 3: Update MIDI CC map**

CC 111-114 → params 76-79.

- [ ] **Step 4: Build and verify**

- [ ] **Step 5: Commit**

---

### Task 6: Create custom EnvPage for 7 envelopes

**Files:**
- Create: `controller/ui_pages/env_page.h`
- Create: `controller/ui_pages/env_page.cc`
- Modify: `controller/ui.h`
- Modify: `controller/ui.cc`

- [ ] **Step 1: Create EnvPage class**

Follows the same pattern as FmPage/KsPage/WcPage. Has a selector (knob 1) that cycles 0-6 (env1-env7).

When selector 0-2: shows LFO controls (rate, shape) on top row + ADSR on bottom row + envelope curve. Reads/writes from `patch_.env_lfo[selector]`.

When selector 3-6: hides LFO controls (top row knobs 2-4 = 0xff). ADSR on bottom row reads/writes from `patch_.extra_env[selector - 3]`. No envelope curve knob.

Display: selector shows "env1" through "env7".

- [ ] **Step 2: Register EnvPage in ui.h and ui.cc**

Replace the existing `PAGE_ENV_LFO` entry to use `EnvPage::event_handlers_` instead of `ParameterEditor::event_handlers_`.

Or: keep PAGE_ENV_LFO for the normal parameter editor and add the EnvPage as the handler. Since the existing page already uses custom parameter indices, swapping the handler is cleanest.

- [ ] **Step 3: Build and verify**

- [ ] **Step 4: Test selector cycling through all 7 envelopes**

- [ ] **Step 5: Commit**

---

### Task 7: Update randomizer for extra envelopes

**Files:**
- Modify: `controller/ui_pages/randomizer.cc`

- [ ] **Step 1: Extend envelope randomization**

The existing loop `for (uint8_t env = 0; env < 3; ++env)` handles env_lfo offsets 24-47. Add a second loop for extra_env offsets 112-127:

```cpp
if (env_depth) {
  // Original 3 envelopes
  for (uint8_t env = 0; env < 3; ++env) {
    uint8_t base = 24 + env * 8;
    RandomizeRange(patch, base, base + 4, 0, 127, env_depth);
  }
  // Extra 4 envelopes
  for (uint8_t env = 0; env < 4; ++env) {
    uint8_t base = 112 + env * 4;
    RandomizeRange(patch, base, base + 4, 0, 127, env_depth);
  }
}
```

- [ ] **Step 2: Commit**

---

### Task 8: Version bump, docs, release

**Files:**
- Modify: `controller/ui_pages/os_info_page.cc`
- Modify: `CARCOSA.md`
- Modify: `README.md`

- [ ] **Step 1: Bump version to v2.0**

- [ ] **Step 2: Update manual**

Add env4-7 documentation to envelope section. Update mod source list (remove seq1/seq2, add env4-7). Update MIDI CC table. Add note about breaking patch format change.

- [ ] **Step 3: Full build both firmwares**

```bash
rm -rf build/ambika_voicecard build/ambika_controller
make all && make bin
make -f controller/makefile && make -f controller/makefile bin
avr-size build/ambika_voicecard/ambika_voicecard.elf build/ambika_controller/ambika_controller.elf
```

Verify: voicecard RAM leaves >= 450 bytes free. Controller stack >= 240 bytes.

- [ ] **Step 4: Commit, push, create release**

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Enum shift breaks all stored patches | Accepted — breaking change for v2.0. Document in release notes. EEPROM will need wiping on update. |
| kNumLfos coupling with kNumEnvelopes | Decoupled: kNumEnvLfoSlots=3, kNumLfos=3, kNumTotalEnvelopes=7 |
| RETRIGGER_ENVELOPE protocol limited to 0-2 | Extended to 0-6 in voicecard_rx.h |
| init_patch size mismatch after Patch grows | Both init_patches (voice.cc and part.cc) extended with 16 bytes |
| Mod source string table broken by enum shift | Replaced with switch statement — no string table dependency |
| EEPROM layout invalid after Patch grows | Document: user must reinitialize from OS info page or save/reload from SD |
| convert_shruthi_data.py broken | Deprecated — document as unmaintained |
| Controller RAM from custom EnvPage | Minimal — follows proven FmPage/KsPage/WcPage pattern with PROGMEM arrays |
