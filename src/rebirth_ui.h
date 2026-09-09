#pragma once
// ReBirth mode — a portrait-orientation front panel for the ReBirth338 engine,
// laid out after the original RB-338: the two acid synths stacked above the
// drum machine, with a 16-step pattern row for whichever instrument is
// selected.
//
// Entered by holding the top-left SOUND button; leaves the same way, or via
// EXIT. The screen rotates to portrait (720x1280) on entry and back on exit.
//
// This deliberately keeps its own drawing and touch code rather than reusing
// mBoton/mRot: those carry fixed landscape coordinates and page semantics, and
// bending them to a second orientation is how the existing UI gets broken.
// The only shared state is the engine itself and the transport.

#include <Arduino.h>

// Own palette rather than the Z* macros from init_colors(): those are defined
// inside LCD_tools.ino, which lands after this header in the combined sketch,
// so they aren't visible here.
#define RB_GREY M5.Display.color565(100, 100, 100)
#define RB_YELLOW M5.Display.color565(201, 191, 40)
#define RB_CYAN M5.Display.color565(126, 225, 247)
#define RB_GREEN M5.Display.color565(90, 232, 170)
#define RB_RED M5.Display.color565(222, 43, 27)
#define RB_ORANGE M5.Display.color565(241, 142, 28)

// Defined in LCD_tools.ino; needed when handing the screen back on exit.
void drawScreen1_ONLY1();

// Portrait canvas.
#define RB_W 720
#define RB_H 1280

// Which instrument the step row is editing: 0 = acid A, 1 = acid B,
// 2..2+drumCount-1 = the drum voices.
#define RB_SEL_ACID_A 0
#define RB_SEL_ACID_B 1
#define RB_SEL_DRUM_BASE 2

bool rebirth_ui_active = false;
bool rebirth_ui_mode_changed = false;
bool rebirth_ui_needs_redraw = false;
uint8_t rebirth_sel = RB_SEL_ACID_A;
uint8_t rebirth_edit_mode = 0;  // 0 = step on/off, 1 = accent, 2 = slide
uint8_t rebirth_machine = 0;    // 0 = 808 kit, 1 = 909 kit

// Which knob the hardware encoder is driving. Its click advances the focus,
// so the whole panel is reachable from a macropad without touching the glass.
uint8_t rebirth_focus = 0;

// Defined in the .ino translation units.
void select_rot();

static int8_t rb_last_step_drawn = -1;

// ---------------------------------------------------------------- geometry
// One knob cell and one step pad, sized for touch on a 720-wide panel.
#define RB_KNOB_W 168
#define RB_KNOB_H 96
#define RB_PAD_W 84
#define RB_PAD_H 84

struct RbKnob {
  int16_t x, y;
  const char *label;
  uint8_t param;  // index into the per-instrument parameter switch below
};

// Acid panel knobs, in the order the RB-338 lays them out.
static const RbKnob rbAcidKnobs[] = {
  { 12, 0, "TUNE", 0 },  { 190, 0, "CUTOFF", 1 },  { 368, 0, "RESO", 2 },
  { 12, 100, "ENVMOD", 3 }, { 190, 100, "DECAY", 4 }, { 368, 100, "ACCENT", 5 },
};
#define RB_ACID_KNOB_COUNT (sizeof(rbAcidKnobs) / sizeof(rbAcidKnobs[0]))

static const char *rbDrumNames[] = { "BD", "SD", "CP", "CH", "OH", "LT", "HT", "CB", "RS" };
static const char *rbDrum909Names[] = { "BD", "SD", "LT", "MT", "HT", "RIM", "CLAP", "CH", "OH", "CR", "RD" };

