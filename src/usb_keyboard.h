#pragma once
// USB HID boot-protocol keyboard support.
//
// Plug a plain USB keyboard into the Tab5's host port and it becomes a
// playable controller: QWERTY laid out as a two-octave piano, transport and
// UI keys, and a learn mode that binds any key to any parameter.
//
// The Tab5 has a single USB host port and the enumeration code claims one
// interface, so it's either a MIDI device or a keyboard — whichever is
// plugged in is detected automatically (see USB_tools.ino).
//
// Key map
//   Piano (lower octave)  z s x d c v g b h n j m   = C C# D D# E F F# G G# A A# B
//   Piano (upper octave)  q 2 w 3 e r 5 t 6 y 7 u   = the octave above
//   [ / ]                 octave down / up
//   Space                 play / stop
//   Enter                 record on/off (starts playback if stopped)
//   Esc                   stop
//   Up / Down             selected parameter +1 / -1
//   Right / Left          selected parameter +10 / -10
//   F1-F8                 select track 0-7   (with Shift: 8-15)
//   L                     arm learn (same as the on-screen LEARN button)
//
// Any key that's been learned to a parameter selects that parameter when
// pressed, and that takes priority over the fixed map above.

#include <Arduino.h>

// HID usage IDs (boot keyboard).
#define HID_KEY_A 0x04
#define HID_KEY_L 0x0F
#define HID_KEY_1 0x1E
#define HID_KEY_ENTER 0x28
#define HID_KEY_ESC 0x29
#define HID_KEY_SPACE 0x2C
#define HID_KEY_LBRACKET 0x2F
#define HID_KEY_RBRACKET 0x30
#define HID_KEY_F1 0x3A
#define HID_KEY_RIGHT 0x4F
#define HID_KEY_LEFT 0x50
#define HID_KEY_DOWN 0x51
#define HID_KEY_UP 0x52

#define HID_MOD_SHIFT 0x22  // left shift (0x02) | right shift (0x20)

// Functions defined in the .ino translation units this header is used from.
void select_rot();
void do_rot();
void synthESP32_TRIGGER_P(int nkey, int ppitch);
void usb_keyboard_poll();  // defined in USB_tools.ino, driven by the USB host task

// Semitone offset (0-23) for the piano keys, or -1 if the key isn't a note.
static int8_t usb_kbd_note_offset(uint8_t keycode) {
  switch (keycode) {
    // lower octave: z s x d c v g b h n j m
    case 0x1D: return 0;   // z  C
    case 0x16: return 1;   // s  C#
    case 0x1B: return 2;   // x  D
    case 0x07: return 3;   // d  D#
    case 0x06: return 4;   // c  E
    case 0x19: return 5;   // v  F
    case 0x0A: return 6;   // g  F#
    case 0x05: return 7;   // b  G
    case 0x0B: return 8;   // h  G#
    case 0x11: return 9;   // n  A
    case 0x0D: return 10;  // j  A#
    case 0x10: return 11;  // m  B
    // upper octave: q 2 w 3 e r 5 t 6 y 7 u
    case 0x14: return 12;  // q  C
    case 0x1F: return 13;  // 2  C#
    case 0x1A: return 14;  // w  D
    case 0x20: return 15;  // 3  D#
    case 0x08: return 16;  // e  E
    case 0x15: return 17;  // r  F
    case 0x22: return 18;  // 5  F#
    case 0x17: return 19;  // t  G
    case 0x23: return 20;  // 6  G#
    case 0x1C: return 21;  // y  A
    case 0x24: return 22;  // 7  A#
    case 0x18: return 23;  // u  B
    default: return -1;
  }
}

static void usb_kbd_nudge(int delta) {
  old_counter1 = counter1;
  counter1 = counter1 + delta;
  do_rot();
}

static void usb_kbd_select_track(uint8_t track) {
  if (track > 15) return;
  selected_sound = track;
  if (selected_sound != oldselected_sound) {
    oldselected_sound = selected_sound;
    refreshSEQ = true;
    select_rot();
    refresh_sound_bars = true;
  }
}

