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

////////////////////////////////////////////////////////////////////////////
// Key mapping — any macropad, any keyboard.
//
// The built-in keypad map only fits devices that report the usages a numeric
// keypad does. Macropads differ wildly: some send function keys, some letters,
// some media usages. So every keycode can instead be bound to an action here,
// learned from the device itself, and stored to SPIFFS.
//
// A learned binding wins over the built-in map, and anything unmapped falls
// through to it, so a plain keyboard keeps working untouched.

#define KEYMAP_FILE "/KEYMAP"
#define KEYMAP_SIZE 256

#define KEYACT_NONE 0
#define KEYACT_PAD 1   // arg = pad 0-15
#define KEYACT_NOTE 2  // arg = semitone 0-11 within the live bank
#define KEYACT_BANK_UP 3
#define KEYACT_BANK_DOWN 4
#define KEYACT_SOUND_NEXT 5
#define KEYACT_SOUND_PREV 6
#define KEYACT_PLAY 7
#define KEYACT_STOP 8
#define KEYACT_RECORD 9
#define KEYACT_LIVE 10

static uint8_t key_action[KEYMAP_SIZE];
static uint8_t key_action_arg[KEYMAP_SIZE];

// The same actions, learnable from MIDI notes, so a pad controller or
// keyboard maps exactly like a USB device does.
#define NOTEMAP_SIZE 128
static uint8_t note_action[NOTEMAP_SIZE];
static uint8_t note_action_arg[NOTEMAP_SIZE];

// Mapping mode: the target being assigned, shown on the KEYMAP button. Keys
// bind to it and step to the next, so a whole pad can be mapped by playing
// through it once.
static bool keymap_armed = false;
static uint8_t keymap_target = 0;

#define KEYMAP_TARGET_COUNT 36  // 16 pads + 12 notes + 8 commands

static void keymap_target_action(uint8_t target, uint8_t *act, uint8_t *arg) {
  if (target < 16) {
    *act = KEYACT_PAD;
    *arg = target;
  } else if (target < 28) {
    *act = KEYACT_NOTE;
    *arg = target - 16;
  } else {
    static const uint8_t cmds[8] = { KEYACT_BANK_UP, KEYACT_BANK_DOWN, KEYACT_SOUND_NEXT,
                                     KEYACT_SOUND_PREV, KEYACT_PLAY, KEYACT_STOP,
                                     KEYACT_RECORD, KEYACT_LIVE };
    *act = cmds[(target - 28) & 7];
    *arg = 0;
  }
}

static void keymap_target_name(uint8_t target, char *out, size_t len) {
  uint8_t act, arg;
  keymap_target_action(target, &act, &arg);
  switch (act) {
    case KEYACT_PAD: snprintf(out, len, "PAD %d", arg); break;
    case KEYACT_NOTE: snprintf(out, len, "NOTE %d", arg); break;
    case KEYACT_BANK_UP: snprintf(out, len, "BANK+"); break;
    case KEYACT_BANK_DOWN: snprintf(out, len, "BANK-"); break;
    case KEYACT_SOUND_NEXT: snprintf(out, len, "SND+"); break;
    case KEYACT_SOUND_PREV: snprintf(out, len, "SND-"); break;
    case KEYACT_PLAY: snprintf(out, len, "PLAY"); break;
    case KEYACT_STOP: snprintf(out, len, "STOP"); break;
    case KEYACT_RECORD: snprintf(out, len, "REC"); break;
    case KEYACT_LIVE: snprintf(out, len, "LIVE"); break;
    default: snprintf(out, len, "-"); break;
  }
}

void keymap_reset() {
  memset(key_action, KEYACT_NONE, sizeof(key_action));
  memset(key_action_arg, 0, sizeof(key_action_arg));
  memset(note_action, KEYACT_NONE, sizeof(note_action));
  memset(note_action_arg, 0, sizeof(note_action_arg));
}

void keymap_save() {
  File file = SPIFFS.open(KEYMAP_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Key map: could not open file for writing");
    return;
  }
  file.write(key_action, sizeof(key_action));
  file.write(key_action_arg, sizeof(key_action_arg));
  file.write(note_action, sizeof(note_action));
  file.write(note_action_arg, sizeof(note_action_arg));
  file.close();
}

void keymap_load() {
  keymap_reset();
  File file = SPIFFS.open(KEYMAP_FILE, FILE_READ);
  if (!file) return;
  file.read(key_action, sizeof(key_action));
  file.read(key_action_arg, sizeof(key_action_arg));
  // A file written before note mapping existed simply stops here, leaving the
  // note tables cleared, which reads as unmapped.
  file.read(note_action, sizeof(note_action));
  file.read(note_action_arg, sizeof(note_action_arg));
  file.close();
}

// One key drives one action: rebinding a key releases whatever it had.
void keymap_bind(uint8_t keycode, uint8_t act, uint8_t arg) {
  if (keycode == 0) return;
  key_action[keycode] = act;
  key_action_arg[keycode] = arg;
  keymap_save();
  Serial.printf("Key map: 0x%02x -> action %d arg %d\n", keycode, act, arg);
}

void keymap_bind_note(uint8_t note, uint8_t act, uint8_t arg) {
  if (note >= NOTEMAP_SIZE) return;
  note_action[note] = act;
  note_action_arg[note] = arg;
  keymap_save();
  Serial.printf("Key map: MIDI note %d -> action %d arg %d\n", note, act, arg);
}

void midi_learn_begin() {
  midi_learn_load();
  keymap_load();
}
