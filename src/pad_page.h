#pragma once
// Pad page — reached by holding any pad.
//
// Left half is a 4x4 grid of the sixteen pads; tap a cell to arm it and the
// next key or MIDI note binds to it, so a pad controller or keyboard is
// mapped by playing it.
//
// Right side is three knobs. Each is learned from a physical knob — its
// clockwise, anticlockwise and click usages — and each carries four
// assignable slots that its click cycles through, so three knobs reach twelve
// parameters.
//
// Drawn over the page content area so the pads and sequencer row stay visible
// underneath, and it uses the same key/note action tables as KEYMAP rather
// than a second set of bindings that could disagree with them.

#include <Arduino.h>

#define PAD_KNOBS 3
#define KNOB_SLOTS 4
#define PADPAGE_FILE "/PADPAGE"

bool pad_page_active = false;
bool pad_page_dirty = true;

// -1 = nothing armed. Only one thing learns at a time.
int8_t pad_page_learn_cell = -1;
int8_t pad_page_learn_knob = -1;
uint8_t pad_page_learn_stage = 0;  // 0 = CW, 1 = CCW, 2 = click

uint8_t knob_slot[PAD_KNOBS] = { 0, 0, 0 };
uint8_t knob_param[PAD_KNOBS][KNOB_SLOTS];
uint8_t knob_cw[PAD_KNOBS] = { 0, 0, 0 };
uint8_t knob_ccw[PAD_KNOBS] = { 0, 0, 0 };
uint8_t knob_click[PAD_KNOBS] = { 0, 0, 0 };

void select_rot();
void do_rot();

// ------------------------------------------------------------- persistence

void pad_page_save() {
  File file = SPIFFS.open(PADPAGE_FILE, FILE_WRITE);
  if (!file) return;
  file.write((uint8_t *)knob_param, sizeof(knob_param));
  file.write(knob_cw, sizeof(knob_cw));
  file.write(knob_ccw, sizeof(knob_ccw));
  file.write(knob_click, sizeof(knob_click));
  file.close();
}

void pad_page_load() {
  for (uint8_t k = 0; k < PAD_KNOBS; k++) {
    for (uint8_t s = 0; s < KNOB_SLOTS; s++) {
      // A sensible spread to start from: cutoff, pitch, volume, pan.
      static const uint8_t defaults[KNOB_SLOTS] = { 15, 12, 14, 13 };
      knob_param[k][s] = defaults[s];
    }
  }
  File file = SPIFFS.open(PADPAGE_FILE, FILE_READ);
  if (!file) return;
  file.read((uint8_t *)knob_param, sizeof(knob_param));
  file.read(knob_cw, sizeof(knob_cw));
  file.read(knob_ccw, sizeof(knob_ccw));
  file.read(knob_click, sizeof(knob_click));
  file.close();
}

// ------------------------------------------------------------- knob actions

// Drives the parameter a knob's current slot points at. The rot system works
// on whatever is selected, so this selects the target first and puts the
// selection back afterwards — otherwise turning a knob would silently move
// what the rest of the UI is pointing at.
static void knob_apply(uint8_t k, int delta) {
  if (k >= PAD_KNOBS) return;
  uint8_t target = knob_param[k][knob_slot[k]];
  if (target >= MAX_BARS) return;

  byte previous = selected_rot;
  selected_rot = target;
  select_rot();
  old_counter1 = counter1;
  counter1 = counter1 + delta;
  do_rot();
  selected_rot = previous;
  select_rot();

  pad_page_dirty = true;
  refresh_sound_bars = true;
}

// Returns true if this usage belonged to a learned knob, so the caller stops
// treating it as an ordinary key.
bool pad_page_handle_usage(uint8_t usage) {
  if (usage == 0) return false;

  // Learning takes the usage in front of it, one stage at a time.
  if (pad_page_learn_knob >= 0) {
    uint8_t k = pad_page_learn_knob;
    if (pad_page_learn_stage == 0) knob_cw[k] = usage;
    else if (pad_page_learn_stage == 1) knob_ccw[k] = usage;
    else knob_click[k] = usage;

    pad_page_learn_stage++;
    if (pad_page_learn_stage > 2) {
      pad_page_learn_stage = 0;
      pad_page_learn_knob = -1;
      pad_page_save();
    }
    pad_page_dirty = true;
    return true;
  }

  for (uint8_t k = 0; k < PAD_KNOBS; k++) {
    if (knob_cw[k] && usage == knob_cw[k]) {
      knob_apply(k, 1);
      return true;
    }
    if (knob_ccw[k] && usage == knob_ccw[k]) {
      knob_apply(k, -1);
      return true;
    }
    if (knob_click[k] && usage == knob_click[k]) {
      // Click steps to this knob's next slot: three knobs, four slots each.
      knob_slot[k] = (knob_slot[k] + 1) % KNOB_SLOTS;
      pad_page_dirty = true;
      return true;
    }
  }
  return false;
}

// A key or note arriving while a grid cell is armed binds to that pad.
// Returns true if it was consumed by learning.
bool pad_page_learn_input(uint8_t keycode, bool is_note) {
  if (pad_page_learn_cell < 0) return false;
  uint8_t pad = (uint8_t)pad_page_learn_cell;
  if (is_note) keymap_bind_note(keycode, KEYACT_PAD, pad);
  else keymap_bind(keycode, KEYACT_PAD, pad);
  pad_page_learn_cell = -1;
  pad_page_dirty = true;
  return true;
}
