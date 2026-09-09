#pragma once
// MIDI Learn + USB keyboard key learn.
//
// Binds any incoming MIDI CC — or any key on a USB keyboard — to any of the
// machine's parameters ("rots"), persisted to SPIFFS so bindings survive a
// reboot.
//
// This sits *alongside* the upstream fixed AKAI APC KEY25 mapping
// (mRot[]->cc / mRot[]->pageRot) rather than replacing it: a learned binding
// is looked up first, and anything not learned still falls through to the
// original behaviour, so an APC still works exactly as it did before.
//
// Flow: tap LEARN (or press L on a USB keyboard) -> select the parameter you
// want -> move a knob / press a key -> bound. Tap LEARN again to cancel,
// SHIFT+LEARN to clear the selected parameter's binding.

#include <Arduino.h>

#define MIDI_LEARN_FILE "/MIDILEARN"

static int8_t learned_cc[MAX_BARS];      // CC number bound to each rot, -1 = none
static uint8_t learned_cc_ch[MAX_BARS];  // MIDI channel (1-16), 0 = any channel
static uint8_t learned_key[MAX_BARS];    // USB HID keycode bound to each rot, 0 = none

static bool learn_armed = false;

void midi_learn_reset() {
  for (int f = 0; f < MAX_BARS; f++) {
    learned_cc[f] = -1;
    learned_cc_ch[f] = 0;
    learned_key[f] = 0;
  }
}

void midi_learn_save() {
  File file = SPIFFS.open(MIDI_LEARN_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("MIDI learn: could not open bindings file for writing");
    return;
  }
  file.write((uint8_t *)learned_cc, sizeof(learned_cc));
  file.write((uint8_t *)learned_cc_ch, sizeof(learned_cc_ch));
  file.write((uint8_t *)learned_key, sizeof(learned_key));
  file.close();
}

void midi_learn_load() {
  midi_learn_reset();
  File file = SPIFFS.open(MIDI_LEARN_FILE, FILE_READ);
  if (!file) return;  // nothing learned yet
  file.read((uint8_t *)learned_cc, sizeof(learned_cc));
  file.read((uint8_t *)learned_cc_ch, sizeof(learned_cc_ch));
  file.read((uint8_t *)learned_key, sizeof(learned_key));
  file.close();
}

// Rot index bound to this CC, or -1 if that CC hasn't been learned.
int midi_learn_lookup_cc(uint8_t channel, uint8_t cc) {
  for (int f = 0; f < MAX_BARS; f++) {
    if (learned_cc[f] == (int8_t)cc && (learned_cc_ch[f] == 0 || learned_cc_ch[f] == channel)) {
      return f;
    }
  }
  return -1;
}

int midi_learn_lookup_key(uint8_t keycode) {
  if (keycode == 0) return -1;
  for (int f = 0; f < MAX_BARS; f++) {
    if (learned_key[f] == keycode) return f;
  }
  return -1;
}

// One physical control drives one parameter: binding a control that's already
// in use releases it from whatever it was on before.
void midi_learn_bind_cc(uint8_t rot, uint8_t channel, uint8_t cc) {
  if (rot >= MAX_BARS) return;
  for (int f = 0; f < MAX_BARS; f++) {
    if (learned_cc[f] == (int8_t)cc) learned_cc[f] = -1;
  }
  learned_cc[rot] = (int8_t)cc;
  learned_cc_ch[rot] = channel;
  midi_learn_save();
  Serial.printf("MIDI learn: CC %d (ch %d) -> parameter %d\n", cc, channel, rot);
}

void midi_learn_bind_key(uint8_t rot, uint8_t keycode) {
  if (rot >= MAX_BARS || keycode == 0) return;
  for (int f = 0; f < MAX_BARS; f++) {
    if (learned_key[f] == keycode) learned_key[f] = 0;
  }
  learned_key[rot] = keycode;
  midi_learn_save();
  Serial.printf("MIDI learn: key 0x%02x -> parameter %d\n", keycode, rot);
}

void midi_learn_clear(uint8_t rot) {
  if (rot >= MAX_BARS) return;
  learned_cc[rot] = -1;
  learned_cc_ch[rot] = 0;
  learned_key[rot] = 0;
  midi_learn_save();
  Serial.printf("MIDI learn: cleared parameter %d\n", rot);
}

void midi_learn_begin() {
  midi_learn_load();
}
