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

// Consumer Control usages, not keyboard keycodes: a media/volume knob sends
// one of these per detent. Macropad knobs are almost always wired this way.
#define HID_CONSUMER_VOL_UP 0xE9    // clockwise
#define HID_CONSUMER_VOL_DOWN 0xEA  // counter-clockwise

#define HID_CONSUMER_MUTE 0xE2  // a media knob's push-click
#define HID_KEY_TAB 0x2B

// Functions defined in the .ino translation units this header is used from.
void select_rot();
void do_rot();
void synthESP32_TRIGGER_P(int nkey, int ppitch);
void synthESP32_TRIGGER(int nkey);
void send_midi_message(uint8_t status_byte, uint8_t channel, uint8_t data1, uint8_t data2);

// ReBirth mode state (rebirth_ui.h, included after this header).
extern bool rebirth_ui_active;
void rb_focus_advance();
void rb_focus_adjust(int delta);

void usb_keyboard_poll();  // defined in USB_tools.ino, driven by the USB host task
void draw_hid_monitor();   // defined in LCD_tools.ino, drawn from the LCD task

// Pad index (0-15) for a numeric keypad key, or -1.
//
// A 12-key macropad reports keypad usages, none of which appear in the piano
// map below — which is why its keys were silent. Mapped to the pads so each
// one plays its corresponding sample, exactly like the on-screen pads.
// Deliberately keypad-only: the number row already carries the piano's upper
// black keys, and taking those would cost more than it gained.
static int8_t usb_kbd_pad_index(uint8_t keycode) {
  switch (keycode) {
    // numeric keypad, laid out so 1-9 land on pads 0-8
    case 0x59: return 0;   // KP 1
    case 0x5A: return 1;   // KP 2
    case 0x5B: return 2;   // KP 3
    case 0x5C: return 3;   // KP 4
    case 0x5D: return 4;   // KP 5
    case 0x5E: return 5;   // KP 6
    case 0x5F: return 6;   // KP 7
    case 0x60: return 7;   // KP 8
    case 0x61: return 8;   // KP 9
    case 0x62: return 9;   // KP 0
    case 0x63: return 10;  // KP .
    case 0x58: return 11;  // KP Enter
    case 0x57: return 12;  // KP +
    case 0x56: return 13;  // KP -
    case 0x55: return 14;  // KP *
    case 0x54: return 15;  // KP /
    default: return -1;
  }
}

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

// A knob detent, wherever it arrives from. In ReBirth mode it drives the
// focused panel knob; otherwise it nudges the selected parameter of the
// landscape UI.
static void usb_kbd_encoder(int delta) {
  if (rebirth_ui_active) {
    rb_focus_adjust(delta);
  } else if (keymap_armed) {
    // While mapping, the knob picks which target the next key will take.
    int t = (int)keymap_target + delta;
    while (t < 0) t += KEYMAP_TARGET_COUNT;
    keymap_target = (uint8_t)(t % KEYMAP_TARGET_COUNT);
    refreshMODES = true;
  } else if (live_play) {
    // While playing live the knob is more useful moving the octave than
    // editing whatever parameter happened to be selected.
    int bank = (int)live_bank + delta;
    live_bank = (uint8_t)constrain(bank, 0, 10);
    refreshMODES = true;
  } else {
    usb_kbd_nudge(delta);
  }
}