// Mirrors the on-screen PLAY button (button 22 in keys.ino).
static void usb_kbd_toggle_play() {
  if (playing) {
    seq.stop();
    sstep = firstStep;
    recording = false;
    clearPADSTEP = true;
    pattern_song_counter = 0;
  } else {
    if (sync_state == 2) {  // slave: wait for external start
      pre_playing = true;
    } else {
      if (songing) pattern_song_counter = selected_pattern;
      tick = 0;
      seq.start();
      sstep = firstStep;
      refreshPADSTEP = true;
    }
  }
  playing = !playing;
  refreshMODES = true;
}

// Mirrors SHIFT+PLAY (record) in keys.ino.
static void usb_kbd_toggle_record() {
  if (playing) {
    recording = !recording;
  } else {
    tick = 0;
    seq.start();
    recording = true;
    playing = true;
    sstep = firstStep;
    refreshPADSTEP = true;
  }
  if (songing) recording = false;  // song mode can't overwrite patterns
  refreshMODES = true;
}

static void usb_kbd_stop() {
  if (playing) {
    seq.stop();
    sstep = firstStep;
    recording = false;
    clearPADSTEP = true;
    pattern_song_counter = 0;
    playing = false;
    refreshMODES = true;
  }
}

void usb_kbd_key_down(uint8_t keycode, uint8_t modifiers) {
  bool shift = (modifiers & HID_MOD_SHIFT) != 0;

  // Learn takes the next key pressed and binds it to the selected parameter.
  if (learn_armed) {
    learn_armed = false;
    refreshMODES = true;
    midi_learn_bind_key(selected_rot, keycode);
    return;
  }

  // A learned key selects its parameter, ahead of the fixed map below.
  int learned = midi_learn_lookup_key(keycode);
  if (learned >= 0) {
    selected_rot = learned;
    select_rot();
    refresh_sound_bars = true;
    return;
  }

  int8_t note = usb_kbd_note_offset(keycode);
  if (note >= 0) {
    int pitch = note + (12 * octave);
    if (pitch > 127) pitch = 127;
    synthESP32_TRIGGER_P(selected_sound, pitch);
    if (recording) {
      bitWrite(pattern[selected_sound], sstep, 1);
      melodic[selected_sound][sstep] = pitch;
    }
    return;
  }

  switch (keycode) {
    case HID_KEY_SPACE: usb_kbd_toggle_play(); break;
    case HID_KEY_ENTER: usb_kbd_toggle_record(); break;
    case HID_KEY_ESC: usb_kbd_stop(); break;
    case HID_KEY_LBRACKET:
      if (octave > 0) octave--;
      break;
    case HID_KEY_RBRACKET:
      if (octave < 10) octave++;
      break;
    case HID_KEY_UP: usb_kbd_nudge(1); break;
    case HID_KEY_DOWN: usb_kbd_nudge(-1); break;
    case HID_KEY_RIGHT: usb_kbd_nudge(10); break;
    case HID_KEY_LEFT: usb_kbd_nudge(-10); break;
    case HID_KEY_L:
      learn_armed = true;
      refreshMODES = true;
      break;
    default:
      if (keycode >= HID_KEY_F1 && keycode <= HID_KEY_F1 + 7) {
        usb_kbd_select_track((keycode - HID_KEY_F1) + (shift ? 8 : 0));
      }
      break;
  }
}

// Decode one 8-byte HID boot report and fire on newly-pressed keys only, so
// holding a key doesn't retrigger. Byte 0 = modifiers, byte 1 reserved,
// bytes 2-7 = up to six simultaneously-held keycodes.
void usb_kbd_handle_report(const uint8_t *report) {
  static uint8_t previous[6] = { 0, 0, 0, 0, 0, 0 };
  uint8_t modifiers = report[0];

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t key = report[2 + i];
    if (key == 0 || key == 1) continue;  // 0 = empty, 1 = rollover error

    bool was_held = false;
    for (uint8_t j = 0; j < 6; j++) {
      if (previous[j] == key) {
        was_held = true;
        break;
      }
    }
    if (!was_held) usb_kbd_key_down(key, modifiers);
  }

  memcpy(previous, &report[2], 6);
}