static uint8_t rb_drum_count() {
  return rebirth_machine ? rebirth338.drum909Count() : rebirth338.drumCount();
}
static const char *rb_drum_name(uint8_t d) {
  return rebirth_machine ? rbDrum909Names[d] : rbDrumNames[d];
}
static uint8_t rb_drum_level(uint8_t d) {
  return rebirth_machine ? rebirth338.drum909Level(d) : rebirth338.drumLevel(d);
}
static void rb_set_drum_level(uint8_t d, uint8_t v) {
  if (rebirth_machine) rebirth338.setDrum909Level(d, v);
  else rebirth338.setDrumLevel(d, v);
}
static bool rb_drum_step(uint8_t d, uint8_t s) {
  return rebirth_machine ? rebirth338.drum909Step(d, s) : rebirth338.drumStep(d, s);
}
static void rb_toggle_drum_step(uint8_t d, uint8_t s) {
  if (rebirth_machine) rebirth338.toggleDrum909Step(d, s);
  else rebirth338.toggleDrumStep(d, s);
}
static void rb_audition_drum(uint8_t d) {
  if (rebirth_machine) rebirth338.auditionDrum909(d);
  else rebirth338.auditionDrum(d);
}

// ---------------------------------------------------------------- helpers

static uint16_t rb_param_value(uint8_t which, uint8_t param) {
  Acid303Voice &v = rebirth338.acid(which);
  switch (param) {
    case 0: return (uint16_t)(v.getTune() + 24);
    case 1: return v.getCutoff();
    case 2: return v.getResonance();
    case 3: return v.getEnvMod();
    case 4: return v.getDecay();
    case 5: return v.getAccentAmount();
  }
  return 0;
}

static uint16_t rb_param_max(uint8_t param) {
  switch (param) {
    case 0: return 48;   // -24..+24 semitones
    case 1: return 255;  // cutoff and resonance are native 0-255
    case 2: return 255;
    default: return 127;
  }
}

static void rb_param_nudge(uint8_t which, uint8_t param, int delta) {
  Acid303Voice &v = rebirth338.acid(which);
  int value = (int)rb_param_value(which, param) + delta;
  int maxv = (int)rb_param_max(param);
  if (value < 0) value = 0;
  if (value > maxv) value = maxv;
  switch (param) {
    case 0: v.setTune((int8_t)(value - 24)); break;
    case 1: v.setCutoff((uint8_t)value); break;
    case 2: v.setResonance((uint8_t)value); break;
    case 3: v.setEnvMod((uint8_t)value); break;
    case 4: v.setDecay((uint8_t)value); break;
    case 5: v.setAccentAmount((uint8_t)value); break;
  }
}

// ----------------------------------------------------------------- focus
// Focus order: acid A's six knobs, acid B's six, then the drum levels of
// whichever kit is showing.
#define RB_FOCUS_DRUM_BASE 12

static uint8_t rb_focus_count() {
  return RB_FOCUS_DRUM_BASE + rb_drum_count();
}

void rb_focus_advance() {
  rebirth_focus = (rebirth_focus + 1) % rb_focus_count();
  rebirth_ui_needs_redraw = true;
}

// One detent moves a wide-range parameter further than a 0-127 one, so a
// full sweep takes a comparable number of turns either way.
void rb_focus_adjust(int delta) {
  if (rebirth_focus < RB_FOCUS_DRUM_BASE) {
    uint8_t which = rebirth_focus / RB_ACID_KNOB_COUNT;
    uint8_t param = rebirth_focus % RB_ACID_KNOB_COUNT;
    int step = (rb_param_max(param) > 127) ? 4 : 2;
    rb_param_nudge(which, param, delta * step);
  } else {
    uint8_t d = rebirth_focus - RB_FOCUS_DRUM_BASE;
    if (d < rb_drum_count()) {
      int v = (int)rb_drum_level(d) + delta * 4;
      rb_set_drum_level(d, (uint8_t)constrain(v, 0, 127));
    }
  }
  rebirth_ui_needs_redraw = true;
}

// ---------------------------------------------------------------- drawing

