# WASABI338 TAB5

A fork of [zircothc/M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026](https://github.com/zircothc/M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026)
for the M5Stack Tab5 (ESP32-P4), restructured for PlatformIO, adding
Bluetooth MIDI and a ReBirth338-style dual acid-303 + 808 engine.

All credit for the original 16-track sampler/synth/sequencer/FX engine,
touchscreen UI and USB MIDI host goes to the upstream author (zcarlos /
zircothc) — see their [README](https://github.com/zircothc/M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026#readme)
for the full feature set of the base machine.

## What this fork adds

### USB keyboard (`src/usb_keyboard.h`)

Plug a plain USB keyboard into the Tab5's host port and it becomes a playable
controller. The USB host enumeration now claims either a MIDI device *or* an
HID boot keyboard, whichever is plugged in.

| Keys | Action |
|---|---|
| `z s x d c v g b h n j m` | Piano, lower octave (C C# D D# E F F# G G# A A# B) |
| `q 2 w 3 e r 5 t 6 y 7 u` | Piano, the octave above |
| `[` / `]` | Octave down / up |
| `Space` | Play / stop |
| `Enter` | Record on/off (starts playback if stopped) |
| `Esc` | Stop |
| `↑` / `↓` | Selected parameter +1 / -1 |
| `→` / `←` | Selected parameter +10 / -10 |
| `F1`–`F8` | Select track 0-7 (with Shift: 8-15) |
| `L` | Arm learn (same as the on-screen LEARN button) |

Notes played while recording are written into the current step, same as the
on-screen piano mode.

> **One port, one device.** The Tab5 has a single USB host port and the
> enumeration claims a single interface, so a MIDI controller and a keyboard
> can't be used at the same time without a hub plus multi-device support.

### MIDI learn / key learn (`src/midi_learn.h`)

Bind any incoming MIDI CC — or any USB keyboard key — to any of the machine's
parameters, saved to flash so bindings survive a reboot.

- Tap **LEARN** on the GLOBAL page (or press `L` on a USB keyboard)
- Select the parameter you want
- Move a knob on your controller, or press a key → bound
- Tap **LEARN** again to cancel, **SHIFT + LEARN** to clear the selected
  parameter's binding

A learned CC is looked up first; anything unlearned still falls through to the
upstream fixed AKAI APC KEY25 page/CC mapping, so an APC keeps working exactly
as it did. A learned *key* selects its parameter (so the arrow keys / on-screen
+1/-1 then act on it).

### ReBirth338 — dual acid-303 + 808 kit (`src/rebirth338.h`)

Two independent, procedurally-synthesized (no samples) TB-303-style acid
bass voices plus a 9-voice TR-808-style drum kit (kick, snare, clap, closed
hat, open hat, low tom, hi tom, cowbell, rimshot), all analog-modeled from
scratch (oscillators + envelopes + filter sweeps, not samples). It runs as
an always-on subsystem mixed straight into the master bus, independent of
the existing 16 sample/synth tracks — this keeps it decoupled from the
original's rotary-encoder/page UI framework, which is intentionally left
untouched.

- Boots with a demo groove already running (kick/hat pattern + an acid
  bassline riff on 303 A) so it's audible with no MIDI gear attached.
- Fully playable live over MIDI (USB host or Bluetooth — see below):

  | MIDI channel | Controls |
  |---|---|
  | 3 | Acid 303 A (note = pitch, velocity ≥ 100 = accent) |
  | 4 | Acid 303 B |
  | 10 | 808 kit (General-MIDI-style drum notes: 36 kick, 38/40 snare, 39 clap, 42/44 closed hat, 46 open hat, 41/43 low tom, 45/47/48/50 hi tom, 56 cowbell, 37 rimshot) |

  CC on channels 3/4: `74` cutoff, `71` resonance, `73` env mod, `72` decay,
  `91` accent amount.

### Input status panel

The GLOBAL page shows a live panel (right of the LEARN / USB KBD buttons)
with the state of every input path, so pairing and debugging don't need a
serial console:

- `BT MIDI: off / init / adv / pairing / REGISTERED` plus a flashing `RX`
  whenever a message arrives
- `KBD: off / init / scan / pairing / REGISTERED` for a Bluetooth keyboard,
  with its own `RX`
- `USB: MIDI ready` or `USB: HID x2 rx:1234` — interface count and report
  total
- With **USB KBD** toggled on, the raw HID report bytes underneath

### Bluetooth MIDI (`src/ble_midi.h`) — experimental

**Requires the platform pin in `platformio.ini` (55.03.35 or newer).** The
hosted-BLE support only exists in arduino-esp32 **3.3.4+**; on 3.2.1 the BLE
library is guarded on `SOC_BLE_SUPPORTED` alone, which is false on the
radio-less ESP32-P4, so `BLEDevice` doesn't exist at all and Bluetooth cannot
work no matter what sdkconfig says. The Tab5's
ESP32-P4 application processor has **no radio of its own** — Bluetooth only
exists via the onboard ESP32-C6 co-processor, bridged through Espressif's
"ESP-Hosted" transport. Support for BLE over that bridge landed in
arduino-esp32 in September 2025 and is still new; there's no track record
of it being used for BLE MIDI on real Tab5 hardware yet.

This is implemented defensively so it can't take the rest of the machine
down with it:
- BLE bring-up is gated behind the exact same compile-time check
  arduino-esp32's own `BLEDevice.cpp` uses. If the pinned framework/IDF
  version doesn't actually support hosted BLE, `ble_midi.h` quietly compiles
  to no-op stubs instead of breaking the build.
- At runtime, a failure to bring up BLE only disables Bluetooth MIDI (logged
  to Serial) — sampler, synth, sequencer and USB MIDI are unaffected.
- Received BLE-MIDI messages are decoded and fed into the same
  `parse_midi_message()` the USB MIDI host uses, so a Bluetooth MIDI
  controller gets identical transport control, rotary-CC mapping and
  ReBirth338 routing "for free". Outgoing messages from `send_midi_message()`
  are mirrored to any connected BLE MIDI central — note the upstream project
  currently has no active call sites for `send_midi_message()` (they're
  present but commented out), so there's little to send yet beyond what a
  future feature wires up.
- Advertises as `WASABI338 TAB5` using the standard BLE-MIDI GATT service
  (`03b80e5a-...`), so any standard BLE-MIDI app/controller should be able
  to connect once/if the hosted transport is actually up on your hardware.

**If it doesn't come up on your board**, USB MIDI keeps working exactly as
before — this was a deliberate design constraint, not an afterthought.

## Building

```
platformio.ini
```
targets `esp32-p4-evboard` via the `pioarduino` fork of `platform-espressif32`
(per the [official Tab5 PlatformIO guidance](https://docs.m5stack.com/en/core/Tab5)),
with the extra `board_build.esp-idf.custom_sdkconfig` options needed for the
experimental hosted-BLE path. `lib/esp32_usb_host_demos/` vendors two small
MIT-licensed header files from
[touchgadget/esp32-usb-host-demos](https://github.com/touchgadget/esp32-usb-host-demos)
that the USB MIDI host code depends on (that repo isn't a proper PlatformIO
library, so the two headers are vendored directly rather than pulled as a
dependency).

## Known limitations

- ReBirth338 mixes into the master dry bus only — it doesn't (yet) route
  through the per-track FX sends (reverb/delay/chorus/etc.) that the 16
  sample/synth tracks use.
- ReBirth338's patterns/parameters aren't exposed on the touchscreen UI yet
  — MIDI is the only way to play/control it for now.
- Bluetooth MIDI is unverified on real hardware (see above) — please open an
  issue with what you find if you test it.