// The knob's click advances which panel knob it's driving, so a macropad can
// reach the whole ReBirth panel without touching the glass.
static void usb_kbd_encoder_click() {
  if (rebirth_ui_active) rb_focus_advance();
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

// Lights a pad and records which note put it there, for the 4x4 grid.
static void pad_flash(uint8_t pad, uint8_t note) {
  if (pad > 15) return;
  pad_note_cell[pad] = note & 15;
  pad_flash_ms[pad] = millis();
  refresh_pad_notes = true;
}

// Routes an incoming MIDI note to the pad layout: the bottom row is a drum
// zone fired as one-shots, one note per pad, and everything outside that zone
// plays the armed melodic pad chromatically. Shared by every MIDI source so
// the routing can't drift between them.
void midi_play_note(uint8_t note) {
  if (note >= DRUM_NOTE_BASE && note < DRUM_NOTE_BASE + 8) {
    uint8_t pad = DRUM_PAD_BASE + (note - DRUM_NOTE_BASE);
    synthESP32_TRIGGER(pad);
    pad_flash(pad, note);
    if (recording) {
      bitWrite(pattern[pad], sstep, 1);
      melodic[pad][sstep] = ROTvalue[pad][12];
    }
  } else {
    synthESP32_TRIGGER_P(melodic_pad, note);
    pad_flash(melodic_pad, note);
    if (recording) {
      bitWrite(pattern[melodic_pad], sstep, 1);
      melodic[melodic_pad][sstep] = note;
    }
  }
}

// Plays a note on the selected sound, mirroring it out over MIDI when live.
static void usb_kbd_play_pitch(int pitch) {
  if (pitch < 0) pitch = 0;
  if (pitch > 127) pitch = 127;
  synthESP32_TRIGGER_P(selected_sound, pitch);
  if (live_play) send_midi_message(0x90, 1, (uint8_t)pitch, 100);
  if (recording) {
    bitWrite(pattern[selected_sound], sstep, 1);
    melodic[selected_sound][sstep] = pitch;
  }
}


// Shared by the key and MIDI-note paths so both drive identical behaviour.
static bool usb_kbd_do_action(uint8_t act, uint8_t arg) {
  switch (act) {
    case KEYACT_PAD:
      synthESP32_TRIGGER(arg);
      if (recording) {
        bitWrite(pattern[arg], sstep, 1);
        melodic[arg][sstep] = ROTvalue[arg][12];
      }
      usb_kbd_select_track(arg);
      break;
    case KEYACT_NOTE: usb_kbd_play_pitch(live_bank * 12 + arg); break;
    case KEYACT_BANK_UP:
      if (live_bank < 10) live_bank++;
      break;
    case KEYACT_BANK_DOWN:
      if (live_bank > 0) live_bank--;
      break;
    case KEYACT_SOUND_NEXT: usb_kbd_select_track((selected_sound + 1) & 15); break;
    case KEYACT_SOUND_PREV: usb_kbd_select_track((selected_sound + 15) & 15); break;
    case KEYACT_PLAY: usb_kbd_toggle_play(); break;
    case KEYACT_STOP: usb_kbd_stop(); break;
    case KEYACT_RECORD: usb_kbd_toggle_record(); break;
    case KEYACT_LIVE: live_play = !live_play; break;
    default: return false;
  }
  refreshMODES = true;
  return true;
}

// Runs a learned key binding. Returns false if the key has none, so the
// caller can fall through to the built-in map.
static bool usb_kbd_run_action(uint8_t keycode) {
  uint8_t act = key_action[keycode];
  if (act == KEYACT_NONE) return false;
  return usb_kbd_do_action(act, key_action_arg[keycode]);
}

// An incoming MIDI note, offered to the map first. Returns false if the note
// is unmapped so the caller falls back to playing it normally. Also consumes
// the note while mapping is armed, so a controller can be learned by playing
// it — same flow as a USB key.
bool midi_note_run_action(uint8_t note) {
  if (note >= NOTEMAP_SIZE) return false;

  if (keymap_armed) {
    uint8_t act, arg;
    keymap_target_action(keymap_target, &act, &arg);
    keymap_bind_note(note, act, arg);
    keymap_target = (keymap_target + 1) % KEYMAP_TARGET_COUNT;
    refreshMODES = true;
    return true;
  }

  uint8_t act = note_action[note];
  if (act == KEYACT_NONE) return false;
  return usb_kbd_do_action(act, note_action_arg[note]);
}

void usb_kbd_key_down(uint8_t keycode, uint8_t modifiers) {
  bool shift = (modifiers & HID_MOD_SHIFT) != 0;

  // Mapping mode: the key just pressed takes the displayed target, then the
  // target steps on, so a whole macropad maps by playing through it once.
  if (keymap_armed) {
    uint8_t act, arg;
    keymap_target_action(keymap_target, &act, &arg);
    keymap_bind(keycode, act, arg);
    keymap_target = (keymap_target + 1) % KEYMAP_TARGET_COUNT;
    refreshMODES = true;
    return;
  }

  // Learn takes the next key pressed and binds it to the selected parameter.
  if (learn_armed) {
    learn_armed = false;
    refreshMODES = true;
    midi_learn_bind_key(selected_rot, keycode);
    return;
  }

  // A media knob detent lands here if the device stuffs Consumer usages into
  // the keyboard report rather than exposing a separate interface. Either way
  // it drives the selected parameter as a relative encoder.
  if (keycode == HID_CONSUMER_VOL_UP || keycode == HID_CONSUMER_VOL_DOWN) {
    int step = (keycode == HID_CONSUMER_VOL_UP) ? 1 : -1;
    if (shift) step *= 10;
    usb_kbd_encoder(step);
    return;
  }

  // Knob click, or Tab from a plain keyboard, steps the ReBirth focus along.
  if (keycode == HID_CONSUMER_MUTE || keycode == HID_KEY_TAB) {
    usb_kbd_encoder_click();
    return;
  }

  // A learned action wins over everything built in, so any macropad can be
  // mapped to suit the device rather than the other way round.
  if (usb_kbd_run_action(keycode)) return;

  // A learned key selects its parameter, ahead of the fixed map below.
  int learned = midi_learn_lookup_key(keycode);
  if (learned >= 0) {
    selected_rot = learned;
    select_rot();
    refresh_sound_bars = true;
    return;
  }

  int8_t pad = usb_kbd_pad_index(keycode);

  // LIVE: the same keys play chromatic notes rather than pads. Twelve of them
  // cover an octave, and the remaining four step the bank and the sound, so a
  // 12-key macropad reaches the whole range without touching the screen.
  if (live_play && pad >= 0) {
    if (pad < 12) {
      int pitch = live_bank * 12 + pad;
      if (pitch > 127) pitch = 127;
      synthESP32_TRIGGER_P(selected_sound, pitch);
      // Mirror out so the machine can play other gear, not just itself.
      send_midi_message(0x90, 1, (uint8_t)pitch, 100);
      if (recording) {
        bitWrite(pattern[selected_sound], sstep, 1);
        melodic[selected_sound][sstep] = pitch;
      }
    } else if (pad == 12) {
      if (live_bank < 10) live_bank++;
    } else if (pad == 13) {
      if (live_bank > 0) live_bank--;
    } else if (pad == 14) {
      usb_kbd_select_track((selected_sound + 1) & 15);
    } else if (pad == 15) {
      usb_kbd_select_track((selected_sound + 15) & 15);
    }
    refreshMODES = true;
    return;
  }

  // Otherwise macropad / numpad keys play their pad, matching the on-screen
  // PAD mode: trigger the track, make it the selection, and write to the
  // pattern when recording.
  if (pad >= 0) {
    synthESP32_TRIGGER(pad);
    if (recording) {
      bitWrite(pattern[pad], sstep, 1);
      melodic[pad][sstep] = ROTvalue[pad][12];
    }
    selected_sound = pad;
    if (selected_sound != oldselected_sound) {
      oldselected_sound = selected_sound;
      refreshSEQ = true;
      select_rot();
      refresh_sound_bars = true;
    }
    return;
  }

  int8_t note = usb_kbd_note_offset(keycode);
  if (note >= 0) {
    int pitch = note + (12 * octave);
    if (pitch > 127) pitch = 127;
    synthESP32_TRIGGER_P(selected_sound, pitch);
    if (live_play) send_midi_message(0x90, 1, (uint8_t)pitch, 100);
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
    case HID_KEY_UP: usb_kbd_encoder(1); break;
    case HID_KEY_DOWN: usb_kbd_encoder(-1); break;
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
//
// The held-key state is per interface: a macropad exposes several HID
// interfaces at once, and sharing one buffer between them would make each
// one's reports look like key-ups for the other.
void usb_kbd_handle_report(uint8_t iface, const uint8_t *report) {
  static uint8_t previous[MAX_HID_IFACES][6] = { { 0 } };
  if (iface >= MAX_HID_IFACES) return;

  uint8_t modifiers = report[0];

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t key = report[2 + i];
    if (key == 0 || key == 1) continue;  // 0 = empty, 1 = rollover error

    bool was_held = false;
    for (uint8_t j = 0; j < 6; j++) {
      if (previous[iface][j] == key) {
        was_held = true;
        break;
      }
    }
    if (!was_held) usb_kbd_key_down(key, modifiers);
  }

  memcpy(previous[iface], &report[2], 6);
}

// Consumer Control reports from a knob's own HID interface. The layout varies
// between devices — some prefix a report ID, some send a 16-bit little-endian
// usage — so rather than assume an offset, look for the usage anywhere in the
// report. A detent is one 0 -> usage transition; the device sends a release
// (an all-zero report) between detents, which is what separates them.
void usb_consumer_handle_report(uint8_t iface, const uint8_t *data, uint8_t len) {
  static uint8_t previous[MAX_HID_IFACES] = { 0 };
  if (iface >= MAX_HID_IFACES) return;

  uint8_t usage = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] == HID_CONSUMER_VOL_UP || data[i] == HID_CONSUMER_VOL_DOWN ||
        data[i] == HID_CONSUMER_MUTE) {
      usage = data[i];
      break;
    }
  }

  if (usage != 0 && previous[iface] == 0) {
    if (usage == HID_CONSUMER_MUTE) usb_kbd_encoder_click();
    else usb_kbd_encoder(usage == HID_CONSUMER_VOL_UP ? 1 : -1);
  }
  previous[iface] = usage;
}