static void rb_draw_knob(int x, int y, const char *label, uint16_t value, uint16_t maxv, uint16_t colour, bool focused) {
  M5.Display.drawRect(x, y, RB_KNOB_W - 8, RB_KNOB_H - 8, focused ? RB_ORANGE : RB_GREY);
  if (focused) M5.Display.drawRect(x + 1, y + 1, RB_KNOB_W - 10, RB_KNOB_H - 10, RB_ORANGE);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(RB_GREY, BLACK);
  M5.Display.setCursor(x + 8, y + 6);
  M5.Display.print(label);

  // Horizontal fill bar rather than a rotary graphic: it reads accurately at
  // a glance and is far cheaper to redraw than an arc.
  int barX = x + 8, barY = y + 34, barW = RB_KNOB_W - 24, barH = 34;
  M5.Display.fillRect(barX, barY, barW, barH, BLACK);
  M5.Display.drawRect(barX, barY, barW, barH, RB_GREY);
  int fill = maxv ? (int)((uint32_t)value * (barW - 4) / maxv) : 0;
  if (fill > 0) M5.Display.fillRect(barX + 2, barY + 2, fill, barH - 4, colour);

  M5.Display.setTextColor(colour, BLACK);
  M5.Display.setCursor(x + RB_KNOB_W - 62, y + 6);
  M5.Display.printf("%3d", value);
}

static void rb_draw_acid_panel(uint8_t which, int y) {
  uint16_t colour = which ? RB_CYAN : RB_YELLOW;
  bool selected = (rebirth_sel == (which ? RB_SEL_ACID_B : RB_SEL_ACID_A));

  M5.Display.drawRect(4, y, RB_W - 8, 210, selected ? colour : RB_GREY);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(colour, BLACK);
  M5.Display.setCursor(14, y + 6);
  M5.Display.printf("BASSLINE %c", which ? 'B' : 'A');

  Acid303Voice &v = rebirth338.acid(which);
  M5.Display.setTextColor(v.getWave() == Acid303Voice::WAVE_SQUARE ? RB_GREEN : RB_ORANGE, BLACK);
  M5.Display.setCursor(RB_W - 150, y + 6);
  M5.Display.print(v.getWave() == Acid303Voice::WAVE_SQUARE ? "SQUARE" : "SAW");

  for (uint8_t k = 0; k < RB_ACID_KNOB_COUNT; k++) {
    const RbKnob &kb = rbAcidKnobs[k];
    bool focused = (rebirth_focus == which * RB_ACID_KNOB_COUNT + k);
    rb_draw_knob(kb.x, y + 26 + kb.y, kb.label,
                 rb_param_value(which, kb.param), rb_param_max(kb.param), colour, focused);
  }
}

static void rb_draw_drum_panel(int y) {
  M5.Display.drawRect(4, y, RB_W - 8, 200, RB_GREY);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(RB_GREEN, BLACK);
  M5.Display.setCursor(14, y + 6);
  M5.Display.printf("DRUMS %s", rebirth_machine ? "909" : "808");

  // Kit selector, so both machines share the one panel.
  for (uint8_t m = 0; m < 2; m++) {
    int mx = RB_W - 240 + m * 116;
    uint16_t colour = (rebirth_machine == m) ? RB_ORANGE : RB_GREY;
    M5.Display.drawRect(mx, y + 2, 108, 28, colour);
    M5.Display.setTextColor(colour, BLACK);
    M5.Display.setCursor(mx + 26, y + 8);
    M5.Display.print(m ? "909" : "808");
  }

  // Voices as a 6-wide grid: tap the name to audition, the bar to set level.
  for (uint8_t d = 0; d < rb_drum_count(); d++) {
    int cx = 8 + (d % 6) * 118;
    int cy = y + 36 + (d / 6) * 80;
    bool sel = (rebirth_sel == RB_SEL_DRUM_BASE + d);
    bool focused = (rebirth_focus == RB_FOCUS_DRUM_BASE + d);
    uint16_t colour = sel ? RB_GREEN : RB_GREY;

    M5.Display.drawRect(cx, cy, 112, 72, focused ? RB_ORANGE : colour);
    M5.Display.setTextColor(focused ? RB_ORANGE : colour, BLACK);
    M5.Display.setCursor(cx + 6, cy + 6);
    M5.Display.print(rb_drum_name(d));

    int lvl = rb_drum_level(d);
    M5.Display.fillRect(cx + 6, cy + 42, 100, 22, BLACK);
    M5.Display.drawRect(cx + 6, cy + 42, 100, 22, RB_GREY);
    M5.Display.fillRect(cx + 8, cy + 44, (lvl * 96) / 127, 18, colour);
  }
}

// The 16-step row for whatever is selected, plus the edit-mode selector.
static void rb_draw_steps(int y) {
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(RB_CYAN, BLACK);
  M5.Display.setCursor(14, y);

  if (rebirth_sel == RB_SEL_ACID_A) M5.Display.print("PATTERN: BASSLINE A");
  else if (rebirth_sel == RB_SEL_ACID_B) M5.Display.print("PATTERN: BASSLINE B");
  else M5.Display.printf("PATTERN: %s", rb_drum_name(rebirth_sel - RB_SEL_DRUM_BASE));

  bool isAcid = (rebirth_sel <= RB_SEL_ACID_B);
  if (isAcid) {
    static const char *modes[3] = { "STEP", "ACCENT", "SLIDE" };
    for (uint8_t m = 0; m < 3; m++) {
      int mx = 380 + m * 112;
      uint16_t colour = (rebirth_edit_mode == m) ? RB_ORANGE : RB_GREY;
      M5.Display.drawRect(mx, y - 6, 104, 32, colour);
      M5.Display.setTextColor(colour, BLACK);
      M5.Display.setCursor(mx + 8, y);
      M5.Display.print(modes[m]);
    }
  }

  // Two rows of eight pads.
  for (uint8_t s = 0; s < 16; s++) {
    int px = 12 + (s % 8) * 88;
    int py = y + 40 + (s / 8) * 92;

    bool on, marked = false;
    if (isAcid) {
      uint8_t which = rebirth_sel;
      on = rebirth338.acidStep(which, s);
      if (rebirth_edit_mode == 1) marked = rebirth338.acidAccent(which, s);
      if (rebirth_edit_mode == 2) marked = rebirth338.acidSlide(which, s);
    } else {
      on = rb_drum_step(rebirth_sel - RB_SEL_DRUM_BASE, s);
    }

    uint16_t colour = RB_GREY;
    if (on) colour = isAcid ? RB_YELLOW : RB_GREEN;
    if (rebirth_edit_mode && marked) colour = RB_ORANGE;

    M5.Display.fillRect(px, py, RB_PAD_W, RB_PAD_H, on ? colour : BLACK);
    M5.Display.drawRect(px, py, RB_PAD_W, RB_PAD_H, colour);

    // Acid steps show their note so a line can be read off the grid.
    if (isAcid && on) {
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(BLACK, colour);
      M5.Display.setCursor(px + 6, py + RB_PAD_H - 14);
      M5.Display.printf("%d", rebirth338.acidNote(rebirth_sel, s));
      M5.Display.setTextSize(2);
    }
  }
}

// Playhead marker under the step pads, redrawn only when the step changes.
static void rb_draw_playhead(int y) {
  if (rb_last_step_drawn == (int8_t)sstep) return;
  for (uint8_t s = 0; s < 16; s++) {
    int px = 12 + (s % 8) * 88;
    int py = y + 40 + (s / 8) * 92 + RB_PAD_H + 3;
    bool here = playing && (s == sstep);
    M5.Display.fillRect(px, py, RB_PAD_W, 6, here ? RB_RED : BLACK);
  }
  rb_last_step_drawn = (int8_t)sstep;
}

static void rb_draw_transport(int y) {
  M5.Display.setTextSize(2);
  uint16_t colour = playing ? RB_GREEN : RB_GREY;
  M5.Display.drawRect(12, y, 200, 60, colour);
  M5.Display.setTextColor(colour, BLACK);
  M5.Display.setCursor(40, y + 20);
  M5.Display.print(playing ? "PLAYING" : "STOPPED");

  M5.Display.drawRect(232, y, 160, 60, RB_CYAN);
  M5.Display.setTextColor(RB_CYAN, BLACK);
  M5.Display.setCursor(258, y + 20);
  M5.Display.printf("BPM %d", bpm);

  M5.Display.drawRect(RB_W - 180, y, 168, 60, RB_ORANGE);
  M5.Display.setTextColor(RB_ORANGE, BLACK);
  M5.Display.setCursor(RB_W - 120, y + 20);
  M5.Display.print("EXIT");
}

#define RB_ACID_A_Y 60
#define RB_ACID_B_Y 280
#define RB_DRUM_Y 500
#define RB_STEP_Y 720
#define RB_TRANSPORT_Y 1000

void rebirth_ui_draw_all() {
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(RB_ORANGE, BLACK);
  M5.Display.setCursor(14, 12);
  M5.Display.print("REBIRTH 338");

  rb_draw_acid_panel(0, RB_ACID_A_Y);
  rb_draw_acid_panel(1, RB_ACID_B_Y);
  rb_draw_drum_panel(RB_DRUM_Y);
  rb_draw_steps(RB_STEP_Y);
  rb_draw_transport(RB_TRANSPORT_Y);
  rb_last_step_drawn = -1;
}

// ---------------------------------------------------------------- touch

static void rb_handle_touch(int tx, int ty) {
  // Transport row
  if (ty >= RB_TRANSPORT_Y && ty < RB_TRANSPORT_Y + 60) {
    if (tx >= RB_W - 180) {
      rebirth_ui_active = false;
      rebirth_ui_mode_changed = true;
      return;
    }
    if (tx < 212) {
      usb_kbd_toggle_play();
      rebirth_ui_needs_redraw = true;
    }
    return;
  }

  // Acid panels: knobs, waveform toggle and selecting the panel
  for (uint8_t which = 0; which < 2; which++) {
    int panelY = which ? RB_ACID_B_Y : RB_ACID_A_Y;
    if (ty < panelY || ty >= panelY + 210) continue;

    rebirth_sel = which ? RB_SEL_ACID_B : RB_SEL_ACID_A;

    if (ty < panelY + 26) {  // header strip: waveform toggle on the right
      if (tx > RB_W - 160) {
        Acid303Voice &v = rebirth338.acid(which);
        v.setWave(v.getWave() == Acid303Voice::WAVE_SAW ? Acid303Voice::WAVE_SQUARE
                                                        : Acid303Voice::WAVE_SAW);
      }
      rebirth_ui_needs_redraw = true;
      return;
    }

    for (uint8_t k = 0; k < RB_ACID_KNOB_COUNT; k++) {
      const RbKnob &kb = rbAcidKnobs[k];
      int kx = kb.x, ky = panelY + 26 + kb.y;
      if (tx >= kx && tx < kx + RB_KNOB_W - 8 && ty >= ky && ty < ky + RB_KNOB_H - 8) {
        // Tapping a knob sets it from the horizontal touch position, so a
        // value can be dialled straight in rather than nudged repeatedly.
        int barX = kx + 8, barW = RB_KNOB_W - 24;
        int rel = tx - (barX + 2);
        if (rel < 0) rel = 0;
        int maxv = rb_param_max(kb.param);
        int value = (rel * maxv) / (barW - 4);
        if (value > maxv) value = maxv;
        rb_param_nudge(which, kb.param, value - (int)rb_param_value(which, kb.param));
        rebirth_focus = which * RB_ACID_KNOB_COUNT + k;
        rebirth_ui_needs_redraw = true;
        return;
      }
    }
    rebirth_ui_needs_redraw = true;
    return;
  }

  // Drum voices
  if (ty >= RB_DRUM_Y && ty < RB_DRUM_Y + 200) {
    // Kit selector in the header strip
    if (ty < RB_DRUM_Y + 32) {
      for (uint8_t m = 0; m < 2; m++) {
        int mx = RB_W - 240 + m * 116;
        if (tx >= mx && tx < mx + 108) {
          rebirth_machine = m;
          // Selection and focus both index the kit, so pull them back in
          // range rather than leaving them pointing past a shorter kit.
          if (rebirth_sel >= RB_SEL_DRUM_BASE) rebirth_sel = RB_SEL_DRUM_BASE;
          if (rebirth_focus >= rb_focus_count()) rebirth_focus = RB_FOCUS_DRUM_BASE;
          rebirth_ui_needs_redraw = true;
          return;
        }
      }
      return;
    }

    for (uint8_t d = 0; d < rb_drum_count(); d++) {
      int cx = 8 + (d % 6) * 118;
      int cy = RB_DRUM_Y + 36 + (d / 6) * 80;
      if (tx < cx || tx >= cx + 112 || ty < cy || ty >= cy + 72) continue;

      if (ty >= cy + 42) {  // level bar
        int rel = tx - (cx + 8);
        if (rel < 0) rel = 0;
        int lvl = (rel * 127) / 96;
        if (lvl > 127) lvl = 127;
        rb_set_drum_level(d, (uint8_t)lvl);
      } else {
        rb_audition_drum(d);
      }
      rebirth_sel = RB_SEL_DRUM_BASE + d;
      rebirth_focus = RB_FOCUS_DRUM_BASE + d;
      rebirth_ui_needs_redraw = true;
      return;
    }
    return;
  }

  // Step row: edit-mode selector, then the pads
  if (ty >= RB_STEP_Y - 6 && ty < RB_STEP_Y + 26 && rebirth_sel <= RB_SEL_ACID_B) {
    for (uint8_t m = 0; m < 3; m++) {
      int mx = 380 + m * 112;
      if (tx >= mx && tx < mx + 104) {
        rebirth_edit_mode = m;
        rebirth_ui_needs_redraw = true;
        return;
      }
    }
  }

  for (uint8_t s = 0; s < 16; s++) {
    int px = 12 + (s % 8) * 88;
    int py = RB_STEP_Y + 40 + (s / 8) * 92;
    if (tx < px || tx >= px + RB_PAD_W || ty < py || ty >= py + RB_PAD_H) continue;

    if (rebirth_sel <= RB_SEL_ACID_B) {
      uint8_t which = rebirth_sel;
      if (rebirth_edit_mode == 1) rebirth338.toggleAcidAccent(which, s);
      else if (rebirth_edit_mode == 2) rebirth338.toggleAcidSlide(which, s);
      else rebirth338.toggleAcidStep(which, s);
    } else {
      rb_toggle_drum_step(rebirth_sel - RB_SEL_DRUM_BASE, s);
    }
    rebirth_ui_needs_redraw = true;
    return;
  }
}

static void rebirth_ui_touch() {
  static bool held = false;
  int n = M5.Display.getTouchRaw(tp, 1);
  if (n > 0) {
    M5.Display.convertRawXY(tp, n);
    if (!held) {
      held = true;
      rb_handle_touch(tp[0].x, tp[0].y);
    }
  } else {
    held = false;
  }
}

// ---------------------------------------------------------------- lifecycle

// Applies a pending mode change: rotate the panel and repaint whichever UI is
// now in charge. Called from the LCD task so no drawing happens off-task.
void rebirth_ui_apply_mode() {
  rebirth_ui_mode_changed = false;
  if (rebirth_ui_active) {
    M5.Display.setRotation(2);  // portrait, flipped end-for-end
    rebirth_ui_draw_all();
  } else {
    M5.Display.setRotation(1);  // back to the landscape machine
    M5.Display.fillScreen(BLACK);
    drawScreen1_ONLY1();
    clearPATTERNPADS = true;
    refreshPATTERN = true;
    refreshSEQ = true;
    refreshMODES = true;
    refresh_sound_bars = true;
    refresh_rPage = true;
    old_rPage = -1;
    s_old_selected_rot = 99;
  }
}

void rebirth_ui_task() {
  rebirth_ui_touch();

  if (rebirth_ui_needs_redraw) {
    rebirth_ui_needs_redraw = false;
    rebirth_ui_draw_all();
  }
  rb_draw_playhead(RB_STEP_Y);
}
